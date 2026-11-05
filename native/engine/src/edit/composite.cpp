#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "../portrait/common.hpp"
#include "photoplus/edit.hpp"
#include "photoplus/enhance.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::edit {
namespace {

using portrait::detail::hashNoise;
using portrait::detail::isColour;
using portrait::detail::offsets;
using portrait::detail::Rgb;
using portrait::detail::toGamma;
using portrait::detail::toLinear;

Error ok() { return Error{PP_OK, {}}; }

// sRGB <-> linear by table: compositing happens in linear light so soft
// edges and shadows blend like real light.
struct Linear {
  std::array<float, 256> fromByte{};
  std::array<float, 4097> toGammaLut{};
  Linear() {
    for (size_t i = 0; i < 256; ++i) fromByte[i] = toLinear(static_cast<float>(i) / 255.0f);
    for (size_t i = 0; i < toGammaLut.size(); ++i) toGammaLut[i] = toGamma(static_cast<float>(i) / 4096.0f);
  }
  float lin(float g) const {  // gamma 0..1 -> linear
    const float f = std::clamp(g, 0.0f, 1.0f) * 255.0f;
    const auto i = std::min<size_t>(static_cast<size_t>(f), 254);
    return fromByte[i] + (fromByte[i + 1] - fromByte[i]) * (f - static_cast<float>(i));
  }
  float gam(float l) const {  // linear -> gamma 0..1
    const float f = std::clamp(l, 0.0f, 1.0f) * 4096.0f;
    const auto i = std::min<size_t>(static_cast<size_t>(f), 4095);
    return toGammaLut[i] + (toGammaLut[i + 1] - toGammaLut[i]) * (f - static_cast<float>(i));
  }
};
const Linear& linear() {
  static const Linear l;
  return l;
}

// A prop as premultiplied float planes (r, g, b, a in 0..1, gamma colour),
// shrunk by box halving until it is at most twice the size it is drawn at,
// so sampling never aliases fine detail into shimmer.
struct Sprite {
  std::array<Plane, 4> p;
  int32_t width() const { return p[3].width(); }
  int32_t height() const { return p[3].height(); }
};

Result<Sprite> spriteFor(const ImageBuffer& prop, float drawWidth) {
  Sprite s;
  for (auto& pl : s.p) {
    auto a = Plane::allocate(prop.width(), prop.height());
    if (!a) return failure(a.error());
    pl = std::move(a).value();
  }
  const Rgb o = offsets(prop);
  for (int32_t y = 0; y < prop.height(); ++y) {
    const uint8_t* q = prop.row(y);
    for (int32_t x = 0; x < prop.width(); ++x, q += 4) {
      const float a = q[3] / 255.0f;
      s.p[0].row(y)[x] = q[o.r] / 255.0f * a;
      s.p[1].row(y)[x] = q[o.g] / 255.0f * a;
      s.p[2].row(y)[x] = q[o.b] / 255.0f * a;
      s.p[3].row(y)[x] = a;
    }
  }
  while (static_cast<float>(s.width()) > 2.0f * drawWidth && s.width() >= 4 && s.height() >= 4) {
    const int32_t w = s.width() / 2, h = s.height() / 2;
    Sprite half;
    for (size_t c = 0; c < 4; ++c) {
      auto a = Plane::allocate(w, h);
      if (!a) return failure(a.error());
      for (int32_t y = 0; y < h; ++y)
        for (int32_t x = 0; x < w; ++x) {
          const Plane& src = s.p[c];
          a->row(y)[x] = 0.25f * (src.row(2 * y)[2 * x] + src.row(2 * y)[2 * x + 1] + src.row(2 * y + 1)[2 * x] +
                                  src.row(2 * y + 1)[2 * x + 1]);
        }
      half.p[c] = std::move(a).value();
    }
    s = std::move(half);
  }
  return s;
}

// Bilinear sample with transparent outside.
float sampleZero(const Plane& p, float x, float y) {
  const int32_t w = p.width(), h = p.height();
  if (x <= -1.0f || y <= -1.0f || x >= static_cast<float>(w) || y >= static_cast<float>(h)) return 0.0f;
  const auto x0 = static_cast<int32_t>(std::floor(x)), y0 = static_cast<int32_t>(std::floor(y));
  const float tx = x - static_cast<float>(x0), ty = y - static_cast<float>(y0);
  auto at = [&](int32_t xx, int32_t yy) { return (xx < 0 || yy < 0 || xx >= w || yy >= h) ? 0.0f : p.row(yy)[xx]; };
  const float top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * tx;
  const float bot = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * tx;
  return top + (bot - top) * ty;
}

// The colour of the light in the photo, from its grey pixels (whites, greys,
// blacks), as per-channel gains with luma 1. Neutral when there are too few.
std::array<float, 3> lightColour(const ImageBuffer& img) {
  const Rgb o = offsets(img);
  const int32_t step = std::max(1, std::max(img.width(), img.height()) / 256);
  double r = 0, g = 0, b = 0;
  size_t n = 0, seen = 0;
  for (int32_t y = 0; y < img.height(); y += step) {
    const uint8_t* row = img.row(y);
    for (int32_t x = 0; x < img.width(); x += step) {
      const uint8_t* p = row + static_cast<size_t>(x) * 4;
      const float pr = p[o.r] / 255.0f, pg = p[o.g] / 255.0f, pb = p[o.b] / 255.0f;
      const float l = 0.299f * pr + 0.587f * pg + 0.114f * pb;
      if (l < 0.15f || l > 0.95f) continue;
      ++seen;
      const float c = std::max({pr, pg, pb}) - std::min({pr, pg, pb});
      if (c > 0.2f * (0.3f + l)) continue;  // near-grey (whites under a warm light still count)
      r += pr / l;
      g += pg / l;
      b += pb / l;
      ++n;
    }
  }
  if (n < 64 || n * 50 < seen) return {1.0f, 1.0f, 1.0f};
  const auto dn = static_cast<double>(n);
  std::array<float, 3> gain{static_cast<float>(r / dn), static_cast<float>(g / dn), static_cast<float>(b / dn)};
  const float l = 0.299f * gain[0] + 0.587f * gain[1] + 0.114f * gain[2];
  for (float& v : gain) v = std::clamp(v / l, 0.8f, 1.25f);
  return gain;
}

}  // namespace

Error placeProp(ImageBuffer& image, const ImageBuffer& prop, const PropParams& pp, const CancellationToken* token,
                PropReport* report) {
  ScopedTimer timer("edit.prop");
  if (!isColour(image) || !isColour(prop)) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Props need colour images");
  for (float v : {pp.cx, pp.cy, pp.width, pp.rotationDeg, pp.opacity, pp.blend, pp.shadow}) {
    if (!std::isfinite(v)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Prop parameters must be finite");
  }
  if (pp.width < 2.0f || prop.width() < 2 || prop.height() < 2) return makeError(PP_ERROR_INVALID_ARGUMENT, "Prop is too small");
  PropReport rep;
  const float opacity = std::clamp(pp.opacity, 0.0f, 1.0f);
  const float blend = std::clamp(pp.blend, 0.0f, 1.0f);
  const float W = pp.width, Hh = W * static_cast<float>(prop.height()) / static_cast<float>(prop.width());
  const float th = pp.rotationDeg * 3.14159265f / 180.0f;
  const float c = std::cos(th), s = std::sin(th);
  const float margin = 0.12f * std::max(W, Hh);
  const float ex = 0.5f * (std::fabs(c) * W + std::fabs(s) * Hh) + margin;
  const float ey = 0.5f * (std::fabs(s) * W + std::fabs(c) * Hh) + margin;
  const int32_t x0 = std::max(0, static_cast<int32_t>(std::floor(pp.cx - ex)));
  const int32_t y0 = std::max(0, static_cast<int32_t>(std::floor(pp.cy - ey)));
  const int32_t x1 = std::min(image.width(), static_cast<int32_t>(std::ceil(pp.cx + ex)));
  const int32_t y1 = std::min(image.height(), static_cast<int32_t>(std::ceil(pp.cy + ey)));
  if (x1 <= x0 || y1 <= y0 || opacity <= 0.0f) {
    if (report != nullptr) *report = rep;
    return ok();
  }
  const int32_t bw = x1 - x0, bh = y1 - y0;
  auto sprite = spriteFor(prop, W);
  if (!sprite) return sprite.error();
  const float k = static_cast<float>(sprite->width()) / W;  // sprite px per image px

  // 1. The prop drawn into a layer covering its box (premultiplied planes).
  std::array<Plane, 4> layer;
  for (auto& pl : layer) {
    auto a = Plane::allocate(bw, bh);
    if (!a) return a.error();
    pl = std::move(a).value();
  }
  const bool drawn = parallelFor(bh, token, [&](int32_t begin, int32_t end) {
    for (int32_t ly = begin; ly < end; ++ly) {
      const float dy = static_cast<float>(y0 + ly) + 0.5f - pp.cy;
      for (int32_t lx = 0; lx < bw; ++lx) {
        const float dx = static_cast<float>(x0 + lx) + 0.5f - pp.cx;
        float u = dx * c + dy * s;
        const float v = -dx * s + dy * c;
        if (pp.flip) u = -u;
        const float sx = (u + 0.5f * W) * k - 0.5f, sy = (v + 0.5f * Hh) * k - 0.5f;
        for (size_t ch = 0; ch < 4; ++ch) layer[ch].row(ly)[lx] = sampleZero(sprite->p[ch], sx, sy);
      }
    }
  });
  if (!drawn) return cancelledError();

  // 2. What the scene is like around the prop.
  const Rgb o = offsets(image);
  auto luma = Plane::allocate(bw, bh);
  if (!luma) return luma.error();
  double lsum = 0.0;
  for (int32_t ly = 0; ly < bh; ++ly) {
    const uint8_t* p = image.row(y0 + ly) + static_cast<size_t>(x0) * 4;
    for (int32_t lx = 0; lx < bw; ++lx, p += 4) {
      const float l = (0.299f * p[o.r] + 0.587f * p[o.g] + 0.114f * p[o.b]) / 255.0f;
      luma->row(ly)[lx] = l;
      lsum += l;
    }
  }
  const float sceneKey = static_cast<float>(lsum / (static_cast<double>(bw) * bh));
  // Light falling across the prop (e.g. a side-lit face): a wide blur of the
  // scene's brightness, relative to its mean under the prop.
  auto light = Plane::allocate(bw, bh);
  if (!light) return light.error();
  if (Error e = gaussianBlur(*luma, *light, std::max(1.0f, 0.25f * std::max(W, Hh)), token); e.status != PP_OK) return e;
  double lm = 0.0, am = 0.0;
  for (int32_t ly = 0; ly < bh; ++ly)
    for (int32_t lx = 0; lx < bw; ++lx) {
      const float a = layer[3].row(ly)[lx];
      lm += a * light->row(ly)[lx];
      am += a;
    }
  const float lightMean = am > 1e-3 ? std::max(0.02f, static_cast<float>(lm / am)) : 0.5f;
  // Darker scenes get a darker prop; the photo's colour of light tints it.
  const float exposure = std::clamp(std::pow(std::max(sceneKey, 0.02f) / 0.42f, 0.3f * blend), 0.7f, 1.15f);
  const auto lc = lightColour(image);
  std::array<float, 3> tint{};
  for (size_t ch = 0; ch < 3; ++ch) tint[ch] = 1.0f + (lc[ch] - 1.0f) * 0.7f * blend;
  // As sharp and as grainy as the photo itself.
  float blurSigma = 0.0f, grain = 0.0f;
  if (bw >= 48 && bh >= 48) {
    grain = enhance::estimateNoise(*luma) * blend;
    auto b = enhance::estimateBlur(*luma, grain, token);
    if (!b) return b.error();
    if (b->edges >= 32) blurSigma = std::min(3.0f, std::max(0.0f, b->sigma - 0.35f) * blend);
  }
  if (blurSigma > 0.2f) {
    for (auto& pl : layer) {
      auto t = Plane::allocate(bw, bh);
      if (!t) return t.error();
      if (Error e = gaussianBlur(pl, *t, blurSigma, token); e.status != PP_OK) return e;
      pl = std::move(t).value();
    }
  }

  // 3. A soft contact shadow under the prop (on the photo only).
  auto shadow = Plane::allocate(bw, bh);
  if (!shadow) return shadow.error();
  if (Error e = gaussianBlur(layer[3], *shadow, std::max(1.0f, 0.05f * Hh), token); e.status != PP_OK) return e;
  const float shadowOffset = 0.04f * Hh;
  const float shadowStrength = 0.5f * std::clamp(pp.shadow, 0.0f, 1.0f) * opacity;

  // 4. Composite in linear light.
  const Linear& L = linear();
  std::vector<int64_t> covered(static_cast<size_t>(bh), 0);
  const bool done = parallelFor(bh, token, [&](int32_t begin, int32_t end) {
    for (int32_t ly = begin; ly < end; ++ly) {
      uint8_t* p = image.row(y0 + ly) + static_cast<size_t>(x0) * 4;
      for (int32_t lx = 0; lx < bw; ++lx, p += 4) {
        const float a = std::clamp(layer[3].row(ly)[lx], 0.0f, 1.0f) * opacity;
        const float sh = shadowStrength * sampleZero(*shadow, static_cast<float>(lx), static_cast<float>(ly) - shadowOffset) * (1.0f - a);
        if (a <= 1e-4f && sh <= 1e-4f) continue;
        std::array<float, 3> dst{L.fromByte[p[o.r]], L.fromByte[p[o.g]], L.fromByte[p[o.b]]};
        for (float& d : dst) d *= 1.0f - sh;
        if (a > 1e-4f) {
          const float shade = std::clamp(std::pow(std::clamp(light->row(ly)[lx] / lightMean, 0.6f, 1.5f), 0.35f * blend), 0.75f, 1.2f);
          const float n = grain * hashNoise(x0 + lx, y0 + ly, 0x9A0Fu);
          const float la = std::max(layer[3].row(ly)[lx], 1e-4f);
          for (size_t ch = 0; ch < 3; ++ch) {
            const float straight = layer[ch].row(ly)[lx] / la;            // the prop's own colour (gamma)
            const float lit = L.lin(straight + n) * exposure * shade * tint[ch];
            dst[ch] = lit * a + dst[ch] * (1.0f - a);
          }
          if (a >= 0.5f) ++covered[static_cast<size_t>(ly)];
        }
        p[o.r] = static_cast<uint8_t>(std::lround(L.gam(dst[0]) * 255.0f));
        p[o.g] = static_cast<uint8_t>(std::lround(L.gam(dst[1]) * 255.0f));
        p[o.b] = static_cast<uint8_t>(std::lround(L.gam(dst[2]) * 255.0f));
      }
    }
  });
  if (!done) return cancelledError();
  for (int64_t v : covered) rep.coveredPixels += v;
  if (report != nullptr) *report = rep;
  return ok();
}

Error overlay(ImageBuffer& image, const ImageBuffer& stamp, int32_t x, int32_t y, float opacity, OverlayReport* report) {
  ScopedTimer timer("edit.overlay");
  if (!isColour(image) || !isColour(stamp)) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Overlay needs colour images");
  if (!std::isfinite(opacity)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Opacity must be finite");
  const float op = std::clamp(opacity, 0.0f, 1.0f);
  const Rgb oi = offsets(image), os = offsets(stamp);
  OverlayReport rep;
  for (int32_t sy = 0; sy < stamp.height(); ++sy) {
    const int32_t iy = y + sy;
    if (iy < 0 || iy >= image.height()) continue;
    const uint8_t* s = stamp.row(sy);
    uint8_t* d = image.row(iy);
    for (int32_t sx = 0; sx < stamp.width(); ++sx) {
      const int32_t ix = x + sx;
      if (ix < 0 || ix >= image.width()) continue;
      const uint8_t* q = s + static_cast<size_t>(sx) * 4;
      const float a = q[3] / 255.0f * op;
      if (a <= 0.0f) continue;
      uint8_t* p = d + static_cast<size_t>(ix) * 4;
      // Text and logos blend like on screen (sRGB), matching the preview.
      p[oi.r] = static_cast<uint8_t>(std::lround(p[oi.r] + (q[os.r] - p[oi.r]) * a));
      p[oi.g] = static_cast<uint8_t>(std::lround(p[oi.g] + (q[os.g] - p[oi.g]) * a));
      p[oi.b] = static_cast<uint8_t>(std::lround(p[oi.b] + (q[os.b] - p[oi.b]) * a));
      if (a >= 0.25f) ++rep.visiblePixels;
    }
  }
  if (report != nullptr) *report = rep;
  return ok();
}

}  // namespace photoplus::edit
