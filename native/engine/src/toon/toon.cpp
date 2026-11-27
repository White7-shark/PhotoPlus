// Toon (toon.hpp): a photo turned into anime or a cartoon.
//
//  1. Eyes: an optional local magnification around each eye (anime eyes).
//  2. Shapes: an edge-preserving smoothing in Lab (separable bilateral,
//     iterated), so areas become flat colour while edges stay sharp.
//  3. Colour: the colour of each area flattened; the light quantised into a
//     few cel bands with crisp, slightly soft steps (two bands on skin: a
//     base and a shadow); highlights lifted; colour made vivid or muted.
//  4. Ink: extended difference-of-Gaussians line art on the smoothed light,
//     cleaned on the skin (only the features are drawn there), plus a bold
//     outline around the person; black, warm brown, or (webtoon) a darker
//     shade of the colour beneath.
//  5. Background: kept and simplified further, softly out of focus, or a
//     painted anime scene: a blue sky with cel-shaded clouds, a sunset, a
//     starry night with a moon, a pastel backdrop.
//  6. Finish: catchlights in the eyes, a glow around the lights, and each
//     style's own (manga screentone, comic halftone dots, retro grain).
#include "photoplus/toon.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "../art/core.hpp"
#include "photoplus/parallel.hpp"

namespace photoplus::toon {

using namespace art::core;

namespace {

struct Look {
  float lineDark;     // ink darkness
  float lineWidth;    // ink width multiplier
  int shades;         // default bands
  float chroma;       // colour gain
  float lift;         // the light lifted (anime is bright)
  float flatten;      // how flat each colour area is (chroma smoothing)
  float softStep;     // width of the band steps (L units)
  bool mono;          // manga
  bool dots;          // comic halftone dots in the shadows
  bool colouredInk;   // webtoon: ink a darker shade of the colour beneath
  Rgb3 ink;
};

constexpr Look kLooks[kStyleCount] = {
    {0.92f, 1.0f, 3, 1.25f, 0.10f, 0.7f, 1.6f, false, false, false, {0.16f, 0.10f, 0.09f}},   // anime
    {1.00f, 1.25f, 3, 0.0f, 0.06f, 0.5f, 0.8f, true, false, false, {0.03f, 0.03f, 0.03f}},     // manga
    {1.00f, 1.6f, 4, 1.45f, 0.02f, 0.8f, 0.6f, false, true, false, {0.02f, 0.02f, 0.03f}},     // comic
    {1.00f, 1.9f, 2, 1.5f, 0.10f, 1.0f, 0.8f, false, false, false, {0.05f, 0.04f, 0.05f}},     // cartoon
    {0.75f, 0.75f, 4, 1.1f, 0.14f, 0.6f, 4.0f, false, false, true, {0.0f, 0.0f, 0.0f}},       // webtoon
    {0.85f, 1.1f, 2, 0.78f, 0.04f, 0.8f, 1.2f, false, false, false, {0.30f, 0.17f, 0.11f}},   // retro
};

Img toLab(const Img& rgb) {
  Img lab(rgb.w, rgb.h, 3);
  parallelFor(rgb.h, nullptr, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < rgb.w; ++x) rgbToLab(rgb.px(x, y), lab.px(x, y));
  });
  return lab;
}

// One pass of a bilateral filter along one axis, in Lab (edges stay sharp, areas go flat).
Img bilateral1D(const Img& lab, int r, float sigS, float sigR, bool horizontal, const CancellationToken* token) {
  Img out(lab.w, lab.h, 3);
  std::vector<float> ws(static_cast<size_t>(2 * r + 1));
  for (int o = -r; o <= r; ++o) ws[static_cast<size_t>(o + r)] = std::exp(-static_cast<float>(o * o) / (2 * sigS * sigS));
  // Range weights by squared Lab distance, tabulated.
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

Img grow(const Img& m, int r) {
  if (r <= 0) return m;
  Img a(m.w, m.h, 1, 0.0f), b(m.w, m.h, 1, 0.0f);
  for (int y = 0; y < m.h; ++y)
    for (int x = 0; x < m.w; ++x) {
      float v = 0;
      for (int o = -r; o <= r; ++o) v = std::max(v, m.at(std::clamp(x + o, 0, m.w - 1), y));
      a.at(x, y) = v;
    }
  for (int y = 0; y < m.h; ++y)
    for (int x = 0; x < m.w; ++x) {
      float v = 0;
      for (int o = -r; o <= r; ++o) v = std::max(v, a.at(x, std::clamp(y + o, 0, m.h - 1)));
      b.at(x, y) = v;
    }
  return b;
}

// Anime eyes: each eye magnified in place (a smooth bulge that fades to nothing at its edge).
void bigEyes(Img& rgb, const std::vector<art::FaceBox>& faces, float amount) {
  if (amount <= 0.01f) return;
  const Img src = rgb;
  const float W = static_cast<float>(rgb.w), H = static_cast<float>(rgb.h);
  for (const art::FaceBox& f : faces) {
    if (!(f.eyes[0] > 0 && f.eyes[2] > 0)) continue;
    const float ex[2] = {f.eyes[0] * W, f.eyes[2] * W}, ey[2] = {f.eyes[1] * H, f.eyes[3] * H};
    const float dist = std::hypot(ex[1] - ex[0], ey[1] - ey[0]);
    const float R = dist * 0.42f;
    const float s = 1 + 0.32f * amount;  // magnification at the eye's centre
    for (int e = 0; e < 2; ++e) {
      const int x0 = std::max(0, static_cast<int>(ex[e] - R)), x1 = std::min(rgb.w - 1, static_cast<int>(ex[e] + R));
      const int y0 = std::max(0, static_cast<int>(ey[e] - R)), y1 = std::min(rgb.h - 1, static_cast<int>(ey[e] + R));
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          const float dx = static_cast<float>(x) - ex[e], dy = static_cast<float>(y) - ey[e];
          const float d = std::sqrt(dx * dx + dy * dy) / R;
          if (d >= 1) continue;
          const float k = 1 - (1 - 1 / s) * (1 - d * d) * (1 - d * d);  // < 1 inside: read from nearer the centre
          src.sample3(ex[e] + dx * k, ey[e] + dy * k, rgb.px(x, y));
        }
    }
  }
}

// A soft white catchlight (and a smaller second one) in each eye.
void catchlights(Img& rgb, const std::vector<art::FaceBox>& faces, float amount) {
  if (amount <= 0.15f) return;
  const float W = static_cast<float>(rgb.w), H = static_cast<float>(rgb.h);
  for (const art::FaceBox& f : faces) {
    if (!(f.eyes[0] > 0 && f.eyes[2] > 0)) continue;
    const float dist = std::hypot((f.eyes[2] - f.eyes[0]) * W, (f.eyes[3] - f.eyes[1]) * H);
    for (int e = 0; e < 2; ++e) {
      const float cx = f.eyes[static_cast<size_t>(e * 2)] * W, cy = f.eyes[static_cast<size_t>(e * 2 + 1)] * H;
      const struct {
        float ox, oy, r, a;
      } dotsArr[2] = {{-0.045f, -0.04f, 0.032f, 0.95f}, {0.035f, 0.03f, 0.016f, 0.8f}};
      for (const auto& d : dotsArr) {
        const float px = cx + d.ox * dist, py = cy + d.oy * dist, r = std::max(1.2f, d.r * dist * (0.6f + 0.5f * amount));
        for (int y = static_cast<int>(py - r - 2); y <= static_cast<int>(py + r + 2); ++y)
          for (int x = static_cast<int>(px - r - 2); x <= static_cast<int>(px + r + 2); ++x) {
            if (x < 0 || y < 0 || x >= rgb.w || y >= rgb.h) continue;
            const float dd = std::hypot(static_cast<float>(x) - px, (static_cast<float>(y) - py) * 1.15f);
            const float a = d.a * (1 - smoothstep(r - 1.0f, r + 0.8f, dd)) * std::min(1.0f, amount * 1.6f);
            float* p = rgb.px(x, y);
            for (int k = 0; k < 3; ++k) p[k] = mix(p[k], 1.0f, a);
          }
      }
    }
  }
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
          const float t = fy;
          for (int k = 0; k < 3; ++k)
            c[k] = t < 0.55f ? mix(a[static_cast<size_t>(k)], b[static_cast<size_t>(k)], t / 0.55f)
                             : mix(b[static_cast<size_t>(k)], cc[static_cast<size_t>(k)], (t - 0.55f) / 0.45f);
        } else {  // night: navy with a lighter horizon
          const Rgb3 top{0.03f, 0.05f, 0.16f}, low{0.13f, 0.17f, 0.38f};
          for (int k = 0; k < 3; ++k) c[k] = mix(top[static_cast<size_t>(k)], low[static_cast<size_t>(k)], fy);
        }
        if (kind <= 1) {
          // Cumulus clouds, cel-shaded: a lit top and a shaded underside, crisp edges.
          // Big puffy cumulus: low-frequency shapes, flattened at their base, banked towards the horizon.
          const float sx = static_cast<float>(x) / (G / 1.7f), sy = static_cast<float>(y) / (G / 2.6f);
          const float n = 0.62f * fbm(sx, sy, seed + 3, 3) + 0.38f * fbm(sx * 3.3f, sy * 3.3f, seed + 5, 3);
          const float band = smoothstep(0.2f, 0.45f, fy) * (1 - smoothstep(0.8f, 1.0f, fy));
          const float density = n * (0.7f + 0.45f * band);
          const float aa = 1.2f / G;  // about a pixel: crisp, cel-painted edges
          const float cl = smoothstep(0.545f - aa * 4, 0.545f + aa * 4, density);  // clouds in banks, open sky between
          // The shaded underside: where the cloud is thinner just above (light comes from above).
          const float above = 0.62f * fbm(sx, sy - 0.09f, seed + 3, 3) + 0.38f * fbm(sx * 3.3f, (sy - 0.09f) * 3.3f, seed + 5, 3);
          const float under = smoothstep(0.012f - aa * 6, 0.012f + aa * 6, density - above * (0.7f + 0.45f * band));
          const Rgb3 lit = kind == 0 ? Rgb3{1.0f, 1.0f, 1.0f} : Rgb3{1.0f, 0.86f, 0.70f};
          const Rgb3 shade = kind == 0 ? Rgb3{0.74f, 0.82f, 0.95f} : Rgb3{0.80f, 0.48f, 0.55f};
          for (int k = 0; k < 3; ++k) {
            const float cloud = mix(lit[static_cast<size_t>(k)], shade[static_cast<size_t>(k)], under);
            c[k] = mix(c[k], cloud, cl);
          }
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

void halftone(Img& rgb, const Img& shadow, float period, float angle, const Rgb3& dot, float strength) {
  const float ca = std::cos(angle), sa = std::sin(angle);
  parallelFor(rgb.h, nullptr, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < rgb.w; ++x) {
        const float s = shadow.at(x, y);
        if (s <= 0.02f) continue;
        const float u = (static_cast<float>(x) * ca + static_cast<float>(y) * sa) / period;
        const float v = (-static_cast<float>(x) * sa + static_cast<float>(y) * ca) / period;
        const float du = u - std::floor(u) - 0.5f, dv = v - std::floor(v) - 0.5f;
        const float d = std::sqrt(du * du + dv * dv);
        const float r = 0.5f * std::sqrt(std::min(1.0f, s));  // area grows with the shadow
        const float cover = 1 - smoothstep(r - 0.06f, r + 0.06f, d);
        float* p = rgb.px(x, y);
        for (int k = 0; k < 3; ++k) p[k] = mix(p[k], dot[static_cast<size_t>(k)], cover * strength);
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
      !ok(o.skin, 0, 1) || !ok(o.glow, 0, 1))
    return {PP_ERROR_INVALID_ARGUMENT, "A toon control is out of range"};
  if (o.subject != nullptr && (o.subject->empty() || o.subject->format() != PixelFormat::Gray8))
    return {PP_ERROR_INVALID_ARGUMENT, "The person mask must be a grey image"};

  const Look& look = kLooks[si];
  const int w = image.width(), h = image.height();
  const float G = static_cast<float>(std::min(w, h));
  const uint32_t seed = o.seed == 0 ? 1U : o.seed;
  const bool bgra = image.format() == PixelFormat::BGRA8;

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
  // The person, and their faces.
  Img subject;
  if (o.subject != nullptr) {
    const ImageBuffer& m = *o.subject;
    Img mask(m.width(), m.height(), 1);
    for (int y = 0; y < m.height(); ++y)
      for (int x = 0; x < m.width(); ++x) mask.at(x, y) = static_cast<float>(m.row(y)[x]) / 255.0f;
    subject = blur(resize(mask, w, h), std::max(1.0f, G / 400.0f), token);
  }
  Img faces(w, h, 1, 0.0f), features(w, h, 1, 0.0f);
  std::vector<art::FaceBox> boxes;
  for (const art::FaceBox& f : o.faces) {
    if (!(f.w > 0 && f.h > 0) || !std::isfinite(f.x) || !std::isfinite(f.y)) continue;
    boxes.push_back(f);
    const float cx = (f.x + f.w * 0.5f) * static_cast<float>(w), cy = (f.y + f.h * 0.5f) * static_cast<float>(h);
    const float rx = f.w * static_cast<float>(w) * 0.55f, ry = f.h * static_cast<float>(h) * 0.62f, fw = f.w * static_cast<float>(w);
    for (int y = std::max(0, static_cast<int>(cy - ry * 1.2f)); y <= std::min(h - 1, static_cast<int>(cy + ry * 1.2f)); ++y)
      for (int x = std::max(0, static_cast<int>(cx - rx * 1.2f)); x <= std::min(w - 1, static_cast<int>(cx + rx * 1.2f)); ++x) {
        const float fx = static_cast<float>(x), fy = static_cast<float>(y);
        const float dx = (fx - cx) / rx, dy = (fy - cy) / ry;
        const float face = 1 - smoothstep(0.8f, 1.0f, std::sqrt(dx * dx + dy * dy));
        faces.at(x, y) = std::max(faces.at(x, y), face);
        float feat = 0;
        if (f.eyes[0] > 0) {
          for (int e = 0; e < 2; ++e) {
            const float ex = f.eyes[static_cast<size_t>(e * 2)] * static_cast<float>(w), ey = f.eyes[static_cast<size_t>(e * 2 + 1)] * static_cast<float>(h);
            const float ddx = (fx - ex) / (fw * 0.2f), ddy = (fy - ey + fw * 0.05f) / (fw * 0.15f);
            feat = std::max(feat, 1 - smoothstep(0.6f, 1.0f, std::sqrt(ddx * ddx + ddy * ddy)));
          }
        }
        if (f.mouth[2] > f.mouth[0]) {
          const float mx = (f.mouth[0] + f.mouth[2]) * 0.5f * static_cast<float>(w), my = (f.mouth[1] + f.mouth[3]) * 0.5f * static_cast<float>(h);
          const float ddx = (fx - mx) / std::max(fw * 0.18f, (f.mouth[2] - f.mouth[0]) * static_cast<float>(w) * 0.65f), ddy = (fy - my) / (fw * 0.09f);
          feat = std::max(feat, 1 - smoothstep(0.6f, 1.0f, std::sqrt(ddx * ddx + ddy * ddy)));
        }
        // The nose's tip and nostrils.
        const float nx = (fx - cx) / (fw * 0.11f), ny = (fy - (cy + 0.1f * f.h * static_cast<float>(h))) / (fw * 0.08f);
        feat = std::max(feat, 0.8f * (1 - smoothstep(0.6f, 1.0f, std::sqrt(nx * nx + ny * ny))));
        features.at(x, y) = std::max(features.at(x, y), feat * face);
      }
  }
  if (!subject.d.empty())
    for (size_t i = 0; i < subject.d.size(); ++i) subject.d[i] = std::max(subject.d[i], faces.d[i]);
  if (isCancelled(token)) return {PP_ERROR_CANCELLED, "Cancelled"};

  // ---- 1. eyes
  bigEyes(rgb, boxes, o.eyes);

  // The photo's own light (after the eyes), for the features' ink.
  const Img photoL = blur(lumaOf(rgb), std::max(0.7f, G / 1100.0f), token);

  // ---- 2. shapes: edge-preserving smoothing (more for simpler styles and less detail)
  Img lab = toLab(rgb);
  const float simplify = (1 - o.detail) * (0.6f + 0.4f * look.flatten);
  const int iters = 1 + static_cast<int>(std::lround(3 * simplify));
  const float sigS = std::max(1.0f, G * mix(1.0f / 380, 1.0f / 140, simplify));
  const int r = std::max(2, static_cast<int>(sigS * 2));
  for (int it = 0; it < iters; ++it) {
    lab = bilateral1D(lab, r, sigS, 9.0f + 6 * simplify, true, token);
    lab = bilateral1D(lab, r, sigS, 9.0f + 6 * simplify, false, token);
    if (isCancelled(token)) return {PP_ERROR_CANCELLED, "Cancelled"};
  }
  // Skin: smoother still (anime skin is one clean tone and its shadow).
  if (o.skin > 0 && !boxes.empty()) {
    Img skinLab = bilateral1D(bilateral1D(lab, r * 2, sigS * 2, 14, true, token), r * 2, sigS * 2, 14, false, token);
    for (size_t i = 0; i < faces.d.size(); ++i) {
      const float k = faces.d[i] * (1 - features.d[i] * 0.85f) * o.skin;
      for (size_t c = 0; c < 3; ++c) lab.d[i * 3 + c] = mix(lab.d[i * 3 + c], skinLab.d[i * 3 + c], k);
    }
  }

  // ---- 3. colour: flat areas, cel bands of light, vivid or muted
  const int shades = std::clamp(o.shades, 2, 6);
  Img L(w, h, 1);
  for (size_t i = 0; i < L.d.size(); ++i) L.d[i] = lab.d[i * 3];
  // Band edges at the light's quantiles (each band used), on the person when known.
  std::vector<float> vals;
  for (size_t i = 0; i < L.d.size(); i += 5)
    if (subject.d.empty() || subject.d[i] > 0.5f) vals.push_back(L.d[i]);
  if (vals.size() < 16)
    for (size_t i = 0; i < L.d.size(); i += 5) vals.push_back(L.d[i]);
  std::sort(vals.begin(), vals.end());
  std::vector<float> edges, levels;
  for (int b = 1; b < shades; ++b) edges.push_back(vals[vals.size() * static_cast<size_t>(b) / static_cast<size_t>(shades)]);
  for (int b = 0; b < shades; ++b) {
    const size_t a0 = vals.size() * static_cast<size_t>(b) / static_cast<size_t>(shades), a1 = vals.size() * static_cast<size_t>(b + 1) / static_cast<size_t>(shades);
    float sum = 0;
    for (size_t i = a0; i < std::max(a0 + 1, a1); ++i) sum += vals[std::min(i, vals.size() - 1)];
    levels.push_back(sum / static_cast<float>(std::max<size_t>(1, a1 - a0)));
  }
  // Skin: two bands, split at the face's own middle light.
  float skinEdge = 50, skinLo = 40, skinHi = 65;
  {
    std::vector<float> sv;
    for (size_t i = 0; i < L.d.size(); i += 3)
      if (faces.d[i] > 0.9f && features.d[i] < 0.2f) sv.push_back(L.d[i]);
    if (sv.size() > 20) {
      std::sort(sv.begin(), sv.end());
      // Base at the face's middle (shine on a forehead doesn't make the skin pale).
      skinEdge = sv[sv.size() * 30 / 100];
      skinLo = sv[sv.size() * 12 / 100];
      skinHi = sv[sv.size() * 52 / 100];
    }
  }
  // Skin everywhere (arms, neck, chest too): the face's own colour, on the person.
  Img skinMap(w, h, 1, 0.0f);
  Img skinLike(w, h, 1, 0.0f);  // skin-coloured at any light (a pale palm, a lit shoulder)
  float skinA = 0, skinB = 0;  // the face's own skin colour (Lab a, b)
  bool skinKnown = false;
  {
    std::vector<float> va, vb;
    for (size_t i = 0; i < L.d.size(); i += 3)
      if (faces.d[i] > 0.9f && features.d[i] < 0.2f) va.push_back(lab.d[i * 3 + 1]), vb.push_back(lab.d[i * 3 + 2]);
    if (va.size() > 20) {
      std::nth_element(va.begin(), va.begin() + static_cast<std::ptrdiff_t>(va.size() / 2), va.end());
      std::nth_element(vb.begin(), vb.begin() + static_cast<std::ptrdiff_t>(vb.size() / 2), vb.end());
      const float ma = va[va.size() / 2], mb = vb[vb.size() / 2];
      skinA = ma, skinB = mb, skinKnown = true;
      // An illustrator's skin colour is never ashy: a skin hue keeps some warmth.
      {
        const float c = std::hypot(ma, mb);
        const float hue = std::atan2(mb, ma) * 57.29578f;
        const float skinHue = smoothstep(0.0f, 15.0f, hue) * (1 - smoothstep(75.0f, 95.0f, hue));
        if (c > 1 && c < 18) {
          const float k = mix(1.0f, 18.0f / c, skinHue);
          skinA *= k, skinB *= k;
        }
      }
      for (size_t i = 0; i < L.d.size(); ++i) {
        const float d = std::hypot(lab.d[i * 3 + 1] - ma, lab.d[i * 3 + 2] - mb);
        const float inL = smoothstep(skinLo - 28, skinLo - 14, L.d[i]) * (1 - smoothstep(skinHi + 14, skinHi + 28, L.d[i]));
        const float onPerson = subject.d.empty() ? 1.0f : subject.d[i];
        // In the face, only what is skin-coloured and skin-light: a beard, hair or
        // shadow under the jaw keeps its own colour (never lifted into grey).
        const float inFaceL = smoothstep(skinLo - 22, skinLo - 10, L.d[i]);
        const float inFace = faces.d[i] * (1 - smoothstep(16.0f, 28.0f, d)) * inFaceL;
        skinMap.d[i] = std::max(inFace, (1 - smoothstep(7.0f, 13.0f, d)) * inL * onPerson);
        skinLike.d[i] = std::max(skinMap.d[i], (1 - smoothstep(9.0f, 16.0f, d)) * onPerson);
      }
      skinMap = blur(skinMap, std::max(1.0f, G / 300.0f), token);
    } else {
      skinMap = faces;
    }
    for (size_t i = 0; i < L.d.size(); ++i) skinMap.d[i] *= 1 - features.d[i];
  }
  // Skin is shaded on its broad forms, not its texture.
  const Img Lbroad = blur(L, std::max(1.5f, G / 70.0f), token);
  // Each area's own colour, flattened (chroma smoothed within edges).
  Img chroma(w, h, 2);
  for (size_t i = 0; i < L.d.size(); ++i) chroma.d[i * 2] = lab.d[i * 3 + 1], chroma.d[i * 2 + 1] = lab.d[i * 3 + 2];
  {
    Img flat(w, h, 3);
    for (size_t i = 0; i < L.d.size(); ++i) flat.d[i * 3] = L.d[i], flat.d[i * 3 + 1] = chroma.d[i * 2], flat.d[i * 3 + 2] = chroma.d[i * 2 + 1];
    const int rr = std::max(2, static_cast<int>(G / 90 * look.flatten));
    flat = bilateral1D(bilateral1D(flat, rr, static_cast<float>(rr) / 2, 16, true, token), rr, static_cast<float>(rr) / 2, 16, false, token);
    for (size_t i = 0; i < L.d.size(); ++i) chroma.d[i * 2] = flat.d[i * 3 + 1], chroma.d[i * 2 + 1] = flat.d[i * 3 + 2];
  }
  const float step = look.softStep;
  const float chromaGain = look.chroma * (1 + 0.45f * o.vivid);
  Img out(w, h, 3);
  Img shadow(w, h, 1, 0.0f);  // for halftones and screentone: how far into the shadow
  parallelFor(h, token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
        const float l = L.d[i];
        // Cel bands.
        float cel = levels[0];
        for (size_t b = 0; b < edges.size(); ++b) cel = mix(cel, levels[b + 1], smoothstep(edges[b] - step, edges[b] + step, l));
        // Skin: base and shadow.
        const float sk = skinMap.d[i] * o.skin;
        if (sk > 0) {
          // Two tones: a clean base and one soft-edged shadow, on the broad forms.
          const float lb = mix(l, Lbroad.d[i], 0.75f);
          const float two = mix(skinLo + 3, skinHi, smoothstep(skinEdge - 3.0f, skinEdge + 3.0f, lb));
          cel = mix(cel, two, sk);
        }
        // A brighter world, highlights lifted.
        // (Skin less: dark skin lifted far turns ashy.)
        float lv = cel + look.lift * (1 - 0.5f * sk) * (100 - cel);
        // Keep a little of the photo's modelling in the features (eyes, mouth).
        lv = mix(lv, l, 0.35f * features.d[i]);
        // Black hair, a beard, a dark coat stay dark (drawn, not greyed by the
        // lifted bands): the deepest values keep close to their own depth.
        const float deep = (1 - smoothstep(10.0f, 26.0f, Lbroad.d[i])) * (1 - sk);
        lv = mix(lv, std::min(lv, 10.0f + 0.8f * l), deep);
        // Skin keeps its own, natural colour (never pushed orange); everything else gets the style's colour.
        const float gain = mix(chromaGain, std::min(chromaGain, 1.0f + 0.1f * o.vivid), std::max(skinMap.d[i], skinLike.d[i]));
        float ca = chroma.d[i * 2], cb = chroma.d[i * 2 + 1];
        if (skinKnown && sk > 0) {
          // Anime skin is one clean colour (a touch richer in its shadow), not
          // the photo's blotches of pink and yellow.
          const float rich = 1.0f + 0.12f * (1 - smoothstep(skinEdge - 3.0f, skinEdge + 3.0f, mix(l, Lbroad.d[i], 0.75f)));
          ca = mix(ca, skinA * rich, 0.7f * sk);
          cb = mix(cb, skinB * rich, 0.7f * sk);
          // Skin made lighter keeps its saturation (brown stays brown, not grey-pink).
          const float sat = mix(1.0f, std::clamp(lv / std::max(15.0f, Lbroad.d[i]), 1.0f, 1.6f), sk);
          ca *= sat, cb *= sat;
        }
        float lab3[3] = {lv, ca * gain, cb * gain};
        float c[3];
        labToRgb(lab3, c);
        if (look.mono) {
          const float g = clamp01(lv / 100.0f);
          c[0] = c[1] = c[2] = g;
        }
        float* p = out.px(x, y);
        for (int k = 0; k < 3; ++k) p[k] = c[k];
        shadow.d[i] = clamp01((levels.back() - lv) / std::max(1.0f, levels.back() - levels[0]));
      }
  });
  if (isCancelled(token)) return {PP_ERROR_CANCELLED, "Cancelled"};

  // ---- 4. ink: line art from the smoothed light, plus the person's outline
  Img l01(w, h, 1);
  for (size_t i = 0; i < L.d.size(); ++i) l01.d[i] = L.d[i] / 100.0f;
  const float sig = std::max(0.6f, G / 900.0f * mix(0.7f, 1.6f, o.lines) * look.lineWidth);
  Img ink = lines(l01, sig, 1.6f, 0.012f + 0.01f * (1 - o.lines), 0.02f, token);
  for (size_t i = 0; i < ink.d.size(); ++i) ink.d[i] *= 1 - 0.85f * std::max(faces.d[i] * (1 - features.d[i]), skinMap.d[i] * o.skin);  // skin is clean
  if (!boxes.empty()) {
    // The features (eyes, brows, nose, mouth) inked from the photo itself, finely: a face keeps its likeness
    // however smooth the skin, light skin as well as dark.
    const float fs = std::max(0.6f, G / 1000.0f * mix(0.8f, 1.4f, o.lines));
    const Img fine = lines(photoL, fs, 1.6f, 0.004f, 0.018f, token);
    for (size_t i = 0; i < ink.d.size(); ++i) ink.d[i] = std::max(ink.d[i], fine.d[i] * features.d[i] * 0.95f);
  }
  if (!subject.d.empty()) {
    // The outline: where the person meets the background.
    const Img soft = blur(subject, std::max(1.0f, G / 500.0f), token);
    for (int y = 1; y < h - 1; ++y)
      for (int x = 1; x < w - 1; ++x) {
        const float gx = soft.at(x + 1, y) - soft.at(x - 1, y), gy = soft.at(x, y + 1) - soft.at(x, y - 1);
        const float e = smoothstep(0.08f, 0.25f, std::hypot(gx, gy) * G / 250.0f * (0.6f + o.lines));
        ink.at(x, y) = std::max(ink.at(x, y), e);
      }
  }
  if (look.lineWidth > 1.3f) ink = grow(ink, static_cast<int>(std::lround(G / 1000.0f * (look.lineWidth - 0.8f) * (0.6f + o.lines))));
  ink = blur(ink, 0.6f, token);
  // Away from the person, fewer lines (a painted background).
  if (!subject.d.empty())
    for (size_t i = 0; i < ink.d.size(); ++i) ink.d[i] *= mix(o.background == Background::Keep ? 0.55f : 0.0f, 1.0f, subject.d[i]);

  // ---- 5. background
  Img back;
  if (!subject.d.empty() && o.background != Background::Keep) {
    back = Img(w, h, 3);
    switch (o.background) {
      case Background::Soft: {
        back = blur(out, G / 40.0f, token);
        break;
      }
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
            for (int k = 0; k < 3; ++k) back.px(x, y)[k] = mix(top[static_cast<size_t>(k)], low[static_cast<size_t>(k)], static_cast<float>(y) / static_cast<float>(h));
      }
    }
    if (look.mono)
      for (size_t i = 0; i < back.d.size() / 3; ++i) {
        const float g = luma(&back.d[i * 3]);
        back.d[i * 3] = back.d[i * 3 + 1] = back.d[i * 3 + 2] = g;
      }
    for (size_t i = 0; i < subject.d.size(); ++i) {
      const float m = subject.d[i];
      for (size_t c = 0; c < 3; ++c) out.d[i * 3 + c] = mix(back.d[i * 3 + c], out.d[i * 3 + c], m);
      shadow.d[i] *= m;
    }
  }

  // ---- 6. finish: style touches, glow, ink, catchlights
  if (look.mono) {
    // Manga: black, white and screentone in the mid-tones.
    Img tone(w, h, 1);
    for (size_t i = 0; i < tone.d.size(); ++i) {
      const float g = out.d[i * 3];
      tone.d[i] = smoothstep(0.85f, 0.55f, g) * (1 - smoothstep(0.35f, 0.18f, g));
    }
    parallelFor(h, nullptr, [&](int32_t y0, int32_t y1) {
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < w; ++x) {
          float* p = out.px(x, y);
          const float g = p[0];
          const float v = g > 0.6f ? 0.98f : g < 0.25f ? 0.06f : 0.97f;  // paper white, solid black; the mid-tones become dots
          for (int k = 0; k < 3; ++k) p[k] = mix(g, v, 0.9f);
        }
    });
    halftone(out, tone, std::max(3.0f, G / 160.0f), 0.785f, {0.05f, 0.05f, 0.05f}, 0.95f);
  }
  if (look.dots) halftone(out, shadow, std::max(3.0f, G / 140.0f), 0.26f, {0.12f, 0.08f, 0.22f}, 0.45f);
  if (o.glow > 0.01f && !look.mono) {
    Img bright(w, h, 3, 0.0f);
    for (size_t i = 0; i < bright.d.size() / 3; ++i) {
      const float b = smoothstep(0.72f, 0.95f, luma(&out.d[i * 3]));
      for (size_t c = 0; c < 3; ++c) bright.d[i * 3 + c] = out.d[i * 3 + c] * b;
    }
    bright = blur(bright, G / 45.0f, token);
    for (size_t i = 0; i < out.d.size(); ++i) out.d[i] = 1 - (1 - out.d[i]) * (1 - 0.55f * o.glow * bright.d[i]);  // screen
  }
  // The ink.
  parallelFor(h, nullptr, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
        const float a = clamp01(ink.d[i] * look.lineDark);
        if (a <= 0.003f) continue;
        float* p = out.px(x, y);
        for (int k = 0; k < 3; ++k) {
          const float c = look.colouredInk ? p[k] * 0.32f : look.ink[static_cast<size_t>(k)];
          p[k] = mix(p[k], c, a);
        }
      }
  });
  catchlights(out, boxes, o.eyes);
  if (o.style == Style::Retro) {
    // 90s cel anime: warm, a touch faded, film grain and a soft vignette.
    grade(out, {.lift = 0.04f, .gain = 0.97f, .saturation = 0.95f, .warmth = 0.12f}, token);
    grain(out, 0.05f, seed + 13);
    vignette(out, 0.18f, hex(0x3A2A20));
  }

  for (int y = 0; y < h; ++y) {
    uint8_t* row = image.row(y);
    for (int x = 0; x < w; ++x) {
      const float* p = out.px(x, y);
      auto q = [](float v) { return static_cast<uint8_t>(std::lround(clamp01(v) * 255.0f)); };
      row[x * 4 + (bgra ? 2 : 0)] = q(p[0]);
      row[x * 4 + 1] = q(p[1]);
      row[x * 4 + (bgra ? 0 : 2)] = q(p[2]);
    }
  }
  return {PP_OK, {}};
}

}  // namespace photoplus::toon
