// Relight (Photoshoot Studio): the person, relit, back in their own photo.
//
// The person's cut-out (straight alpha, the photo's own frame) is lit by the
// studio light; this puts it back over the photo, whose background can be
// brightened or darkened (a studio's darker backdrop), softened (a blur that
// leaves the person out, so no halo of their colours) and given more or less
// colour.
#include <algorithm>
#include <cmath>

#include "../portrait/common.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/profiler.hpp"
#include "photoplus/studio.hpp"

namespace photoplus::studio {

namespace {
using portrait::detail::offsets;
inline float c01(float v) { return std::clamp(v, 0.0f, 1.0f); }
}  // namespace

Error relightCompose(ImageBuffer& photo, const ImageBuffer& lit, const RelightBackground& bg, const CancellationToken* token) {
  ScopedTimer timer("studio.relightCompose");
  const int32_t W = photo.width(), H = photo.height();
  if (lit.width() != W || lit.height() != H) return Error{PP_ERROR_INVALID_ARGUMENT, "The cut-out must match the photo"};
  const auto fin = [](float v) { return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f; };
  const float brightness = fin(bg.brightness), colour = fin(bg.colour);
  const float blur = std::isfinite(bg.blur) ? std::clamp(bg.blur, 0.0f, 1.0f) : 0.0f;
  const auto po = offsets(photo), lo = offsets(lit);

  // The background softened without the person: a blur of background-only
  // colour, divided by the blur of the background's share (normalised), on a
  // grid of at most 512 px (a soft blur needs no more).
  const int32_t f = std::max<int32_t>(1, (std::max(W, H) + 511) / 512);
  const int32_t gw = (W + f - 1) / f, gh = (H + f - 1) / f;
  Result<Plane> pr = Plane::allocate(1, 1), pg = Plane::allocate(1, 1), pb = Plane::allocate(1, 1), pw = Plane::allocate(1, 1);
  if (blur > 0.0f) {
    pr = Plane::allocate(gw, gh), pg = Plane::allocate(gw, gh), pb = Plane::allocate(gw, gh), pw = Plane::allocate(gw, gh);
    for (auto* p : {&pr, &pg, &pb, &pw})
      if (!*p) return p->error();
    for (int32_t gy = 0; gy < gh; ++gy)
      for (int32_t gx = 0; gx < gw; ++gx) {
        float r = 0, g = 0, b = 0, w = 0;
        for (int32_t y = gy * f; y < std::min(H, (gy + 1) * f); ++y)
          for (int32_t x = gx * f; x < std::min(W, (gx + 1) * f); ++x) {
            const uint8_t* p = photo.row(y) + x * 4;
            const float a = 1.0f - lit.row(y)[x * 4 + 3] / 255.0f;  // the background's share
            r += a * p[po.r];
            g += a * p[po.g];
            b += a * p[po.b];
            w += a;
          }
        const float n = static_cast<float>(f * f) * 255.0f;
        pr->row(gy)[gx] = r / n;
        pg->row(gy)[gx] = g / n;
        pb->row(gy)[gx] = b / n;
        pw->row(gy)[gx] = w / static_cast<float>(f * f);
      }
    const float sigma = std::max(0.5f, blur * 0.025f * static_cast<float>(std::max(gw, gh)));
    auto tmp = Plane::allocate(gw, gh);
    if (!tmp) return tmp.error();
    for (auto* p : {&pr, &pg, &pb, &pw}) {
      if (Error e = gaussianBlur(**p, *tmp, sigma, token); e.status != PP_OK) return e;
      std::swap(**p, *tmp);
    }
  }
  const float k = brightness > 0.0f ? 1.2f * brightness : 0.7f * brightness;  // v(1+k)/(1+kv)
  const float mixBlur = c01(blur * 3.0f);
  const float inv = 1.0f / static_cast<float>(f);
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* row = photo.row(y);
      const uint8_t* lrow = lit.row(y);
      const float sy = (static_cast<float>(y) + 0.5f) * inv - 0.5f;
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* p = row + x * 4;
        float r = p[po.r] / 255.0f, g = p[po.g] / 255.0f, b = p[po.b] / 255.0f;
        if (blur > 0.0f) {
          const float sx = (static_cast<float>(x) + 0.5f) * inv - 0.5f;
          const float w = pw->sample(sx, sy);
          if (w > 1e-3f) {
            r += (pr->sample(sx, sy) / w - r) * mixBlur;
            g += (pg->sample(sx, sy) / w - g) * mixBlur;
            b += (pb->sample(sx, sy) / w - b) * mixBlur;
          }
        }
        if (k != 0.0f) {
          r = r * (1.0f + k) / (1.0f + k * r);
          g = g * (1.0f + k) / (1.0f + k * g);
          b = b * (1.0f + k) / (1.0f + k * b);
        }
        if (colour != 0.0f) {
          const float l = 0.299f * r + 0.587f * g + 0.114f * b, s = 1.0f + colour;
          r = l + (r - l) * s, g = l + (g - l) * s, b = l + (b - l) * s;
        }
        // The relit person over it.
        const uint8_t* q = lrow + x * 4;
        const float a = q[3] / 255.0f;
        r += (q[lo.r] / 255.0f - r) * a;
        g += (q[lo.g] / 255.0f - g) * a;
        b += (q[lo.b] / 255.0f - b) * a;
        p[po.r] = static_cast<uint8_t>(std::lround(c01(r) * 255.0f));
        p[po.g] = static_cast<uint8_t>(std::lround(c01(g) * 255.0f));
        p[po.b] = static_cast<uint8_t>(std::lround(c01(b) * 255.0f));
        p[3] = 255;
      }
    }
  });
  return done ? Error{PP_OK, {}} : cancelledError();
}

}  // namespace photoplus::studio
