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
float smoothstep(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

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

// ------------------------------------------------------------- room memory

void RoomMemory::reset() {
  w_ = h_ = 0;
  colour_.clear();
  seen_.clear();
  held_.clear();
}

Result<float> RoomMemory::check(const ImageBuffer& frame, ImageBuffer& mask, const Similarity* fromPrevious,
                               const CancellationToken* token) {
  ScopedTimer timer("video.room");
  const PixelFormat ff = frame.format();
  if ((ff != PixelFormat::RGBA8 && ff != PixelFormat::BGRA8) || mask.format() != PixelFormat::Gray8) {
    return failure(invalid("RoomMemory needs an RGBA frame and a GRAY8 mask"));
  }
  if (frame.width() < 16 || frame.height() < 16 || mask.width() < 16 || mask.height() < 16) {
    return failure(invalid("Frame or mask too small"));
  }
  if (isCancelled(token)) return failure(cancelledError());
  // A coarse grid (longest side 320): each cell a few pixels, so a little
  // hand shake or noise does not matter, and the work is a few milliseconds.
  const int32_t MW = mask.width(), MH = mask.height();
  const float scale = 320.0f / static_cast<float>(std::max(MW, MH));
  const int32_t gw = std::max<int32_t>(8, static_cast<int32_t>(std::lround(static_cast<float>(MW) * std::min(scale, 1.0f))));
  const int32_t gh = std::max<int32_t>(8, static_cast<int32_t>(std::lround(static_cast<float>(MH) * std::min(scale, 1.0f))));
  bool fresh = false;
  if (gw != w_ || gh != h_) {
    reset();
    w_ = gw, h_ = gh;
    colour_.assign(static_cast<size_t>(gw) * gh * 3, 0.0f);
    seen_.assign(static_cast<size_t>(gw) * gh, 0);
    held_.assign(static_cast<size_t>(gw) * gh, 0.0f);
    fresh = true;
  }
  const size_t n = static_cast<size_t>(gw) * gh;
  // The room memory follows the camera: each cell takes what the previous
  // frame showed at the point that moved onto it (nearest cell; off-frame:
  // unseen). Where the person has been stays put: in a selfie the person
  // moves with the phone while the room slides behind them.
  if (fromPrevious != nullptr && !fresh) {
    const Similarity& S = *fromPrevious;
    const double det = S.a * S.a + S.b * S.b;
    const bool still = std::abs(S.a - 1.0) < 1e-4 && std::abs(S.b) < 1e-4 && std::abs(S.tx) < 0.05 && std::abs(S.ty) < 0.05;
    if (det > 1e-6 && std::isfinite(det) && std::isfinite(S.tx) && std::isfinite(S.ty) && !still) {
      const double cw = static_cast<double>(frame.width()) / gw, ch = static_cast<double>(frame.height()) / gh;
      const double hw = frame.width() * 0.5, hh = frame.height() * 0.5;
      std::vector<float> colour(n * 3, 0.0f);
      std::vector<uint8_t> seen(n, 0);
      for (int32_t y = 0; y < gh; ++y)
        for (int32_t x = 0; x < gw; ++x) {
          const double X = (x + 0.5) * cw - hw - S.tx, Y = (y + 0.5) * ch - hh - S.ty;
          const double px = (S.a * X + S.b * Y) / det + hw, py = (-S.b * X + S.a * Y) / det + hh;
          const auto sx = static_cast<int32_t>(std::floor(px / cw)), sy = static_cast<int32_t>(std::floor(py / ch));
          if (sx < 0 || sy < 0 || sx >= gw || sy >= gh) continue;
          const size_t i = static_cast<size_t>(y) * gw + x, j = static_cast<size_t>(sy) * gw + sx;
          for (int k = 0; k < 3; ++k) colour[i * 3 + static_cast<size_t>(k)] = colour_[j * 3 + static_cast<size_t>(k)];
          seen[i] = seen_[j];
        }
      colour_.swap(colour), seen_.swap(seen);
    }
  }
  const int ro = ff == PixelFormat::RGBA8 ? 0 : 2, bo = 2 - ro;
  // Cell colour (box average of the frame) and cell mask (box average).
  std::vector<float> col(n * 3), m(n);
  const int32_t FW = frame.width(), FH = frame.height();
  for (int32_t gy = 0; gy < gh; ++gy) {
    const int32_t fy0 = gy * FH / gh, fy1 = std::max(fy0 + 1, (gy + 1) * FH / gh);
    const int32_t my0 = gy * MH / gh, my1 = std::max(my0 + 1, (gy + 1) * MH / gh);
    for (int32_t gx = 0; gx < gw; ++gx) {
      const int32_t fx0 = gx * FW / gw, fx1 = std::max(fx0 + 1, (gx + 1) * FW / gw);
      const int32_t mx0 = gx * MW / gw, mx1 = std::max(mx0 + 1, (gx + 1) * MW / gw);
      double r = 0, g = 0, b = 0;
      int32_t cnt = 0;
      // Every other pixel is plenty for a cell average.
      for (int32_t y = fy0; y < fy1; y += 2) {
        const uint8_t* row = frame.row(y);
        for (int32_t x = fx0; x < fx1; x += 2, ++cnt) {
          const uint8_t* q = row + x * 4;
          r += q[ro], g += q[1], b += q[bo];
        }
      }
      const size_t i = static_cast<size_t>(gy) * gw + gx;
      const double inv = 1.0 / (255.0 * std::max(cnt, 1));
      col[i * 3] = static_cast<float>(r * inv), col[i * 3 + 1] = static_cast<float>(g * inv), col[i * 3 + 2] = static_cast<float>(b * inv);
      double ms = 0;
      for (int32_t y = my0; y < my1; ++y) {
        const uint8_t* row = mask.row(y);
        for (int32_t x = mx0; x < mx1; ++x) ms += row[x];
      }
      m[i] = static_cast<float>(ms / (255.0 * (my1 - my0) * (mx1 - mx0)));
    }
  }
  auto blurVec = [&](const std::vector<float>& src, float sigma) -> Result<std::vector<float>> {
    auto p = Plane::allocate(gw, gh), q = Plane::allocate(gw, gh);
    if (!p || !q) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the room"));
    std::copy(src.begin(), src.end(), p->data());
    if (Error e = gaussianBlur(*p, *q, sigma, token); e.status != PP_OK) return failure(e);
    return std::vector<float>(q->data(), q->data() + n);
  };
  const float shortSide = static_cast<float>(std::min(gw, gh));
  auto dist = [](const float* a, const float* b) {
    const float dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
    return std::sqrt(dr * dr + dg * dg + db * db);
  };
  constexpr int kReach = 2;
  constexpr uint8_t kSeen = 3;
  // 0. The camera's exposure and white balance drift as the person moves
  //    (a phone keeps re-metering): the remembered room is brought to this
  //    frame's exposure first, from the cells that are room now and before.
  {
    auto nearNow = blurVec(m, 1.5f);
    if (!nearNow) return failure(nearNow.error());
    std::vector<float> ratio[3];
    for (size_t i = 0; i < n; ++i) {
      if (seen_[i] < kSeen || (*nearNow)[i] > 0.03f) continue;
      for (int k = 0; k < 3; ++k) {
        const float mem = colour_[i * 3 + static_cast<size_t>(k)];
        if (mem > 0.04f) ratio[k].push_back(col[i * 3 + static_cast<size_t>(k)] / mem);
      }
    }
    if (ratio[0].size() > n / 20 && ratio[1].size() > n / 20 && ratio[2].size() > n / 20) {
      for (int k = 0; k < 3; ++k) {
        auto& r = ratio[k];
        std::nth_element(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(r.size() / 2), r.end());
        const float gain = std::clamp(r[r.size() / 2], 0.5f, 2.0f);
        for (size_t i = 0; i < n; ++i) {
          float& c = colour_[i * 3 + static_cast<size_t>(k)];
          c = std::min(1.0f, c * gain);
        }
      }
    }
  }
  // 1. Where the mask matches the remembered room (within ±2 cells).
  std::vector<float> match(n, 0.0f), dmin(n, 1e9f);
  std::vector<int32_t> best(n, -1);
  for (int32_t y = 0; y < gh; ++y)
    for (int32_t x = 0; x < gw; ++x) {
      const size_t i = static_cast<size_t>(y) * gw + x;
      if (m[i] < 0.3f) continue;
      for (int dy = -kReach; dy <= kReach; ++dy)
        for (int dx = -kReach; dx <= kReach; ++dx) {
          const int32_t yy = y + dy, xx = x + dx;
          if (yy < 0 || xx < 0 || yy >= gh || xx >= gw) continue;
          const size_t j = static_cast<size_t>(yy) * gw + xx;
          if (seen_[j] < kSeen) continue;
          const float d = dist(&col[i * 3], &colour_[j * 3]);
          if (d < dmin[i]) dmin[i] = d, best[i] = static_cast<int32_t>(j);
        }
      if (best[i] >= 0) match[i] = 1.0f - smoothstep(0.035f, 0.08f, dmin[i]);
    }
  // 2. The person's colour near each cell, from deep inside the mask and
  //    not from anything that matched the room.
  auto inner = blurVec(m, 0.04f * shortSide);
  if (!inner) return failure(inner.error());
  std::vector<float> wF(n), wr(n), wg(n), wb(n);
  for (size_t i = 0; i < n; ++i) {
    wF[i] = smoothstep(0.85f, 0.98f, (*inner)[i]) * (1.0f - match[i]);
    wr[i] = col[i * 3] * wF[i], wg[i] = col[i * 3 + 1] * wF[i], wb[i] = col[i * 3 + 2] * wF[i];
  }
  const float fs = 0.1f * shortSide;
  auto sw = blurVec(wF, fs), sr = blurVec(wr, fs), sg = blurVec(wg, fs), sb = blurVec(wb, fs);
  if (!sw || !sr || !sg || !sb) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the room"));
  // And the person's colours right beside each cell, from the part of them
  // that has been there all along (at the crown: their hair). A cell that
  // looks like that is the person, however much it also looks like the room.
  std::vector<float> wN(n), nr(n), ng(n), nb(n);
  for (size_t i = 0; i < n; ++i) {
    wN[i] = m[i] * smoothstep(0.6f, 0.9f, held_[i]) * (1.0f - match[i] * (1.0f - smoothstep(0.6f, 0.9f, held_[i])));
    nr[i] = col[i * 3] * wN[i], ng[i] = col[i * 3 + 1] * wN[i], nb[i] = col[i * 3 + 2] * wN[i];
  }
  const float ns = 0.03f * shortSide;
  auto nw = blurVec(wN, ns), nR = blurVec(nr, ns), nG = blurVec(ng, ns), nB = blurVec(nb, ns);
  if (!nw || !nR || !nG || !nB) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the room"));
  // 3. Given back: matches the room, and the person nearby is clearly a
  //    different colour (and further from this cell than the room is). A
  //    swallowed piece of room reaches well out from where the person has
  //    been all along; the person's own edge (dark hair against a dark shelf
  //    is a close call) hugs it. Only cells a few cells clear of it are judged.
  std::vector<float> heldZone(n);
  for (size_t i = 0; i < n; ++i) heldZone[i] = held_[i] > 0.5f ? 1.0f : 0.0f;
  auto nearHeld = blurVec(heldZone, 2.0f);
  if (!nearHeld) return failure(nearHeld.error());
  std::vector<float> give(n, 0.0f);
  for (size_t i = 0; i < n; ++i) {
    if (match[i] <= 0.0f || (*sw)[i] < 1e-3f) continue;
    const float inv = 1.0f / (*sw)[i];
    const float F[3] = {(*sr)[i] * inv, (*sg)[i] * inv, (*sb)[i] * inv};
    const float* room = &colour_[static_cast<size_t>(best[i]) * 3];
    const float distinct = smoothstep(0.08f, 0.18f, dist(F, room));
    float personD = dist(&col[i * 3], F);
    if ((*nw)[i] > 0.02f) {
      const float ni = 1.0f / (*nw)[i];
      const float N[3] = {(*nR)[i] * ni, (*nG)[i] * ni, (*nB)[i] * ni};
      personD = std::min(personD, dist(&col[i * 3], N));
    }
    const float closer = smoothstep(1.3f, 2.2f, personD / std::max(dmin[i], 0.01f)) * smoothstep(0.06f, 0.12f, personD);
    const float clearOfPerson = 1.0f - smoothstep(0.05f, 0.25f, (*nearHeld)[i]);
    // Where the person has been all along is the person (a cheek in shadow
    // against a dark shelf); only a sudden growth into the room is judged.
    const float lately = smoothstep(0.35f, 0.7f, held_[i]);
    // Dark against dark (hair or a shaded cheek against a dark shelf) is a
    // close call: kept, unless the person nearby is a very different colour.
    const float* cc = &col[i * 3];
    const float lightness = 0.299f * cc[0] + 0.587f * cc[1] + 0.114f * cc[2];
    const float notDark = std::max(smoothstep(0.18f, 0.3f, lightness), smoothstep(0.2f, 0.3f, personD));
    give[i] = match[i] * distinct * closer * (1.0f - lately) * clearOfPerson * notDark;
  }
  // 4. Applied to the mask at its own resolution.
  double before = 0, removed = 0;
  const float sx = static_cast<float>(gw) / static_cast<float>(MW), sy = static_cast<float>(gh) / static_cast<float>(MH);
  auto gp = Plane::allocate(gw, gh);
  if (!gp) return failure(gp.error());
  std::copy(give.begin(), give.end(), gp->data());
  for (int32_t y = 0; y < MH; ++y) {
    uint8_t* row = mask.row(y);
    const float gyf = (static_cast<float>(y) + 0.5f) * sy - 0.5f;
    for (int32_t x = 0; x < MW; ++x) {
      if (row[x] == 0) continue;
      before += row[x];
      const float k = gp->sample((static_cast<float>(x) + 0.5f) * sx - 0.5f, gyf);
      if (k <= 0.01f) continue;
      const auto v = static_cast<uint8_t>(std::lround(static_cast<float>(row[x]) * (1.0f - std::clamp(k, 0.0f, 1.0f))));
      removed += row[x] - v;
      row[x] = v;
    }
  }
  // 5. The room as it is now, where it is clearly visible (a margin away
  //    from the corrected mask).
  // Where the person has been lately: an average of the model's own mask
  // (about a second and a half at 30 fps to learn), the first frame as it is.
  // It is the model's mask, not the corrected one, so a correction never
  // feeds on itself: a piece of room swallowed for a few frames stays far
  // from "held", while what the model has always called the person is.
  // Learnt slowly, forgotten quickly: once the model sees room there again
  // (the person moved away), the spot stops counting as theirs.
  for (size_t i = 0; i < n; ++i) {
    const float rate = m[i] >= 0.5f ? 0.03f : 0.3f;
    held_[i] = fresh ? m[i] : held_[i] + rate * (m[i] - held_[i]);
  }
  for (size_t i = 0; i < n; ++i) m[i] *= 1.0f - give[i];
  auto near = blurVec(m, 1.5f);
  if (!near) return failure(near.error());
  for (size_t i = 0; i < n; ++i) {
    if ((*near)[i] > 0.03f) continue;  // a few cells clear of the person (hair edges never enter the memory)
    float* c = &colour_[i * 3];
    const float rate = seen_[i] == 0 ? 1.0f : 0.3f;
    for (int k = 0; k < 3; ++k) c[k] += rate * (col[i * 3 + static_cast<size_t>(k)] - c[k]);
    if (seen_[i] < 255) ++seen_[i];
  }
  return before > 0 ? static_cast<float>(removed / before) : 0.0f;
}

}  // namespace photoplus::video
