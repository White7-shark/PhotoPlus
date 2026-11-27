// Toon (toon.hpp): a photo turned into anime or a cartoon.
//
//  0. Eyes: an optional local magnification around each eye (anime eyes);
//     the eye contours move with it.
//  1. Clean-up: sensor noise measured and removed (lightness lightly, colour
//     strongly, edge-aware), then low light brought up (exposure, CLAHE on
//     lightness), so noise is never posterised into blotches.
//  2. Where things are: the person mask refined to the photo's own edges
//     (guided filter: the outline and the colour meet exactly), the faces
//     from their contour points (or boxes), the skin by the face's own
//     colour, the hair above and around the face.
//  3. Shapes: edge-preserving smoothing per style (bilateral, Kuwahara or
//     guided); the scene simplified harder than the person; the hair's local
//     contrast raised so it keeps its shape and strands.
//  4. Colour: palettes found by k-means in Lab, one for the person and one
//     for the scene; skin in hue-true tones (shadow, base, highlight) of the
//     face's own colour, highlights clamped (no white shine); each style's
//     colour (presets.hpp).
//  5. The face drawn from its contours: eye whites, irises with pupils and
//     catchlights, lash lines, brows, nostrils, lips and the mouth line —
//     never a black blob.
//  6. Line art: XDoG on the smoothed light, gated to real edges (small dark
//     spots get a ring, not a fill), specks removed, none on the face (the
//     face is drawn), strands in the hair, few in the scene; the person's
//     outline from the refined mask, on the colour's own edge.
//  7. Finish: screentone and dots only in shadows and the scene, bloom from
//     the lights (not the skin), period palette, grain, colour fringe, VHS
//     warmth; then blended with the photo by Strength.
#include "photoplus/toon.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "../art/core.hpp"
#include "filters.hpp"
#include "photoplus/parallel.hpp"
#include "presets.hpp"

namespace photoplus::toon {

using namespace art::core;
using filters::Pt;

namespace {

// Every pixel index, rows in parallel.
template <typename Fn>
void each(int w, int h, const CancellationToken* token, Fn&& fn) {
  parallelFor(h, token, [&](int32_t y0, int32_t y1) {
    for (size_t i = static_cast<size_t>(y0) * static_cast<size_t>(w); i < static_cast<size_t>(y1) * static_cast<size_t>(w); ++i) fn(i);
  });
}

// Cube root, fast (a bit-level first guess and two Newton steps: within 1e-6).
inline float fastCbrt(float x) {
  if (x <= 0) return 0;
  uint32_t i;
  std::memcpy(&i, &x, 4);
  i = i / 3 + 709921077U;
  float y;
  std::memcpy(&y, &i, 4);
  y = y - (y - x / (y * y)) * (1.0f / 3);
  y = y - (y - x / (y * y)) * (1.0f / 3);
  return y;
}

// sRGB to Lab, fast: the transfer curve from a table, the cube roots by fastCbrt (the same as rgbToLab to 1e-3).
Img toLab(const Img& rgb) {
  static const std::array<float, 4097> lin = [] {
    std::array<float, 4097> t{};
    for (int i = 0; i <= 4096; ++i) {
      const float v = static_cast<float>(i) / 4096.0f;
      t[static_cast<size_t>(i)] = v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    }
    return t;
  }();
  auto L = [&](float v) {
    const float f = std::clamp(v, 0.0f, 1.0f) * 4096.0f;
    const int i = std::min(4095, static_cast<int>(f));
    return mix(lin[static_cast<size_t>(i)], lin[static_cast<size_t>(i + 1)], f - static_cast<float>(i));
  };
  Img lab(rgb.w, rgb.h, 3);
  each(rgb.w, rgb.h, nullptr, [&](size_t i) {
    const float* p = &rgb.d[i * 3];
    const float r = L(p[0]), g = L(p[1]), b = L(p[2]);
    auto f = [](float t) { return t > 0.008856f ? fastCbrt(t) : 7.787f * t + 16.0f / 116.0f; };
    const float X = f((0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f), Y = f(0.2126f * r + 0.7152f * g + 0.0722f * b),
                Z = f((0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f);
    float* o = &lab.d[i * 3];
    o[0] = 116.0f * Y - 16.0f, o[1] = 500.0f * (X - Y), o[2] = 200.0f * (Y - Z);
  });
  return lab;
}

Img toRgb(const Img& lab) {
  Img rgb(lab.w, lab.h, 3);
  parallelFor(lab.h, nullptr, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < lab.w; ++x) labToRgb(lab.px(x, y), rgb.px(x, y));
  });
  return rgb;
}

Img channel(const Img& src, int k, float scale = 1) {
  Img out(src.w, src.h, 1);
  each(src.w, src.h, nullptr, [&](size_t i) { out.d[i] = src.d[i * static_cast<size_t>(src.c) + static_cast<size_t>(k)] * scale; });
  return out;
}

// One pass of a bilateral filter along one axis, in Lab (edges stay sharp, areas go flat).
Img bilateral1D(const Img& lab, int r, float sigS, float sigR, bool horizontal, const CancellationToken* token) {
  Img out(lab.w, lab.h, 3);
  std::vector<float> ws(static_cast<size_t>(2 * r + 1));
  for (int o = -r; o <= r; ++o) ws[static_cast<size_t>(o + r)] = std::exp(-static_cast<float>(o * o) / (2 * sigS * sigS));
  constexpr int kTab = 1024;
  const float maxD2 = 9 * sigR * sigR;
  std::array<float, kTab + 1> wr{};
  for (int i = 0; i <= kTab; ++i) wr[static_cast<size_t>(i)] = std::exp(-(static_cast<float>(i) / kTab * maxD2) / (2 * sigR * sigR));
  parallelFor(lab.h, token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < lab.w; ++x) {
        const float* c = lab.px(x, y);
        float s0 = 0, s1 = 0, s2 = 0, sw = 0;
        for (int o = -r; o <= r; ++o) {
          const int xx = horizontal ? std::clamp(x + o, 0, lab.w - 1) : x;
          const int yy = horizontal ? y : std::clamp(y + o, 0, lab.h - 1);
          const float* q = lab.px(xx, yy);
          const float d0 = q[0] - c[0], d1 = q[1] - c[1], d2 = q[2] - c[2];
          const float d = d0 * d0 + d1 * d1 + d2 * d2;
          if (d >= maxD2) continue;
          const float w = ws[static_cast<size_t>(o + r)] * wr[static_cast<size_t>(d / maxD2 * kTab)];
          s0 += w * q[0], s1 += w * q[1], s2 += w * q[2], sw += w;
        }
        float* p = out.px(x, y);
        p[0] = s0 / sw, p[1] = s1 / sw, p[2] = s2 / sw;
      }
  });
  return out;
}

// A blur that only gathers where `weight` is (normalised convolution): the
// scene blurred without the person's colours bleeding into it.
Img weightedBlur(const Img& src, const Img& weight, float sigma, const CancellationToken* token) {
  Img wsrc(src.w, src.h, src.c);
  for (size_t i = 0; i < weight.d.size(); ++i)
    for (int k = 0; k < src.c; ++k) wsrc.d[i * static_cast<size_t>(src.c) + static_cast<size_t>(k)] = src.d[i * static_cast<size_t>(src.c) + static_cast<size_t>(k)] * weight.d[i];
  const Img a = blur(wsrc, sigma, token), b = blur(weight, sigma, token);
  Img out(src.w, src.h, src.c);
  for (size_t i = 0; i < weight.d.size(); ++i)
    for (int k = 0; k < src.c; ++k) {
      const size_t j = i * static_cast<size_t>(src.c) + static_cast<size_t>(k);
      out.d[j] = b.d[i] > 1e-3f ? a.d[j] / b.d[i] : src.d[j];
    }
  return out;
}

// ---- faces ---------------------------------------------------------------------

struct Face {
  art::FaceBox box;  // normalised
  bool drawn = false;  // from contour points
  std::vector<Pt> oval, eye[2], brow[2], lipsOuter, mouthInner, mouthLine, lowerLip;
  Pt nose[3]{};
  float eyeDist = 0;  // px
  Pt axis{1, 0};      // along the eyes
  // Filled in while drawing: the irises (for the catchlights).
  Pt iris[2]{};
  float irisR[2]{0, 0};
};

Pt mean(const std::vector<Pt>& v) {
  Pt m;
  for (const Pt& p : v) m.x += p.x, m.y += p.y;
  if (!v.empty()) m.x /= static_cast<float>(v.size()), m.y /= static_cast<float>(v.size());
  return m;
}

// Points sorted along the face's horizontal axis (from one side to the other).
std::vector<Pt> along(std::vector<Pt> v, Pt axis, bool reverse) {
  std::sort(v.begin(), v.end(), [&](const Pt& a, const Pt& b) {
    const float ta = a.x * axis.x + a.y * axis.y, tb = b.x * axis.x + b.y * axis.y;
    return reverse ? ta > tb : ta < tb;
  });
  return v;
}

// A closed shape from scattered points: sorted by angle around their centre.
std::vector<Pt> around(std::vector<Pt> v) {
  const Pt c = mean(v);
  std::sort(v.begin(), v.end(), [&](const Pt& a, const Pt& b) { return std::atan2(a.y - c.y, a.x - c.x) < std::atan2(b.y - c.y, b.x - c.x); });
  return v;
}

std::vector<Pt> scaled(const std::vector<Pt>& v, float s) {
  const Pt c = mean(v);
  std::vector<Pt> out;
  for (const Pt& p : v) out.push_back({c.x + (p.x - c.x) * s, c.y + (p.y - c.y) * s});
  return out;
}

// The contour points of a face in pixels; false if any part is missing.
bool fromContour(const FaceContour& fc, int w, int h, Face& f) {
  auto pts = [&](int from, int count, std::vector<Pt>& out) {
    out.clear();
    for (int i = from; i < from + count; ++i) {
      const float x = fc.p[static_cast<size_t>(i * 2)], y = fc.p[static_cast<size_t>(i * 2 + 1)];
      if (!(x >= 0 && y >= 0 && x <= 1.5f && y <= 1.5f) || !std::isfinite(x) || !std::isfinite(y)) return false;
      out.push_back({x * static_cast<float>(w), y * static_cast<float>(h)});
    }
    return true;
  };
  using namespace contour;
  std::vector<Pt> oval, lbt, lbb, rbt, rbb, le, re, ult, ulb, llt, llb, nb, nbot;
  if (!pts(kOval, 36, oval) || !pts(kLeftBrowTop, 5, lbt) || !pts(kLeftBrowBottom, 5, lbb) || !pts(kRightBrowTop, 5, rbt) ||
      !pts(kRightBrowBottom, 5, rbb) || !pts(kLeftEye, 16, le) || !pts(kRightEye, 16, re) || !pts(kUpperLipTop, 11, ult) ||
      !pts(kUpperLipBottom, 9, ulb) || !pts(kLowerLipTop, 9, llt) || !pts(kLowerLipBottom, 9, llb) || !pts(kNoseBridge, 2, nb) ||
      !pts(kNoseBottom, 3, nbot))
    return false;
  const Pt e0 = mean(le), e1 = mean(re);
  f.eyeDist = std::hypot(e1.x - e0.x, e1.y - e0.y);
  if (f.eyeDist < 4) return false;
  f.axis = {(e1.x - e0.x) / f.eyeDist, (e1.y - e0.y) / f.eyeDist};
  if (f.axis.x < 0) f.axis = {-f.axis.x, -f.axis.y};
  f.oval = filters::smoothCurve(around(oval), 3, true);
  f.eye[0] = around(le);
  f.eye[1] = around(re);
  auto browShape = [&](const std::vector<Pt>& top, const std::vector<Pt>& bottom) {
    std::vector<Pt> s = along(top, f.axis, false);
    const std::vector<Pt> b = along(bottom, f.axis, true);
    s.insert(s.end(), b.begin(), b.end());
    return s;
  };
  f.brow[0] = browShape(lbt, lbb);
  f.brow[1] = browShape(rbt, rbb);
  // The lips: the outer line (upper top, then lower bottom back), the opening between them.
  {
    std::vector<Pt> s = along(ult, f.axis, false);
    const std::vector<Pt> b = along(llb, f.axis, true);
    s.insert(s.end(), b.begin(), b.end());
    f.lipsOuter = filters::smoothCurve(s, 3, true);
    const std::vector<Pt> u = along(ulb, f.axis, false);
    const std::vector<Pt> l = along(llt, f.axis, true);
    std::vector<Pt> in = u;
    in.insert(in.end(), l.begin(), l.end());
    f.mouthInner = filters::smoothCurve(in, 3, true);
    // The mouth line runs corner to corner along the upper lip's lower edge.
    std::vector<Pt> line = along(ulb, f.axis, false);
    const std::vector<Pt> outer = along(ult, f.axis, false);
    line.insert(line.begin(), outer.front());
    line.push_back(outer.back());
    f.mouthLine = filters::smoothCurve(line, 3, false);
    f.lowerLip = along(llb, f.axis, false);
  }
  const std::vector<Pt> nose = along(nbot, f.axis, false);
  for (int i = 0; i < 3; ++i) f.nose[i] = nose[static_cast<size_t>(i)];
  f.drawn = true;
  return true;
}

// Anime eyes: each eye magnified in place (a smooth bulge that fades to nothing at its edge).
// Returns, for moving contour points, nothing: the caller maps them with eyeWarpPoint.
struct EyeWarp {
  Pt c;
  float R = 0, s = 1;
};

float bulge(float d, float s) { return 1 - (1 - 1 / s) * (1 - d * d) * (1 - d * d); }  // < 1 inside: read from nearer the centre

void bigEyes(Img& rgb, const std::vector<EyeWarp>& warps) {
  if (warps.empty()) return;
  const Img src = rgb;
  for (const EyeWarp& e : warps) {
    const int x0 = std::max(0, static_cast<int>(e.c.x - e.R)), x1 = std::min(rgb.w - 1, static_cast<int>(e.c.x + e.R));
    const int y0 = std::max(0, static_cast<int>(e.c.y - e.R)), y1 = std::min(rgb.h - 1, static_cast<int>(e.c.y + e.R));
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        const float dx = static_cast<float>(x) - e.c.x, dy = static_cast<float>(y) - e.c.y;
        const float d = std::sqrt(dx * dx + dy * dy) / e.R;
        if (d >= 1) continue;
        const float k = bulge(d, e.s);
        src.sample3(e.c.x + dx * k, e.c.y + dy * k, rgb.px(x, y));
      }
  }
}

// Where a point of the photo lands after the bulge (the inverse of the lookup, by bisection).
Pt warpPoint(Pt p, const std::vector<EyeWarp>& warps) {
  for (const EyeWarp& e : warps) {
    const float dx = p.x - e.c.x, dy = p.y - e.c.y;
    const float ds = std::sqrt(dx * dx + dy * dy) / e.R;
    if (ds >= 1 || ds <= 1e-6f) continue;
    float lo = 0, hi = 1;
    for (int i = 0; i < 30; ++i) {
      const float m = 0.5f * (lo + hi);
      (m * bulge(m, e.s) < ds ? lo : hi) = m;
    }
    const float d = 0.5f * (lo + hi);
    p = {e.c.x + dx / ds * d, e.c.y + dy / ds * d};
  }
  return p;
}

// ---- painted backgrounds ----------------------------------------------------------

void paintSky(Img& out, float G, uint32_t seed, int kind) {
  const int w = out.w, h = out.h;
  parallelFor(h, nullptr, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        const float fy = static_cast<float>(y) / static_cast<float>(h), fx = static_cast<float>(x) / static_cast<float>(w);
        float c[3];
        if (kind == 0) {  // day: deep blue at the top to a pale horizon
          const Rgb3 top{0.13f, 0.38f, 0.82f}, low{0.66f, 0.86f, 0.98f};
          for (int k = 0; k < 3; ++k) c[k] = mix(top[static_cast<size_t>(k)], low[static_cast<size_t>(k)], std::pow(fy, 0.8f));
        } else if (kind == 1) {  // sunset: violet, rose, gold
          const Rgb3 a{0.27f, 0.20f, 0.50f}, b{0.93f, 0.45f, 0.45f}, cc{1.0f, 0.80f, 0.47f};
          for (int k = 0; k < 3; ++k)
            c[k] = fy < 0.55f ? mix(a[static_cast<size_t>(k)], b[static_cast<size_t>(k)], fy / 0.55f)
                              : mix(b[static_cast<size_t>(k)], cc[static_cast<size_t>(k)], (fy - 0.55f) / 0.45f);
        } else {  // night: navy with a lighter horizon
          const Rgb3 top{0.03f, 0.05f, 0.16f}, low{0.13f, 0.17f, 0.38f};
          for (int k = 0; k < 3; ++k) c[k] = mix(top[static_cast<size_t>(k)], low[static_cast<size_t>(k)], fy);
        }
        if (kind <= 1) {
          // Big puffy cumulus, cel-shaded: low-frequency shapes banked towards the horizon, a shaded underside, crisp edges.
          const float sx = static_cast<float>(x) / (G / 1.7f), sy = static_cast<float>(y) / (G / 2.6f);
          const float n = 0.62f * fbm(sx, sy, seed + 3, 3) + 0.38f * fbm(sx * 3.3f, sy * 3.3f, seed + 5, 3);
          const float band = smoothstep(0.2f, 0.45f, fy) * (1 - smoothstep(0.8f, 1.0f, fy));
          const float density = n * (0.7f + 0.45f * band);
          const float aa = 1.2f / G;
          const float cl = smoothstep(0.545f - aa * 4, 0.545f + aa * 4, density);
          const float above = 0.62f * fbm(sx, sy - 0.09f, seed + 3, 3) + 0.38f * fbm(sx * 3.3f, (sy - 0.09f) * 3.3f, seed + 5, 3);
          const float under = smoothstep(0.012f - aa * 6, 0.012f + aa * 6, density - above * (0.7f + 0.45f * band));
          const Rgb3 lit = kind == 0 ? Rgb3{1.0f, 1.0f, 1.0f} : Rgb3{1.0f, 0.86f, 0.70f};
          const Rgb3 shade = kind == 0 ? Rgb3{0.74f, 0.82f, 0.95f} : Rgb3{0.80f, 0.48f, 0.55f};
          for (int k = 0; k < 3; ++k) c[k] = mix(c[k], mix(lit[static_cast<size_t>(k)], shade[static_cast<size_t>(k)], under), cl);
          if (kind == 1) {  // the low sun's glow
            const float d = std::hypot(fx - 0.7f, fy - 0.82f);
            const float g = std::exp(-d * d / 0.02f);
            for (int k = 0; k < 3; ++k) c[k] = std::min(1.0f, c[k] + g * (k == 2 ? 0.15f : 0.35f));
          }
        } else {
          // Stars, and a moon with its glow.
          const float sv = hash01(x / 3, y / 3, seed + 7);
          if (sv > 0.9965f && hash01(x, y, seed + 9) > 0.6f) {
            const float tw = 0.6f + 0.4f * hash01(x / 3, y / 3, seed + 11);
            for (int k = 0; k < 3; ++k) c[k] = std::max(c[k], tw);
          }
          const float d = std::hypot((fx - 0.78f) * static_cast<float>(w) / G, (fy - 0.18f) * static_cast<float>(h) / G);
          const float moon = 1 - smoothstep(0.065f, 0.07f, d);
          const float glow = std::exp(-d * d / 0.02f) * 0.35f;
          for (int k = 0; k < 3; ++k) c[k] = std::min(1.0f, mix(c[k] + glow * (k == 2 ? 1.0f : 0.8f), 0.98f - 0.04f * static_cast<float>(2 - k), moon));
        }
        float* p = out.px(x, y);
        for (int k = 0; k < 3; ++k) p[k] = clamp01(c[k]);
      }
  });
}

// Dots on a rotated grid, their area growing with `amount` (0..1) — darkening the colour beneath (or a set colour).
void dots(Img& rgb, const Img& amount, float period, float angle, float darken, const Rgb3* colour, float strength) {
  const float ca = std::cos(angle), sa = std::sin(angle);
  parallelFor(rgb.h, nullptr, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < rgb.w; ++x) {
        const float s = amount.at(x, y);
        if (s <= 0.02f) continue;
        const float u = (static_cast<float>(x) * ca + static_cast<float>(y) * sa) / period;
        const float v = (-static_cast<float>(x) * sa + static_cast<float>(y) * ca) / period;
        const float du = u - std::floor(u) - 0.5f, dv = v - std::floor(v) - 0.5f;
        const float d = std::sqrt(du * du + dv * dv);
        const float r = 0.5f * std::sqrt(std::min(1.0f, s));
        const float aa = 0.7f / period;
        const float cover = (1 - smoothstep(r - aa, r + aa, d)) * strength;
        float* p = rgb.px(x, y);
        for (int k = 0; k < 3; ++k) p[k] = mix(p[k], colour != nullptr ? (*colour)[static_cast<size_t>(k)] : p[k] * darken, cover);
      }
  });
}

}  // namespace

Error render(ImageBuffer& image, const Options& o, const CancellationToken* token) {
  if (image.empty() || (image.format() != PixelFormat::RGBA8 && image.format() != PixelFormat::BGRA8))
    return {PP_ERROR_INVALID_ARGUMENT, "Toon needs an RGBA or BGRA image"};
  if (image.width() < 16 || image.height() < 16) return {PP_ERROR_INVALID_ARGUMENT, "The photo is too small"};
  const int si = static_cast<int>(o.style), bi = static_cast<int>(o.background);
  if (si < 0 || si >= kStyleCount || bi < 0 || bi >= kBackgroundCount) return {PP_ERROR_INVALID_ARGUMENT, "Unknown style or background"};
  auto ok = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
  if (!ok(o.lines, 0, 1) || o.shades < 2 || o.shades > 6 || !ok(o.vivid, -1, 1) || !ok(o.detail, 0, 1) || !ok(o.eyes, 0, 1) ||
      !ok(o.skin, 0, 1) || !ok(o.glow, 0, 1) || !ok(o.strength, 0, 1))
    return {PP_ERROR_INVALID_ARGUMENT, "A toon control is out of range"};
  if (o.subject != nullptr && (o.subject->empty() || o.subject->format() != PixelFormat::Gray8))
    return {PP_ERROR_INVALID_ARGUMENT, "The person mask must be a grey image"};
  if (o.strength <= 0) return {PP_OK, {}};  // the photo itself

  const StylePreset& P = kPresets[static_cast<size_t>(si)];
  const int w = image.width(), h = image.height();
  const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
  const float G = static_cast<float>(std::min(w, h));
  const float px = G / 1000.0f;  // a style's widths are in thousandths of the short side
  const uint32_t seed = o.seed == 0 ? 1U : o.seed;
  const bool bgra = image.format() == PixelFormat::BGRA8;
  auto cancelled = [&] { return isCancelled(token); };
  const Error kCancelled{PP_ERROR_CANCELLED, "Cancelled"};

  Img rgb(w, h, 3);
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = image.row(y);
    for (int x = 0; x < w; ++x) {
      float* p = rgb.px(x, y);
      p[0] = static_cast<float>(row[x * 4 + (bgra ? 2 : 0)]) / 255.0f;
      p[1] = static_cast<float>(row[x * 4 + 1]) / 255.0f;
      p[2] = static_cast<float>(row[x * 4 + (bgra ? 0 : 2)]) / 255.0f;
    }
  }

  // ---- the faces: from their contour points where known, else their boxes
  std::vector<Face> faces;
  for (const art::FaceBox& b : o.faces)
    if (b.w > 0 && b.h > 0 && std::isfinite(b.x) && std::isfinite(b.y)) {
      Face f;
      f.box = b;
      faces.push_back(f);
    }
  for (const FaceContour& fc : o.contours) {
    Face f;
    if (!fromContour(fc, w, h, f)) continue;
    // The box it belongs to: the one around the eyes' midpoint (or a new face).
    const Pt c = mean({mean(f.eye[0]), mean(f.eye[1])});
    Face* owner = nullptr;
    float best = 1e9f;
    for (Face& g : faces) {
      if (g.drawn) continue;
      const float bx = g.box.x * static_cast<float>(w), by = g.box.y * static_cast<float>(h), bw = g.box.w * static_cast<float>(w),
                  bh = g.box.h * static_cast<float>(h);
      if (c.x < bx || c.y < by || c.x > bx + bw || c.y > by + bh) continue;
      const float d = std::hypot(c.x - (bx + bw / 2), c.y - (by + bh / 2));
      if (d < best) best = d, owner = &g;
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const Pt& p : f.oval) x0 = std::min(x0, p.x), y0 = std::min(y0, p.y), x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
    if (owner == nullptr) {
      f.box = {.x = x0 / static_cast<float>(w), .y = y0 / static_cast<float>(h), .w = (x1 - x0) / static_cast<float>(w), .h = (y1 - y0) / static_cast<float>(h)};
      const Pt a = mean(f.eye[0]), b = mean(f.eye[1]);
      f.box.eyes = {a.x / static_cast<float>(w), a.y / static_cast<float>(h), b.x / static_cast<float>(w), b.y / static_cast<float>(h)};
      faces.push_back(f);
    } else {
      f.box = owner->box;
      *owner = f;
    }
    if (faces.size() >= 8) break;
  }

  // ---- 0. anime eyes (the photo and the eye contours move together)
  if (o.eyes > 0.01f) {
    std::vector<EyeWarp> warps;
    for (const Face& f : faces) {
      Pt e[2];
      if (f.drawn) e[0] = mean(f.eye[0]), e[1] = mean(f.eye[1]);
      else if (f.box.eyes[0] > 0 && f.box.eyes[2] > 0)
        e[0] = {f.box.eyes[0] * static_cast<float>(w), f.box.eyes[1] * static_cast<float>(h)},
        e[1] = {f.box.eyes[2] * static_cast<float>(w), f.box.eyes[3] * static_cast<float>(h)};
      else continue;
      const float dist = std::hypot(e[1].x - e[0].x, e[1].y - e[0].y);
      for (const Pt& c : e) warps.push_back({c, dist * 0.42f, 1 + 0.3f * o.eyes});
    }
    bigEyes(rgb, warps);
    for (Face& f : faces) {
      if (!f.drawn) continue;
      for (auto* v : {&f.eye[0], &f.eye[1], &f.brow[0], &f.brow[1]})
        for (Pt& p : *v) p = warpPoint(p, warps);
    }
  }
  if (cancelled()) return kCancelled;

  // ---- 1. clean-up: noise, then low light
  Img lab = toLab(rgb);
  rgb = Img();  // (memory: the photo stays in `image` for Strength)
  Img L = channel(lab, 0);
  const float noise = filters::noiseSigma(L);  // L units
  {
    // Lightness lightly (fine detail stays), colour strongly: sensor noise is mostly colour.
    const float k = smoothstep(0.4f, 2.5f, noise);
    const Img Lq = filters::guided(L, L, std::max(1, static_cast<int>(G / 700)), std::pow(1.0f + 2.5f * noise, 2.0f), token);
    const int rc = std::max(2, static_cast<int>(G / 260));
    const float epsC = std::pow(2.0f + 3.0f * noise, 2.0f);
    const Img aq = filters::guidedFast(Lq, channel(lab, 1), rc, epsC, token), bq = filters::guidedFast(Lq, channel(lab, 2), rc, epsC, token);
    each(w, h, token, [&](size_t i) {
      L.d[i] = mix(L.d[i], Lq.d[i], 0.35f + 0.55f * k);
      lab.d[i * 3 + 1] = mix(lab.d[i * 3 + 1], aq.d[i], 0.6f + 0.4f * k);
      lab.d[i * 3 + 2] = mix(lab.d[i * 3 + 2], bq.d[i], 0.6f + 0.4f * k);
    });
  }
  if (cancelled()) return kCancelled;
  {
    std::vector<float> v;
    for (size_t i = 0; i < n; i += 7) v.push_back(L.d[i]);
    std::sort(v.begin(), v.end());
    const float med = v[v.size() / 2], hi = v[v.size() * 98 / 100];
    const float lowLight = 1 - smoothstep(28.0f, 48.0f, med);
    const Img before = L;
    if (med < 40 && hi < 97) {
      // Exposure: the middle brought towards 45 (a gamma on lightness: whites stay white).
      const float target = std::min(46.0f, med * 1.7f);
      const float g = std::log(target / 100.0f) / std::log(std::max(1.0f, med) / 100.0f);
      for (float& l : L.d) l = 100.0f * std::pow(std::clamp(l / 100.0f, 0.0f, 1.0f), g);
    }
    // Local contrast (CLAHE), strong in low light, a touch otherwise.
    Img eq = L;
    filters::clahe(eq, static_cast<int>(G / 6), 2.0f, token);
    const float amt = 0.2f + 0.45f * lowLight;
    each(w, h, token, [&](size_t i) {
      L.d[i] = mix(L.d[i], eq.d[i], amt);
      // Brighter colour keeps its saturation (a lifted dark doesn't turn grey).
      const float lift = std::clamp((L.d[i] + 4) / (before.d[i] + 4), 1.0f, 1.5f);
      lab.d[i * 3] = L.d[i];
      lab.d[i * 3 + 1] *= std::sqrt(lift);
      lab.d[i * 3 + 2] *= std::sqrt(lift);
    });
  }
  if (cancelled()) return kCancelled;

  // ---- 2. where things are
  // The person: the mask refined to the photo's own edges, so the outline and the colour meet exactly.
  Img m;
  const bool hasMask = o.subject != nullptr;
  if (hasMask) {
    const ImageBuffer& mb = *o.subject;
    Img mask(mb.width(), mb.height(), 1);
    for (int y = 0; y < mb.height(); ++y)
      for (int x = 0; x < mb.width(); ++x) mask.at(x, y) = static_cast<float>(mb.row(y)[x]) / 255.0f;
    // The radius covers the mask's own coarseness (ML Kit's mask is at most 1024 px; an export is larger).
    const float upscale = static_cast<float>(w) / static_cast<float>(std::max(1, mb.width()));
    const int r = std::max({2, static_cast<int>(G / 160), static_cast<int>(std::lround(2.5f * upscale + 1))});
    mask = resize(mask, w, h);
    const Img q = filters::guided(channel(lab, 0, 0.01f), mask, r, 0.0025f, token);
    m = Img(w, h, 1);
    for (size_t i = 0; i < n; ++i) m.d[i] = smoothstep(0.3f, 0.7f, q.d[i]);
  } else {
    m = Img(w, h, 1, 1.0f);
  }
  // Faces: the oval, its inside, and the features.
  Img faceOval(w, h, 1, 0.0f), faceInner(w, h, 1, 0.0f), featZone(w, h, 1, 0.0f), featCore(w, h, 1, 0.0f), headZone(w, h, 1, 0.0f);
  for (const Face& f : faces) {
    const float bx = f.box.x * static_cast<float>(w), by = f.box.y * static_cast<float>(h), bw = f.box.w * static_cast<float>(w),
                bh = f.box.h * static_cast<float>(h);
    // The head (where the hair is): above and around the face.
    {
      const float cx = bx + bw / 2, cy = by + bh * 0.3f, rx = bw * 0.95f, ry = bh * 0.95f;
      for (int y = std::max(0, static_cast<int>(cy - ry)); y <= std::min(h - 1, static_cast<int>(cy + ry * 1.6f)); ++y)
        for (int x = std::max(0, static_cast<int>(cx - rx)); x <= std::min(w - 1, static_cast<int>(cx + rx)); ++x) {
          const float dx = (static_cast<float>(x) - cx) / rx, dy = (static_cast<float>(y) - cy) / (static_cast<float>(y) < cy ? ry : ry * 1.6f);
          headZone.at(x, y) = std::max(headZone.at(x, y), 1 - smoothstep(0.8f, 1.0f, std::sqrt(dx * dx + dy * dy)));
        }
    }
    if (f.drawn) {
      filters::fillPolygon(faceOval, f.oval, 1);
      filters::fillPolygon(faceInner, scaled(f.oval, 0.86f), 1);
      for (int e = 0; e < 2; ++e) {
        filters::fillPolygon(featCore, f.eye[e], 1);
        filters::fillPolygon(featCore, f.brow[e], 1);
        filters::fillPolygon(featZone, scaled(f.eye[e], 1.6f), 1);
        filters::fillPolygon(featZone, scaled(f.brow[e], 1.3f), 1);
      }
      filters::fillPolygon(featCore, f.lipsOuter, 1);
      filters::fillPolygon(featZone, scaled(f.lipsOuter, 1.2f), 1);
      std::vector<Pt> nostrils = {f.nose[0], f.nose[1], f.nose[2], {f.nose[1].x, f.nose[1].y - 0.12f * f.eyeDist}};
      filters::fillPolygon(featZone, scaled(around(nostrils), 1.6f), 1);
      continue;
    }
    // A face known only by its box: an ellipse, and zones for the eyes, mouth and nose.
    const float cx = bx + bw * 0.5f, cy = by + bh * 0.5f, rx = bw * 0.55f, ry = bh * 0.62f;
    for (int y = std::max(0, static_cast<int>(cy - ry * 1.2f)); y <= std::min(h - 1, static_cast<int>(cy + ry * 1.2f)); ++y)
      for (int x = std::max(0, static_cast<int>(cx - rx * 1.2f)); x <= std::min(w - 1, static_cast<int>(cx + rx * 1.2f)); ++x) {
        const float fx = static_cast<float>(x), fy = static_cast<float>(y);
        const float dx = (fx - cx) / rx, dy = (fy - cy) / ry;
        const float r = std::sqrt(dx * dx + dy * dy);
        const float face = 1 - smoothstep(0.8f, 1.0f, r);
        faceOval.at(x, y) = std::max(faceOval.at(x, y), face);
        faceInner.at(x, y) = std::max(faceInner.at(x, y), 1 - smoothstep(0.62f, 0.8f, r));
        float feat = 0;
        if (f.box.eyes[0] > 0)
          for (int e = 0; e < 2; ++e) {
            const float ex = f.box.eyes[static_cast<size_t>(e * 2)] * static_cast<float>(w), ey = f.box.eyes[static_cast<size_t>(e * 2 + 1)] * static_cast<float>(h);
            const float ddx = (fx - ex) / (bw * 0.2f), ddy = (fy - ey + bw * 0.05f) / (bw * 0.15f);
            feat = std::max(feat, 1 - smoothstep(0.6f, 1.0f, std::sqrt(ddx * ddx + ddy * ddy)));
          }
        if (f.box.mouth[2] > f.box.mouth[0]) {
          const float mx = (f.box.mouth[0] + f.box.mouth[2]) * 0.5f * static_cast<float>(w), my = (f.box.mouth[1] + f.box.mouth[3]) * 0.5f * static_cast<float>(h);
          const float ddx = (fx - mx) / std::max(bw * 0.18f, (f.box.mouth[2] - f.box.mouth[0]) * static_cast<float>(w) * 0.65f), ddy = (fy - my) / (bw * 0.09f);
          feat = std::max(feat, 1 - smoothstep(0.6f, 1.0f, std::sqrt(ddx * ddx + ddy * ddy)));
        }
        const float nx = (fx - cx) / (bw * 0.11f), ny = (fy - (cy + 0.1f * bh)) / (bw * 0.08f);
        feat = std::max(feat, 0.8f * (1 - smoothstep(0.6f, 1.0f, std::sqrt(nx * nx + ny * ny))));
        featZone.at(x, y) = std::max(featZone.at(x, y), feat * face);
        featCore.at(x, y) = std::max(featCore.at(x, y), smoothstep(0.5f, 0.9f, feat) * face);
      }
  }
  if (hasMask)
    for (size_t i = 0; i < n; ++i) m.d[i] = std::max(m.d[i], faceOval.d[i]);

  // Skin: the face's own colour (trimmed of beard, shine and features), then everywhere it appears on the person.
  struct Tone3 {
    float L, a, b;
  };
  Tone3 skinTone[3]{};  // shadow, base, highlight
  float thrShadow = 0, thrHigh = 0;
  bool skinKnown = false;
  Img skin(w, h, 1, 0.0f);
  Img skinLike(w, h, 1, 0.0f);  // skin-coloured at any light, features' rims included: never colour-boosted
  {
    std::vector<float> sl, sa, sb;
    for (size_t i = 0; i < n; i += 2)
      if (faceInner.d[i] > 0.9f && featZone.d[i] < 0.1f) sl.push_back(L.d[i]), sa.push_back(lab.d[i * 3 + 1]), sb.push_back(lab.d[i * 3 + 2]);
    if (sl.size() > 40) {
      std::vector<float> sorted = sl;
      std::sort(sorted.begin(), sorted.end());
      auto pct = [&](float p) { return sorted[std::min(sorted.size() - 1, static_cast<size_t>(p * static_cast<float>(sorted.size())))]; };
      const float lo = pct(0.12f), hi = pct(0.9f);
      std::vector<float> ta, tb, tl;
      for (size_t i = 0; i < sl.size(); ++i)
        if (sl[i] >= lo && sl[i] <= hi) ta.push_back(sa[i]), tb.push_back(sb[i]), tl.push_back(sl[i]);
      auto median = [](std::vector<float> v) {
        std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
        return v[v.size() / 2];
      };
      float A = median(ta), B = median(tb);
      // An illustrator's skin is never ashy: a skin hue keeps some warmth.
      const float c = std::hypot(A, B), hue = std::atan2(B, A) * 57.29578f;
      const float skinHue = smoothstep(0.0f, 15.0f, hue) * (1 - smoothstep(75.0f, 95.0f, hue));
      if (c > 1 && c < 18) {
        const float k = mix(1.0f, 18.0f / c, skinHue);
        A *= k, B *= k;
      }
      const float base = median(tl);
      thrShadow = pct(0.32f);
      thrHigh = pct(0.86f);
      const float shadowL = std::min(base - 7, pct(0.15f));
      const float highL = std::clamp(pct(0.9f), base + 2, base + P.skinHighlight);
      // Hue-true shades: the shadow a touch redder and richer, the highlight a touch lighter in colour.
      auto shade = [&](float l, float chroma, float turn) {
        const float hh = std::atan2(B, A) + turn, cc = std::hypot(A, B) * chroma;
        return Tone3{l, cc * std::cos(hh), cc * std::sin(hh)};
      };
      skinTone[0] = shade(shadowL, 1.1f, -0.08f);
      skinTone[1] = shade(base, 1.0f, 0);
      skinTone[2] = shade(highL, 0.9f, 0.04f);
      skinKnown = true;
      // Skin everywhere on the person: near the face's colour and within its range of light.
      const float tol = std::max(8.0f, 0.45f * std::hypot(A, B));
      const float A0 = median(ta), B0 = median(tb);
      each(w, h, token, [&](size_t i) {
        const float d = std::hypot(lab.d[i * 3 + 1] - A0, lab.d[i * 3 + 2] - B0);
        const float l = L.d[i];
        // Shine on skin is still skin (its light is clamped to the highlight tone later: no white patches).
        const float inL = smoothstep(lo - 16, lo - 6, l) * (1 - smoothstep(hi + 10, hi + 22, l) * smoothstep(tol * 0.8f, tol * 1.4f, d));
        const float body = (1 - smoothstep(tol, tol * 1.7f, d)) * inL * m.d[i];
        // In the face, all of it but a beard (dark and far from the skin's colour) and the features.
        const float notBeard = smoothstep(lo - 22, lo - 10, l) * (1 - smoothstep(tol * 1.4f, tol * 2.6f, d) * (1 - smoothstep(lo - 6, lo + 4, l)));
        const float face = faceOval.d[i] * notBeard;
        skin.d[i] = std::max(body, face) * (1 - featCore.d[i]);
        skinLike.d[i] = (1 - smoothstep(tol * 1.2f, tol * 2.2f, d)) * std::max(m.d[i], faceOval.d[i]);
      });
      skin = blur(skin, std::max(0.8f, G / 500), token);
    }
  }
  // The hair: on the person, around the head, not face and not skin.
  Img hair(w, h, 1);
  for (size_t i = 0; i < n; ++i) hair.d[i] = headZone.d[i] * m.d[i] * (1 - faceOval.d[i]) * (1 - skin.d[i]);
  if (cancelled()) return kCancelled;

  // ---- 3. shapes
  {
    // The hair's local contrast raised: its shape and strands survive the flattening.
    const Img Lb = blur(L, std::max(1.5f, G / 220), token);
    for (size_t i = 0; i < n; ++i)
      if (hair.d[i] > 0.01f) lab.d[i * 3] = std::clamp(L.d[i] + 0.9f * hair.d[i] * (L.d[i] - Lb.d[i]), 0.0f, 100.0f);
  }
  const float simplify = std::clamp(P.smooth * (1.15f - 0.7f * o.detail), 0.1f, 1.2f);
  // Edge-preserving smoothing at a working size of at most ~1100 px on the short side (a large export
  // costs no more than a preview here); brought back up guided by the full-size light, so edges stay crisp.
  auto smoothAt = [&](const Img& src) {
    const int ww = src.w, hh = src.h;
    const size_t nn = static_cast<size_t>(ww) * static_cast<size_t>(hh);
    const float g = static_cast<float>(std::min(ww, hh));
    Img out;
    switch (P.smoothing) {
      case Smoothing::Kuwahara: {
        // At half size (fast), back up, then one bilateral pass to settle the edges.
        const Img half = down2(toRgb(src));
        const Img k = kuwahara(half, std::max(2, static_cast<int>(std::lround(g / 2 / 260 * (1 + 2 * simplify)))), token);
        out = toLab(resize(k, ww, hh));
        // Keep the fine light from the full size where the half size blurred an edge.
        for (size_t i = 0; i < nn; ++i) out.d[i * 3] = mix(out.d[i * 3], src.d[i * 3], 0.35f * (1 - simplify * 0.5f));
        const float s = std::max(1.0f, g / 500);
        out = bilateral1D(bilateral1D(out, static_cast<int>(2 * s), s, 8, true, token), static_cast<int>(2 * s), s, 8, false, token);
        break;
      }
      case Smoothing::Guided: {
        const Img gl = channel(src, 0, 0.01f);
        const int r = std::max(2, static_cast<int>(g / 140 * (0.5f + simplify)));
        out = Img(ww, hh, 3);
        for (int k = 0; k < 3; ++k) {
          const Img q = filters::guided(gl, channel(src, k), r, 0.002f, token);
          for (size_t i = 0; i < nn; ++i) out.d[i * 3 + static_cast<size_t>(k)] = q.d[i];
        }
        const float s = std::max(1.0f, g / 420);
        out = bilateral1D(bilateral1D(out, static_cast<int>(2 * s), s, 10, true, token), static_cast<int>(2 * s), s, 10, false, token);
        break;
      }
      case Smoothing::Bilateral: {
        out = src;
        const int iters = 1 + static_cast<int>(std::lround(2 * simplify));
        const float sigS = std::max(1.0f, g * mix(1.0f / 520, 1.0f / 230, std::min(1.0f, simplify)));
        const int r = std::max(2, static_cast<int>(sigS * 2));
        for (int it = 0; it < iters && !cancelled(); ++it) {
          out = bilateral1D(out, r, sigS, 7.0f + 5 * simplify, true, token);
          out = bilateral1D(out, r, sigS, 7.0f + 5 * simplify, false, token);
        }
        break;
      }
    }
    return out;
  };
  constexpr float kWorkingSide = 1100;
  Img sm;
  if (G <= kWorkingSide * 1.25f) {
    sm = smoothAt(lab);
  } else {
    const float f = kWorkingSide / G;
    const Img low = smoothAt(resize(lab, std::max(16, static_cast<int>(static_cast<float>(w) * f)), std::max(16, static_cast<int>(static_cast<float>(h) * f))));
    const Img up = resize(low, w, h);
    const Img guide = channel(lab, 0, 0.01f);
    const int r = std::max(2, static_cast<int>(std::lround(1.5f / f)));
    sm = Img(w, h, 3);
    for (int k = 0; k < 3; ++k) {
      const Img q = filters::guidedFast(guide, channel(up, k), r, k == 0 ? 0.0006f : 0.002f, token);
      each(w, h, token, [&](size_t i) { sm.d[i * 3 + static_cast<size_t>(k)] = q.d[i]; });
    }
  }
  if (cancelled()) return kCancelled;
  // The scene softened harder than the person (and without the person's colours bleeding in).
  Img smBack;
  if (hasMask) {
    Img wBack(w, h, 1);
    for (size_t i = 0; i < n; ++i) wBack.d[i] = 1 - m.d[i];
    smBack = weightedBlur(sm, wBack, std::max(1.0f, P.backBlur * px * (1.3f - 0.6f * o.detail)), token);
  }

  // ---- 4. colour
  const int k = std::clamp(P.clusters + (o.shades - 3) * 2 + static_cast<int>(std::lround((o.detail - 0.5f) * 4)), 3, 18);
  Img col;
  {
    Img where(w, h, 1);
    for (size_t i = 0; i < n; ++i) where.d[i] = m.d[i] > 0.5f && skin.d[i] < 0.5f ? 1.0f : 0.0f;
    auto pal = filters::kmeansLab(sm, &where, k, P.lWeight, 16000, seed);
    if (pal.empty()) pal = filters::kmeansLab(sm, nullptr, k, P.lWeight, 16000, seed);
    for (auto& c : pal) c[0] = std::min(c[0], 96.0f);  // no paper-white patches from shine
    filters::assignPalette(sm, pal, P.lWeight, P.celSoft, col, token);
    // A palette colour never hijacks a pixel's hue: no more colourful than the pixel itself allows
    // (dark hair stays hair-coloured, not the jeans' blue), and dark pixels keep their own hue.
    auto keepHue = [&](Img& c, const Img& own) {
      each(w, h, token, [&](size_t i) {
        float* p = &c.d[i * 3];
        const float* q = &own.d[i * 3];
        const float cc = std::hypot(p[1], p[2]), co = std::hypot(q[1], q[2]);
        const float maxC = co * 1.35f + 6;
        if (cc > maxC) p[1] *= maxC / cc, p[2] *= maxC / cc;
        const float darkT = 0.6f * (1 - smoothstep(14.0f, 32.0f, p[0]));
        p[1] = mix(p[1], q[1], darkT), p[2] = mix(p[2], q[2], darkT);
      });
    };
    keepHue(col, sm);
    if (hasMask) {
      Img colB;
      for (size_t i = 0; i < n; ++i) where.d[i] = m.d[i] < 0.5f ? 1.0f : 0.0f;
      auto palB = filters::kmeansLab(smBack, &where, std::clamp(P.backClusters + static_cast<int>(std::lround((o.detail - 0.5f) * 2)), 2, 12), P.lWeight, 12000, seed + 1);
      // The scene painted softly: wide cel boundaries and some of its own gradients (smooth bokeh never turns into blotches).
      filters::assignPalette(smBack, palB, P.lWeight, P.celSoft * 3 + 3, colB, token);
      keepHue(colB, smBack);
      const float backGrad = std::max(P.gradient, 0.3f);
      for (size_t i = 0; i < n; ++i)
        for (size_t c = 0; c < 3; ++c) col.d[i * 3 + c] = mix(mix(colB.d[i * 3 + c], smBack.d[i * 3 + c], backGrad), col.d[i * 3 + c], m.d[i]);
    }
    for (size_t i = 0; i < n; ++i)
      for (size_t c = 0; c < 3; ++c) col.d[i * 3 + c] = mix(col.d[i * 3 + c], sm.d[i * 3 + c], P.gradient * m.d[i]);
  }
  if (cancelled()) return kCancelled;
  // Skin: its own tones on its broad forms (no texture, no shine).
  Img skinShadowness(w, h, 1, 0.0f);
  if (skinKnown) {
    const Img Lb = blur(channel(sm, 0), std::max(1.5f, G / 110), token);
    const float soft = 1.2f + P.celSoft * 0.8f + P.gradient * 8;
    const bool three = P.skinTones >= 3 && o.shades >= 3;
    const float strength = 0.45f + 0.55f * o.skin;
    each(w, h, token, [&](size_t i) {
      const float s = skin.d[i];
      if (s <= 0.003f) return;
      const float lb = mix(sm.d[i * 3], Lb.d[i], 0.6f);
      const float tS = smoothstep(thrShadow - soft, thrShadow + soft, lb);
      const float tH = three ? smoothstep(thrHigh - soft, thrHigh + soft, lb) : 0.0f;
      float t[3];
      for (int c = 0; c < 3; ++c) {
        auto at = [c](const Tone3& v) { return c == 0 ? v.L : c == 1 ? v.a : v.b; };
        t[c] = mix(mix(at(skinTone[0]), at(skinTone[1]), tS), at(skinTone[2]), tH);
      }
      // Skin's own light kept a little within each tone (a drawn face, not a mask) where Skin is low.
      t[0] = mix(t[0], std::min(sm.d[i * 3], skinTone[2].L), (1 - o.skin) * 0.25f);
      const float a = s * strength;
      for (size_t c = 0; c < 3; ++c) col.d[i * 3 + c] = mix(col.d[i * 3 + c], t[c], a);
      skinShadowness.d[i] = s * (1 - tS);
    });
  }

  // Only the smoothed light is needed from here (the line art): the rest goes (memory).
  const Img smL = channel(sm, 0, 0.01f);
  const Img smBackL = hasMask ? channel(smBack, 0, 0.01f) : Img();
  sm = Img(), smBack = Img();
  // Each style's colour, then RGB.
  Img out(w, h, 3);
  Img shadow(w, h, 1, 0.0f);  // how far into the shadow (for tone and dots)
  {
    std::vector<float> v;
    for (size_t i = 0; i < n; i += 5)
      if (m.d[i] > 0.5f) v.push_back(col.d[i * 3]);
    if (v.size() < 16)
      for (size_t i = 0; i < n; i += 5) v.push_back(col.d[i * 3]);
    std::sort(v.begin(), v.end());
    const float p25 = v[v.size() / 4], p60 = v[v.size() * 6 / 10];
    std::vector<Rgb3> period;
    std::vector<std::array<float, 3>> periodLab;
    if (P.periodPalette)
      for (uint32_t hx : kPeriodPalette) {
        const Rgb3 c = hex(hx);
        std::array<float, 3> l{};
        rgbToLab(c.data(), l.data());
        periodLab.push_back(l);
      }
    const float gain = P.saturation * (1 + 0.45f * o.vivid);
    parallelFor(h, token, [&](int32_t y0, int32_t y1) {
      for (size_t i = static_cast<size_t>(y0) * static_cast<size_t>(w); i < static_cast<size_t>(y1) * static_cast<size_t>(w); ++i) {
        float l = col.d[i * 3], a = col.d[i * 3 + 1], b = col.d[i * 3 + 2];
        const float sk = skin.d[i], person = m.d[i];
        const float sh = 1 - smoothstep(p25, p60, l);
        shadow.d[i] = sh;
        // Contrast about the middle.
        l = 50 + (l - 50) * (1 + P.contrast * (1 - 0.6f * sk));
        // Colour: the style's gain, more on muted colours (vibrance); skin barely (never pushed orange).
        const float c = std::hypot(a, b);
        float g = gain * (1 + P.vibrance * (1 - smoothstep(10.0f, 45.0f, c)));
        g = mix(g, std::min(g, 1.0f + 0.1f * o.vivid), std::max(sk, skinLike.d[i]));
        a *= g, b *= g;
        // Warm lights, cool shadows (comic), lighter on skin.
        const float cool = P.shadowCool * (1 - 0.7f * sk);
        a += cool * (3.0f * sh - 1.0f * (1 - sh));
        b += cool * (-10.0f * sh + 4.0f * (1 - sh));
        // Pastel: lighter, softer colour.
        l += P.pastel * (100 - l) * 0.32f;
        a *= 1 - 0.28f * P.pastel, b *= 1 - 0.28f * P.pastel;
        // The kept scene quieter than the person.
        if (o.background == Background::Keep && hasMask) {
          const float q = (1 - person) * P.backDim;
          l = mix(l, 50 + (l - 50) * 0.7f + 5, q);
          a *= 1 - q, b *= 1 - q;
        }
        float lab3[3] = {l, a, b};
        // A period palette: each colour moved to the nearest 90s cel paint (skin keeps its own tones).
        if (!periodLab.empty()) {
          float bd = 1e30f;
          size_t bj = 0;
          for (size_t j = 0; j < periodLab.size(); ++j) {
            const float dl = (l - periodLab[j][0]) * 0.7f, da = a - periodLab[j][1], db = b - periodLab[j][2];
            const float d = dl * dl + da * da + db * db;
            if (d < bd) bd = d, bj = j;
          }
          const float t = 0.7f * (1 - 0.85f * sk);
          for (size_t q = 0; q < 3; ++q) lab3[q] = mix(lab3[q], periodLab[bj][q], t);
        }
        float rgbv[3];
        labToRgb(lab3, rgbv);
        if (P.mono) rgbv[0] = rgbv[1] = rgbv[2] = clamp01(lab3[0] / 100.0f);
        for (size_t q = 0; q < 3; ++q) out.d[i * 3 + q] = rgbv[q];
      }
    });
  }
  col = Img();
  if (cancelled()) return kCancelled;

  // ---- the background: painted, or softened
  if (hasMask && o.background != Background::Keep) {
    Img back(w, h, 3);
    switch (o.background) {
      case Background::Soft:
        back = blur(out, G / 40.0f, token);
        break;
      case Background::Sky:
        paintSky(back, G, seed, 0);
        break;
      case Background::Sunset:
        paintSky(back, G, seed, 1);
        break;
      case Background::Night:
        paintSky(back, G, seed, 2);
        break;
      default: {
        const Rgb3 top{0.98f, 0.86f, 0.90f}, low{0.84f, 0.86f, 0.98f};  // pastel
        for (int y = 0; y < h; ++y)
          for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) back.px(x, y)[c] = mix(top[static_cast<size_t>(c)], low[static_cast<size_t>(c)], static_cast<float>(y) / static_cast<float>(h));
      }
    }
    if (P.mono)
      for (size_t i = 0; i < n; ++i) back.d[i * 3] = back.d[i * 3 + 1] = back.d[i * 3 + 2] = luma(&back.d[i * 3]);
    for (size_t i = 0; i < n; ++i) {
      for (size_t c = 0; c < 3; ++c) out.d[i * 3 + c] = mix(back.d[i * 3 + c], out.d[i * 3 + c], m.d[i]);
      shadow.d[i] *= m.d[i];
    }
  }

  // ---- 5. the face, drawn from its contours
  Img featInk(w, h, 1, 0.0f);
  const float lineScale = mix(0.65f, 1.6f, o.lines);
  for (Face& f : faces) {
    if (!f.drawn) continue;
    const float base = std::max(0.9f, P.faceLine * px * lineScale * std::clamp(f.eyeDist / (G * 0.12f), 0.6f, 1.4f));
    // Everything drawn here lies within the face: work only there.
    std::vector<size_t> region;
    {
      float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
      for (const Pt& p : f.oval) x0 = std::min(x0, p.x), y0 = std::min(y0, p.y), x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
      const float mx = 0.15f * (x1 - x0), my = 0.15f * (y1 - y0);
      for (int y = std::max(0, static_cast<int>(y0 - my)); y <= std::min(h - 1, static_cast<int>(y1 + my)); ++y)
        for (int x = std::max(0, static_cast<int>(x0 - mx)); x <= std::min(w - 1, static_cast<int>(x1 + mx)); ++x)
          region.push_back(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x));
    }
    auto paint = [&](const Img& alpha, const float* lab3, float opacity) {
      float c[3];
      labToRgb(lab3, c);
      if (P.mono) c[0] = c[1] = c[2] = clamp01(lab3[0] / 100.0f);
      for (size_t i : region) {
        const float a = alpha.d[i] * opacity;
        if (a <= 0.002f) continue;
        for (size_t q = 0; q < 3; ++q) out.d[i * 3 + q] = mix(out.d[i * 3 + q], c[q], a);
      }
    };
    // The median colour of the photo (cleaned) inside a shape, of its darkest `share`.
    auto colourIn = [&](const Img& alpha, float share, float* lab3) {
      std::vector<std::array<float, 3>> v;
      for (size_t i : region)
        if (alpha.d[i] > 0.6f) v.push_back({lab.d[i * 3], lab.d[i * 3 + 1], lab.d[i * 3 + 2]});
      if (v.empty()) return false;
      std::sort(v.begin(), v.end(), [](auto& p, auto& q) { return p[0] < q[0]; });
      v.resize(std::max<size_t>(1, static_cast<size_t>(static_cast<float>(v.size()) * share)));
      for (int c = 0; c < 3; ++c) {
        std::vector<float> s;
        for (auto& p : v) s.push_back(p[static_cast<size_t>(c)]);
        std::nth_element(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(s.size() / 2), s.end());
        lab3[c] = s[s.size() / 2];
      }
      return true;
    };
    // Brows: filled in their own (hair) colour — makeup and brows never vanish.
    for (int e = 0; e < 2; ++e) {
      Img a(w, h, 1, 0.0f);
      filters::fillPolygon(a, filters::smoothCurve(f.brow[e], 2, true), 1);
      float c[3] = {30, 6, 8};
      colourIn(a, 0.5f, c);
      c[0] = std::clamp(c[0] * 0.8f, P.mono ? 12.0f : 10.0f, 38.0f);
      paint(a, c, P.mono ? 0.95f : 0.88f);
    }
    // Eyes: white, iris, pupil; lash line above, a light line below.
    for (int e = 0; e < 2; ++e) {
      const std::vector<Pt> eye = filters::smoothCurve(f.eye[e], 3, true);
      float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
      Pt cA, cB;  // the corners: the two points furthest apart along the face
      float tA = 1e9f, tB = -1e9f;
      for (const Pt& p : f.eye[e]) {
        const float t = p.x * f.axis.x + p.y * f.axis.y;
        if (t < tA) tA = t, cA = p;
        if (t > tB) tB = t, cB = p;
      }
      const float eyeW = std::hypot(cB.x - cA.x, cB.y - cA.y);
      // Height across the axis.
      float up = 0, down = 0;
      for (const Pt& p : f.eye[e]) {
        const float s = (p.x - cA.x) * -f.axis.y + (p.y - cA.y) * f.axis.x;  // + below
        up = std::min(up, s), down = std::max(down, s);
        x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
      }
      const float eyeH = down - up;
      // The two lids: the points above and below the corner-to-corner line.
      std::vector<Pt> upper{cA}, lower{cA};
      for (const Pt& p : f.eye[e]) {
        const float s = (p.x - cA.x) * -f.axis.y + (p.y - cA.y) * f.axis.x;
        const float t = (p.x - cA.x) * f.axis.x + (p.y - cA.y) * f.axis.y;
        if (t <= 0.02f * eyeW || t >= 0.98f * eyeW) continue;
        (s < 0 ? upper : lower).push_back(p);
      }
      upper.push_back(cB), lower.push_back(cB);
      upper = filters::smoothCurve(along(upper, f.axis, false), 3, false);
      lower = filters::smoothCurve(along(lower, f.axis, false), 3, false);
      // Which corner is outer (towards the side of the face)?
      const Pt fc = mean(f.oval);
      const bool aOuter = std::hypot(cA.x - fc.x, cA.y - fc.y) > std::hypot(cB.x - fc.x, cB.y - fc.y);
      const bool open = eyeH > 0.16f * eyeW && eyeW > 6;
      if (open) {
        Img white(w, h, 1, 0.0f);
        filters::fillPolygon(white, eye, 1);
        const float wc[3] = {P.mono ? 98.0f : 90.0f, -0.5f, P.mono ? 0.0f : -3.0f};
        paint(white, wc, 0.9f);
        // The iris: where the photo is darkest inside the eye.
        float sx = 0, sy = 0, sw = 0, med = 0;
        {
          std::vector<float> v;
          for (int y = static_cast<int>(y0); y <= static_cast<int>(y1); ++y)
            for (int x = static_cast<int>(x0); x <= static_cast<int>(x1); ++x)
              if (x >= 0 && y >= 0 && x < w && y < h && white.at(x, y) > 0.6f) v.push_back(L.at(x, y));
          if (!v.empty()) {
            std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
            med = v[v.size() / 2];
          }
          for (int y = static_cast<int>(y0); y <= static_cast<int>(y1); ++y)
            for (int x = static_cast<int>(x0); x <= static_cast<int>(x1); ++x) {
              if (x < 0 || y < 0 || x >= w || y >= h || white.at(x, y) < 0.6f) continue;
              const float d = std::max(0.0f, med - L.at(x, y) + 2);
              sx += d * d * static_cast<float>(x), sy += d * d * static_cast<float>(y), sw += d * d;
            }
        }
        const Pt ec = mean(f.eye[e]);
        Pt ic = sw > 0 ? Pt{sx / sw, sy / sw} : ec;
        ic = {mix(ec.x, ic.x, 0.8f), mix(ec.y, ic.y, 0.4f)};  // irises sit near the eye's middle height
        const float r = std::clamp(0.62f * eyeH, 0.2f * eyeW, 0.3f * eyeW) * (1 + 0.12f * o.eyes);
        f.iris[e] = ic, f.irisR[e] = r;
        Img irisA(w, h, 1, 0.0f), pupilA(w, h, 1, 0.0f);
        const int xa = std::max(0, static_cast<int>(ic.x - r - 2)), xb = std::min(w - 1, static_cast<int>(ic.x + r + 2));
        const int ya = std::max(0, static_cast<int>(ic.y - r - 2)), yb = std::min(h - 1, static_cast<int>(ic.y + r + 2));
        float iris[3] = {28, 8, 14};
        {
          Img dark(w, h, 1, 0.0f);
          for (int y = ya; y <= yb; ++y)
            for (int x = xa; x <= xb; ++x)
              if (std::hypot(static_cast<float>(x) - ic.x, static_cast<float>(y) - ic.y) < r) dark.at(x, y) = white.at(x, y);
          colourIn(dark, 0.5f, iris);
        }
        iris[0] = std::clamp(iris[0], 18.0f, 42.0f);
        const float ichroma = std::max(10.0f, std::hypot(iris[1], iris[2]) * 1.35f), ihue = std::atan2(iris[2], iris[1]);
        for (int y = ya; y <= yb; ++y)
          for (int x = xa; x <= xb; ++x) {
            const float d = std::hypot(static_cast<float>(x) - ic.x, static_cast<float>(y) - ic.y);
            irisA.at(x, y) = clamp01(r + 0.5f - d) * white.at(x, y);
            pupilA.at(x, y) = clamp01(0.44f * r + 0.5f - d) * white.at(x, y);
          }
        // The iris: darker at the top (the lid's shadow), lighter below; a ring in manga.
        for (int y = ya; y <= yb; ++y)
          for (int x = xa; x <= xb; ++x) {
            const float a = irisA.at(x, y);
            if (a <= 0.002f) continue;
            const float t = std::clamp((static_cast<float>(y) - (ic.y - r)) / (2 * r), 0.0f, 1.0f);
            float c3[3] = {P.mono ? 55.0f : std::max(20.0f, iris[0] * mix(0.8f, 1.35f, t) + 4 * t), ichroma * std::cos(ihue), ichroma * std::sin(ihue)};
            float c[3];
            labToRgb(c3, c);
            if (P.mono) c[0] = c[1] = c[2] = c3[0] / 100;
            float* p = out.px(x, y);
            for (int q = 0; q < 3; ++q) p[q] = mix(p[q], c[q], a);
          }
        // The pupil: a deep shade of the iris, never a black hole.
        const float pc[3] = {P.mono ? 14.0f : 16.0f, 0.6f * ichroma * std::cos(ihue), 0.6f * ichroma * std::sin(ihue)};
        paint(pupilA, pc, 0.95f);
        // The iris's rim.
        std::vector<Pt> ring;
        for (int i = 0; i <= 32; ++i) {
          const float an = static_cast<float>(i) / 32 * 2 * kPi;
          ring.push_back({ic.x + r * std::cos(an), ic.y + r * std::sin(an)});
        }
        Img rim(w, h, 1, 0.0f);
        filters::stroke(rim, ring, base * 0.45f, base * 0.45f, 0.8f);
        for (size_t i : region) featInk.d[i] = std::max(featInk.d[i], rim.d[i] * white.d[i]);
      }
      // Lash line: thicker towards the outer corner; the lower lid light and partial.
      filters::stroke(featInk, upper, base * (aOuter ? 1.7f : 0.7f), base * (aOuter ? 0.7f : 1.7f), 1.0f);
      std::vector<Pt> lowerPart;
      for (size_t i = 0; i < lower.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(std::max<size_t>(1, lower.size() - 1));
        if (aOuter ? t < 0.65f : t > 0.35f) lowerPart.push_back(lower[i]);
      }
      filters::stroke(featInk, lowerPart, base * 0.45f, base * 0.45f, open ? 0.55f : 0.0f);
    }
    // Nose: a hint — the nostrils' small curves, never filled.
    for (int s = 0; s < 3; s += 2) {
      const Pt a = f.nose[s], c = f.nose[1];
      const Pt mid = {mix(a.x, c.x, 0.35f), mix(a.y, c.y, 0.35f) - 0.03f * f.eyeDist};
      filters::stroke(featInk, filters::smoothCurve({a, mid, {mix(a.x, c.x, 0.6f), mix(a.y, c.y, 0.6f)}}, 4, false), base * 0.55f, base * 0.3f, 0.7f);
    }
    // Lips: their own colour (two tones), the opening (teeth, not pure white), the mouth line.
    {
      Img lipsA(w, h, 1, 0.0f), innerA(w, h, 1, 0.0f);
      filters::fillPolygon(lipsA, f.lipsOuter, 1);
      filters::fillPolygon(innerA, f.mouthInner, 1);
      for (size_t i : region) lipsA.d[i] *= 1 - innerA.d[i];
      float lc[3] = {45, 25, 15};
      colourIn(lipsA, 0.8f, lc);
      if (skinKnown) {
        // Lips a touch deeper and richer than the skin (never pale pink paint on a face).
        lc[0] = std::clamp(lc[0], skinTone[0].L - 12, skinTone[1].L - 3);
        const float c = std::max(std::hypot(lc[1], lc[2]) * 1.25f, std::hypot(skinTone[1].a, skinTone[1].b) + 9);
        const float hh = std::atan2(lc[2], lc[1]);
        lc[1] = c * std::cos(hh), lc[2] = c * std::sin(hh);
      }
      if (!P.skinWhite) paint(lipsA, lc, 0.85f);
      // The upper lip a shade darker (the light comes from above).
      {
        Img upperA(w, h, 1, 0.0f);
        std::vector<Pt> ul = along({f.lipsOuter.begin(), f.lipsOuter.begin() + static_cast<std::ptrdiff_t>(f.lipsOuter.size() / 2)}, f.axis, false);
        const std::vector<Pt> ml = along(f.mouthLine, f.axis, true);
        ul.insert(ul.end(), ml.begin(), ml.end());
        filters::fillPolygon(upperA, ul, 1);
        for (size_t i : region) upperA.d[i] *= lipsA.d[i];
        float dc[3] = {lc[0] - 7, lc[1] * 1.05f, lc[2] * 1.05f};
        if (!P.skinWhite) paint(upperA, dc, 0.7f);
      }
      // The opening.
      float area = 0;
      for (size_t i : region) area += innerA.d[i];
      const float mouthW = std::hypot(f.mouthLine.back().x - f.mouthLine.front().x, f.mouthLine.back().y - f.mouthLine.front().y);
      const bool openMouth = area > 0.06f * mouthW * mouthW && mouthW > 6;
      if (openMouth) {
        float med[3] = {40, 10, 10};
        colourIn(innerA, 1.0f, med);
        Img teeth(w, h, 1, 0.0f);
        for (size_t i : region) {
          if (innerA.d[i] <= 0.01f) continue;
          const float c = std::hypot(lab.d[i * 3 + 1], lab.d[i * 3 + 2]);
          teeth.d[i] = innerA.d[i] * smoothstep(med[0] - 2, med[0] + 10, L.d[i]) * (1 - smoothstep(18.0f, 30.0f, c));
        }
        teeth = blur(teeth, 0.8f, token);
        const float dark[3] = {24, 18, 10}, tc[3] = {P.mono ? 97.0f : 90.0f, 0, P.mono ? 0.0f : 5.0f};
        paint(innerA, dark, 0.95f);
        paint(teeth, tc, 0.95f);
        filters::stroke(featInk, f.mouthInner, base * 0.7f, base * 0.7f, 0.85f);
      } else {
        filters::stroke(featInk, f.mouthLine, base * 0.5f, base * 0.5f, 0.95f);
        // Taper the line's ends: thinner, fainter at the corners.
      }
      // A light line under the lower lip (its shadow), short.
      if (f.lowerLip.size() >= 5) {
        const size_t c = f.lowerLip.size() / 2;
        std::vector<Pt> u = {f.lowerLip[c - 1], f.lowerLip[c], f.lowerLip[c + 1]};
        for (Pt& p : u) p.y += 0.04f * f.eyeDist;
        filters::stroke(featInk, filters::smoothCurve(u, 4, false), base * 0.4f, base * 0.4f, P.skinWhite ? 0.8f : 0.35f);
      }
    }
  }
  if (cancelled()) return kCancelled;

  // ---- 6. line art
  Img ink(w, h, 1, 0.0f);
  {
    const float sigma = std::max(0.6f, P.lineWidth * px * 0.5f * lineScale);
    auto xdog = [&](const Img& l01, float s, float thr) {
      const Img a = blur(l01, s, token), b = blur(l01, s * 1.6f, token);
      Img e(w, h, 1);
      parallelFor(h, token, [&](int32_t y0, int32_t y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            const float d = b.d[i] - a.d[i];
            // Gated to real edges: the middle of a small dark spot (an eye, a nostril) gets no ink — a ring, not a blob.
            const float gx = a.at(std::min(x + 1, w - 1), y) - a.at(std::max(x - 1, 0), y), gy = a.at(x, std::min(y + 1, h - 1)) - a.at(x, std::max(y - 1, 0));
            const float g = std::hypot(gx, gy) * 0.5f * s;
            e.d[i] = smoothstep(thr, thr + 0.012f, d) * smoothstep(thr * 0.35f, thr * 0.9f, g);
          }
      });
      return e;
    };
    const float thr = mix(0.011f, 0.0055f, o.detail) * (2 - o.lines * 0.8f) * 0.8f;
    const Img person = xdog(smL, sigma, thr);
    each(w, h, token, [&](size_t i) {
      // None on the face where it is drawn (features come from its contours); faint on skin; strands in the hair.
      const float faceKeep = mix(0.15f, 1.0f, featZone.d[i]);
      float wgt = mix(1.0f, faceKeep, faceInner.d[i]);
      wgt *= mix(1.0f, 0.3f, skin.d[i] * (1 - faceInner.d[i]));
      wgt = mix(wgt, P.hairLines, hair.d[i]);
      ink.d[i] = person.d[i] * wgt;
    });
    // Where faces are drawn from contours, nothing generic inside them.
    for (const Face& f : faces) {
      if (!f.drawn) continue;
      Img inner(w, h, 1, 0.0f);
      filters::fillPolygon(inner, scaled(f.oval, 0.93f), 1);
      for (size_t i = 0; i < n; ++i) ink.d[i] *= 1 - inner.d[i];
    }
    // Faces known only by a box: their features from the photo, finely (a ring for every dark spot, no fills).
    bool boxOnly = false;
    for (const Face& f : faces) boxOnly = boxOnly || !f.drawn;
    if (boxOnly) {
      const Img fine = xdog(channel(lab, 0, 0.01f), std::max(0.6f, P.faceLine * px * 0.4f * lineScale), 0.008f);
      for (size_t i = 0; i < n; ++i) ink.d[i] = std::max(ink.d[i], fine.d[i] * featZone.d[i] * faceInner.d[i] * 0.85f);
    }
    if (hasMask) {
      // The scene: few lines, from its own softened light, and only the long ones.
      Img inkB = xdog(smBackL, sigma * 1.2f, thr * 1.6f);
      filters::dropSpecks(inkB, static_cast<int>(std::max(12.0f, P.speck * px * 3 * sigma)));
      for (size_t i = 0; i < n; ++i) ink.d[i] = mix(inkB.d[i] * P.backInk, ink.d[i], m.d[i]);
    }
    filters::dropSpecks(ink, static_cast<int>(std::max(6.0f, P.speck * px * sigma * 0.9f)));
    if (P.lineRound > 0) {
      const Img r = blur(ink, sigma * 0.8f, token);
      for (size_t i = 0; i < n; ++i) ink.d[i] = mix(ink.d[i], smoothstep(0.28f, 0.55f, r.d[i]), P.lineRound);
    }
  }
  // The person's outline: on the refined mask's edge, exactly where the person's colour meets the scene's.
  if (hasMask) {
    const float wO = std::max(1.0f, P.outline * px * lineScale);
    // Distance from the edge, from the mask softened to the outline's width (its 0.5 crossing stays put):
    // a thick outline is as thick as asked, centred where the person's colour meets the scene's.
    const Img ms = blur(m, std::max(1.0f, wO * 0.8f), token);
    parallelFor(h, token, [&](int32_t y0, int32_t y1) {
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < w; ++x) {
          const float gx = 0.5f * (ms.at(std::min(x + 1, w - 1), y) - ms.at(std::max(x - 1, 0), y));
          const float gy = 0.5f * (ms.at(x, std::min(y + 1, h - 1)) - ms.at(x, std::max(y - 1, 0)));
          const float g = std::hypot(gx, gy);
          if (g < 0.004f) continue;
          const float d = (ms.at(x, y) - 0.5f) / g;  // px from the edge, + inside
          const float a = clamp01(wO * 0.5f + 0.5f - std::fabs(d - wO * 0.3f));
          float& v = ink.at(x, y);
          v = std::max(v, a);
        }
    });
  }
  if (cancelled()) return kCancelled;

  // ---- 7. finish
  // Tone: Ben-Day dots (comic) or screentone (manga), only in shadows and the scene — never on lit skin or a face.
  if (P.tone != Tone::None || P.mono) {
    Img allow(w, h, 1);
    each(w, h, token, [&](size_t i) {
      const float onSkin = skin.d[i] * (1 - 0.6f * skinShadowness.d[i] / std::max(skin.d[i], 1e-3f));
      allow.d[i] = (1 - onSkin) * (1 - faceOval.d[i]) * shadow.d[i];
    });
    if (P.mono) {
      // Manga: paper white, solid black, and screentone between; skin is the paper.
      Img tone(w, h, 1, 0.0f);
      for (size_t i = 0; i < n; ++i) {
        float* p = &out.d[i * 3];
        const float g = p[0];
        const float skinW = P.skinWhite ? std::max(skin.d[i], faceInner.d[i] * (1 - featCore.d[i])) : 0.0f;
        float v = g > 0.66f ? 0.98f : g < 0.24f ? 0.05f : 0.97f;
        const float mid = g >= 0.24f && g <= 0.66f ? 1.0f : 0.0f;
        tone.d[i] = mid * smoothstep(0.7f, 0.25f, g) * mix(1.0f, allow.d[i] > 0 ? 1.0f : 0.35f, m.d[i]) * (1 - skinW);
        v = mix(v, 0.98f, skinW);
        for (int c = 0; c < 3; ++c) p[c] = mix(g, v, 0.92f);
      }
      // The kept scene: lighter, all screentone (and speed lines towards the person).
      if (hasMask && o.background == Background::Keep) {
        Pt c{static_cast<float>(w) / 2, static_cast<float>(h) / 2};
        double sx = 0, sy = 0, sw = 0;
        for (int y = 0; y < h; y += 4)
          for (int x = 0; x < w; x += 4) sx += m.at(x, y) * static_cast<float>(x), sy += m.at(x, y) * static_cast<float>(y), sw += m.at(x, y);
        if (sw > 0) c = {static_cast<float>(sx / sw), static_cast<float>(sy / sw)};
        for (int y = 0; y < h; ++y)
          for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            const float q = 1 - m.d[i];
            if (q <= 0.001f) continue;
            const float g = out.d[i * 3];
            tone.d[i] = mix(tone.d[i], 0.55f * smoothstep(0.95f, 0.2f, g), q);
            // Speed lines: thin radial streaks, denser towards the frame's edge.
            const float an = (std::atan2(static_cast<float>(y) - c.y, static_cast<float>(x) - c.x) + kPi) / (2 * kPi) * 540.0f;
            const float rr = std::hypot(static_cast<float>(x) - c.x, static_cast<float>(y) - c.y) / G;
            const int bin = static_cast<int>(std::floor(an));
            const float frac = an - static_cast<float>(bin);
            // Some angular bins carry a line, thinner towards the person.
            const float on = hash01(bin, 0, seed + 21) > 0.78f ? 1.0f : 0.0f;
            const float width = 0.12f + 0.28f * smoothstep(0.3f, 1.2f, rr);
            const float streak = on * smoothstep(0.32f, 0.7f, rr) * (1 - smoothstep(width * 0.5f, width * 0.5f + 0.08f, std::fabs(frac - 0.5f)));
            const float paper = mix(g, 0.98f, 0.6f);
            for (int k2 = 0; k2 < 3; ++k2) out.d[i * 3 + static_cast<size_t>(k2)] = mix(out.d[i * 3 + static_cast<size_t>(k2)], mix(paper, 0.08f, streak * 0.7f), q);
          }
      }
      const Rgb3 ink3{0.06f, 0.06f, 0.07f};
      dots(out, tone, std::max(3.0f, G / 170.0f), 0.785f, 0, &ink3, 0.9f);
    } else if (P.tone == Tone::BenDay) {
      Img amt(w, h, 1);
      for (size_t i = 0; i < n; ++i) amt.d[i] = allow.d[i] * 0.85f;
      dots(out, amt, std::max(3.0f, G / 135.0f), 0.26f, 0.55f, nullptr, P.toneStrength);
    }
  }
  // Bloom from the lights (not from skin: no orange haze past the outline), and the webtoon's soft skin glow.
  if (P.bloom * o.glow > 0.01f && !P.mono) {
    Img bright(w, h, 3, 0.0f);
    each(w, h, token, [&](size_t i) {
      const float b = smoothstep(0.78f, 0.97f, luma(&out.d[i * 3])) * (1 - skin.d[i]);
      for (size_t c = 0; c < 3; ++c) bright.d[i * 3 + c] = out.d[i * 3 + c] * b;
    });
    bright = blur(bright, G / 55.0f, token);
    const float s = 0.8f * P.bloom * o.glow;
    for (size_t i = 0; i < out.d.size(); ++i) out.d[i] = 1 - (1 - out.d[i]) * (1 - s * bright.d[i]);
  }
  if (P.skinGlow > 0 && skinKnown) {
    const Img g = blur(skin, G / 60.0f, token);
    for (size_t i = 0; i < n; ++i) {
      const float a = P.skinGlow * (0.4f + 0.6f * o.glow) * 0.12f * g.d[i] * m.d[i];
      for (size_t c = 0; c < 3; ++c) out.d[i * 3 + c] = 1 - (1 - out.d[i * 3 + c]) * (1 - a);
    }
  }
  // The ink.
  {
    const float dark = P.lineDark;
    parallelFor(h, token, [&](int32_t y0, int32_t y1) {
      for (size_t i = static_cast<size_t>(y0) * static_cast<size_t>(w); i < static_cast<size_t>(y1) * static_cast<size_t>(w); ++i) {
        const float a = clamp01(std::max(ink.d[i], featInk.d[i]) * dark);
        if (a <= 0.003f) continue;
        float* p = &out.d[i * 3];
        float c[3];
        if (P.ink == Ink::Coloured) {
          // A darker shade of the colour beneath.
          float lab3[3];
          rgbToLab(p, lab3);
          lab3[0] = lab3[0] * 0.42f;
          lab3[1] *= 1.15f, lab3[2] *= 1.15f;
          labToRgb(lab3, c);
        } else {
          for (int q = 0; q < 3; ++q) c[q] = P.inkColour[static_cast<size_t>(q)];
        }
        for (int q = 0; q < 3; ++q) p[q] = mix(p[q], c[q], a);
      }
    });
  }
  // Catchlights: a soft highlight (and a smaller second) in each iris.
  const float sparkle = P.catchlight * o.eyes;
  if (sparkle > 0.12f)
    for (const Face& f : faces)
      for (int e = 0; e < 2; ++e) {
        Pt c;
        float r;
        if (f.drawn) {
          if (f.irisR[e] <= 0) continue;
          c = f.iris[e], r = f.irisR[e];
        } else {
          if (!(f.box.eyes[0] > 0 && f.box.eyes[2] > 0)) continue;
          c = {f.box.eyes[static_cast<size_t>(e * 2)] * static_cast<float>(w), f.box.eyes[static_cast<size_t>(e * 2 + 1)] * static_cast<float>(h)};
          r = 0.11f * std::hypot((f.box.eyes[2] - f.box.eyes[0]) * static_cast<float>(w), (f.box.eyes[3] - f.box.eyes[1]) * static_cast<float>(h));
        }
        const struct {
          float ox, oy, rr, a;
        } spots[2] = {{-0.38f, -0.38f, 0.3f, 0.95f}, {0.32f, 0.3f, 0.14f, 0.8f}};
        for (const auto& s : spots) {
          const float sx = c.x + s.ox * r, sy = c.y + s.oy * r, rad = std::max(0.9f, s.rr * r * (0.7f + 0.4f * o.eyes));
          for (int y = static_cast<int>(sy - rad - 2); y <= static_cast<int>(sy + rad + 2); ++y)
            for (int x = static_cast<int>(sx - rad - 2); x <= static_cast<int>(sx + rad + 2); ++x) {
              if (x < 0 || y < 0 || x >= w || y >= h) continue;
              const float a = s.a * clamp01(rad + 0.5f - std::hypot(static_cast<float>(x) - sx, static_cast<float>(y) - sy)) * std::min(1.0f, sparkle * 1.5f);
              float* p = out.px(x, y);
              for (int q = 0; q < 3; ++q) p[q] = mix(p[q], 1.0f, a);
            }
        }
      }
  // Retro: VHS warmth (lifted blacks, warm cast, faint scanlines), a slight colour fringe, film grain, a vignette.
  if (P.vhs > 0) {
    grade(out, {.lift = 0.05f * P.vhs, .gain = 0.97f, .saturation = 0.96f, .warmth = 0.14f * P.vhs}, token);
    const float period = std::max(2.0f, G / 330);
    for (int y = 0; y < h; ++y) {
      const float s = 1 - 0.035f * P.vhs * (0.5f + 0.5f * std::cos(static_cast<float>(y) / period * 2 * kPi));
      for (int x = 0; x < w; ++x)
        for (int q = 0; q < 3; ++q) out.px(x, y)[q] *= s;
    }
  }
  if (P.aberration > 0) {
    const float s = P.aberration * px;
    const Img src = out;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        out.px(x, y)[0] = src.sample(static_cast<float>(x) - s, static_cast<float>(y), 0);
        out.px(x, y)[2] = src.sample(static_cast<float>(x) + s, static_cast<float>(y), 2);
      }
  }
  if (P.grain > 0) grain(out, P.grain, seed + 13);
  if (P.vhs > 0) vignette(out, 0.2f, hex(0x3A2A20));

  // ---- Strength: blended with the photo.
  const float st = o.strength;
  for (int y = 0; y < h; ++y) {
    uint8_t* row = image.row(y);
    for (int x = 0; x < w; ++x) {
      const float* p = out.px(x, y);
      for (int q = 0; q < 3; ++q) {
        const int ch = q == 1 ? 1 : (q == 0 ? (bgra ? 2 : 0) : (bgra ? 0 : 2));
        const float photo = static_cast<float>(row[x * 4 + ch]) / 255.0f;
        row[x * 4 + ch] = static_cast<uint8_t>(std::lround(clamp01(mix(photo, p[q], st)) * 255.0f));
      }
    }
  }
  return {PP_OK, {}};
}

}  // namespace photoplus::toon
