// Video stabilisation and temporal helpers (Phase 6, "Glide").
//
// Motion: both frames are reduced to ~512 px luma. A coarse global search
// finds the main shift; then ~100 textured blocks are matched around it
// (sum of absolute differences, sub-pixel by parabola fit) and a similarity
// (shift, rotation, scale) is fitted to their displacements by least
// squares, re-fitted on the inliers twice. A person moving in the frame is a
// minority of blocks and drops out as outliers, so the camera's own motion
// is what remains.
//
// Path: the per-frame motions are chained into the camera's path relative
// to frame 0, its shift, rotation and scale are smoothed with a Gaussian
// (point-reflected at the ends so pans keep their speed), and each frame is warped from its real position to the
// smooth one. A crop zoom hides the edges the warp reveals; it is the
// smallest zoom that covers every frame, capped, and frames that would need
// more keep a share of their original motion (eased in and out) instead.
#include <algorithm>
#include <cmath>
#include <numeric>

#include "photoplus/filters.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/profiler.hpp"
#include "photoplus/video.hpp"

namespace photoplus::video {

namespace {

Error ok() { return Error{PP_OK, {}}; }
Error invalid(const char* m) { return makeError(PP_ERROR_INVALID_ARGUMENT, m); }

bool colour(const ImageBuffer& img) {
  return img.format() == PixelFormat::RGBA8 || img.format() == PixelFormat::BGRA8;
}

// Area-averaged luma reduced by an integer factor.
Result<Plane> smallLuma(const ImageBuffer& img, int32_t f) {
  const int32_t w = img.width() / f, h = img.height() / f;
  auto p = Plane::allocate(std::max(1, w), std::max(1, h));
  if (!p) return failure(p.error());
  const int bpp = bytesPerPixel(img.format());
  const bool bgr = img.format() == PixelFormat::BGRA8;
  for (int32_t y = 0; y < p->height(); ++y)
    for (int32_t x = 0; x < p->width(); ++x) {
      float s = 0;
      for (int32_t yy = y * f; yy < (y + 1) * f; ++yy) {
        const uint8_t* r = img.row(yy) + static_cast<size_t>(x) * f * bpp;
        for (int32_t xx = 0; xx < f; ++xx, r += bpp) {
          if (bpp == 1) {
            s += r[0];
          } else {
            const float R = bgr ? r[2] : r[0], B = bgr ? r[0] : r[2];
            s += 0.299f * R + 0.587f * r[1] + 0.114f * B;
          }
        }
      }
      p->row(y)[x] = s / static_cast<float>(f * f * 255);
    }
  return p;
}

Result<Plane> boxDownPlane(const Plane& src, int32_t f) {
  const int32_t w = std::max(1, src.width() / f), h = std::max(1, src.height() / f);
  auto out = Plane::allocate(w, h);
  if (!out) return failure(out.error());
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      float s = 0;
      for (int32_t yy = 0; yy < f; ++yy)
        for (int32_t xx = 0; xx < f; ++xx) s += src.row(std::min(src.height() - 1, y * f + yy))[std::min(src.width() - 1, x * f + xx)];
      out->row(y)[x] = s / static_cast<float>(f * f);
    }
  return out;
}

float sad(const Plane& a, const Plane& b, int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t size) {
  float s = 0;
  for (int32_t y = 0; y < size; ++y) {
    const float* ra = a.row(ay + y) + ax;
    const float* rb = b.row(by + y) + bx;
    for (int32_t x = 0; x < size; ++x) s += std::fabs(ra[x] - rb[x]);
  }
  return s;
}

struct Vec {
  double x, y;
};

// Global shift by exhaustive search on a 4x-reduced copy (robust start).
Vec coarseShift(const Plane& p, const Plane& c, int32_t radius) {
  double best = 1e30;
  Vec bestV{0, 0};
  const int32_t m = radius;
  const int32_t w = p.width() - 2 * m, h = p.height() - 2 * m;
  if (w <= 4 || h <= 4) return bestV;
  for (int32_t dy = -radius; dy <= radius; ++dy)
    for (int32_t dx = -radius; dx <= radius; ++dx) {
      double s = 0;
      for (int32_t y = 0; y < h; y += 2) {
        const float* rp = p.row(m + y) + m;
        const float* rc = c.row(m + y + dy) + m + dx;
        for (int32_t x = 0; x < w; x += 2) s += std::fabs(rp[x] - rc[x]);
      }
      if (s < best) best = s, bestV = {static_cast<double>(dx), static_cast<double>(dy)};
    }
  return bestV;
}

// Solves the least-squares similarity from point pairs (centred coords).
bool fitSimilarity(const std::vector<Vec>& from, const std::vector<Vec>& to, const std::vector<char>& use, Similarity& out) {
  double n = 0, sx = 0, sy = 0, sxp = 0, syp = 0, sxx = 0, sa = 0, sb = 0;
  for (size_t i = 0; i < from.size(); ++i) {
    if (!use[i]) continue;
    n += 1;
    sx += from[i].x, sy += from[i].y, sxp += to[i].x, syp += to[i].y;
  }
  if (n < 3) return false;
  const double mx = sx / n, my = sy / n, mxp = sxp / n, myp = syp / n;
  for (size_t i = 0; i < from.size(); ++i) {
    if (!use[i]) continue;
    const double x = from[i].x - mx, y = from[i].y - my, xp = to[i].x - mxp, yp = to[i].y - myp;
    sxx += x * x + y * y;
    sa += x * xp + y * yp;
    sb += x * yp - y * xp;
  }
  if (sxx < 1e-9) return false;
  out.a = sa / sxx;
  out.b = sb / sxx;
  out.tx = mxp - (out.a * mx - out.b * my);
  out.ty = myp - (out.b * mx + out.a * my);
  return std::isfinite(out.a) && std::isfinite(out.b);
}

// Affine helpers (2x3, row-major) for the path.
struct M23 {
  double a, b, c, d, e, f;  // x' = a x + b y + c, y' = d x + e y + f
};
M23 ofSim(const Similarity& s) { return {s.a, -s.b, s.tx, s.b, s.a, s.ty}; }
M23 mul(const M23& p, const M23& q) {  // p ∘ q
  return {p.a * q.a + p.b * q.d, p.a * q.b + p.b * q.e, p.a * q.c + p.b * q.f + p.c,
          p.d * q.a + p.e * q.d, p.d * q.b + p.e * q.e, p.d * q.c + p.e * q.f + p.f};
}
M23 inverse(const M23& m) {
  const double det = m.a * m.e - m.b * m.d;
  const double ia = m.e / det, ib = -m.b / det, id = -m.d / det, ie = m.a / det;
  return {ia, ib, -(ia * m.c + ib * m.f), id, ie, -(id * m.c + ie * m.f)};
}
struct Params {
  double tx, ty, angle, logScale;
};
Params paramsOf(const M23& m) {
  return {m.c, m.f, std::atan2(m.d, m.a), std::log(std::max(1e-6, std::sqrt(m.a * m.e - m.b * m.d)))};
}
M23 ofParams(const Params& p) {
  const double s = std::exp(p.logScale), c = std::cos(p.angle) * s, n = std::sin(p.angle) * s;
  return {c, -n, p.tx, n, c, p.ty};
}

std::vector<double> gaussianSmooth(const std::vector<double>& v, double sigma) {
  const auto n = static_cast<int32_t>(v.size());
  if (sigma < 0.3 || n < 2) return v;
  const auto r = static_cast<int32_t>(std::ceil(3 * sigma));
  std::vector<double> out(v.size());
  for (int32_t i = 0; i < n; ++i) {
    double s = 0, w = 0;
    for (int32_t k = -r; k <= r; ++k) {
      const int32_t j = i + k;
      // Point reflection at the ends (v(-j) = 2·v(0) − v(j)): a steady pan
      // continues past the ends instead of bending back, so it keeps its speed.
      double x;
      if (j < 0) {
        x = 2 * v[0] - v[static_cast<size_t>(std::min(-j, n - 1))];
      } else if (j >= n) {
        x = 2 * v[static_cast<size_t>(n - 1)] - v[static_cast<size_t>(std::max(0, 2 * (n - 1) - j))];
      } else {
        x = v[static_cast<size_t>(j)];
      }
      const double g = std::exp(-0.5 * k * k / (sigma * sigma));
      s += g * x;
      w += g;
    }
    out[static_cast<size_t>(i)] = s / w;
  }
  return out;
}

// Zoom needed so that output corners map inside the source, for warp W
// (centred coordinates, output o → source W(o / zoom)).
bool fits(const M23& w, double zoom, double hw, double hh) {
  for (const double sx : {-1.0, 1.0})
    for (const double sy : {-1.0, 1.0}) {
      const double ox = sx * hw / zoom, oy = sy * hh / zoom;
      const double x = w.a * ox + w.b * oy + w.c, y = w.d * ox + w.e * oy + w.f;
      if (std::fabs(x) > hw + 1e-6 || std::fabs(y) > hh + 1e-6) return false;
    }
  return true;
}
double zoomNeeded(const M23& w, double hw, double hh, double cap) {
  if (fits(w, 1.0, hw, hh)) return 1.0;
  double lo = 1.0, hi = cap;
  if (!fits(w, hi, hw, hh)) return cap + 1.0;  // not coverable within the cap
  for (int i = 0; i < 30; ++i) {
    const double mid = 0.5 * (lo + hi);
    (fits(w, mid, hw, hh) ? hi : lo) = mid;
  }
  return hi;
}

}  // namespace

Result<Motion> estimateMotion(const ImageBuffer& previous, const ImageBuffer& current, const CancellationToken* token) {
  ScopedTimer timer("video.motion");
  if (previous.empty() || current.empty() || previous.width() != current.width() || previous.height() != current.height()) {
    return failure(invalid("Frames must be non-empty and the same size"));
  }
  if (previous.format() != current.format()) return failure(invalid("Frames must share a format"));
  if (previous.width() < 32 || previous.height() < 32) return failure(invalid("Frames are too small"));
  const int32_t f = std::max(1, (std::max(previous.width(), previous.height()) + 511) / 512);
  auto P = smallLuma(previous, f), C = smallLuma(current, f);
  if (!P) return failure(P.error());
  if (!C) return failure(C.error());
  if (isCancelled(token)) return failure(cancelledError());
  const int32_t w = P->width(), h = P->height();
  // 1. Coarse global shift on a 4x smaller copy (handles fast pans).
  Vec t0{0, 0};
  {
    auto bp = boxDownPlane(*P, 4), bc = boxDownPlane(*C, 4);
    if (bp && bc) {
      const Vec v = coarseShift(*bp, *bc, std::max<int32_t>(4, std::min(bp->width(), bp->height()) / 8));
      t0 = {v.x * 4, v.y * 4};
    }
  }
  // 2. Block matching around it.
  const int32_t bs = 24, search = 6, step = std::max(28, std::min(w, h) / 12);
  const int32_t margin = search + static_cast<int32_t>(std::ceil(std::max(std::fabs(t0.x), std::fabs(t0.y)))) + 2;
  std::vector<Vec> from, to;
  for (int32_t by = margin; by + bs + margin < h; by += step)
    for (int32_t bx = margin; bx + bs + margin < w; bx += step) {
      // Skip flat blocks (sky, walls): they match anywhere.
      double m = 0, m2 = 0;
      for (int32_t y = 0; y < bs; ++y)
        for (int32_t x = 0; x < bs; ++x) {
          const double v = P->row(by + y)[bx + x];
          m += v, m2 += v * v;
        }
      const double n = bs * bs, var = m2 / n - (m / n) * (m / n);
      if (var < 2e-4) continue;
      const auto cx0 = static_cast<int32_t>(std::lround(t0.x)), cy0 = static_cast<int32_t>(std::lround(t0.y));
      float best = 1e30f;
      int32_t bdx = 0, bdy = 0;
      for (int32_t dy = -search; dy <= search; ++dy)
        for (int32_t dx = -search; dx <= search; ++dx) {
          const int32_t qx = bx + cx0 + dx, qy = by + cy0 + dy;
          if (qx < 0 || qy < 0 || qx + bs > w || qy + bs > h) continue;
          const float s = sad(*P, *C, bx, by, qx, qy, bs);
          if (s < best) best = s, bdx = dx, bdy = dy;
        }
      if (best >= 1e29f) continue;
      // Sub-pixel: parabola through the neighbours on each axis.
      auto cost = [&](int32_t dx, int32_t dy) {
        const int32_t qx = bx + cx0 + dx, qy = by + cy0 + dy;
        if (qx < 0 || qy < 0 || qx + bs > w || qy + bs > h) return best;
        return sad(*P, *C, bx, by, qx, qy, bs);
      };
      auto refine = [](float l, float c0, float r) {
        const float d = l - 2 * c0 + r;
        return d > 1e-9f ? std::clamp(0.5f * (l - r) / d, -0.5f, 0.5f) : 0.0f;
      };
      const double sx = refine(cost(bdx - 1, bdy), best, cost(bdx + 1, bdy));
      const double sy = refine(cost(bdx, bdy - 1), best, cost(bdx, bdy + 1));
      const double px = bx + bs * 0.5 - w * 0.5, py = by + bs * 0.5 - h * 0.5;
      from.push_back({px, py});
      to.push_back({px + cx0 + bdx + sx, py + cy0 + bdy + sy});
    }
  Motion out;
  std::vector<char> use(from.size(), 1);
  Similarity s;
  bool good = fitSimilarity(from, to, use, s);
  for (int iter = 0; good && iter < 3; ++iter) {
    std::vector<double> res(from.size());
    for (size_t i = 0; i < from.size(); ++i) {
      const double x = s.a * from[i].x - s.b * from[i].y + s.tx, y = s.b * from[i].x + s.a * from[i].y + s.ty;
      res[i] = std::hypot(x - to[i].x, y - to[i].y);
    }
    std::vector<double> sorted = res;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
    const double limit = std::max(0.75, 2.5 * sorted[sorted.size() / 2]);
    for (size_t i = 0; i < from.size(); ++i) use[i] = res[i] <= limit ? 1 : 0;
    good = fitSimilarity(from, to, use, s);
  }
  const auto inliers = static_cast<float>(std::count(use.begin(), use.end(), 1));
  if (good && from.size() >= 6) {
    out.confidence = inliers / static_cast<float>(from.size());
  } else {
    s = Similarity{1, 0, t0.x, t0.y};  // too little texture: the global shift only
    out.confidence = 0.1f;
  }
  // Implausible zoom or rotation between two frames: keep the shift only.
  const double scale = std::hypot(s.a, s.b);
  if (scale < 0.9 || scale > 1.1 || std::fabs(std::atan2(s.b, s.a)) > 0.1) {
    s = Similarity{1, 0, s.tx, s.ty};
    out.confidence *= 0.5f;
  }
  out.m = Similarity{s.a, s.b, s.tx * f, s.ty * f};
  return out;
}

Result<Plan> stabilize(const std::vector<Motion>& motions, int32_t width, int32_t height, float strength, float maxZoom) {
  ScopedTimer timer("video.stabilize");
  if (motions.empty() || width < 2 || height < 2) return failure(invalid("Need motions and a frame size"));
  if (!std::isfinite(strength) || !std::isfinite(maxZoom)) return failure(invalid("Non-finite stabilisation setting"));
  strength = std::clamp(strength, 0.0f, 1.0f);
  maxZoom = std::clamp(maxZoom, 1.0f, 1.5f);
  const size_t n = motions.size();
  const double hw = width * 0.5, hh = height * 0.5;
  // Camera path: P[k] maps frame-0 content positions to frame k.
  std::vector<M23> P(n);
  P[0] = {1, 0, 0, 0, 1, 0};
  for (size_t k = 1; k < n; ++k) {
    const Similarity& s = motions[k].m;
    if (!std::isfinite(s.a) || !std::isfinite(s.b) || !std::isfinite(s.tx) || !std::isfinite(s.ty)) {
      return failure(invalid("Non-finite motion"));
    }
    P[k] = mul(ofSim(s), P[k - 1]);
  }
  std::vector<double> tx(n), ty(n), an(n), ls(n);
  for (size_t k = 0; k < n; ++k) {
    const Params p = paramsOf(P[k]);
    tx[k] = p.tx, ty[k] = p.ty, an[k] = p.angle, ls[k] = p.logScale;
    if (k > 0) {  // unwrap the angle
      while (an[k] - an[k - 1] > M_PI) an[k] -= 2 * M_PI;
      while (an[k] - an[k - 1] < -M_PI) an[k] += 2 * M_PI;
    }
  }
  // Smoothing over ±(3·sigma) frames; strength 1 ≈ 0.5 s at 30 fps.
  const double sigma = 1.0 + 14.0 * strength;
  const auto stx = gaussianSmooth(tx, sigma), sty = gaussianSmooth(ty, sigma), san = gaussianSmooth(an, sigma),
             sls = gaussianSmooth(ls, sigma);
  // Per frame: how much of the correction fits within the zoom cap.
  auto warpFor = [&](size_t k, double alpha) {
    const Params q{tx[k] + alpha * (stx[k] - tx[k]), ty[k] + alpha * (sty[k] - ty[k]), an[k] + alpha * (san[k] - an[k]),
                   ls[k] + alpha * (sls[k] - ls[k])};
    return mul(P[k], inverse(ofParams(q)));  // output (smooth) → source (real)
  };
  std::vector<double> alpha(n, 1.0);
  for (size_t k = 0; k < n; ++k) {
    if (zoomNeeded(warpFor(k, 1.0), hw, hh, maxZoom) <= maxZoom) continue;
    double lo = 0, hi = 1;
    for (int i = 0; i < 25; ++i) {
      const double mid = 0.5 * (lo + hi);
      (zoomNeeded(warpFor(k, mid), hw, hh, maxZoom) <= maxZoom ? lo : hi) = mid;
    }
    alpha[k] = lo;
  }
  // Ease the held-back frames in and out (a sudden change would read as a jolt):
  // take the minimum over a window, then soften it.
  {
    std::vector<double> m(n);
    const int32_t r = 8;
    for (size_t k = 0; k < n; ++k) {
      double v = 1.0;
      for (int32_t d = -r; d <= r; ++d) {
        const auto j = static_cast<int64_t>(k) + d;
        if (j >= 0 && j < static_cast<int64_t>(n)) v = std::min(v, alpha[static_cast<size_t>(j)]);
      }
      m[k] = v;
    }
    auto smoothAlpha = gaussianSmooth(m, 4.0);
    for (size_t k = 0; k < n; ++k) alpha[k] = std::min(alpha[k], smoothAlpha[k]);
  }
  Plan plan;
  double zoom = 1.0;
  std::vector<M23> W(n);
  for (size_t k = 0; k < n; ++k) {
    W[k] = warpFor(k, alpha[k]);
    zoom = std::max(zoom, std::min<double>(zoomNeeded(W[k], hw, hh, maxZoom), maxZoom));
  }
  plan.zoom = static_cast<float>(zoom);
  // Shake: mean frame-to-frame change of the shown path (corner displacement).
  double before = 0, after = 0;
  for (size_t k = 1; k < n; ++k) {
    auto corner = [&](const M23& m) { return Vec{m.a * hw + m.b * hh + m.c, m.d * hw + m.e * hh + m.f}; };
    const Vec r0 = corner(inverse(P[k - 1])), r1 = corner(inverse(P[k]));
    const M23 s0 = mul(P[k - 1], inverse(W[k - 1])), s1 = mul(P[k], inverse(W[k]));
    const Vec q0 = corner(inverse(s0)), q1 = corner(inverse(s1));
    before += std::hypot(r1.x - r0.x, r1.y - r0.y);
    after += std::hypot(q1.x - q0.x, q1.y - q0.y);
  }
  plan.shakeBefore = n > 1 ? static_cast<float>(before / static_cast<double>(n - 1)) : 0.0f;
  plan.shakeAfter = n > 1 ? static_cast<float>(after / static_cast<double>(n - 1)) : 0.0f;
  // Output pixel (top-left origin) → centred → zoom → warp → source pixel.
  plan.warps.resize(n);
  for (size_t k = 0; k < n; ++k) {
    const M23 Z{1.0 / zoom, 0, -hw / zoom, 0, 1.0 / zoom, -hh / zoom};  // o_centred = (q − c) / zoom
    const M23 T{1, 0, hw, 0, 1, hh};                                     // back to top-left origin
    const M23 m = mul(T, mul(W[k], Z));
    plan.warps[k] = {m.a, m.b, m.c, m.d, m.e, m.f};
  }
  return plan;
}

Error warp(const ImageBuffer& src, const Affine& m, ImageBuffer& dst, const CancellationToken* token) {
  ScopedTimer timer("video.warp");
  if (src.empty() || dst.empty() || src.format() != dst.format()) return invalid("Bad warp images");
  for (double v : m)
    if (!std::isfinite(v)) return invalid("Non-finite warp");
  const int bpp = bytesPerPixel(src.format());
  const int32_t sw = src.width(), sh = src.height();
  const bool done = parallelFor(dst.height(), token, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) {
      uint8_t* d = dst.row(y);
      const double py = y + 0.5;
      for (int32_t x = 0; x < dst.width(); ++x) {
        const double px = x + 0.5;
        const double fx = std::clamp(m[0] * px + m[1] * py + m[2] - 0.5, 0.0, static_cast<double>(sw - 1));
        const double fy = std::clamp(m[3] * px + m[4] * py + m[5] - 0.5, 0.0, static_cast<double>(sh - 1));
        const auto ix = static_cast<int32_t>(fx), iy = static_cast<int32_t>(fy);
        const int32_t jx = std::min(sw - 1, ix + 1), jy = std::min(sh - 1, iy + 1);
        const float tx = static_cast<float>(fx - ix), ty = static_cast<float>(fy - iy);
        const uint8_t* a = src.row(iy) + ix * bpp;
        const uint8_t* b = src.row(iy) + jx * bpp;
        const uint8_t* c = src.row(jy) + ix * bpp;
        const uint8_t* e = src.row(jy) + jx * bpp;
        for (int ch = 0; ch < bpp; ++ch) {
          const float v = (a[ch] * (1 - tx) + b[ch] * tx) * (1 - ty) + (c[ch] * (1 - tx) + e[ch] * tx) * ty;
          d[x * bpp + ch] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
        }
      }
    }
  });
  return done ? ok() : cancelledError();
}

Result<Curves> fitCurves(const ImageBuffer& before, const ImageBuffer& after) {
  if (!colour(before) || before.format() != after.format() || before.width() != after.width() ||
      before.height() != after.height()) {
    return failure(invalid("Curves need two same-sized colour images"));
  }
  Curves c;
  const bool bgr = before.format() == PixelFormat::BGRA8;
  for (int ch = 0; ch < 3; ++ch) {
    const int o = bgr ? 2 - ch : ch;
    std::array<double, 256> sum{}, cnt{};
    for (int32_t y = 0; y < before.height(); y += 2) {
      const uint8_t* p = before.row(y);
      const uint8_t* q = after.row(y);
      for (int32_t x = 0; x < before.width(); x += 2) {
        sum[p[x * 4 + o]] += q[x * 4 + o];
        cnt[p[x * 4 + o]] += 1;
      }
    }
    // Mean output per input level; gaps filled by interpolation; monotone.
    std::array<double, 256> v{};
    int last = -1;
    for (int i = 0; i < 256; ++i) {
      if (cnt[i] < 3) continue;
      v[i] = sum[i] / cnt[i];
      if (last < 0) {
        for (int j = 0; j < i; ++j) v[j] = v[i] * j / std::max(1, i);  // dark end: toward black
      } else {
        for (int j = last + 1; j < i; ++j) v[j] = v[last] + (v[i] - v[last]) * (j - last) / (i - last);
      }
      last = i;
    }
    if (last < 0) {
      for (int i = 0; i < 256; ++i) v[i] = i;
    } else {
      for (int j = last + 1; j < 256; ++j) v[j] = v[last] + (255.0 - v[last]) * (j - last) / std::max(1, 255 - last);
    }
    // Light smoothing, then monotone (a curve must never reverse tones).
    std::array<double, 256> sm{};
    for (int i = 0; i < 256; ++i) {
      double s = 0, w = 0;
      for (int k = -3; k <= 3; ++k) {
        const int j = std::clamp(i + k, 0, 255);
        const double g = std::exp(-0.5 * k * k / 2.0);
        s += g * v[j], w += g;
      }
      sm[i] = s / w;
    }
    double run = 0;
    for (int i = 0; i < 256; ++i) {
      run = std::max(run, sm[i]);
      c.rgb[ch][i] = static_cast<uint8_t>(std::clamp(std::lround(run), 0L, 255L));
    }
  }
  return c;
}

Error applyCurves(ImageBuffer& image, const Curves& curves, const CancellationToken* token) {
  if (!colour(image)) return invalid("Curves need a colour image");
  const bool bgr = image.format() == PixelFormat::BGRA8;
  const bool done = parallelFor(image.height(), token, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) {
      uint8_t* r = image.row(y);
      for (int32_t x = 0; x < image.width(); ++x)
        for (int ch = 0; ch < 3; ++ch) {
          const int o = bgr ? 2 - ch : ch;
          r[x * 4 + o] = curves.rgb[ch][r[x * 4 + o]];
        }
    }
  });
  return done ? ok() : cancelledError();
}

Error enhanceFrame(const ImageBuffer& cur, const ImageBuffer* prev, const ImageBuffer* next, const Similarity& fromPrev,
                   const Similarity& toNext, const FrameParams& params, const Curves* curves, ImageBuffer& dst,
                   const CancellationToken* token) {
  ScopedTimer timer("video.enhanceFrame");
  if (!colour(cur) || dst.format() != cur.format() || dst.width() != cur.width() || dst.height() != cur.height()) {
    return invalid("Frame and destination must be same-sized colour images");
  }
  for (const ImageBuffer* n : {prev, next})
    if (n != nullptr && (n->format() != cur.format() || n->width() != cur.width() || n->height() != cur.height())) {
      return invalid("Neighbour frames must match the frame");
    }
  for (float v : {params.noiseSigma, params.blurSigma, params.denoise, params.sharpen})
    if (!std::isfinite(v)) return invalid("Non-finite frame setting");
  const int32_t w = cur.width(), h = cur.height();
  const double cx = w * 0.5, cy = h * 0.5;
  // Where a pixel of this frame is in the previous one (inverse of fromPrev)
  // and in the next one (toNext), centred coordinates.
  const M23 toPrevM = inverse(ofSim(fromPrev)), toNextM = ofSim(toNext);
  const float den = std::clamp(params.denoise, 0.0f, 1.0f);
  // Agreement threshold: the clip's noise, never below the level of video
  // compression noise (so blocky flicker is averaged away too).
  const float sigma = std::max(params.noiseSigma, 0.006f) * 255.0f;
  const bool temporal = den > 0.0f && (prev != nullptr || next != nullptr);
  const bool done = parallelFor(h, token, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) {
      const uint8_t* c = cur.row(y);
      uint8_t* d = dst.row(y);
      for (int32_t x = 0; x < w; ++x) {
        const uint8_t* p = c + x * 4;
        float acc[3] = {static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2])};
        if (temporal) {
          float wsum = 1.0f;
          const double qx = x + 0.5 - cx, qy = y + 0.5 - cy;
          for (int side = 0; side < 2; ++side) {
            const ImageBuffer* nb = side == 0 ? prev : next;
            if (nb == nullptr) continue;
            const M23& m = side == 0 ? toPrevM : toNextM;
            const double sx = m.a * qx + m.b * qy + m.c + cx - 0.5, sy = m.d * qx + m.e * qy + m.f + cy - 0.5;
            if (sx < 0 || sy < 0 || sx > w - 1 || sy > h - 1) continue;
            const auto ix = static_cast<int32_t>(sx), iy = static_cast<int32_t>(sy);
            const int32_t jx = std::min(w - 1, ix + 1), jy = std::min(h - 1, iy + 1);
            const float tx = static_cast<float>(sx - ix), ty = static_cast<float>(sy - iy);
            float s[3];
            for (int ch = 0; ch < 3; ++ch) {
              s[ch] = (nb->row(iy)[ix * 4 + ch] * (1 - tx) + nb->row(iy)[jx * 4 + ch] * tx) * (1 - ty) +
                      (nb->row(jy)[ix * 4 + ch] * (1 - tx) + nb->row(jy)[jx * 4 + ch] * tx) * ty;
            }
            // Agreeing pixels (same surface, only noise apart) are merged;
            // anything that moved on its own is left out. Judged on every
            // channel: a red shirt moving over a grey wall of the same
            // brightness must not blend into it.
            float diff = 0.0f;
            for (int ch = 0; ch < 3; ++ch) diff = std::max(diff, std::fabs(s[ch] - static_cast<float>(p[ch])));
            diff /= 1.3f * sigma;
            const float wt = den * std::clamp(1.0f - (diff - 1.5f) / 2.0f, 0.0f, 1.0f);
            if (wt <= 0.0f) continue;
            for (int ch = 0; ch < 3; ++ch) acc[ch] += wt * s[ch];
            wsum += wt;
          }
          for (float& a : acc) a /= wsum;
        }
        for (int ch = 0; ch < 3; ++ch) d[x * 4 + ch] = static_cast<uint8_t>(std::clamp(acc[ch] + 0.5f, 0.0f, 255.0f));
        d[x * 4 + 3] = p[3];
      }
    }
  });
  if (!done) return cancelledError();
  // Lens deblur: a sharpen sized to the measured blur (a sharp capture
  // measures ~0.5 px and gets almost nothing; a soft lens gets more).
  const float excess = std::max(0.0f, params.blurSigma - 0.5f);
  const float amount = std::clamp(params.sharpen, 0.0f, 1.0f) * std::min(1.2f, 0.9f * excess);
  if (amount > 0.02f) {
    auto sharp = ops::sharpenLuma(dst, amount, std::clamp(params.blurSigma, 0.8f, 3.0f), 0.01f, token);
    if (!sharp) return sharp.error();
    for (int32_t y = 0; y < h; ++y) std::copy_n(sharp->row(y), static_cast<size_t>(w) * 4, dst.row(y));
  }
  if (curves != nullptr) return applyCurves(dst, *curves, token);
  return ok();
}

Error steadyMask(ImageBuffer& previous, const ImageBuffer& current, float smoothing) {
  if (previous.format() != PixelFormat::Gray8 || current.format() != PixelFormat::Gray8 ||
      previous.width() != current.width() || previous.height() != current.height()) {
    return invalid("Masks must be same-sized GRAY8");
  }
  if (!std::isfinite(smoothing)) return invalid("Non-finite smoothing");
  const float s = std::clamp(smoothing, 0.0f, 0.9f);
  for (int32_t y = 0; y < current.height(); ++y) {
    uint8_t* p = previous.row(y);
    const uint8_t* c = current.row(y);
    for (int32_t x = 0; x < current.width(); ++x) {
      const float d = std::fabs(static_cast<float>(c[x]) - p[x]) / 255.0f;
      // Small disagreement (edge flicker): average. Large (real movement): follow at once.
      const float keep = s * std::clamp(1.0f - (d - 0.1f) / 0.25f, 0.0f, 1.0f);
      p[x] = static_cast<uint8_t>(std::lround(keep * p[x] + (1.0f - keep) * c[x]));
    }
  }
  return ok();
}

Error consensusMask(const ImageBuffer* previous, const ImageBuffer& current, const ImageBuffer* next, ImageBuffer& out) {
  auto same = [&](const ImageBuffer* m) {
    return m == nullptr || (m->format() == PixelFormat::Gray8 && m->width() == current.width() && m->height() == current.height());
  };
  if (current.format() != PixelFormat::Gray8 || !same(previous) || !same(next) || !same(&out)) {
    return invalid("Masks must be same-sized GRAY8");
  }
  for (int32_t y = 0; y < current.height(); ++y) {
    const uint8_t* c = current.row(y);
    const uint8_t* p = previous != nullptr ? previous->row(y) : c;
    const uint8_t* n = next != nullptr ? next->row(y) : c;
    uint8_t* o = out.row(y);
    for (int32_t x = 0; x < current.width(); ++x) o[x] = std::max(std::min(p[x], c[x]), std::min(std::max(p[x], c[x]), n[x]));
  }
  return ok();
}

}  // namespace photoplus::video
