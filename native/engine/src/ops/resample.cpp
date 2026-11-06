// Resampling, blur and geometric warps.
//
// Separable filters run in horizontal strips: each strip filters only the
// source rows its output rows need, so scratch stays a few megabytes at any
// image size (a full intermediate image would be ~200 MB at 12 MP).

#include <algorithm>
#include <cmath>
#include <vector>

#include "common.hpp"
#include "photoplus/filters.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::ops {

using detail::FirstError;
using detail::Layout;
using detail::layoutOf;
using detail::Scratch;
using detail::toByte;

namespace {

constexpr float kPi = 3.14159265358979f;

enum class Kernel { Bilinear, Area, Lanczos, Gaussian };

// For each output index: the first source index and its weights (normalised).
struct Taps {
  std::vector<int32_t> start;
  std::vector<float> weights;  // [out][maxCount]
  int32_t maxCount = 0;
  int32_t n = 0;  // source length

  float weight(int32_t o, int32_t k) const { return weights[static_cast<std::size_t>(o) * maxCount + k]; }
  int32_t source(int32_t o, int32_t k) const { return std::clamp(start[o] + k, 0, n - 1); }
};

float lanczos3(float x) {
  x = std::fabs(x);
  if (x < 1e-6f) return 1.0f;
  if (x >= 3.0f) return 0.0f;
  const float px = kPi * x;
  return 3.0f * std::sin(px) * std::sin(px / 3.0f) / (px * px);
}

Taps makeTaps(int32_t n, int32_t m, Kernel kernel, float sigma = 0.0f) {
  Taps t;
  t.n = n;
  const float scale = static_cast<float>(n) / static_cast<float>(m);  // source pixels per output pixel
  const float f = (kernel == Kernel::Area || kernel == Kernel::Lanczos) ? std::max(1.0f, scale) : 1.0f;
  float support = 1.0f;
  switch (kernel) {
    case Kernel::Bilinear:
      support = 1.0f;
      break;
    case Kernel::Area:
      support = 0.5f * f + 0.5f;
      break;
    case Kernel::Lanczos:
      support = 3.0f * f;
      break;
    case Kernel::Gaussian:
      support = std::max(1.0f, 3.0f * sigma);
      break;
  }
  t.maxCount = static_cast<int32_t>(std::ceil(2.0f * support)) + 2;
  t.start.resize(static_cast<std::size_t>(m));
  t.weights.assign(static_cast<std::size_t>(m) * t.maxCount, 0.0f);
  for (int32_t o = 0; o < m; ++o) {
    const float center = kernel == Kernel::Gaussian ? static_cast<float>(o) : (static_cast<float>(o) + 0.5f) * scale - 0.5f;
    const int32_t first = static_cast<int32_t>(std::floor(center - support));
    t.start[o] = first;
    float sum = 0.0f;
    for (int32_t k = 0; k < t.maxCount; ++k) {
      const float d = static_cast<float>(first + k) - center;
      float w = 0.0f;
      switch (kernel) {
        case Kernel::Bilinear:
          w = std::max(0.0f, 1.0f - std::fabs(d));
          break;
        case Kernel::Area: {
          // Overlap of the source pixel [d-0.5, d+0.5] with the footprint [-f/2, f/2].
          const float lo = std::max(d - 0.5f, -0.5f * f), hi = std::min(d + 0.5f, 0.5f * f);
          w = std::max(0.0f, hi - lo);
          break;
        }
        case Kernel::Lanczos:
          w = lanczos3(d / f);
          break;
        case Kernel::Gaussian:
          w = std::fabs(d) <= support ? std::exp(-d * d / (2.0f * sigma * sigma)) : 0.0f;
          break;
      }
      t.weights[static_cast<std::size_t>(o) * t.maxCount + k] = w;
      sum += w;
    }
    if (std::fabs(sum) > 1e-8f) {
      for (int32_t k = 0; k < t.maxCount; ++k) t.weights[static_cast<std::size_t>(o) * t.maxCount + k] /= sum;
    } else {
      t.weights[static_cast<std::size_t>(o) * t.maxCount] = 1.0f;
    }
  }
  return t;
}

// One source row as floats, colour premultiplied by alpha.
void loadRow(const uint8_t* src, int32_t width, const Layout& L, float* out) {
  for (int32_t x = 0; x < width; ++x) {
    const uint8_t* p = src + static_cast<std::size_t>(x) * L.bpp;
    if (L.alpha >= 0) {
      const float a = p[L.alpha] * (1.0f / 255.0f);
      for (int32_t c = 0; c < L.bpp; ++c) out[x * L.bpp + c] = c == L.alpha ? p[c] : p[c] * a;
    } else {
      for (int32_t c = 0; c < L.bpp; ++c) out[x * L.bpp + c] = p[c];
    }
  }
}

void storePixel(const float* v, const Layout& L, uint8_t* dst) {
  if (L.alpha >= 0) {
    const float a = v[L.alpha];
    const float inv = a > 0.5f ? 255.0f / a : 0.0f;
    for (int32_t c = 0; c < L.bpp; ++c) dst[c] = c == L.alpha ? toByte(a) : toByte(v[c] * inv);
  } else {
    for (int32_t c = 0; c < L.bpp; ++c) dst[c] = toByte(v[c]);
  }
}

// Separable filter: horizontal taps `th` (src width -> dst width), vertical
// taps `tv` (src height -> dst height). Output rows are split into strips run
// in parallel; within a strip, rows are produced in order from a small ring of
// horizontally filtered source rows (just the rows the vertical taps need), so
// scratch is a few rows per thread at any image size.
Error separable(const ImageBuffer& src, ImageBuffer& dst, const Taps& th, const Taps& tv, const CancellationToken* token) {
  const Layout L = layoutOf(src.format());
  const int32_t sw = src.width(), dw = dst.width(), dh = dst.height();
  const int32_t C = L.bpp;
  const int32_t ring = tv.maxCount;
  const int32_t strips = std::clamp<int32_t>(dh / 8, 1, 32);
  const int32_t stripRows = (dh + strips - 1) / strips;
  FirstError err;
  runParallel(strips, [&](int32_t s) {
    if (err.failed() || isCancelled(token)) return;
    const int32_t y0 = s * stripRows, y1 = std::min(dh, y0 + stripRows);
    if (y0 >= y1) return;
    auto mid = Scratch<float>::allocate(static_cast<std::size_t>(ring) * dw * C);
    auto line = Scratch<float>::allocate(static_cast<std::size_t>(sw) * C);
    if (!mid || !line) {
      err.set(!mid ? mid.error() : line.error());
      return;
    }
    std::vector<int32_t> slotRow(static_cast<std::size_t>(ring), -1);
    auto filtered = [&](int32_t r) -> const float* {
      const int32_t slot = r % ring;
      float* out = mid->data() + static_cast<std::size_t>(slot) * dw * C;
      if (slotRow[slot] != r) {
        loadRow(src.row(r), sw, L, line->data());
        for (int32_t x = 0; x < dw; ++x) {
          float acc[4] = {0, 0, 0, 0};
          for (int32_t k = 0; k < th.maxCount; ++k) {
            const float w = th.weight(x, k);
            if (w == 0.0f) continue;
            const float* p = line->data() + static_cast<std::size_t>(th.source(x, k)) * C;
            for (int32_t c = 0; c < C; ++c) acc[c] += w * p[c];
          }
          for (int32_t c = 0; c < C; ++c) out[x * C + c] = acc[c];
        }
        slotRow[slot] = r;
      }
      return out;
    };
    std::vector<const float*> rows(static_cast<std::size_t>(tv.maxCount));
    for (int32_t y = y0; y < y1; ++y) {
      if (isCancelled(token)) return;
      for (int32_t k = 0; k < tv.maxCount; ++k) rows[k] = tv.weight(y, k) == 0.0f ? nullptr : filtered(tv.source(y, k));
      uint8_t* d = dst.row(y);
      for (int32_t x = 0; x < dw; ++x) {
        float acc[4] = {0, 0, 0, 0};
        for (int32_t k = 0; k < tv.maxCount; ++k) {
          if (rows[k] == nullptr) continue;
          const float w = tv.weight(y, k);
          const float* p = rows[k] + static_cast<std::size_t>(x) * C;
          for (int32_t c = 0; c < C; ++c) acc[c] += w * p[c];
        }
        storePixel(acc, L, d + static_cast<std::size_t>(x) * C);
      }
    }
    if (token != nullptr) token->advance(static_cast<float>(y1 - y0) / static_cast<float>(dh));
  });
  if (err.failed()) return err.take();
  if (isCancelled(token)) return detail::cancelledErr();
  return detail::ok();
}

Error checkImage(const ImageBuffer& src) {
  if (src.empty()) return detail::invalid("Empty image");
  return detail::ok();
}

}  // namespace

GaussianKernel gaussianKernel(float sigma) {
  // The kernel blur() applies at every interior pixel (shared with the GPU
  // path): taps at offsets first..first+count-1, edges clamp the index.
  const Taps t = makeTaps(64, 64, Kernel::Gaussian, sigma);
  GaussianKernel k;
  k.first = t.start[32] - 32;
  k.weights.assign(t.weights.begin() + 32 * t.maxCount, t.weights.begin() + 33 * t.maxCount);
  return k;
}

Result<ImageBuffer> resize(const ImageBuffer& src, int32_t width, int32_t height, ResizeFilter filter,
                           const CancellationToken* token) {
  ScopedTimer timer("op.resize");
  if (Error e = checkImage(src); e.status != PP_OK) return failure(e);
  if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension) {
    return failure(detail::invalid("Resize target must be 1.." + std::to_string(kMaxDimension) + " pixels"));
  }
  const auto pick = [&](int32_t n, int32_t m) {
    switch (filter) {
      case ResizeFilter::Bilinear:
        return Kernel::Bilinear;
      case ResizeFilter::Area:
        return Kernel::Area;
      case ResizeFilter::Lanczos:
        return Kernel::Lanczos;
      case ResizeFilter::Auto:
        break;
    }
    return m < n ? Kernel::Area : Kernel::Lanczos;
  };
  auto out = ImageBuffer::allocate(width, height, src.format());
  if (!out) return failure(out.error());
  const Taps th = makeTaps(src.width(), width, pick(src.width(), width));
  const Taps tv = makeTaps(src.height(), height, pick(src.height(), height));
  if (Error e = separable(src, out.value(), th, tv, token); e.status != PP_OK) return failure(e);
  return std::move(out).value();
}

Result<ImageBuffer> blur(const ImageBuffer& src, float sigma, const CancellationToken* token) {
  ScopedTimer timer("op.blur");
  if (Error e = checkImage(src); e.status != PP_OK) return failure(e);
  if (!std::isfinite(sigma) || sigma < 0.0f) return failure(detail::invalid("Blur radius must be 0 or more"));
  sigma = std::min(sigma, 500.0f);
  if (sigma < 0.3f) return cloneImage(src);
  if (sigma > 8.0f) {
    // Blur a reduced copy: the result is smooth, so detail lost to the
    // reduction is invisible, and cost no longer grows with sigma.
    const float factor = sigma / 4.0f;
    const int32_t sw = std::max(1, static_cast<int32_t>(std::lround(static_cast<float>(src.width()) / factor)));
    const int32_t sh = std::max(1, static_cast<int32_t>(std::lround(static_cast<float>(src.height()) / factor)));
    auto small = resize(src, sw, sh, ResizeFilter::Area, token);
    if (!small) return failure(small.error());
    // The area reduction and the enlargement each add a little blur of their own.
    const float rest = std::sqrt(std::max(0.25f, 16.0f - 0.33f));
    auto blurred = blur(small.value(), rest, token);
    if (!blurred) return failure(blurred.error());
    return resize(blurred.value(), src.width(), src.height(), ResizeFilter::Bilinear, token);
  }
  auto out = ImageBuffer::allocate(src.width(), src.height(), src.format());
  if (!out) return failure(out.error());
  const Taps th = makeTaps(src.width(), src.width(), Kernel::Gaussian, sigma);
  const Taps tv = makeTaps(src.height(), src.height(), Kernel::Gaussian, sigma);
  if (Error e = separable(src, out.value(), th, tv, token); e.status != PP_OK) return failure(e);
  return std::move(out).value();
}

// ---- perspective ---------------------------------------------------------------

bool solveHomography(const float from[8], const float to[8], double h[9]) {
  // u = (h0 x + h1 y + h2) / (h6 x + h7 y + 1), v = (h3 x + h4 y + h5) / (...)
  double a[8][9] = {};
  for (int i = 0; i < 4; ++i) {
    const double x = from[2 * i], y = from[2 * i + 1], u = to[2 * i], v = to[2 * i + 1];
    double* r1 = a[2 * i];
    double* r2 = a[2 * i + 1];
    r1[0] = x, r1[1] = y, r1[2] = 1, r1[3] = 0, r1[4] = 0, r1[5] = 0, r1[6] = -u * x, r1[7] = -u * y, r1[8] = u;
    r2[0] = 0, r2[1] = 0, r2[2] = 0, r2[3] = x, r2[4] = y, r2[5] = 1, r2[6] = -v * x, r2[7] = -v * y, r2[8] = v;
  }
  for (int c = 0; c < 8; ++c) {
    int piv = c;
    for (int r = c + 1; r < 8; ++r) {
      if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
    }
    if (std::fabs(a[piv][c]) < 1e-12) return false;
    if (piv != c) {
      for (int k = 0; k < 9; ++k) std::swap(a[c][k], a[piv][k]);
    }
    for (int r = 0; r < 8; ++r) {
      if (r == c) continue;
      const double f = a[r][c] / a[c][c];
      for (int k = c; k < 9; ++k) a[r][k] -= f * a[c][k];
    }
  }
  for (int i = 0; i < 8; ++i) h[i] = a[i][8] / a[i][i];
  h[8] = 1.0;
  for (int i = 0; i < 9; ++i) {
    if (!std::isfinite(h[i])) return false;
  }
  return true;
}

namespace {

double cross(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

// A usable quadrilateral: finite, non-degenerate and convex.
bool convexQuad(const float q[8]) {
  int sign = 0;
  for (int i = 0; i < 4; ++i) {
    if (!std::isfinite(q[2 * i]) || !std::isfinite(q[2 * i + 1])) return false;
    const int j = (i + 1) % 4, k = (i + 2) % 4;
    const double z = cross(q[2 * j] - q[2 * i], q[2 * j + 1] - q[2 * i + 1], q[2 * k] - q[2 * j], q[2 * k + 1] - q[2 * j + 1]);
    if (std::fabs(z) < 1e-6) return false;
    const int s = z > 0 ? 1 : -1;
    if (sign == 0) sign = s;
    if (s != sign) return false;
  }
  return true;
}

}  // namespace

Result<ImageBuffer> warpPerspective(const ImageBuffer& src, const float quad[8], int32_t width, int32_t height,
                                    const CancellationToken* token) {
  ScopedTimer timer("op.warp");
  if (Error e = checkImage(src); e.status != PP_OK) return failure(e);
  if (quad == nullptr || !convexQuad(quad)) return failure(detail::invalid("The corners must form a convex shape"));
  if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension) {
    return failure(detail::invalid("Output size must be 1.." + std::to_string(kMaxDimension) + " pixels"));
  }
  const float rect[8] = {0, 0, static_cast<float>(width), 0, static_cast<float>(width), static_cast<float>(height), 0,
                         static_cast<float>(height)};
  double hm[9];
  if (!solveHomography(rect, quad, hm)) return failure(detail::invalid("The corners don't define a perspective"));
  auto out = ImageBuffer::allocate(width, height, src.format());
  if (!out) return failure(out.error());
  ImageBuffer& dst = out.value();
  const Layout L = layoutOf(src.format());
  const int32_t sw = src.width(), sh = src.height();
  const bool ok = parallelFor(height, token, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) {
      if (isCancelled(token)) return;
      uint8_t* d = dst.row(y);
      const double py = y + 0.5;
      for (int32_t x = 0; x < width; ++x) {
        const double px = x + 0.5;
        const double wq = hm[6] * px + hm[7] * py + 1.0;
        const double u = (hm[0] * px + hm[1] * py + hm[2]) / wq - 0.5;
        const double v = (hm[3] * px + hm[4] * py + hm[5]) / wq - 0.5;
        const double fx = std::clamp(u, 0.0, static_cast<double>(sw - 1));
        const double fy = std::clamp(v, 0.0, static_cast<double>(sh - 1));
        const int32_t x0 = static_cast<int32_t>(fx), y0i = static_cast<int32_t>(fy);
        const int32_t x1 = std::min(sw - 1, x0 + 1), y1i = std::min(sh - 1, y0i + 1);
        const float tx = static_cast<float>(fx - x0), ty = static_cast<float>(fy - y0i);
        const uint8_t* corners[4] = {src.row(y0i) + x0 * L.bpp, src.row(y0i) + x1 * L.bpp, src.row(y1i) + x0 * L.bpp,
                                     src.row(y1i) + x1 * L.bpp};
        const float wts[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
        float acc[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
          const float a = L.alpha >= 0 ? corners[i][L.alpha] * (1.0f / 255.0f) : 1.0f;
          for (int32_t c = 0; c < L.bpp; ++c) {
            acc[c] += wts[i] * (c == L.alpha ? corners[i][c] : corners[i][c] * a);
          }
        }
        storePixel(acc, L, d + static_cast<std::size_t>(x) * L.bpp);
      }
    }
  });
  if (!ok) return failure(detail::cancelledErr());
  return std::move(out).value();
}

void keystoneQuad(int32_t width, int32_t height, float vertical, float horizontal, float q[8]) {
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  const float a = 0.3f * std::clamp(vertical, -1.0f, 1.0f), b = 0.3f * std::clamp(horizontal, -1.0f, 1.0f);
  // Top-left, top-right, bottom-right, bottom-left of the source region that fills the output.
  float tlx = 0, tly = 0, trx = w, try_ = 0, brx = w, bry = h, blx = 0, bly = h;
  if (a > 0) {
    tlx += a * w / 2, trx -= a * w / 2;
  } else {
    blx -= a * w / 2, brx += a * w / 2;
  }
  if (b > 0) {
    try_ += b * h / 2, bry -= b * h / 2;
  } else {
    tly -= b * h / 2, bly += b * h / 2;
  }
  const float out[8] = {tlx, tly, trx, try_, brx, bry, blx, bly};
  std::copy(out, out + 8, q);
}

Result<ImageBuffer> keystone(const ImageBuffer& src, float vertical, float horizontal, const CancellationToken* token) {
  if (Error e = checkImage(src); e.status != PP_OK) return failure(e);
  if (!std::isfinite(vertical) || !std::isfinite(horizontal)) return failure(detail::invalid("Perspective must be finite"));
  if (std::fabs(vertical) < 1e-4f && std::fabs(horizontal) < 1e-4f) return cloneImage(src);
  float q[8];
  keystoneQuad(src.width(), src.height(), vertical, horizontal, q);
  return warpPerspective(src, q, src.width(), src.height(), token);
}

float detectVerticalKeystone(const ImageBuffer& src, const CancellationToken* token) {
  ScopedTimer timer("op.detectKeystone");
  if (src.empty()) return 0.0f;
  // Work on a small copy: line directions don't need full resolution.
  const float k = std::min(1.0f, 512.0f / static_cast<float>(std::max(src.width(), src.height())));
  const int32_t w = std::max(16, static_cast<int32_t>(static_cast<float>(src.width()) * k)), h = std::max(16, static_cast<int32_t>(static_cast<float>(src.height()) * k));
  auto small = resize(src, w, h, ResizeFilter::Area, token);
  if (!small) return 0.0f;
  const Layout L = layoutOf(small->format());
  std::vector<float> luma(static_cast<std::size_t>(w) * h);
  for (int32_t y = 0; y < h; ++y) {
    const uint8_t* r = small->row(y);
    for (int32_t x = 0; x < w; ++x) {
      const uint8_t* p = r + x * L.bpp;
      luma[static_cast<std::size_t>(y) * w + x] = L.colors == 1 ? p[0] : 0.299f * p[L.r] + 0.587f * p[L.g] + 0.114f * p[L.b];
    }
  }
  // Near-vertical edges vote with their lean (sideways shift per unit up) at
  // their position across the frame: converging verticals lean in proportion
  // to their distance from the centre. Fit lean = kappa * u + roll.
  struct Vote {
    float u, lean, weight;
  };
  std::vector<Vote> votes;
  votes.reserve(static_cast<std::size_t>(w) * h / 8);
  double total = 0;
  for (int32_t y = 1; y < h - 1; ++y) {
    for (int32_t x = 1; x < w - 1; ++x) {
      auto at = [&](int32_t xx, int32_t yy) { return luma[static_cast<std::size_t>(yy) * w + xx]; };
      const float gx = (at(x + 1, y - 1) + 2 * at(x + 1, y) + at(x + 1, y + 1)) - (at(x - 1, y - 1) + 2 * at(x - 1, y) + at(x - 1, y + 1));
      const float gy = (at(x - 1, y + 1) + 2 * at(x, y + 1) + at(x + 1, y + 1)) - (at(x - 1, y - 1) + 2 * at(x, y - 1) + at(x + 1, y - 1));
      const float mag = std::sqrt(gx * gx + gy * gy);
      if (mag < 80.0f || std::fabs(gx) < 1e-3f) continue;
      const float lean = gy / gx;
      if (std::fabs(lean) > 0.35f) continue;
      votes.push_back({static_cast<float>((x + 0.5) / w - 0.5), lean, mag});
      total += mag;
    }
  }
  if (total < 2000.0 * (w * h) / (512.0 * 384.0)) return 0.0f;  // too few vertical lines to judge
  // Weighted least squares over the votes within `band` of the current
  // model. Texture edges that are not part of any straight vertical line
  // still vote, with leans unrelated to their position; left in, they pull
  // the slope towards zero (under-correction). So the fit is repeated with
  // a narrowing band, keeping only the edges that agree with the lines.
  double kappa = 0, roll = 0;
  bool fitted = false;
  for (const double band : {1.0, 0.12, 0.06, 0.03}) {
    double sw = 0, su = 0, suu = 0, sl = 0, sul = 0;
    for (const Vote& v : votes) {
      if (fitted && std::fabs(v.lean - (kappa * v.u + roll)) > band) continue;
      sw += v.weight, su += v.weight * v.u, suu += v.weight * v.u * v.u, sl += v.weight * v.lean,
          sul += v.weight * v.u * v.lean;
    }
    // Keep the previous fit if too little agrees with it to refine further.
    if (fitted && sw < 0.1 * total) break;
    const double det = sw * suu - su * su;
    if (std::fabs(det) < 1e-9) {
      if (!fitted) return 0.0f;
      break;
    }
    kappa = (sw * sul - su * sl) / det;
    roll = (sl - kappa * su) / sw;
    fitted = true;
  }
  // Our correction with amount a narrows the source top by a*w, which makes
  // a line at u lean by a*u*w/h: kappa = a*w/h, and the correction undoes it.
  const double a = -kappa * static_cast<double>(h) / static_cast<double>(w);
  const float v = static_cast<float>(std::clamp(a / 0.3, -1.0, 1.0));
  return std::fabs(v) < 0.03f ? 0.0f : v;
}

}  // namespace photoplus::ops
