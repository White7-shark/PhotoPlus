// Backdrops: see backdrop.hpp. Layout of one run:
//   1. a working copy (~768 px, linear light) and the matte: hair the mask
//      missed recovered by colour, then colour guided-filter
//      coefficients on that grid, evaluated per pixel so edges are full-res;
//   2. clean plates: the background blurred WITHOUT the subject (normalised
//      convolution), at three scales — nothing of the person bleeds into it;
//   3. the design's new background, painted on the working grid (they are
//      all smooth or soft, so the grid is enough; Colour Pop is per pixel);
//   4. per pixel: foreground colour recovered at edges (I = aF + (1-a)B),
//      the subject re-lit for the design, composite in linear light, a
//      breath of grain so gradients never band.
#include "photoplus/backdrop.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "../matte/tools.hpp"
#include "../portrait/common.hpp"
#include "photoplus/portrait.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::backdrop {

using namespace portrait::detail;
using namespace matte;

namespace {

inline float hash01(int32_t x, int32_t y, uint32_t seed) {
  auto h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFFu) / 16777215.0f;
}
// Smooth value noise and a few octaves of it.
float vnoise(float x, float y, uint32_t seed) {
  const float fx = std::floor(x), fy = std::floor(y);
  const auto ix = static_cast<int32_t>(fx), iy = static_cast<int32_t>(fy);
  float tx = x - fx, ty = y - fy;
  tx = tx * tx * (3 - 2 * tx), ty = ty * ty * (3 - 2 * ty);
  const float a = hash01(ix, iy, seed), b = hash01(ix + 1, iy, seed), c = hash01(ix, iy + 1, seed), d = hash01(ix + 1, iy + 1, seed);
  return a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty;
}
float fbm(float x, float y, uint32_t seed, int octaves) {
  float s = 0, amp = 0.5f, norm = 0;
  for (int i = 0; i < octaves; ++i) {
    s += amp * vnoise(x, y, seed + static_cast<uint32_t>(i) * 17u);
    norm += amp;
    x *= 2.03f, y *= 2.03f, amp *= 0.5f;
  }
  return s / norm;
}

struct Geometry {
  float cx = 0.5f, cy = 0.45f;  // subject centre (0..1)
  float top = 0.1f;             // top of the subject (0..1)
  float aspect = 1.0f;          // width / height
};

// How the subject is re-lit for a design.
struct Relight {
  float gain = 1.0f;
  float mono = 0.0f;
  float contrast = 0.0f;
  float saturation = 1.0f;
  V3 tint{1, 1, 1};
  float fade = 0.0f;       // stage light falling off below the shoulders
  float rim = 0.0f;        // rim light on the edge…
  V3 rimLeft{1, 1, 1};     // …coloured by side
  V3 rimRight{1, 1, 1};
  float rimSide = 0.0f;    // 0: all round; 1: only the side facing each light
  float wrap = 0.12f;      // the new background's light wrapping onto the edge
  float glow = 0.0f;       // soft bloom over the subject (Dream)
  float alphaTighten = 0.0f;  // harder matte for dark / plain backgrounds
};

struct Scene {
  int32_t w = 0, h = 0;  // working grid
  Geometry geo;
  uint32_t seed = 7;
  const Rgb3* plateS = nullptr;
  const Rgb3* plateM = nullptr;
  const Rgb3* plateL = nullptr;
  V3 mean;  // mean background colour
};

inline float radial(float u, float v, float cu, float cv, float radius, float aspect) {
  const float dx = (u - cu) * aspect, dy = v - cv;
  const float d = std::sqrt(dx * dx + dy * dy) / radius;
  return std::exp(-0.5f * d * d);
}

// Directional blur of a plate around the subject: along the radius (zoom
// burst) or around it (swirl).
V3 spinSample(const Rgb3& p, float x, float y, float cx, float cy, bool zoom, float amount) {
  V3 acc;
  constexpr int kN = 18;
  const float dx = x - cx, dy = y - cy;
  for (int i = 0; i < kN; ++i) {
    const float t = (static_cast<float>(i) / (kN - 1)) - 0.5f;
    float sx, sy;
    if (zoom) {
      const float s = 1.0f + amount * t;
      sx = cx + dx * s, sy = cy + dy * s;
    } else {
      const float ang = amount * t, c = std::cos(ang), sn = std::sin(ang);
      sx = cx + dx * c - dy * sn, sy = cy + dx * sn + dy * c;
    }
    acc = acc + p.sample(sx, sy);
  }
  return acc * (1.0f / kN);
}

// Paints the design's background at one working-grid pixel (linear RGB).
V3 paint(Design d, const Scene& s, int32_t x, int32_t y) {
  const auto fx = static_cast<float>(x), fy = static_cast<float>(y);
  const float u = (fx + 0.5f) / static_cast<float>(s.w), v = (fy + 0.5f) / static_cast<float>(s.h);
  const float asp = s.geo.aspect;
  const size_t i = static_cast<size_t>(y) * static_cast<size_t>(s.w) + static_cast<size_t>(x);
  const float vd = std::hypot((u - 0.5f) * asp, v - 0.5f) / (0.5f * std::hypot(asp, 1.0f));
  switch (d) {
    case Design::SoftStudio: {
      const V3 p = s.plateL->at(i);
      V3 c = mix(p, grey(lum(p)), 0.4f) * 1.12f + grey(0.015f);
      return c * (1.0f - 0.22f * smoothstep(0.5f, 1.1f, vd));
    }
    case Design::Stage:
    case Design::Noir: {
      // Not flat black: a faint pool of light behind the head.
      const float pool = radial(u, v, s.geo.cx, s.geo.top + 0.12f, 0.42f, asp);
      return grey(0.0004f + 0.0035f * pool);
    }
    case Design::Porcelain: {
      const float floor = smoothstep(0.55f, 1.0f, v);
      const float light = radial(u, v, s.geo.cx, 0.3f, 0.75f, asp);
      return grey(0.62f + 0.26f * light - 0.12f * floor);
    }
    case Design::Canvas: {
      // Mottled, brushed canvas: large soft clouds, medium blotches, fine strokes.
      const float clouds = fbm(u * asp * 2.2f, v * 2.2f, s.seed, 4);
      const float blot = fbm(u * asp * 6.5f + 11.0f, v * 6.5f, s.seed + 5u, 3);
      const float strokes = fbm(u * asp * 21.0f, v * 17.0f, s.seed + 9u, 2);
      const float t = smoothstep(0.12f, 0.88f, 0.55f * clouds + 0.33f * blot + 0.12f * strokes);
      const V3 dark = lin(0.20f, 0.19f, 0.18f), mid = lin(0.42f, 0.38f, 0.33f), warm = lin(0.56f, 0.47f, 0.37f);
      V3 c = t < 0.5f ? mix(dark, mid, t * 2.0f) : mix(mid, warm, (t - 0.5f) * 2.0f);
      const float pool = radial(u, v, s.geo.cx, s.geo.top + 0.18f, 0.55f, asp);
      return c * (0.5f + 0.95f * pool) * (1.0f - 0.35f * smoothstep(0.55f, 1.1f, vd));
    }
    case Design::GoldenHour: {
      const V3 p = s.plateM->at(i);
      V3 c = p * V3{1.22f, 1.0f, 0.72f} * 0.9f;
      const bool left = s.geo.cx > 0.6f;  // the sun sits on the open side (right unless the person is clearly there)
      const float su = left ? 0.14f : 0.86f;
      const float sun = radial(u, v, su, 0.2f, 0.42f, asp), core = radial(u, v, su, 0.2f, 0.1f, asp);
      c = screen(c, V3{1.0f, 0.62f, 0.25f} * (0.55f * sun) + V3{1.0f, 0.9f, 0.7f} * (0.7f * core));
      return screen(c, V3{0.9f, 0.5f, 0.25f} * (0.10f * (1.0f - v)));  // warm haze toward the sky
    }
    case Design::CityLights: {
      const V3 p = s.plateL->at(i);
      return mix(p, grey(lum(p)), 0.3f) * V3{0.045f, 0.055f, 0.1f} + V3{0.002f, 0.003f, 0.008f};
    }
    case Design::Neon: {
      V3 c{0.006f, 0.004f, 0.016f};
      c = screen(c, V3{1.0f, 0.02f, 0.45f} * (0.2f * radial(u, v, -0.05f, 0.42f, 0.42f, asp)));
      c = screen(c, V3{0.0f, 0.55f, 1.0f} * (0.2f * radial(u, v, 1.05f, 0.6f, 0.42f, asp)));
      // Faint haze streaks, as light in smoke.
      return c * (0.8f + 0.4f * fbm(u * asp * 3.0f, v * 9.0f, s.seed, 3));
    }
    case Design::Velocity: {
      const V3 c = spinSample(*s.plateS, fx, fy, s.geo.cx * static_cast<float>(s.w), s.geo.cy * static_cast<float>(s.h), true, 0.34f);
      // Speed lines: streaks of light along the rush, stronger toward the edges.
      const float du = (u - s.geo.cx) * asp, dv = v - s.geo.cy;
      const float theta = std::atan2(dv, du), r = std::hypot(du, dv);
      const float streak = vnoise(theta * 22.0f + 40.0f, 0.5f, s.seed) * vnoise(theta * 61.0f + 90.0f, 1.5f, s.seed + 1u);
      return screen(c * (0.9f - 0.3f * smoothstep(0.5f, 1.1f, vd)), grey(0.5f * streak * smoothstep(0.12f, 0.75f, r)));
    }
    case Design::Swirl: {
      const float r = std::hypot((u - s.geo.cx) * asp, v - s.geo.cy);
      const V3 c = spinSample(*s.plateS, fx, fy, s.geo.cx * static_cast<float>(s.w), s.geo.cy * static_cast<float>(s.h), false,
                              0.12f + 0.75f * r);
      return c * (1.0f - 0.5f * smoothstep(0.4f, 1.05f, vd));
    }
    case Design::Aurora: {
      V3 c = mix(V3{0.004f, 0.01f, 0.03f}, V3{0.012f, 0.02f, 0.06f}, v);
      const V3 cols[3] = {{0.08f, 0.95f, 0.55f}, {0.45f, 0.22f, 1.0f}, {0.08f, 0.5f, 1.0f}};
      for (int k = 0; k < 3; ++k) {
        const auto fk = static_cast<float>(k);
        const float ph = static_cast<float>(s.seed % 13u) + 2.1f * fk;
        const float y0 = 0.2f + 0.17f * fk + 0.11f * std::sin(u * asp * 3.3f + ph) + 0.04f * std::sin(u * asp * 8.1f + 1.7f * ph);
        const float wdt = 0.07f + 0.03f * fk;
        // Curtains: sharp lower edge, long fade upward, vertical rays.
        const float dv = v - y0;
        const float band = dv > 0 ? std::exp(-0.5f * (dv / (0.35f * wdt)) * (dv / (0.35f * wdt))) : std::exp(dv / (2.2f * wdt));
        const float rays = 0.55f + 0.45f * vnoise(u * asp * 26.0f + 5.0f * fk, 0.5f, s.seed + static_cast<uint32_t>(k));
        c = screen(c, cols[k] * (0.5f * band * rays));
      }
      // A few stars.
      if (hash01(x, y, s.seed + 99u) > 0.9988f) c = screen(c, grey(0.5f * hash01(x, y, s.seed + 3u)));
      return c;
    }
    case Design::Dream: {
      const V3 p = s.plateL->at(i);
      const V3 pastel = v < 0.5f ? mix(lin(0.98f, 0.80f, 0.86f), lin(0.84f, 0.80f, 0.97f), v * 2.0f)
                                 : mix(lin(0.84f, 0.80f, 0.97f), lin(0.99f, 0.86f, 0.78f), (v - 0.5f) * 2.0f);
      V3 c = mix(p, pastel * (0.35f + 0.9f * lum(p)), 0.62f);
      c = screen(c, pastel * (0.3f * radial(u, v, 0.2f, 0.25f, 0.4f, asp) + 0.2f * radial(u, v, 0.85f, 0.7f, 0.35f, asp)));
      return screen(c, grey(0.06f));
    }
    default:
      return s.plateM->at(i);
  }
}

// Night bokeh: soft discs of light, a brighter ring at the edge like a real lens.
void addBokeh(Rgb3& bg, const Scene& s, const Plane& alpha) {
  const float shortSide = static_cast<float>(std::min(s.w, s.h));
  const V3 palette[5] = {{1.0f, 0.62f, 0.22f}, {1.0f, 0.82f, 0.5f}, {0.25f, 0.75f, 1.0f}, {1.0f, 0.3f, 0.5f}, {0.9f, 0.9f, 1.0f}};
  constexpr int kLights = 54;
  for (int k = 0; k < kLights; ++k) {
    const float px = hash01(k, 1, s.seed) * static_cast<float>(s.w), py = hash01(k, 2, s.seed) * static_cast<float>(s.h) * 0.92f;
    const float rad = shortSide * (0.014f + 0.085f * hash01(k, 3, s.seed) * hash01(k, 6, s.seed));
    const float power = 0.1f + 0.55f * hash01(k, 4, s.seed) * hash01(k, 7, s.seed);
    const V3 col = palette[static_cast<int>(hash01(k, 5, s.seed) * 4.999f)];
    const int32_t x0 = std::max(0, static_cast<int32_t>(px - rad - 2)), x1 = std::min(s.w - 1, static_cast<int32_t>(px + rad + 2));
    const int32_t y0 = std::max(0, static_cast<int32_t>(py - rad - 2)), y1 = std::min(s.h - 1, static_cast<int32_t>(py + rad + 2));
    for (int32_t y = y0; y <= y1; ++y)
      for (int32_t x = x0; x <= x1; ++x) {
        const float d = std::hypot(static_cast<float>(x) - px, static_cast<float>(y) - py) / rad;
        if (d >= 1.05f) continue;
        const float disc = 1.0f - smoothstep(0.9f, 1.05f, d);
        const float ring = 0.75f + 0.5f * smoothstep(0.6f, 0.95f, d);
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(s.w) + static_cast<size_t>(x);
        // Lights hidden behind the person never show through.
        const float vis = 1.0f - alpha.data()[i];
        bg.set(i, screen(bg.at(i), col * (power * disc * ring * vis)));
      }
  }
}

Relight relightFor(Design d) {
  Relight r;
  switch (d) {
    case Design::SoftStudio: r.gain = 1.04f; r.wrap = 0.16f; break;
    case Design::Stage: r.fade = 1.0f; r.contrast = 0.12f; r.wrap = 0.0f; r.alphaTighten = 0.8f; r.rim = 0.10f; break;
    case Design::Noir: r.fade = 1.0f; r.mono = 1.0f; r.contrast = 0.3f; r.wrap = 0.0f; r.alphaTighten = 0.8f; r.rim = 0.12f; break;
    case Design::Porcelain: r.gain = 1.08f; r.wrap = 0.2f; r.alphaTighten = 0.8f; break;
    case Design::Canvas: r.contrast = 0.08f; r.wrap = 0.08f; r.alphaTighten = 0.6f; r.rim = 0.08f; break;
    case Design::GoldenHour:
      r.tint = {1.06f, 1.0f, 0.9f}; r.rim = 0.55f; r.rimLeft = r.rimRight = {1.0f, 0.72f, 0.38f}; r.rimSide = 1.0f; r.wrap = 0.25f;
      break;
    case Design::CityLights: r.gain = 0.96f; r.rim = 0.2f; r.rimLeft = {1.0f, 0.7f, 0.4f}; r.rimRight = {0.4f, 0.75f, 1.0f}; r.rimSide = 1.0f;
      r.wrap = 0.1f; r.alphaTighten = 0.6f; break;
    case Design::Neon:
      r.gain = 0.9f; r.contrast = 0.12f; r.rim = 0.9f; r.rimLeft = {1.0f, 0.08f, 0.55f}; r.rimRight = {0.05f, 0.75f, 1.0f}; r.rimSide = 1.0f;
      r.wrap = 0.15f; r.alphaTighten = 0.7f;
      break;
    case Design::ColorPop: r.saturation = 1.12f; r.wrap = 0.0f; break;
    case Design::Velocity: r.wrap = 0.1f; break;
    case Design::Swirl: r.wrap = 0.1f; break;
    case Design::Aurora: r.gain = 0.92f; r.rim = 0.4f; r.rimLeft = {0.1f, 0.95f, 0.6f}; r.rimRight = {0.45f, 0.3f, 1.0f}; r.rimSide = 1.0f;
      r.wrap = 0.12f; r.alphaTighten = 0.7f; break;
    case Design::Dream: r.gain = 1.05f; r.glow = 0.3f; r.wrap = 0.3f; r.tint = {1.02f, 0.99f, 1.01f}; break;
    default: break;
  }
  return r;
}

}  // namespace

Error apply(ImageBuffer& image, const ImageBuffer& subject, const ImageBuffer* depth, const Params& params,
            const CancellationToken* token, Report* report) {
  ScopedTimer timer("backdrop.apply");
  if (!isColour(image)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Backdrops need an RGBA image");
  if (image.width() < 16 || image.height() < 16) return makeError(PP_ERROR_INVALID_ARGUMENT, "Image too small");
  if (subject.empty() || subject.width() < 2 || subject.height() < 2) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Subject mask is required");
  }
  const auto di = static_cast<int32_t>(params.design);
  if (di < 0 || di >= static_cast<int32_t>(Design::Count) || !std::isfinite(params.strength)) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Unknown backdrop");
  }
  const float strength = std::clamp(params.strength, 0.0f, 1.0f);
  if (report != nullptr) *report = Report{};
  if (strength <= 0.0f) return ok();
  const Design design = params.design;
  const int32_t W = image.width(), H = image.height();
  const Rgb o = offsets(image);

  // Signature: the depth-aware lens blur, with the background a touch
  // darker and calmer so the person carries the frame.
  if (design == Design::Signature) {
    portrait::Params p;
    p.blur = 0.8f * strength;
    p.retouch = 0.0f;
    p.brighten = 0.0f;
    p.pop = 0.35f * strength;
    p.bgLight = -0.2f * strength;
    p.bgColor = -0.06f * strength;
    p.edge = 0.6f;
    if (Error e = portrait::apply(image, subject, nullptr, nullptr, depth, p, token, nullptr); e.status != PP_OK) return e;
  }

  // 1. Working grid and matte.
  const int32_t f = std::max<int32_t>(1, (std::max(W, H) + 767) / 768);
  const int32_t lw = (W + f - 1) / f, lh = (H + f - 1) / f;
  auto lowR = allocRgb(lw, lh);
  auto gamR = allocRgb(lw, lh);
  if (!lowR || !gamR) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
  Rgb3 low = std::move(lowR).value();
  Rgb3 gam = std::move(gamR).value();  // the same, in gamma: the matte's colour guide
  {
    std::array<float, 256> lut{};
    for (int i = 0; i < 256; ++i) lut[static_cast<size_t>(i)] = toLinear(static_cast<float>(i) / 255.0f);
    const bool done = parallelFor(lh, token, [&](int32_t begin, int32_t end) {
      for (int32_t ly = begin; ly < end; ++ly)
        for (int32_t lx = 0; lx < lw; ++lx) {
          float sr = 0, sg = 0, sb = 0, gr2 = 0, gg = 0, gb = 0;
          int32_t n = 0;
          for (int32_t y = ly * f; y < std::min(H, (ly + 1) * f); ++y) {
            const uint8_t* row = image.row(y);
            for (int32_t x = lx * f; x < std::min(W, (lx + 1) * f); ++x, ++n) {
              const uint8_t* q = row + x * 4;
              sr += lut[q[o.r]], sg += lut[q[o.g]], sb += lut[q[o.b]];
              gr2 += q[o.r], gg += q[o.g], gb += q[o.b];
            }
          }
          const float inv = n ? 1.0f / static_cast<float>(n) : 0.0f;
          low.r.row(ly)[lx] = sr * inv, low.g.row(ly)[lx] = sg * inv, low.b.row(ly)[lx] = sb * inv;
          const float gi = inv / 255.0f;
          gam.r.row(ly)[lx] = gr2 * gi, gam.g.row(ly)[lx] = gg * gi, gam.b.row(ly)[lx] = gb * gi;
        }
    });
    if (!done) return cancelledError();
  }
  auto coarse = maskPlane(subject);
  if (!coarse) return coarse.error();
  auto coarseUp = resizeBilinear(*coarse, lw, lh, token);
  if (!coarseUp) return coarseUp.error();
  if (Error e = dropIslands(*coarseUp, token); e.status != PP_OK) return e;
  // Signature keeps the real background, so the mask's own edge is enough.
  if (design != Design::Signature) {
    if (Error e = recoverHair(gam, *coarseUp, token); e.status != PP_OK) return e;
  }
  std::array<Plane, 3> mA;
  auto mB = Plane::allocate(lw, lh), alphaP = Plane::allocate(lw, lh);
  for (auto& p : mA) {
    auto q = Plane::allocate(lw, lh);
    if (!q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
    p = std::move(q).value();
  }
  if (!mB || !alphaP) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
  // A colour guide: hair against a wall of the same brightness still differs in hue.
  const int32_t gr = std::max<int32_t>(2, std::min(lw, lh) / 190);
  if (Error e = guidedCoefficientsColor({&gam.r, &gam.g, &gam.b}, *coarseUp, gr, 1e-3f, mA, *mB, token); e.status != PP_OK) return e;
  const Relight rl = relightFor(design);
  // Only the faintest haze is trimmed: hair keeps its soft edge, and the
  // colour recovery below keeps it free of the old background.
  const float t0 = 0.12f * rl.alphaTighten, t1 = 1.0f - 0.08f * rl.alphaTighten;
  auto shape = [&](float a) { return rl.alphaTighten > 0.0f ? smoothstep(t0, t1, a) : std::clamp(a, 0.0f, 1.0f); };
  auto matte = [&](float lx, float ly, float r, float g, float b) {
    return shape(mA[0].sample(lx, ly) * r + mA[1].sample(lx, ly) * g + mA[2].sample(lx, ly) * b + mB->sample(lx, ly));
  };
  Plane& alpha = *alphaP;
  double asum = 0, ax = 0, ay = 0;
  int32_t topRow = lh;
  for (int32_t y = 0; y < lh; ++y)
    for (int32_t x = 0; x < lw; ++x) {
      const size_t i = static_cast<size_t>(y) * static_cast<size_t>(lw) + static_cast<size_t>(x);
      const float a = shape(mA[0].data()[i] * gam.r.data()[i] + mA[1].data()[i] * gam.g.data()[i] +
                            mA[2].data()[i] * gam.b.data()[i] + mB->data()[i]);
      alpha.data()[i] = a;
      asum += a, ax += static_cast<double>(a) * x, ay += static_cast<double>(a) * y;
      if (a > 0.6f && y < topRow) topRow = y;
    }
  Scene sc;
  sc.w = lw, sc.h = lh, sc.seed = params.seed;
  sc.geo.aspect = static_cast<float>(W) / static_cast<float>(H);
  if (asum > 1.0) {
    sc.geo.cx = static_cast<float>(ax / asum / lw);
    sc.geo.cy = static_cast<float>(ay / asum / lh);
    sc.geo.top = static_cast<float>(topRow) / static_cast<float>(lh);
  }
  // Swirls and bursts centre on the head and chest, not the whole body.
  sc.geo.cy = std::min(sc.geo.cy, sc.geo.top + 0.28f);
  const float shortLow = static_cast<float>(std::min(lw, lh));
  auto lowA = blurred(alpha, 0.03f * shortLow, token);
  if (!lowA) return lowA.error();

  const bool vignetteOnly = design == Design::Signature;
  Rgb3 plateS, plateM, plateL, bg, glowSrc;
  if (!vignetteOnly) {
    // 2. Clean plates.
    double wr = 0, wg = 0, wb = 0, wsum = 0;
    for (size_t i = 0; i < alpha.size(); ++i) {
      const float wv = 1.0f - alpha.data()[i];
      wr += wv * low.r.data()[i], wg += wv * low.g.data()[i], wb += wv * low.b.data()[i], wsum += wv;
    }
    sc.mean = wsum > 1.0 ? V3{static_cast<float>(wr / wsum), static_cast<float>(wg / wsum), static_cast<float>(wb / wsum)} : grey(0.2f);
    auto ps = cleanPlate(low, alpha, 0.012f * shortLow, sc.mean, token);
    auto pm = cleanPlate(low, alpha, 0.035f * shortLow, sc.mean, token);
    auto pl = cleanPlate(low, alpha, 0.09f * shortLow, sc.mean, token);
    if (!ps || !pm || !pl) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
    plateS = std::move(ps).value(), plateM = std::move(pm).value(), plateL = std::move(pl).value();
    sc.plateS = &plateS, sc.plateM = &plateM, sc.plateL = &plateL;
    // 3. The new background.
    auto b = allocRgb(lw, lh);
    if (!b) return b.error();
    bg = std::move(b).value();
    if (design != Design::ColorPop) {
      const bool done = parallelFor(lh, token, [&](int32_t begin, int32_t end) {
        for (int32_t y = begin; y < end; ++y)
          for (int32_t x = 0; x < lw; ++x) bg.set(static_cast<size_t>(y) * static_cast<size_t>(lw) + static_cast<size_t>(x), paint(design, sc, x, y));
      });
      if (!done) return cancelledError();
      if (design == Design::CityLights) addBokeh(bg, sc, *lowA);
    }
    if (rl.glow > 0.0f) {
      auto g = allocRgb(lw, lh);
      if (!g) return g.error();
      glowSrc = std::move(g).value();
      const Plane* src[3] = {&low.r, &low.g, &low.b};
      Plane* dst[3] = {&glowSrc.r, &glowSrc.g, &glowSrc.b};
      for (int c = 0; c < 3; ++c) {
        auto soft = blurred(*src[c], 0.02f * shortLow, token);
        if (!soft) return soft.error();
        *dst[c] = std::move(soft).value();
      }
    }
  }

  // 4. Composite.
  const float inv = 1.0f / static_cast<float>(f);
  const float diag = std::hypot(sc.geo.aspect, 1.0f);
  const float fadeFrom = sc.geo.top + 0.42f * (1.0f - sc.geo.top), fadeTo = 1.02f;
  std::vector<double> rowA(static_cast<size_t>(H), 0.0), rowE(static_cast<size_t>(H), 0.0);
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* row = image.row(y);
      const float ly = (static_cast<float>(y) + 0.5f) * inv - 0.5f;
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(H);
      double sa = 0, se = 0;
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* q = row + x * 4;
        const float lx = (static_cast<float>(x) + 0.5f) * inv - 0.5f;
        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(W);
        const float R = q[o.r] / 255.0f, G = q[o.g] / 255.0f, B = q[o.b] / 255.0f;
        const float a = matte(lx, ly, R, G, B);
        sa += a;
        if (a > 0.02f && a < 0.98f) se += 1.0;
        V3 out;
        if (vignetteOnly) {
          const float vd = std::hypot((u - 0.5f) * sc.geo.aspect, v - 0.5f) / (0.5f * diag);
          const float k = 1.0f - 0.24f * strength * smoothstep(0.4f, 1.05f, vd) * (1.0f - a);
          q[o.r] = toByte(toGamma(toLinear(R) * k));
          q[o.g] = toByte(toGamma(toLinear(G) * k));
          q[o.b] = toByte(toGamma(toLinear(B) * k));
          continue;
        }
        const V3 I{toLinear(R), toLinear(G), toLinear(B)};
        V3 nb;
        if (design == Design::ColorPop) {
          // The world in black and white (kept sharp), a little deeper.
          V3 src = I;
          if (a > 0.02f) {  // at edges, the background's own colour, not the person's
            const V3 p = plateS.sample(lx, ly);
            src = mix(I, p, smoothstep(0.02f, 0.6f, a));
          }
          const float m = lum(src);
          nb = grey(m * (0.82f + 0.18f * m));
        } else {
          nb = bg.sample(lx, ly);
        }
        if (a <= 0.003f) {
          out = nb;
        } else {
          // Foreground colour: I = aF + (1-a)B, solved against the clean plate.
          V3 F = I;
          if (a < 0.97f) {
            const V3 Bp = plateS.sample(lx, ly);
            const float ar = std::max(a, 0.3f);
            const V3 S = (I - Bp * (1.0f - ar)) * (1.0f / ar);
            F = mix(V3{std::clamp(S.r, 0.0f, 1.0f), std::clamp(S.g, 0.0f, 1.0f), std::clamp(S.b, 0.0f, 1.0f)}, I, smoothstep(0.75f, 0.97f, a));
          }
          // Re-light the person for this scene.
          if (rl.glow > 0.0f) F = screen(F, glowSrc.sample(lx, ly) * (rl.glow * 0.5f));
          F = F * rl.tint * rl.gain;
          const float m = lum(F);
          if (rl.mono > 0.0f) F = mix(F, grey(m), rl.mono);
          if (rl.saturation != 1.0f) F = grey(m) + (F - grey(m)) * rl.saturation;
          if (rl.contrast != 0.0f) {
            // An S-curve around mid grey, on luminance (hue holds).
            const float g = toGamma(m), d = g - 0.5f;
            const float g2 = std::clamp(g + 0.9f * rl.contrast * d * (1.0f - 2.0f * std::fabs(d)) * 2.0f, 0.0f, 1.0f);
            F = F * (toLinear(g2) / std::max(m, 1e-4f));
          }
          const float la = lowA->sample(lx, ly);
          const float band = smoothstep(0.1f, 0.6f, 1.0f - la);  // near the silhouette
          if (rl.rim > 0.0f) {
            const float gx = lowA->sample(lx + 2.0f, ly) - lowA->sample(lx - 2.0f, ly);  // >0: subject to the right, so this is its left edge
            const float side = std::clamp(gx * 12.0f, -1.0f, 1.0f);
            const float wl = mixf(1.0f, std::max(0.0f, side), rl.rimSide), wr2 = mixf(1.0f, std::max(0.0f, -side), rl.rimSide);
            F = screen(F, (rl.rimLeft * wl + rl.rimRight * wr2) * (rl.rim * 0.5f * band * smoothstep(0.5f, 0.95f, a) * (0.35f + 0.65f * m)));
          }
          if (rl.wrap > 0.0f) F = mix(F, screen(F, nb), rl.wrap * band);
          if (rl.fade > 0.0f) F = F * (1.0f - rl.fade * 0.96f * smoothstep(fadeFrom, fadeTo, v));
          out = F * a + nb * (1.0f - a);
        }
        // A breath of grain keeps smooth gradients from banding.
        const float n = 0.0035f * hashNoise(x, y, params.seed) * (1.0f - a);
        const float r2 = toGamma(std::max(out.r, 0.0f)) + n, g2 = toGamma(std::max(out.g, 0.0f)) + n, b2 = toGamma(std::max(out.b, 0.0f)) + n;
        q[o.r] = toByte(R + strength * (r2 - R));
        q[o.g] = toByte(G + strength * (g2 - G));
        q[o.b] = toByte(B + strength * (b2 - B));
      }
      rowA[static_cast<size_t>(y)] = sa, rowE[static_cast<size_t>(y)] = se;
    }
  });
  if (!done) return cancelledError();
  if (report != nullptr) {
    double ta = 0, te = 0;
    for (size_t i = 0; i < rowA.size(); ++i) ta += rowA[i], te += rowE[i];
    const double n = static_cast<double>(W) * H;
    report->subjectCoverage = static_cast<float>(ta / n);
    report->edgeCoverage = static_cast<float>(te / n);
  }
  return ok();
}

}  // namespace photoplus::backdrop
