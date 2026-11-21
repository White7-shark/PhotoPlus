// Studio light and finishing (Photoshoot Studio, Phase 11).
//
//  - relight(): a person (a cut-out) lit as if by studio lights: a key light
//    from any direction and height, hard or soft, in any colour (gels,
//    golden hour); a fill from the other side; rim lights on either edge.
//    A photo has no depth, so the person's shape is estimated: their outline
//    "inflated" (rounder in the middle, turning away at the edges), with a
//    face shape and a nose where the face is. The light is relative: the
//    person's average brightness is kept, and `strength` blends back toward
//    the photo as it was, so anything the estimate gets wrong can be eased.
//  - finish(): the whole frame graded like a photographer would: warmth and
//    tint, contrast, faded blacks, saturation (black and white at -1), split
//    toning (a colour in the shadows, another in the highlights), a vignette
//    and film grain.
//  - paintGradient(): a backdrop that blends from one colour to another
//    (linear, or radial from a pool of light), mixed in linear light and
//    dithered, so it never bands.

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

constexpr float kPi = 3.14159265f;

const std::array<float, 256>& lut() {
  static const std::array<float, 256> t = [] {
    std::array<float, 256> v{};
    for (int i = 0; i < 256; ++i) v[static_cast<size_t>(i)] = toLinear(static_cast<float>(i) / 255.0f);
    return v;
  }();
  return t;
}

struct N3 {
  float x, y, z;
};
N3 norm3(N3 v) {
  const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return l > 1e-6f ? N3{v.x / l, v.y / l, v.z / l} : N3{0, 0, 1};
}
float dot3(N3 a, N3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// A light's direction (towards the light) from its angle around the person
// (0: from the camera; negative: from the left of the frame) and its height.
N3 lightDir(float angleDeg, float heightDeg) {
  const float az = angleDeg * kPi / 180.0f, el = heightDeg * kPi / 180.0f;
  return norm3({std::sin(az) * std::cos(el), -std::sin(el), std::cos(az) * std::cos(el)});
}
bool finite(std::initializer_list<float> vs) {
  for (float v : vs)
    if (!std::isfinite(v)) return false;
  return true;
}
V3 gain3(const std::array<float, 3>& c) { return {std::clamp(c[0], 0.0f, 4.0f), std::clamp(c[1], 0.0f, 4.0f), std::clamp(c[2], 0.0f, 4.0f)}; }

}  // namespace

Error relight(ImageBuffer& cutout, const LightParams& p, const CancellationToken* token) {
  ScopedTimer timer("studio.relight");
  if (!isColour(cutout)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Light needs an RGBA cut-out");
  if (!finite({p.strength, p.keyAngle, p.keyHeight, p.keySoftness, p.key, p.fill, p.rimLeft, p.rimRight, p.faceLeft, p.faceTop, p.faceRight,
               p.faceBottom, p.keyColour[0], p.keyColour[1], p.keyColour[2], p.fillColour[0], p.fillColour[1], p.fillColour[2],
               p.rimColour[0], p.rimColour[1], p.rimColour[2]})) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Bad light settings");
  }
  const float strength = std::clamp(p.strength, 0.0f, 1.0f);
  const float rimL = std::clamp(p.rimLeft, 0.0f, 1.0f), rimR = std::clamp(p.rimRight, 0.0f, 1.0f);
  if (strength <= 0.0f && rimL <= 0.0f && rimR <= 0.0f) return ok();
  const int32_t W = cutout.width(), H = cutout.height();
  const Rgb o = offsets(cutout);
  const auto& L = lut();

  // The working grid (long edge <= 768): the person's cover.
  const int32_t f = std::max(1, (std::max(W, H) + 767) / 768);
  const int32_t w = (W + f - 1) / f, h = (H + f - 1) / f;
  auto aR = Plane::allocate(w, h);
  if (!aR) return aR.error();
  Plane& A = *aR;
  int32_t bl = w, br = -1, bt = h, bb = -1;
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      float s = 0;
      int32_t n = 0;
      for (int32_t yy = y * f; yy < std::min(H, (y + 1) * f); ++yy)
        for (int32_t xx = x * f; xx < std::min(W, (x + 1) * f); ++xx, ++n) s += static_cast<float>(cutout.row(yy)[xx * 4 + 3]) / 255.0f;
      const float a = n ? s / static_cast<float>(n) : 0.0f;
      A.row(y)[x] = a;
      if (a > 0.1f) bl = std::min(bl, x), br = std::max(br, x), bt = std::min(bt, y), bb = std::max(bb, y);
    }
  if (br < 0) return ok();  // nobody there
  const float S = static_cast<float>(std::max(br - bl + 1, bb - bt + 1));

  // The shape: the outline inflated (two scales: the limbs, the body).
  auto h1 = blurred(A, std::max(0.8f, 0.025f * S), token), h2 = blurred(A, std::max(1.0f, 0.09f * S), token);
  if (!h1 || !h2) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to light");
  auto hR = Plane::allocate(w, h);
  if (!hR) return hR.error();
  Plane& Hh = *hR;
  for (size_t i = 0; i < Hh.size(); ++i) Hh.data()[i] = std::sqrt(std::clamp(0.5f * h1->data()[i] + 0.5f * h2->data()[i], 0.0f, 1.0f));
  // The face, when known: an oval dome with a nose down its middle.
  const bool hasFace = p.faceRight - p.faceLeft > 0.01f && p.faceBottom - p.faceTop > 0.01f;
  const float fcx = 0.5f * (p.faceLeft + p.faceRight) * static_cast<float>(w), fcy = (p.faceTop + 0.52f * (p.faceBottom - p.faceTop)) * static_cast<float>(h);
  const float frx = 0.56f * (p.faceRight - p.faceLeft) * static_cast<float>(w), fry = 0.62f * (p.faceBottom - p.faceTop) * static_cast<float>(h);

  const float soft = std::clamp(p.keySoftness, 0.0f, 1.0f);
  const N3 key = lightDir(std::clamp(p.keyAngle, -180.0f, 180.0f), std::clamp(p.keyHeight, -60.0f, 90.0f));
  // The fill: from the other side, nearer the camera, low.
  const N3 fill = lightDir(-0.55f * std::clamp(p.keyAngle, -180.0f, 180.0f), 8.0f);
  const float keyI = std::clamp(p.key, 0.0f, 2.0f), fillI = std::clamp(p.fill, 0.0f, 1.0f) * keyI;
  const V3 keyC = gain3(p.keyColour), fillC = gain3(p.fillColour), rimC = gain3(p.rimColour);
  // Soft light wraps further round (a big modifier lights more of the form).
  const float wrap = 0.15f + 0.85f * soft;

  auto mr = Plane::allocate(w, h), mg = Plane::allocate(w, h), mb = Plane::allocate(w, h), rr = Plane::allocate(w, h), rg = Plane::allocate(w, h),
       rb = Plane::allocate(w, h);
  if (!mr || !mg || !mb || !rr || !rg || !rb) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to light");
  const float K = 0.55f * S;  // height-field slope -> normal tilt
  double sumL = 0, sumA = 0;
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      const float gx = Hh.at(x + 1, y) - Hh.at(x - 1, y), gy = Hh.at(x, y + 1) - Hh.at(x, y - 1);
      N3 n = norm3({-gx * 0.5f * K, -gy * 0.5f * K, 1.0f});
      if (hasFace) {
        const float u = (static_cast<float>(x) + 0.5f - fcx) / frx, v = (static_cast<float>(y) + 0.5f - fcy) / fry;
        const float r2 = u * u + v * v;
        if (r2 < 1.0f) {
          // Ellipsoid normal, and the nose: a ridge down the middle from the
          // eyes to its tip (it throws the small shadow of a side light).
          N3 nf = norm3({u, v * 0.8f, std::sqrt(1.0f - r2)});
          const float ridge = std::exp(-(u / 0.12f) * (u / 0.12f)) * smoothstep(-0.25f, -0.1f, v) * (1.0f - smoothstep(0.15f, 0.3f, v));
          nf = norm3({nf.x + 2.2f * u / 0.12f * ridge * 0.12f, nf.y - 0.25f * ridge * smoothstep(0.05f, 0.25f, v), nf.z});
          n = norm3({mixf(n.x, nf.x, smoothstep(1.0f, 0.75f, std::sqrt(r2))), mixf(n.y, nf.y, smoothstep(1.0f, 0.75f, std::sqrt(r2))),
                     mixf(n.z, nf.z, smoothstep(1.0f, 0.75f, std::sqrt(r2)))});
        }
      }
      // Hard light falls off faster across the form than soft light.
      const float dk = std::pow(std::clamp((dot3(n, key) + wrap) / (1.0f + wrap), 0.0f, 1.0f), 1.0f + 0.9f * (1.0f - soft));
      const float df = std::clamp((dot3(n, fill) + 0.6f) / 1.6f, 0.0f, 1.0f);
      const V3 M = grey(0.04f) + keyC * (keyI * dk) + fillC * (fillI * df);
      const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
      mr->data()[i] = M.r, mg->data()[i] = M.g, mb->data()[i] = M.b;
      const float a = A.data()[i];
      sumL += static_cast<double>(lum(M) * a), sumA += a;
      rr->data()[i] = rg->data()[i] = rb->data()[i] = 0.0f;
    }
  // Rim light: a thin band just inside the outline, on the side facing the
  // rim light (behind the person), brightest at the very edge. Not along
  // the photo's own border (the person is cut off there, not lit).
  if (rimL > 0.0f || rimR > 0.0f) {
    auto ab = blurred(A, std::max(1.2f, 0.008f * S), token);
    if (!ab) return ab.error();
    const float border = 0.015f * static_cast<float>(std::max(w, h));
    for (int32_t y = 0; y < h; ++y)
      for (int32_t x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
        const float a = A.data()[i];
        if (a < 0.3f) continue;
        const float v = ab->data()[i];
        const float band = 1.0f - smoothstep(0.55f, 0.97f, v);
        if (band <= 0.0f) continue;
        const float gx = ab->at(x + 1, y) - ab->at(x - 1, y), gy = ab->at(x, y + 1) - ab->at(x, y - 1);
        const float gl = std::sqrt(gx * gx + gy * gy);
        if (gl < 1e-5f) continue;
        const float ox = -gx / gl, oy = -gy / gl;  // outward
        const float fx = static_cast<float>(x), fy = static_cast<float>(y);
        const float edgeOk = smoothstep(0.0f, border, std::min({fx, fy, static_cast<float>(w - 1) - fx, static_cast<float>(h - 1) - fy}));
        // Light from behind and a little above.
        const float k = band * edgeOk * (rimL * std::max(0.0f, -ox) + rimR * std::max(0.0f, ox)) * (0.75f + 0.25f * std::max(0.0f, -oy)) * 1.4f;
        rr->data()[i] = rimC.r * k, rg->data()[i] = rimC.g * k, rb->data()[i] = rimC.b * k;
      }
  }
  // The person's average light is kept.
  const float mean = sumA > 0 ? static_cast<float>(sumL / sumA) : 1.0f;
  // Softer light also has softer edges between light and shade.
  const float sig = std::max(0.6f, (0.004f + 0.02f * soft) * S);
  auto Mr = blurred(*mr, sig, token), Mg = blurred(*mg, sig, token), Mb = blurred(*mb, sig, token);
  auto Rr = blurred(*rr, std::max(0.6f, 0.003f * S), token), Rg = blurred(*rg, std::max(0.6f, 0.003f * S), token),
       Rb = blurred(*rb, std::max(0.6f, 0.003f * S), token);
  if (!Mr || !Mg || !Mb || !Rr || !Rg || !Rb) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to light");
  const float invMean = 1.0f / std::max(mean, 1e-3f);
  const float invF = 1.0f / static_cast<float>(f);
  const bool done = parallelFor(H, token, [&](int32_t b0, int32_t b1) {
    for (int32_t y = b0; y < b1; ++y) {
      uint8_t* row = cutout.row(y);
      const float gy = (static_cast<float>(y) + 0.5f) * invF - 0.5f;
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* q = row + x * 4;
        if (q[3] == 0) continue;
        const float gx = (static_cast<float>(x) + 0.5f) * invF - 0.5f;
        V3 c{L[q[o.r]], L[q[o.g]], L[q[o.b]]};
        // Relative to the person's average, and the photo's own light is
        // already in it: the new light is laid on with some contrast, eased
        // back by `strength`.
        auto rel = [&](float v) { return std::clamp(std::exp(1.35f * strength * std::log(std::max(v * invMean, 1e-3f))), 0.12f, 2.5f); };
        const V3 m{rel(Mr->sample(gx, gy)), rel(Mg->sample(gx, gy)), rel(Mb->sample(gx, gy))};
        c = c * m;
        // Highlights roll off instead of clipping.
        const float mx = std::max({c.r, c.g, c.b});
        if (mx > 0.85f) c = c * ((0.85f + (1.0f - std::exp(-(mx - 0.85f) * 4.0f)) * 0.15f) / mx);
        // The rim stays on the person: none on the matte's soft outer edge,
        // which would otherwise show as a light outline over a darker
        // background.
        const float inside = smoothstep(0.75f, 0.98f, q[3] / 255.0f);
        const V3 rim = V3{Rr->sample(gx, gy), Rg->sample(gx, gy), Rb->sample(gx, gy)} * inside;
        if (rim.r + rim.g + rim.b > 1e-4f) c = screen(c, V3{std::min(rim.r, 1.0f), std::min(rim.g, 1.0f), std::min(rim.b, 1.0f)});
        q[o.r] = toByte(toGamma(c.r)), q[o.g] = toByte(toGamma(c.g)), q[o.b] = toByte(toGamma(c.b));
      }
    }
  });
  return done ? ok() : cancelledError();
}

Error finish(ImageBuffer& frame, const FinishParams& p, const CancellationToken* token) {
  ScopedTimer timer("studio.finish");
  if (!isColour(frame)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Finish needs an RGBA image");
  if (!finite({p.vignette, p.vignetteSize, p.warmth, p.tint, p.contrast, p.saturation, p.fade, p.toneAmount, p.grain, p.grainSize,
               p.shadowTone[0], p.shadowTone[1], p.shadowTone[2], p.highlightTone[0], p.highlightTone[1], p.highlightTone[2]})) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Bad finish settings");
  }
  const int32_t W = frame.width(), H = frame.height();
  const Rgb o = offsets(frame);
  const auto& L = lut();
  const float vig = std::clamp(p.vignette, -1.0f, 1.0f), vsize = std::clamp(p.vignetteSize, 0.0f, 1.0f);
  const float warmth = std::clamp(p.warmth, -1.0f, 1.0f), tint = std::clamp(p.tint, -1.0f, 1.0f);
  const float contrast = std::clamp(p.contrast, -1.0f, 1.0f), sat = std::clamp(p.saturation, -1.0f, 1.0f);
  const float fade = std::clamp(p.fade, 0.0f, 1.0f), tone = std::clamp(p.toneAmount, 0.0f, 1.0f);
  const float grain = std::clamp(p.grain, 0.0f, 1.0f), gsize = 1.0f + 2.5f * std::clamp(p.grainSize, 0.0f, 1.0f);
  // White balance as channel gains (warm: more red, less blue; tint: magenta / green), luminance kept.
  float wb[3] = {1.0f + 0.18f * warmth + 0.06f * tint, 1.0f - 0.1f * tint, 1.0f - 0.22f * warmth + 0.06f * tint};
  const float wl = 0.2126f * wb[0] + 0.7152f * wb[1] + 0.0722f * wb[2];
  for (float& v : wb) v /= wl;
  const V3 shT{std::clamp(p.shadowTone[0], -1.0f, 1.0f), std::clamp(p.shadowTone[1], -1.0f, 1.0f), std::clamp(p.shadowTone[2], -1.0f, 1.0f)};
  const V3 hiT{std::clamp(p.highlightTone[0], -1.0f, 1.0f), std::clamp(p.highlightTone[1], -1.0f, 1.0f), std::clamp(p.highlightTone[2], -1.0f, 1.0f)};
  const float cx = 0.5f * static_cast<float>(W), cy = 0.5f * static_cast<float>(H);
  const float diag = std::sqrt(cx * cx + cy * cy);
  // Grain at its own size: value noise on a coarser lattice.
  auto grainAt = [&](int32_t x, int32_t y) {
    const float fx = static_cast<float>(x) / gsize, fy = static_cast<float>(y) / gsize;
    const auto x0 = static_cast<int32_t>(std::floor(fx)), y0 = static_cast<int32_t>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    const float a = hashNoise(x0, y0, p.seed), b = hashNoise(x0 + 1, y0, p.seed), c = hashNoise(x0, y0 + 1, p.seed), d = hashNoise(x0 + 1, y0 + 1, p.seed);
    return mixf(mixf(a, b, tx), mixf(c, d, tx), ty);
  };
  const bool done = parallelFor(H, token, [&](int32_t b0, int32_t b1) {
    for (int32_t y = b0; y < b1; ++y) {
      uint8_t* row = frame.row(y);
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* q = row + x * 4;
        V3 c{L[q[o.r]] * wb[0], L[q[o.g]] * wb[1], L[q[o.b]] * wb[2]};
        // Vignette (in light, so it looks like a lens).
        if (vig != 0.0f) {
          const float dx = static_cast<float>(x) + 0.5f - cx, dy = static_cast<float>(y) + 0.5f - cy;
          const float r = std::sqrt(dx * dx + dy * dy) / diag;
          const float k = smoothstep(0.25f + 0.45f * vsize, 1.05f + 0.25f * vsize, r);
          c = c * (vig > 0.0f ? 1.0f - 0.75f * vig * k : 1.0f - 0.6f * vig * k);
        }
        // To the eye's scale for the tone work.
        V3 g{toGamma(c.r), toGamma(c.g), toGamma(c.b)};
        float Y = 0.2126f * g.r + 0.7152f * g.g + 0.0722f * g.b;
        if (p.mono || sat != 0.0f) {
          const float s = p.mono ? 0.0f : 1.0f + sat;
          g = V3{Y + (g.r - Y) * s, Y + (g.g - Y) * s, Y + (g.b - Y) * s};
        }
        if (contrast != 0.0f) {
          // An S-curve around the middle grey (softer at the ends).
          auto curve = [&](float v) {
            const float t = std::clamp(v, 0.0f, 1.0f);
            const float s = t * t * (3.0f - 2.0f * t);
            return contrast > 0.0f ? mixf(t, s, contrast) : mixf(t, 0.5f + (t - 0.5f) * 0.6f, -contrast);
          };
          g = V3{curve(g.r), curve(g.g), curve(g.b)};
        }
        if (tone > 0.0f) {
          Y = 0.2126f * g.r + 0.7152f * g.g + 0.0722f * g.b;
          const float ws = (1.0f - smoothstep(0.1f, 0.55f, Y)) * tone * 0.18f, wh = smoothstep(0.45f, 0.9f, Y) * tone * 0.14f;
          g = g + shT * ws + hiT * wh;
        }
        if (fade > 0.0f) g = grey(0.12f * fade) + g * (1.0f - 0.16f * fade);
        if (grain > 0.0f) {
          Y = std::clamp(0.2126f * g.r + 0.7152f * g.g + 0.0722f * g.b, 0.0f, 1.0f);
          // Film grain shows most in the mid-tones.
          const float n = grainAt(x, y) * grain * 0.06f * (0.35f + 2.6f * Y * (1.0f - Y));
          g = g + grey(n);
        }
        q[o.r] = toByte(g.r), q[o.g] = toByte(g.g), q[o.b] = toByte(g.b);
      }
    }
  });
  return done ? ok() : cancelledError();
}

Error paintGradient(ImageBuffer& out, const std::array<uint8_t, 3>& from, const std::array<uint8_t, 3>& to, const GradientParams& p,
                    const CancellationToken* token) {
  ScopedTimer timer("studio.gradient");
  if (!isColour(out)) return makeError(PP_ERROR_INVALID_ARGUMENT, "A gradient needs an RGBA image");
  if (!finite({p.angle, p.centerX, p.centerY, p.radius})) return makeError(PP_ERROR_INVALID_ARGUMENT, "Bad gradient");
  const int32_t W = out.width(), H = out.height();
  const Rgb o = offsets(out);
  const auto& L = lut();
  const V3 a{L[from[0]], L[from[1]], L[from[2]]}, b{L[to[0]], L[to[1]], L[to[2]]};
  const float ang = p.angle * kPi / 180.0f, ux = std::cos(ang), uy = std::sin(ang);
  const float aspect = static_cast<float>(W) / static_cast<float>(std::max(H, 1));
  // Half the frame's extent along the direction (so t spans corner to corner).
  const float half = 0.5f * (std::fabs(ux) * aspect + std::fabs(uy));
  const float radius = std::max(0.05f, p.radius);
  const bool done = parallelFor(H, token, [&](int32_t b0, int32_t b1) {
    for (int32_t y = b0; y < b1; ++y) {
      uint8_t* row = out.row(y);
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(H);
      for (int32_t x = 0; x < W; ++x) {
        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(W);
        float t;
        if (p.kind == 1) {
          const float dx = (u - p.centerX) * aspect, dy = v - p.centerY;
          t = std::sqrt(dx * dx + dy * dy) / radius;
        } else {
          t = 0.5f + ((u - 0.5f) * aspect * ux + (v - 0.5f) * uy) / (2.0f * half);
        }
        t = smoothstep(0.0f, 1.0f, t);
        const V3 c = mix(a, b, t);
        // Dither: under one step of 8-bit colour, so no bands.
        const float d = hashNoise(x, y, p.seed) * (0.5f / 255.0f);
        uint8_t* q = row + x * 4;
        q[o.r] = toByte(toGamma(c.r) + d), q[o.g] = toByte(toGamma(c.g) + d), q[o.b] = toByte(toGamma(c.b) + d), q[3] = 255;
      }
    }
  });
  return done ? ok() : cancelledError();
}

}  // namespace photoplus::studio
