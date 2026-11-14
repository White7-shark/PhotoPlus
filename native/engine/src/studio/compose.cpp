// Studio compose: see studio.hpp. Layout of one run:
//   1. the person's box in the cut-out, and whether their feet are in it
//      (a portrait cut at the chest casts no shadow on the floor);
//   2. the placement as a map from frame to cut-out (anchor, size, mirror,
//      tilt, angle); for strong reductions a pre-reduced cut-out, so hair
//      does not alias;
//   3. a working grid of the background (linear light): statistics around
//      where the person will stand, the lens blur, the light that wraps,
//      the person's silhouette as placed, and the shadows;
//   4. harmonisation, damped so the person keeps their own colours:
//      exposure toward the scene's local brightness, white balance toward
//      the scene's light, contrast toward its spread (after Reinhard et al.
//      2001 and Sunkavalli et al. 2010, at a global scale);
//   5. per pixel: background (softened, shadowed), person (harmonised,
//      wrapped in the scene's light), composited in linear light, the
//      person's grain matched to the background's.
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "../matte/tools.hpp"
#include "photoplus/profiler.hpp"
#include "photoplus/studio.hpp"

namespace photoplus::studio {

using namespace matte;

namespace {

constexpr int32_t kGrid = 1024;

inline float lumOf(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

struct LinLut {
  std::array<float, 256> v{};
  LinLut() {
    for (int i = 0; i < 256; ++i) v[static_cast<size_t>(i)] = toLinear(static_cast<float>(i) / 255.0f);
  }
};
const LinLut& linLut() {
  static const LinLut l;
  return l;
}

// The cut-out (or a reduced copy), sampled bilinearly with premultiplied
// alpha in linear light, so edges carry no dark or bright halo.
struct CutSampler {
  const ImageBuffer* img = nullptr;
  Rgb o{0, 1, 2};
  // Returns premultiplied linear colour and alpha.
  void sample(float x, float y, V3& c, float& a) const {
    const int32_t w = img->width(), h = img->height();
    const float fx = x - 0.5f, fy = y - 0.5f;
    const auto x0 = static_cast<int32_t>(std::floor(fx)), y0 = static_cast<int32_t>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    c = V3{};
    a = 0.0f;
    const auto& L = linLut().v;
    for (int dy = 0; dy < 2; ++dy) {
      const int32_t yy = y0 + dy;
      if (yy < 0 || yy >= h) continue;
      const float wy = dy ? ty : 1.0f - ty;
      const uint8_t* row = img->row(yy);
      for (int dx = 0; dx < 2; ++dx) {
        const int32_t xx = x0 + dx;
        if (xx < 0 || xx >= w) continue;
        const float wgt = wy * (dx ? tx : 1.0f - tx);
        const uint8_t* p = row + xx * 4;
        const float pa = static_cast<float>(p[3]) / 255.0f * wgt;
        if (pa <= 0.0f) continue;
        c = c + V3{L[p[o.r]], L[p[o.g]], L[p[o.b]]} * pa;
        a += pa;
      }
    }
  }
};

// An integer reduction of a straight-alpha cut-out (area average of
// premultiplied colour, stored straight again).
Result<ImageBuffer> reduceCutout(const ImageBuffer& src, int32_t k, const CancellationToken* token) {
  const int32_t w = std::max(1, src.width() / k), h = std::max(1, src.height() / k);
  auto out = ImageBuffer::allocate(w, h, src.format());
  if (!out) return out;
  const auto& L = linLut().v;
  const bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y)
      for (int32_t x = 0; x < w; ++x) {
        float r = 0, g = 0, b = 0, a = 0;
        for (int32_t yy = y * k; yy < (y + 1) * k; ++yy) {
          const uint8_t* row = src.row(yy);
          for (int32_t xx = x * k; xx < (x + 1) * k; ++xx) {
            const uint8_t* p = row + xx * 4;
            const float pa = static_cast<float>(p[3]) / 255.0f;
            r += L[p[0]] * pa, g += L[p[1]] * pa, b += L[p[2]] * pa, a += pa;
          }
        }
        uint8_t* d = out->row(y) + x * 4;
        const float n = static_cast<float>(k * k);
        if (a > 1e-4f) {
          d[0] = toByte(toGamma(r / a)), d[1] = toByte(toGamma(g / a)), d[2] = toByte(toGamma(b / a));
        } else {
          d[0] = d[1] = d[2] = 0;
        }
        d[3] = toByte(a / n);
      }
  });
  if (!done) return failure(cancelledError());
  return out;
}

// Noise level (std-dev, gamma 0..1) of an image's luma from flat-ish
// places: the median absolute deviation from a 3x3 mean.
float noiseLevel(const ImageBuffer& img, int32_t x0, int32_t y0, int32_t x1, int32_t y1, bool useAlpha) {
  const Rgb o = offsets(img);
  std::vector<float> dev;
  const int32_t step = std::max(1, std::max(x1 - x0, y1 - y0) / 400);
  auto luma = [&](int32_t x, int32_t y) {
    const uint8_t* p = img.row(y) + x * 4;
    return (0.299f * p[o.r] + 0.587f * p[o.g] + 0.114f * p[o.b]) / 255.0f;
  };
  for (int32_t y = std::max(1, y0); y < std::min(img.height() - 1, y1); y += step)
    for (int32_t x = std::max(1, x0); x < std::min(img.width() - 1, x1); x += step) {
      if (useAlpha) {
        bool solid = true;
        for (int dy = -1; dy <= 1 && solid; ++dy)
          for (int dx = -1; dx <= 1; ++dx)
            if (img.row(y + dy)[(x + dx) * 4 + 3] < 250) {
              solid = false;
              break;
            }
        if (!solid) continue;
      }
      float m = 0;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) m += luma(x + dx, y + dy);
      dev.push_back(std::fabs(luma(x, y) - m / 9.0f));
    }
  if (dev.size() < 50) return 0.0f;
  // The quieter half of the samples: texture is not noise.
  std::sort(dev.begin(), dev.end());
  const float med = dev[dev.size() / 4];
  return med * 1.4826f * 1.12f;  // MAD → sigma; the 3x3 mean removes ~1/9 of the variance
}

}  // namespace

Error compose(ImageBuffer& background, const ImageBuffer& cutout, const Placement& placement, const Look& look,
              const CancellationToken* token, ComposeReport* report) {
  ScopedTimer timer("studio.compose");
  if (!isColour(background) || cutout.format() != background.format()) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Compose needs RGBA images of the same channel order");
  }
  if (background.width() < 16 || background.height() < 16 || cutout.width() < 4 || cutout.height() < 4) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Image too small");
  }
  const float vals[] = {placement.x, placement.y, placement.height, placement.tilt, placement.angle, look.colorMatch,
                        look.lightWrap, look.shadow, look.castShadow, look.lightX, look.horizon, look.bgBlur, look.grain, look.rim,
                        look.spotlight, look.gain, look.contrast, look.balance[0], look.balance[1], look.balance[2],
                        placement.boxLeft, placement.boxTop, placement.boxRight, placement.boxBottom};
  for (float v : vals)
    if (!std::isfinite(v)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Bad placement");
  if (placement.height <= 0.01f || placement.height > 20.0f) return makeError(PP_ERROR_INVALID_ARGUMENT, "Bad size");
  if (placement.fixedBox && (placement.boxRight - placement.boxLeft < 1e-3f || placement.boxBottom - placement.boxTop < 1e-3f)) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Bad box");
  }
  if (report != nullptr) *report = ComposeReport{};
  const int32_t W = background.width(), H = background.height();
  const Rgb o = offsets(background);
  const float colorMatch = std::clamp(look.colorMatch, 0.0f, 1.0f), wrap = std::clamp(look.lightWrap, 0.0f, 1.0f);
  const float shadow = std::clamp(look.shadow, 0.0f, 1.0f), cast = std::clamp(look.castShadow, 0.0f, 1.0f);
  const float spot = std::clamp(look.spotlight, 0.0f, 1.0f);
  // Spotlight softens the scene further (as a wider aperture would).
  const float bgBlur = std::clamp(look.bgBlur + 0.45f * spot * (1.0f - look.bgBlur), 0.0f, 1.0f);
  const float grain = std::clamp(look.grain, 0.0f, 1.0f);
  const float lightX = std::clamp(look.lightX, -1.0f, 1.0f), horizon = std::clamp(look.horizon, 0.0f, 1.0f);
  const float tilt = std::clamp(placement.tilt, -1.0f, 1.0f);
  const float rim = std::clamp(look.rim, 0.0f, 1.0f);

  // 1. The person's box in the cut-out.
  const int32_t cw = cutout.width(), ch = cutout.height();
  int32_t bl = cw, br = -1, bt = ch, bb = -1;
  int32_t bottomRun = 0;
  for (int32_t y = 0; y < ch; ++y) {
    const uint8_t* row = cutout.row(y);
    for (int32_t x = 0; x < cw; ++x)
      if (row[x * 4 + 3] > 25) {
        bl = std::min(bl, x), br = std::max(br, x), bt = std::min(bt, y), bb = std::max(bb, y);
        if (y == ch - 1) ++bottomRun;
      }
  }
  bool grounded = static_cast<float>(bottomRun) / static_cast<float>(cw) < 0.03f;
  if (placement.fixedBox) {
    // The caller's box (video: the same for every frame; the person may even be out of it).
    bl = std::clamp(static_cast<int32_t>(std::lround(placement.boxLeft * static_cast<float>(cw))), 0, cw - 1);
    br = std::clamp(static_cast<int32_t>(std::lround(placement.boxRight * static_cast<float>(cw))) - 1, bl, cw - 1);
    bt = std::clamp(static_cast<int32_t>(std::lround(placement.boxTop * static_cast<float>(ch))), 0, ch - 1);
    bb = std::clamp(static_cast<int32_t>(std::lround(placement.boxBottom * static_cast<float>(ch))) - 1, bt, ch - 1);
    grounded = placement.boxBottom < 0.97f;
  } else if (br < 0) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "The cut-out is empty");
  }
  const float boxH = static_cast<float>(bb + 1 - bt);
  const float ancCx = 0.5f * static_cast<float>(bl + br + 1), ancCy = static_cast<float>(bb + 1);

  // 2. Placement: frame → cut-out.
  const float s = placement.height * static_cast<float>(H) / boxH;  // cut-out px → frame px
  const float ancX = placement.x * static_cast<float>(W), ancY = placement.y * static_cast<float>(H);
  const float rad = placement.angle * 3.14159265f / 180.0f;
  const float cs = std::cos(rad), sn = std::sin(rad);
  const float topScale = 1.0f - 0.25f * tilt;  // width at the top of the box relative to the feet
  // Reduced cut-out for strong reductions.
  ImageBuffer reduced;
  int32_t k = 1;
  if (s < 0.6f) k = std::max(1, static_cast<int32_t>(std::floor(1.0f / s)));
  if (k > 1) {
    auto r = reduceCutout(cutout, k, token);
    if (!r) return r.error();
    reduced = std::move(r).value();
  }
  CutSampler cut;
  cut.img = k > 1 ? &reduced : &cutout;
  cut.o = offsets(cutout);
  const float invK = 1.0f / static_cast<float>(k);
  // Frame point (x, y) (pixel centres) → cut-out coordinates; false if outside the box.
  auto toCut = [&](float x, float y, float& cx, float& cy) {
    const float dx = x - ancX, dy = y - ancY;
    const float qx = cs * dx + sn * dy, qy = -sn * dx + cs * dy;  // undo the rotation
    const float v = std::clamp(-qy / (boxH * s), 0.0f, 1.2f);     // 0 at the feet, 1 at the top
    const float widen = 1.0f + (topScale - 1.0f) * v;
    float ux = qx / (s * widen);
    if (placement.flip) ux = -ux;
    cx = ancCx + ux, cy = ancCy + qy / s;
    return cx >= static_cast<float>(bl) - 2 && cx <= static_cast<float>(br) + 3 && cy >= static_cast<float>(bt) - 2 &&
           cy <= static_cast<float>(bb) + 3;
  };
  // The person's box in the frame (forward map of the corners).
  float fl = 1e9f, fr = -1e9f, ft = 1e9f, fb = -1e9f;
  for (int corner = 0; corner < 4; ++corner) {
    const float ux = (corner & 1 ? static_cast<float>(br + 1) : static_cast<float>(bl)) - ancCx;
    const float uy = (corner & 2 ? static_cast<float>(bb + 1) : static_cast<float>(bt)) - ancCy;
    const float v = std::clamp(-uy / boxH, 0.0f, 1.0f);
    float qx = ux * s * (1.0f + (topScale - 1.0f) * v);
    if (placement.flip) qx = -qx;
    const float qy = uy * s;
    const float x = ancX + cs * qx - sn * qy, y = ancY + sn * qx + cs * qy;
    fl = std::min(fl, x), fr = std::max(fr, x), ft = std::min(ft, y), fb = std::max(fb, y);
  }
  const int32_t px0 = std::clamp(static_cast<int32_t>(std::floor(fl)) - 2, 0, W);
  const int32_t px1 = std::clamp(static_cast<int32_t>(std::ceil(fr)) + 2, 0, W);
  const int32_t py0 = std::clamp(static_cast<int32_t>(std::floor(ft)) - 2, 0, H);
  const int32_t py1 = std::clamp(static_cast<int32_t>(std::ceil(fb)) + 2, 0, H);

  // 3. Background grid (linear).
  const int32_t f = std::max<int32_t>(1, (std::max(W, H) + kGrid - 1) / kGrid);
  const int32_t lw = (W + f - 1) / f, lh = (H + f - 1) / f;
  const float shortG = static_cast<float>(std::min(lw, lh));
  auto lowR = allocRgb(lw, lh);
  if (!lowR) return lowR.error();
  Rgb3 low = std::move(lowR).value();
  {
    const auto& L = linLut().v;
    const bool done = parallelFor(lh, token, [&](int32_t begin, int32_t end) {
      for (int32_t ly = begin; ly < end; ++ly)
        for (int32_t lx = 0; lx < lw; ++lx) {
          float sr = 0, sg = 0, sb = 0;
          int32_t n = 0;
          for (int32_t y = ly * f; y < std::min(H, (ly + 1) * f); ++y) {
            const uint8_t* row = background.row(y);
            for (int32_t x = lx * f; x < std::min(W, (lx + 1) * f); ++x, ++n) {
              const uint8_t* q = row + x * 4;
              sr += L[q[o.r]], sg += L[q[o.g]], sb += L[q[o.b]];
            }
          }
          const float kk = n ? 1.0f / static_cast<float>(n) : 0.0f;
          low.r.row(ly)[lx] = sr * kk, low.g.row(ly)[lx] = sg * kk, low.b.row(ly)[lx] = sb * kk;
        }
    });
    if (!done) return cancelledError();
  }
  const float invF = 1.0f / static_cast<float>(f);
  // The person as placed, on the grid.
  auto paR = Plane::allocate(lw, lh);
  if (!paR) return paR.error();
  Plane& PA = *paR;
  for (int32_t ly = 0; ly < lh; ++ly)
    for (int32_t lx = 0; lx < lw; ++lx) {
      float cx, cy, a = 0.0f;
      V3 c;
      if (toCut((static_cast<float>(lx) + 0.5f) * static_cast<float>(f), (static_cast<float>(ly) + 0.5f) * static_cast<float>(f), cx, cy)) {
        cut.sample(cx * invK, cy * invK, c, a);
      }
      PA.row(ly)[lx] = a;
    }
  const float footG = ancY * invF, subjHG = placement.height * static_cast<float>(lh), horizonG = horizon * static_cast<float>(lh);

  // 4. Harmonisation.
  float gain = 1.0f, contrast = 1.0f, logMeanS = 0.0f;
  V3 balance{1, 1, 1};
  if (colorMatch > 0.0f) {
    // The person's interior.
    double sr = 0, sg = 0, sb = 0, sl = 0, sl2 = 0, n = 0;
    const auto& L = linLut().v;
    const int32_t step = std::max(1, static_cast<int32_t>(std::sqrt(static_cast<double>(cw) * ch / 60000.0)));
    for (int32_t y = bt; y <= bb; y += step) {
      const uint8_t* row = cutout.row(y);
      for (int32_t x = bl; x <= br; x += step) {
        const uint8_t* p = row + x * 4;
        if (p[3] < 242) continue;
        const float r = L[p[cut.o.r]], g = L[p[cut.o.g]], b = L[p[cut.o.b]];
        const float lum = std::max(lumOf(r, g, b), 1e-4f);
        sr += r, sg += g, sb += b, sl += std::log(lum), sl2 += std::log(lum) * std::log(lum), n += 1;
      }
    }
    // The scene where the person stands: the box and around it.
    double br2 = 0, bg2 = 0, bb2 = 0, bl2 = 0, bl22 = 0, bn = 0;
    const float gx0 = fl * invF, gx1 = fr * invF, gy0 = ft * invF, gy1 = fb * invF;
    const float mx = 0.5f * (gx1 - gx0) + 0.1f * shortG, my = 0.35f * (gy1 - gy0) + 0.1f * shortG;
    for (int32_t ly = 0; ly < lh; ++ly)
      for (int32_t lx = 0; lx < lw; ++lx) {
        const float x = static_cast<float>(lx) + 0.5f, y = static_cast<float>(ly) + 0.5f;
        const float ddx = std::max({gx0 - x, x - gx1, 0.0f}) / mx, ddy = std::max({gy0 - y, y - gy1, 0.0f}) / my;
        const float wgt = std::exp(-(ddx * ddx + ddy * ddy)) * 0.9f + 0.1f;
        const float r = low.r.row(ly)[lx], g = low.g.row(ly)[lx], b = low.b.row(ly)[lx];
        const float lum = std::max(lumOf(r, g, b), 1e-4f);
        br2 += wgt * r, bg2 += wgt * g, bb2 += wgt * b, bl2 += wgt * std::log(lum), bl22 += wgt * std::log(lum) * std::log(lum), bn += wgt;
      }
    if (n > 50 && bn > 0) {
      const V3 ms{static_cast<float>(sr / n), static_cast<float>(sg / n), static_cast<float>(sb / n)};
      const V3 mb{static_cast<float>(br2 / bn), static_cast<float>(bg2 / bn), static_cast<float>(bb2 / bn)};
      logMeanS = static_cast<float>(sl / n);
      const float logMeanB = static_cast<float>(bl2 / bn);
      const auto sdS = static_cast<float>(std::sqrt(std::max(1e-4, sl2 / n - (sl / n) * (sl / n))));
      const auto sdB = static_cast<float>(std::sqrt(std::max(1e-4, bl22 / bn - (bl2 / bn) * (bl2 / bn))));
      // Exposure: people are lit like their scene, but stay a touch brighter
      // than the average of it, and a dark scene does not swallow them.
      // Damped and bounded: a white studio is bright because its walls are
      // white, not because the light on the person is stronger.
      gain = std::clamp(std::exp(0.25f * colorMatch * (logMeanB + 0.3f - logMeanS)), 0.7f, 1.22f);
      // White balance: the scene's colour of light against the person's,
      // damped (a red dress is not a red light).
      const float lms = std::max(lumOf(ms.r, ms.g, ms.b), 1e-4f), lmb = std::max(lumOf(mb.r, mb.g, mb.b), 1e-4f);
      const float raw[3] = {(mb.r / lmb) / std::max(ms.r / lms, 1e-3f), (mb.g / lmb) / std::max(ms.g / lms, 1e-3f),
                            (mb.b / lmb) / std::max(ms.b / lms, 1e-3f)};
      float gch[3];
      for (int c = 0; c < 3; ++c) gch[c] = std::clamp(std::pow(std::max(raw[c], 1e-3f), 0.4f * colorMatch), 0.82f, 1.22f);
      const float norm = lumOf(gch[0], gch[1], gch[2]);
      balance = V3{gch[0] / norm, gch[1] / norm, gch[2] / norm};
      contrast = std::clamp(std::pow(sdB / sdS, 0.25f * colorMatch), 0.95f, 1.1f);
    }
  }

  if (look.fixedHarmony) {
    gain = std::clamp(look.gain, 0.5f, 1.5f), contrast = std::clamp(look.contrast, 0.8f, 1.25f);
    balance = V3{std::clamp(look.balance[0], 0.7f, 1.4f), std::clamp(look.balance[1], 0.7f, 1.4f), std::clamp(look.balance[2], 0.7f, 1.4f)};
    if (logMeanS == 0.0f) {
      // The contrast pivot: the person's mean light, measured here.
      double sl = 0, n = 0;
      const auto& L = linLut().v;
      for (int32_t y = bt; y <= bb; y += 4)
        for (int32_t x = bl; x <= br; x += 4) {
          const uint8_t* p = cutout.row(y) + x * 4;
          if (p[3] < 242) continue;
          sl += std::log(std::max(lumOf(L[p[cut.o.r]], L[p[cut.o.g]], L[p[cut.o.b]]), 1e-4f)), n += 1;
        }
      logMeanS = n > 0 ? static_cast<float>(sl / n) : std::log(0.18f);
    }
  }

  // Spotlight: a pool of light around the person (an ellipse a little wider
  // than them), the scene falling off outside it.
  const float poolX = 0.5f * (fl + fr), poolY = ft + 0.5f * (fb - ft);
  const float poolRx = std::max(0.75f * (fr - fl), 0.22f * static_cast<float>(W));
  const float poolRy = std::max(0.62f * (fb - ft), 0.22f * static_cast<float>(H));
  auto poolAt = [&](float x, float y) {
    const float dx = (x - poolX) / poolRx, dy = (y - poolY) / poolRy;
    return std::exp(-0.5f * (dx * dx + dy * dy));
  };

  // Background blur on the grid, sharper on the ground near the feet.
  Rgb3 soft;
  const bool blurBg = bgBlur > 0.0f;
  if (blurBg) {
    const float sigma = std::max(0.6f, bgBlur * 0.014f * shortG);
    auto sr = blurred(low.r, sigma, token), sg = blurred(low.g, sigma, token), sb = blurred(low.b, sigma, token);
    if (!sr || !sg || !sb) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to compose");
    soft = Rgb3{std::move(sr).value(), std::move(sg).value(), std::move(sb).value()};
  }
  auto focusWeight = [&](float yG) {
    if (!grounded) return 1.0f;
    if (yG <= horizonG) return 1.0f;
    // On the ground: in focus at the feet, softer nearer the camera and the horizon.
    const float span = std::max(footG - horizonG, 0.05f * static_cast<float>(lh));
    return std::clamp(std::fabs(yG - footG) / span, 0.0f, 1.0f);
  };
  // Light that wraps: the (softened) scene, blurred.
  Rgb3 wrapSrc;
  Plane edgeBand, rimSide;
  if (wrap > 0.0f || rim > 0.0f) {
    const Rgb3& base = blurBg ? soft : low;
    auto wr = blurred(base.r, 0.02f * shortG, token), wg = blurred(base.g, 0.02f * shortG, token), wb = blurred(base.b, 0.02f * shortG, token);
    // The wrap is a thin rim, scaled to the person (not the frame).
    auto lowA = blurred(PA, std::clamp(0.012f * subjHG, 0.6f, 0.012f * shortG), token);
    if (!wr || !wg || !wb || !lowA) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to compose");
    wrapSrc = Rgb3{std::move(wr).value(), std::move(wg).value(), std::move(wb).value()};
    if (rim > 0.0f) {
      // Which way each edge faces: >0 where the person lies to the right
      // (their left-hand edge on screen).
      auto sideR = Plane::allocate(lw, lh);
      if (!sideR) return sideR.error();
      for (int32_t ly = 0; ly < lh; ++ly)
        for (int32_t lx = 0; lx < lw; ++lx) {
          const float gx = lowA->at(lx + 2, ly) - lowA->at(lx - 2, ly);
          sideR->row(ly)[lx] = std::clamp(gx * 8.0f, -1.0f, 1.0f);
        }
      rimSide = std::move(sideR).value();
    }
    for (size_t i = 0; i < lowA->size(); ++i) lowA->data()[i] = smoothstep(0.1f, 0.6f, 1.0f - lowA->data()[i]);
    edgeBand = std::move(lowA).value();
  }
  // Shadows.
  Plane shade;
  const bool shadows = grounded && (shadow > 0.0f || cast > 0.0f) && footG > horizonG;
  if (shadows) {
    auto feet = Plane::allocate(lw, lh), proj = Plane::allocate(lw, lh);
    if (!feet || !proj) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to compose");
    std::fill(feet->data(), feet->data() + feet->size(), 0.0f);
    std::fill(proj->data(), proj->data() + proj->size(), 0.0f);
    const float band = std::max(1.5f, 0.05f * subjHG);
    const float drop = std::max(0.5f, 0.012f * subjHG);
    // Contact: the bottom of the person, nudged onto the floor and squashed.
    for (int32_t ly = 0; ly < lh; ++ly)
      for (int32_t lx = 0; lx < lw; ++lx) {
        const float y = static_cast<float>(ly) + 0.5f;
        if (y < footG - band || y > footG + 1.0f) continue;
        const float a = PA.row(ly)[lx];
        if (a <= 0.05f) continue;
        const auto ty = static_cast<int32_t>(std::lround(footG - 0.5f + drop * 0.5f + (y - footG) * 0.25f));
        if (ty >= 0 && ty < lh) feet->row(ty)[lx] = std::max(feet->row(ty)[lx], a);
      }
    // Cast: every part of the person projected onto the ground, away from the light.
    const float dX = -lightX * 0.9f, dY = -0.22f;
    if (cast > 0.0f) {
      for (int32_t ly = 0; ly < lh; ++ly) {
        const float y = static_cast<float>(ly) + 0.5f;
        if (y > footG) continue;
        const float t = footG - y;
        if (t > 1.15f * subjHG) continue;
        for (int32_t lx = 0; lx < lw; ++lx) {
          const float a = PA.row(ly)[lx];
          if (a <= 0.2f) continue;
          const float sx = static_cast<float>(lx) + 0.5f + dX * t, sy = footG + dY * t;
          if (sy <= horizonG + 1.0f) continue;
          const auto ix = static_cast<int32_t>(sx), iy = static_cast<int32_t>(sy);
          if (ix < 0 || iy < 0 || ix >= lw || iy >= lh) continue;
          const float v = a * (1.0f - 0.55f * std::clamp(t / subjHG, 0.0f, 1.0f));
          proj->row(iy)[ix] = std::max(proj->row(iy)[ix], v);
          if (iy + 1 < lh) proj->row(iy + 1)[ix] = std::max(proj->row(iy + 1)[ix], v);
        }
      }
    }
    auto contact = blurred(*feet, std::max(0.8f, 0.018f * subjHG), token);
    auto ambient = blurred(*feet, std::max(1.5f, 0.07f * subjHG), token);
    if (!contact || !ambient) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to compose");
    Plane castNear, castFar;
    if (cast > 0.0f) {
      auto n1 = blurred(*proj, std::max(0.8f, 0.012f * subjHG), token), n2 = blurred(*proj, std::max(1.5f, 0.045f * subjHG), token);
      if (!n1 || !n2) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to compose");
      castNear = std::move(n1).value(), castFar = std::move(n2).value();
    }
    // Normalise contact so a small foot still darkens the floor under it.
    float cmax = 0.0f, amax = 0.0f;
    for (size_t i = 0; i < contact->size(); ++i) cmax = std::max(cmax, contact->data()[i]), amax = std::max(amax, ambient->data()[i]);
    auto shadeR = Plane::allocate(lw, lh);
    if (!shadeR) return shadeR.error();
    shade = std::move(shadeR).value();
    for (int32_t ly = 0; ly < lh; ++ly)
      for (int32_t lx = 0; lx < lw; ++lx) {
        const size_t i = static_cast<size_t>(ly) * static_cast<size_t>(lw) + static_cast<size_t>(lx);
        const float y = static_cast<float>(ly) + 0.5f;
        float c = std::min(1.0f, contact->data()[i] / std::max(cmax, 1e-4f) * 1.4f);
        float am = std::min(1.0f, ambient->data()[i] / std::max(amax, 1e-4f) * 1.2f);
        const float ground = smoothstep(horizonG, horizonG + 0.03f * static_cast<float>(lh), y);
        float v = shadow * (0.62f * c + 0.28f * am) * ground;
        if (cast > 0.0f) {
          const float dist = std::clamp((footG - y) / std::max(1.0f, -dY * subjHG), 0.0f, 1.0f);
          const float cv = castNear.data()[i] + (castFar.data()[i] - castNear.data()[i]) * dist;
          v = 1.0f - (1.0f - v) * (1.0f - cast * 0.5f * std::min(1.0f, cv) * ground);
        }
        shade.data()[i] = 1.0f - std::clamp(v, 0.0f, 0.8f);
      }
  }

  // Grain.
  float noiseAdd = 0.0f;
  if (grain > 0.0f) {
    const float nb = noiseLevel(background, 0, 0, W, H, false);
    const float ns = noiseLevel(cutout, bl, bt, br + 1, bb + 1, true) * std::min(1.0f, s);  // reduction averages it away
    noiseAdd = grain * std::sqrt(std::max(0.0f, nb * nb - ns * ns));
  }

  // 5. Per pixel.
  const auto& Lut = linLut().v;
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* row = background.row(y);
      const float ly = (static_cast<float>(y) + 0.5f) * invF - 0.5f;
      const float fw = blurBg ? focusWeight(ly + 0.5f) : 0.0f;
      const bool inRows = y >= py0 && y < py1;
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* q = row + x * 4;
        const float lx = (static_cast<float>(x) + 0.5f) * invF - 0.5f;
        V3 B{Lut[q[o.r]], Lut[q[o.g]], Lut[q[o.b]]};
        bool touched = false;
        if (blurBg && fw > 0.0f) {
          B = mix(B, soft.sample(lx, ly), fw * smoothstep(0.0f, 0.25f, bgBlur));
          touched = true;
        }
        if (shadows) {
          const float sv = shade.sample(lx, ly);
          if (sv < 0.999f) B = B * sv, touched = true;
        }
        float pool = 0.0f;
        if (spot > 0.0f) {
          pool = poolAt(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
          B = B * (1.0f - spot * 0.55f * (1.0f - pool) + spot * 0.1f * pool);
          touched = true;
        }
        float a = 0.0f;
        V3 Fp;
        if (inRows && x >= px0 && x < px1) {
          float cx, cy;
          if (toCut(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, cx, cy)) cut.sample(cx * invK, cy * invK, Fp, a);
        }
        if (a <= 0.002f) {
          if (touched) q[o.r] = toByte(toGamma(B.r)), q[o.g] = toByte(toGamma(B.g)), q[o.b] = toByte(toGamma(B.b));
          continue;
        }
        V3 F = Fp * (1.0f / a);
        F = F * balance * gain;
        if (spot > 0.0f) {
          // Lit by the spotlight.
          F = F * (1.0f + 0.2f * spot * (0.5f + 0.5f * pool));
        }
        if (contrast != 1.0f) {
          const float lum = std::max(lumOf(F.r, F.g, F.b), 1e-5f);
          const float lg = std::log(lum), target = logMeanS + std::log(gain) + contrast * (lg - logMeanS - std::log(gain));
          F = F * (std::exp(target) / lum);
        }
        if (wrap > 0.0f || rim > 0.0f) {
          const float bandv = edgeBand.sample(lx, ly);
          if (bandv > 0.0f) {
            const V3 sceneLight = wrapSrc.sample(lx, ly);
            if (wrap > 0.0f) F = mix(F, screen(F, sceneLight), wrap * 0.45f * bandv);
            if (rim > 0.0f) {
              // Light from the left lights the edges whose outside is on the left.
              const float facing = std::clamp(-lightX * rimSide.sample(lx, ly), 0.0f, 1.0f);
              const float rimK = rim * facing * smoothstep(0.3f, 0.9f, a) * bandv;
              if (rimK > 0.0f) F = screen(F, sceneLight * (1.6f * rimK));
            }
          }
        }
        const V3 outc = F * a + B * (1.0f - a);
        float r = toGamma(std::max(outc.r, 0.0f)), g = toGamma(std::max(outc.g, 0.0f)), b = toGamma(std::max(outc.b, 0.0f));
        if (noiseAdd > 0.0f) {
          const float nz = noiseAdd * hashNoise(x, y, look.seed) * a;
          r += nz, g += nz, b += nz;
        }
        q[o.r] = toByte(r), q[o.g] = toByte(g), q[o.b] = toByte(b);
      }
    }
  });
  if (!done) return cancelledError();
  if (report != nullptr) {
    report->left = std::clamp(fl / static_cast<float>(W), 0.0f, 1.0f);
    report->right = std::clamp(fr / static_cast<float>(W), 0.0f, 1.0f);
    report->top = std::clamp(ft / static_cast<float>(H), 0.0f, 1.0f);
    report->bottom = std::clamp(fb / static_cast<float>(H), 0.0f, 1.0f);
    report->grounded = grounded;
    report->exposure = gain;
    report->balance = {balance.r, balance.g, balance.b};
    report->contrast = contrast;
  }
  return ok();
}

}  // namespace photoplus::studio
