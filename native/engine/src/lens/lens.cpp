#include "photoplus/lens.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "../portrait/common.hpp"
#include "photoplus/enhance.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::lens {
namespace {

using portrait::detail::offsets;
using portrait::detail::Rgb;

Error ok() { return Error{PP_OK, {}}; }

bool isColour(const ImageBuffer& img) {
  return img.format() == PixelFormat::RGBA8 || img.format() == PixelFormat::BGRA8;
}

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// Full-resolution luma (0..1) of a rectangle.
Result<Plane> lumaTile(const ImageBuffer& img, int32_t x0, int32_t y0, int32_t w, int32_t h) {
  auto p = Plane::allocate(w, h);
  if (!p) return failure(p.error());
  const Rgb o = offsets(img);
  for (int32_t y = 0; y < h; ++y) {
    const uint8_t* src = img.row(y0 + y) + static_cast<size_t>(x0) * 4;
    float* dst = p->row(y);
    for (int32_t x = 0; x < w; ++x, src += 4) {
      dst[x] = (0.299f * src[o.r] + 0.587f * src[o.g] + 0.114f * src[o.b]) * (1.0f / 255.0f);
    }
  }
  return p;
}

// Bilinear sample of one 8-bit channel (offset `c` within 4-byte pixels).
float channelAt(const ImageBuffer& img, int c, float fx, float fy) {
  const int32_t w = img.width(), h = img.height();
  fx = std::clamp(fx, 0.0f, static_cast<float>(w - 1));
  fy = std::clamp(fy, 0.0f, static_cast<float>(h - 1));
  const int32_t x0 = std::min(static_cast<int32_t>(fx), w - 2 < 0 ? 0 : w - 2);
  const int32_t y0 = std::min(static_cast<int32_t>(fy), h - 2 < 0 ? 0 : h - 2);
  const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
  const uint8_t* r0 = img.row(y0) + static_cast<size_t>(x0) * 4 + c;
  const uint8_t* r1 = img.row(y0 + 1) + static_cast<size_t>(x0) * 4 + c;
  const float top = r0[0] + (static_cast<float>(r0[4]) - r0[0]) * tx;
  const float bot = r1[0] + (static_cast<float>(r1[4]) - r1[0]) * tx;
  return top + (bot - top) * ty;
}

// Same, from a single-channel buffer.
float grayAt(const ImageBuffer& img, float fx, float fy) {
  const int32_t w = img.width(), h = img.height();
  fx = std::clamp(fx, 0.0f, static_cast<float>(w - 1));
  fy = std::clamp(fy, 0.0f, static_cast<float>(h - 1));
  const int32_t x0 = std::min(static_cast<int32_t>(fx), std::max(w - 2, 0));
  const int32_t y0 = std::min(static_cast<int32_t>(fy), std::max(h - 2, 0));
  const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
  const uint8_t* r0 = img.row(y0) + x0;
  const uint8_t* r1 = img.row(y0 + 1) + x0;
  const float top = r0[0] + (static_cast<float>(r0[1]) - r0[0]) * tx;
  const float bot = r1[0] + (static_cast<float>(r1[1]) - r1[0]) * tx;
  return top + (bot - top) * ty;
}

struct Geometry {
  float cx, cy, halfDiag;
  explicit Geometry(const ImageBuffer& img)
      : cx(0.5f * static_cast<float>(img.width() - 1)),
        cy(0.5f * static_cast<float>(img.height() - 1)),
        halfDiag(0.5f * std::hypot(static_cast<float>(img.width()), static_cast<float>(img.height()))) {}
};

// ---- 1. Sharpness across the frame -----------------------------------------
//
// Each region gets an edge-based blur estimate, and a lens model is fitted
// to them: blur² = a + b·r² (r = distance from the centre, 1 at the
// corners), as field curvature makes a lens softer towards the corners. The
// lens' own blur must be judged where the photo is in focus, so the fit
// starts from the inner regions and only regions far *above* it (an
// out-of-focus background) are left out; regions at or below it always
// count, which keeps the estimate unbiased for evenly focused photos.
struct Sharpness {
  float centre = 0.0f, corner = 0.0f, noise = 0.0f;
  int32_t tiles = 0;
};

Result<Sharpness> measureSharpness(const ImageBuffer& img, const Geometry& g, const CancellationToken* token) {
  const int32_t W = img.width(), H = img.height();
  const int32_t cols = W >= H ? 5 : 4, rows = W >= H ? 4 : 5;
  const int32_t tile = std::clamp(std::min(W / cols, H / rows), 48, 512);
  struct T {
    float r2, s2;
  };
  std::vector<T> tiles;
  std::vector<float> noises;
  for (int32_t j = 0; j < rows; ++j) {
    for (int32_t i = 0; i < cols; ++i) {
      if (isCancelled(token)) return failure(cancelledError());
      const float ccx = (static_cast<float>(i) + 0.5f) * static_cast<float>(W) / static_cast<float>(cols);
      const float ccy = (static_cast<float>(j) + 0.5f) * static_cast<float>(H) / static_cast<float>(rows);
      const int32_t x0 = std::clamp(static_cast<int32_t>(ccx) - tile / 2, 0, W - tile);
      const int32_t y0 = std::clamp(static_cast<int32_t>(ccy) - tile / 2, 0, H - tile);
      auto luma = lumaTile(img, x0, y0, tile, tile);
      if (!luma) return failure(luma.error());
      const float noise = enhance::estimateNoise(*luma);
      noises.push_back(noise);
      auto b = enhance::estimateBlur(*luma, noise, token);
      if (!b) return failure(b.error());
      if (b->edges < 32) continue;
      const float r = std::hypot(ccx - g.cx, ccy - g.cy) / g.halfDiag;
      tiles.push_back({r * r, b->sigma * b->sigma});
    }
  }
  Sharpness s;
  std::nth_element(noises.begin(), noises.begin() + static_cast<std::ptrdiff_t>(noises.size() / 2), noises.end());
  s.noise = noises[noises.size() / 2];
  s.tiles = static_cast<int32_t>(tiles.size());
  if (tiles.empty()) return s;
  if (tiles.size() < 3) {
    float lo = tiles[0].s2;
    for (const auto& t : tiles) lo = std::min(lo, t.s2);
    s.centre = s.corner = std::sqrt(lo);
    return s;
  }
  // Start: the inner regions' median, flat across the frame.
  std::vector<float> inner;
  for (const auto& t : tiles)
    if (t.r2 < 0.36f) inner.push_back(t.s2);
  if (inner.empty())
    for (const auto& t : tiles) inner.push_back(t.s2);
  std::nth_element(inner.begin(), inner.begin() + static_cast<std::ptrdiff_t>(inner.size() / 2), inner.end());
  double a = inner[inner.size() / 2], b = 0.0;
  for (int iter = 0; iter < 6; ++iter) {
    double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (const auto& t : tiles) {
      // Over ~1.35x blurrier than the model: out of focus, not the lens.
      const double model = std::max(a + b * t.r2, 0.0);
      if (t.s2 > 1.8 * model + 0.3) continue;
      n += 1;
      sx += t.r2;
      sy += t.s2;
      sxx += static_cast<double>(t.r2) * t.r2;
      sxy += static_cast<double>(t.r2) * t.s2;
    }
    if (n < 2) break;
    const double den = n * sxx - sx * sx;
    double nb = den > 1e-9 ? (n * sxy - sx * sy) / den : 0.0;
    nb = std::max(nb, 0.0);  // a lens is never sharper in the corners than in the centre
    a = (sy - nb * sx) / n;
    b = nb;
  }
  s.centre = static_cast<float>(std::sqrt(std::max(a, 0.0)));
  s.corner = static_cast<float>(std::sqrt(std::max(a + b, 0.0)));
  return s;
}

// ---- 2. Colour fringes ------------------------------------------------------
//
// Lateral chromatic aberration: the lens images red and blue at a slightly
// different scale than green, so edges grow coloured fringes that widen
// towards the corners. For strong edges facing the centre, the radial
// derivative of red (resampled at a candidate scale) is correlated with that
// of green; the best scale is refined to sub-pixel accuracy.
struct Fringe {
  float red = 0.0f, blue = 0.0f;
  bool reliable = false;
};

Fringe measureFringes(const ImageBuffer& img, const Geometry& g) {
  const int32_t W = img.width(), H = img.height();
  const Rgb o = offsets(img);
  struct P {
    float x, y, ux, uy, dg, strength;
  };
  std::vector<P> pts;
  const auto stride = std::max<int32_t>(1, static_cast<int32_t>(std::sqrt(static_cast<double>(W) * H / 1.5e6)));
  auto G = [&](int32_t x, int32_t y) { return static_cast<float>(img.row(y)[static_cast<size_t>(x) * 4 + o.g]); };
  for (int32_t y = 3; y < H - 3; y += stride) {
    const uint8_t* row = img.row(y);
    for (int32_t x = 3; x < W - 3; x += stride) {
      const float dx = static_cast<float>(x) - g.cx, dy = static_cast<float>(y) - g.cy;
      const float r = std::hypot(dx, dy);
      if (r < 0.3f * g.halfDiag) continue;
      const uint8_t* px = row + static_cast<size_t>(x) * 4;
      if (px[o.r] > 250 || px[o.g] > 250 || px[o.b] > 250) continue;  // clipped colour carries no fringe
      const float gx = 0.5f * (G(x + 1, y) - G(x - 1, y)), gy = 0.5f * (G(x, y + 1) - G(x, y - 1));
      const float ux = dx / r, uy = dy / r;
      const float gr = gx * ux + gy * uy;
      if (std::fabs(gr) < 12.0f || gr * gr < 0.64f * (gx * gx + gy * gy)) continue;
      pts.push_back({static_cast<float>(x), static_cast<float>(y), ux, uy, 0.0f, std::fabs(gr)});
    }
  }
  Fringe f;
  if (pts.size() < 300) return f;
  constexpr size_t kMax = 40000;
  if (pts.size() > kMax) {
    std::nth_element(pts.begin(), pts.begin() + kMax, pts.end(), [](const P& a, const P& b) { return a.strength > b.strength; });
    pts.resize(kMax);
  }
  double gg = 0.0;
  for (auto& p : pts) {
    // Edge locations are compared (absolute radial gradients, mean removed),
    // so a red edge whose contrast differs from green's still lines up.
    p.dg = std::fabs(channelAt(img, o.g, p.x + p.ux, p.y + p.uy) - channelAt(img, o.g, p.x - p.ux, p.y - p.uy));
  }
  double meanG = 0.0;
  for (const auto& p : pts) meanG += p.dg;
  meanG /= static_cast<double>(pts.size());
  for (auto& p : pts) {
    p.dg = static_cast<float>(p.dg - meanG);
    gg += static_cast<double>(p.dg) * p.dg;
  }
  constexpr int kSteps = 24;                  // ±6 px at the corners
  const float step = 0.25f / g.halfDiag;      // 0.25 px at the corners
  auto solve = [&](int c, bool& reliable) -> float {
    std::array<double, 2 * kSteps + 1> score{};
    for (int k = -kSteps; k <= kSteps; ++k) {
      const float s = 1.0f + static_cast<float>(k) * step;
      double num = 0.0, cc = 0.0, sum = 0.0;
      for (const auto& p : pts) {
        const float sx = g.cx + (p.x - g.cx) * s, sy = g.cy + (p.y - g.cy) * s;
        const double d = std::fabs(channelAt(img, c, sx + p.ux, sy + p.uy) - channelAt(img, c, sx - p.ux, sy - p.uy));
        num += d * p.dg;  // p.dg has zero mean, so this is a covariance
        cc += d * d;
        sum += d;
      }
      const double n = static_cast<double>(pts.size());
      const double var = cc - sum * sum / n;
      score[static_cast<size_t>(k + kSteps)] = var > 0.0 ? num / std::sqrt(var * gg) : 0.0;
    }
    size_t best = 0;
    for (size_t i = 1; i < score.size(); ++i)
      if (score[i] > score[best]) best = i;
    reliable = score[best] > 0.5;
    float offset = 0.0f;
    if (best > 0 && best + 1 < score.size()) {
      const double a = score[best - 1], b = score[best], c2 = score[best + 1];
      const double den = a - 2.0 * b + c2;
      if (den < 0.0) offset = static_cast<float>(std::clamp(0.5 * (a - c2) / den, -0.5, 0.5));
    }
    // At the edge of the search range the true value may lie beyond: then it
    // is not a fringe this model can explain, so nothing is corrected.
    if (best == 0 || best + 1 == score.size()) reliable = false;
    return (static_cast<float>(static_cast<int>(best) - kSteps) + offset) * step;
  };
  bool okR = false, okB = false;
  const float red = solve(o.r, okR), blue = solve(o.b, okB);
  f.red = okR ? red : 0.0f;
  f.blue = okB ? blue : 0.0f;
  f.reliable = okR || okB;
  return f;
}

float gradeOf(const Analysis& a, int32_t w, int32_t h) {
  const float extra = std::sqrt(std::max(a.cornerBlur * a.cornerBlur - a.detailBlur * a.detailBlur, 0.0f));
  const float detail = clamp01((1.7f - a.detailBlur) / 1.0f);
  const float corners = clamp01(1.0f - extra / 1.5f);
  const float fringes = clamp01(1.0f - (a.fringePx - 0.5f) / 2.0f);
  const float noise = clamp01(1.0f - (a.noise - 0.006f) / 0.02f);
  const float mp = static_cast<float>(w) * static_cast<float>(h) / 1e6f;
  const float resolution = clamp01((mp - 2.0f) / 6.0f);
  return 0.4f * detail + 0.15f * corners + 0.15f * fringes + 0.15f * noise + 0.15f * resolution;
}

}  // namespace

Result<Analysis> analyze(const ImageBuffer& image, const CancellationToken* token) {
  ScopedTimer timer("lens.analyze");
  if (!isColour(image)) return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Lens analysis needs a colour photo"));
  if (image.width() < 64 || image.height() < 64) {
    return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Photo is too small to judge"));
  }
  const Geometry g(image);
  Analysis a;
  auto s = measureSharpness(image, g, token);
  if (!s) return failure(s.error());
  a.detailBlur = s->centre;
  a.cornerBlur = std::max(s->corner, s->centre);
  a.noise = s->noise;
  a.measuredTiles = s->tiles;
  if (isCancelled(token)) return failure(cancelledError());
  const Fringe f = measureFringes(image, g);
  a.fringeRed = f.red;
  a.fringeBlue = f.blue;
  a.fringePx = std::max(std::fabs(f.red), std::fabs(f.blue)) * g.halfDiag;
  if (isCancelled(token)) return failure(cancelledError());
  a.reliable = a.measuredTiles >= 4;
  a.grade = gradeOf(a, image.width(), image.height());
  return a;
}

Error restore(const ImageBuffer& input, ImageBuffer& output, const Params& params, const CancellationToken* token,
              Report* report) {
  ScopedTimer timer("lens.restore");
  if (params.upscale != 1 && params.upscale != 2) return makeError(PP_ERROR_INVALID_ARGUMENT, "upscale must be 1 or 2");
  if (!isColour(input) || !isColour(output)) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Transform needs colour photos");
  if (output.width() != input.width() * params.upscale || output.height() != input.height() * params.upscale) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Output must match the input size times upscale");
  }
  const float strength = clamp01(params.strength);
  auto measured = analyze(input, token);
  if (!measured) return measured.error();
  const Analysis a = *measured;
  Report rep;
  rep.before = a;
  const Geometry g(input);
  const int32_t W = input.width(), H = input.height();

  // 1. Optics: re-align the colour fringes, into a working buffer the
  //    enhancement pass then reads (the output itself when the size is
  //    unchanged).
  ImageBuffer staging;
  const bool sameSize = params.upscale == 1;
  if (!sameSize) {
    auto st = ImageBuffer::allocate(W, H, input.format());
    if (!st) return st.error();
    staging = std::move(st).value();
  }
  ImageBuffer& work = sameSize ? output : staging;
  const bool fixFringes = a.fringePx >= 0.35f && strength > 0.0f;
  const bool aliased = work.row(0) == input.row(0);
  if (fixFringes || !aliased) {
    ImageBuffer redCopy, blueCopy;
    const Rgb oi = offsets(input), oo = offsets(work);
    if (fixFringes) {
      // Copies of red and blue: the output may be the input itself.
      auto rc = ImageBuffer::allocate(W, H, PixelFormat::Gray8);
      if (!rc) return rc.error();
      auto bc = ImageBuffer::allocate(W, H, PixelFormat::Gray8);
      if (!bc) return bc.error();
      redCopy = std::move(rc).value();
      blueCopy = std::move(bc).value();
      for (int32_t y = 0; y < H; ++y) {
        const uint8_t* src = input.row(y);
        uint8_t* r = redCopy.row(y);
        uint8_t* b = blueCopy.row(y);
        for (int32_t x = 0; x < W; ++x) {
          r[x] = src[static_cast<size_t>(x) * 4 + oi.r];
          b[x] = src[static_cast<size_t>(x) * 4 + oi.b];
        }
      }
      rep.fringeFixedPx = a.fringePx * strength;
    }
    const float sr = 1.0f + a.fringeRed * strength, sb = 1.0f + a.fringeBlue * strength;
    const bool ok = parallelFor(H, token, [&](int32_t begin, int32_t end) {
      for (int32_t y = begin; y < end; ++y) {
        const uint8_t* src = input.row(y);
        uint8_t* dst = work.row(y);
        const float dy = static_cast<float>(y) - g.cy;
        for (int32_t x = 0; x < W; ++x) {
          const uint8_t* s = src + static_cast<size_t>(x) * 4;
          uint8_t* d = dst + static_cast<size_t>(x) * 4;
          const uint8_t green = s[oi.g], alpha = s[3];
          uint8_t red = s[oi.r], blue = s[oi.b];
          if (fixFringes) {
            const float dx = static_cast<float>(x) - g.cx;
            red = static_cast<uint8_t>(std::lround(std::clamp(grayAt(redCopy, g.cx + dx * sr, g.cy + dy * sr), 0.0f, 255.0f)));
            blue = static_cast<uint8_t>(std::lround(std::clamp(grayAt(blueCopy, g.cx + dx * sb, g.cy + dy * sb), 0.0f, 255.0f)));
          }
          d[oo.r] = red;
          d[oo.g] = green;
          d[oo.b] = blue;
          d[3] = alpha;
        }
      }
    });
    if (!ok) return cancelledError();
  }

  // 2. Detail, noise and finish through the enhancement pipeline, with the
  //    deblur set by the lens measurement (and stronger in the corners).
  //    Always at the photo's own resolution: restoring before enlarging
  //    keeps leftover grain fine instead of growing it into blotches.
  enhance::Params ep;
  ep.upscale = 1;
  ep.denoise = 1.0f;
  ep.fullDenoise = true;  // a high-end camera's photos are clean, even zoomed in
  ep.sharpen = strength;
  ep.detail = params.detail * strength;
  ep.autoTone = false;  // never re-balance the light: golden hour stays golden
  ep.vibrance = params.enrich ? 0.15f * strength : 0.0f;
  // The centre pass uses the lens model's blur across the central area
  // (r² = 0.1, where subjects usually are), the corner pass the rest. The
  // model's blur is the optical blur itself (enhance treats a measurement as
  // optical blur plus the ~0.4 px every capture reads, hence the + 0.16).
  const float c2 = a.detailBlur * a.detailBlur, k2 = a.cornerBlur * a.cornerBlur;
  const float centre2 = std::max(c2 + 0.1f * (k2 - c2), 0.25f);
  ep.measuredBlur = a.measuredTiles > 0 ? std::sqrt(centre2 + 0.16f) : 0.0f;
  const float extra = std::sqrt(std::max(k2 - centre2, 0.0f));
  ep.cornerBlur = extra >= 0.35f ? std::min(extra, 1.5f) * strength : 0.0f;
  enhance::Report er;
  const ImageBuffer* frame = &work;
  auto run = enhance::run({frame}, ep, token, &er, &work);
  if (!run) return run.error();

  // 3. Twice the detail: Catmull-Rom enlargement of the restored photo, with
  //    a gentle, edge-limited sharpen that offsets the resampling softness.
  if (!sameSize) {
    auto ycc = toYcc(staging, token);
    if (!ycc) return ycc.error();
    staging = ImageBuffer{};
    const int32_t ow = output.width(), oh = output.height();
    YccImage big;
    auto y = resizeCubic(ycc->y, ow, oh, token);
    if (!y) return y.error();
    ycc->y = Plane{};
    auto cb = resizeCubic(ycc->cb, ow, oh, token);
    if (!cb) return cb.error();
    ycc->cb = Plane{};
    auto cr = resizeCubic(ycc->cr, ow, oh, token);
    if (!cr) return cr.error();
    ycc->cr = Plane{};
    big.y = std::move(y).value();
    big.cb = std::move(cb).value();
    big.cr = std::move(cr).value();
    {
      auto soft = Plane::allocate(ow, oh);
      if (!soft) return soft.error();
      if (Error e = gaussianBlur(big.y, *soft, 1.0f, token); e.status != PP_OK) return e;
      const float amount = 0.45f * strength;
      for (size_t i = 0; i < big.y.size(); ++i) {
        const float d = big.y.data()[i] - soft->data()[i];
        big.y.data()[i] = std::clamp(big.y.data()[i] + amount * d / (1.0f + 12.0f * std::fabs(d)), 0.0f, 1.0f);
      }
    }
    if (Error e = fromYcc(big, output, token); e.status != PP_OK) return e;
  }
  rep.deblurCentre = er.deblurSigma * static_cast<float>(params.upscale);
  rep.deblurCorner = ep.cornerBlur * static_cast<float>(params.upscale);
  rep.noiseBefore = er.noiseBefore;
  rep.noiseAfter = er.noiseAfter;
  if (report != nullptr) *report = rep;
  return ok();
}

}  // namespace photoplus::lens
