// Masters: twenty artists, each painting the photo as they worked (art.hpp).
//
// Every oil painter here starts from a toned ground and builds the picture
// with strokes from broad to fine (core::strokeLayer: each placed where the
// canvas still differs from the subject, following its forms), in that
// artist's palette, light and handling; the drawings and prints are made
// the way those are made (chalk hatching on laid paper, an engraver's
// swelling lines, cut blocks of flat colour with key lines). Then the
// finish of the real object: canvas weave or panel, the paint's relief
// lit from the upper left, varnish, craquelure; paper and its tooth.
#include "photoplus/art.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "core.hpp"
#include "photoplus/parallel.hpp"

namespace photoplus::art {

using namespace core;

namespace {

struct Ctx {
  int w = 0, h = 0;
  float S = 1;  // the mark unit, px (the short side times the brush control): every mark size is relative to it
  float G = 1;  // the short side, px: for places and distances in the picture
  Controls ctl;
  Level level = Level::Painting;
  uint32_t seed = 1;
  const CancellationToken* token = nullptr;
  Img photo;    // the photo, 0..1
  Img subject;  // the person (1 channel), empty if unknown
  Img faces;    // the faces (1 channel, soft), empty if none
  std::vector<FaceBox> faceBoxes;
  bool cancelled() const { return isCancelled(token); }
};

std::vector<Rgb3> pal(std::initializer_list<uint32_t> hexes) {
  std::vector<Rgb3> out;
  for (uint32_t c : hexes) out.push_back(hex(c));
  return out;
}

void mixInto(Img& dst, const Img& src, float amount, const Img* where = nullptr) {
  for (int y = 0; y < dst.h; ++y)
    for (int x = 0; x < dst.w; ++x) {
      const float a = amount * (where != nullptr ? where->at(x, y) : 1.0f);
      if (a <= 0) continue;
      float* d = dst.px(x, y);
      const float* s = src.px(x, y);
      for (int k = 0; k < dst.c; ++k) d[k] = mix(d[k], s[k], a);
    }
}

void mixColour(Img& dst, Rgb3 c, float amount, const Img* where = nullptr) {
  for (int y = 0; y < dst.h; ++y)
    for (int x = 0; x < dst.w; ++x) {
      const float a = amount * (where != nullptr ? where->at(x, y) : 1.0f);
      if (a <= 0) continue;
      float* d = dst.px(x, y);
      for (int k = 0; k < 3; ++k) d[k] = mix(d[k], c[static_cast<size_t>(k)], a);
    }
}

Img inverse(const Img& m) {
  Img o = m;
  for (float& v : o.d) v = 1 - v;
  return o;
}

// The background: where the person isn't (or, unknown, nowhere).
Img background(const Ctx& c) {
  if (c.subject.d.empty() || c.ctl.background <= 0) return Img(c.w, c.h, 1, 0.0f);
  Img bg = inverse(c.subject);
  if (c.ctl.background < 1)
    for (float& v : bg.d) v *= c.ctl.background;
  return bg;
}

// Where the light falls: a pool on the face (or the person, or the upper
// middle), from the upper left — for the chiaroscuro painters.
Img lightPool(const Ctx& c, float spread, float floorLight) {
  float cx = 0.5f * static_cast<float>(c.w), cy = 0.4f * static_cast<float>(c.h), r = 0.42f * c.G;
  if (!c.faceBoxes.empty()) {
    const FaceBox& f = c.faceBoxes.front();
    cx = (f.x + f.w * 0.5f) * static_cast<float>(c.w), cy = (f.y + f.h * 0.5f) * static_cast<float>(c.h);
    r = std::max(f.w * static_cast<float>(c.w), f.h * static_cast<float>(c.h)) * 1.6f;
  } else if (!c.subject.d.empty()) {
    double sx = 0, sy = 0, sw = 0;
    int top = c.h, bottom = 0;
    for (int y = 0; y < c.h; ++y)
      for (int x = 0; x < c.w; ++x) {
        const float m = c.subject.at(x, y);
        if (m < 0.5f) continue;
        sx += x * static_cast<double>(m), sy += y * static_cast<double>(m), sw += m;
        top = std::min(top, y), bottom = std::max(bottom, y);
      }
    if (sw > 0) {
      cx = static_cast<float>(sx / sw);
      cy = static_cast<float>(top) + 0.3f * static_cast<float>(bottom - top);
      r = 0.45f * static_cast<float>(bottom - top);
    }
  }
  cx -= 0.08f * c.G, cy -= 0.08f * c.G;  // the light comes from the upper left
  r *= spread;
  Img out(c.w, c.h, 1);
  for (int y = 0; y < c.h; ++y)
    for (int x = 0; x < c.w; ++x) {
      const float dx = (static_cast<float>(x) - cx) / r, dy = (static_cast<float>(y) - cy) / (r * 1.3f);
      float v = std::exp(-(dx * dx + dy * dy));
      if (!c.subject.d.empty()) v = std::max(v, 0.45f * c.subject.at(x, y) * std::exp(-0.35f * (dx * dx + dy * dy)));
      out.at(x, y) = mix(floorLight, 1.0f, v);
    }
  return out;
}

void multiplyBy(Img& rgb, const Img& m) {
  for (int y = 0; y < rgb.h; ++y)
    for (int x = 0; x < rgb.w; ++x) {
      float* p = rgb.px(x, y);
      for (int k = 0; k < 3; ++k) p[k] = clamp01(p[k] * m.at(x, y));
    }
}

// A mottled painted ground for the dark backgrounds of the old masters.
void darkGround(Img& rgb, const Ctx& c, Rgb3 colour, float amount, const Img& where) {
  for (int y = 0; y < c.h; ++y)
    for (int x = 0; x < c.w; ++x) {
      const float a = amount * where.at(x, y);
      if (a <= 0) continue;
      const float n = 0.75f + 0.5f * fbm(static_cast<float>(x) / (c.S * 0.18f), static_cast<float>(y) / (c.S * 0.18f), c.seed + 31U, 4);
      float* p = rgb.px(x, y);
      for (int k = 0; k < 3; ++k) p[k] = mix(p[k], clamp01(colour[static_cast<size_t>(k)] * n), a);
    }
}

// Skin (by colour, within the person when known, and the faces), soft 0..1.
Img skinMask(const Ctx& c) {
  Img skin(c.w, c.h, 1);
  for (int y = 0; y < c.h; ++y)
    for (int x = 0; x < c.w; ++x) {
      const float* p = c.photo.px(x, y);
      const float Y = luma(p), cb = 0.5f + (-0.1687f * p[0] - 0.3313f * p[1] + 0.5f * p[2]), cr = 0.5f + (0.5f * p[0] - 0.4187f * p[1] - 0.0813f * p[2]);
      float s = smoothstep(0.27f, 0.32f, cb) * (1 - smoothstep(0.48f, 0.53f, cb)) * smoothstep(0.51f, 0.55f, cr) * (1 - smoothstep(0.76f, 0.82f, cr)) *
                smoothstep(0.06f, 0.14f, Y);
      if (!c.subject.d.empty()) s *= c.subject.at(x, y);
      if (!c.faces.d.empty()) s = std::max(s, c.faces.at(x, y) * smoothstep(0.06f, 0.14f, Y));
      skin.at(x, y) = s;
    }
  skin = blur(skin, c.S / 200.0f, c.token);
  for (float& v : skin.d) v = smoothstep(0.25f, 0.6f, v);
  return skin;
}

// Edge-preserving smoothing at a size that keeps it quick, back at full size.
Img flatten(const Ctx& c, const Img& rgb, float radiusFrac, int passes) {
  const float scale = std::min(1.0f, 900.0f / c.G);
  Img small = scale < 1 ? resize(rgb, std::max(16, static_cast<int>(static_cast<float>(c.w) * scale)), std::max(16, static_cast<int>(static_cast<float>(c.h) * scale))) : rgb;
  const int r = std::max(2, static_cast<int>(radiusFrac * c.S * scale + 0.5f));
  for (int i = 0; i < passes && !c.cancelled(); ++i) small = kuwahara(small, r, c.token);
  return scale < 1 ? resize(small, c.w, c.h) : small;
}

// The radii of the layers, broad to fine, as far as the level goes.
std::vector<float> radii(const Ctx& c, float maxFrac, float minFrac) {
  std::vector<float> out;
  for (float r = maxFrac * c.S; r >= minFrac * c.S * 0.99f; r *= 0.5f) out.push_back(std::max(1.0f, r));
  // A study leaves out the finest brush; a masterpiece adds one finer still.
  if (c.level == Level::Study && out.size() > 2) out.pop_back();
  if (c.level == Level::Masterpiece) out.push_back(std::max(1.0f, out.back() * 0.6f));
  return out;
}

// One layer of strokes, with the Texture control on the marks themselves:
// smoother means fewer bristle streaks, no dry broken tails, flatter paint
// (the artist's handling stays: stroke size, direction and colour).
bool strokes(const Ctx& c, Canvas& cv, const Img& ref, const Field& field, Brush b, uint32_t seed, const Img* where) {
  const float t = std::clamp(c.ctl.texture, 0.0f, 1.0f);
  b.bristleAmp *= 0.25f + 0.75f * t;
  b.dryness *= t;
  b.ridge *= t;
  b.thick *= 0.3f + 0.7f * t;
  b.thickLights *= t;
  b.tooth *= t;  // chalk and pastel skip less of the paper's grain
  b.softness = mix(std::max(b.softness, 0.5f), b.softness, t);
  return strokeLayer(cv, ref, field, b, seed, where, c.token);
}

struct Oil {
  Rgb3 ground{0.5f, 0.42f, 0.32f};
  float maxFrac = 1.0f / 30, minFrac = 1.0f / 400;
  Brush brush;
  bool faceDetail = true;
};

Canvas paintOil(const Ctx& c, const Img& ref, const Field& field, const Oil& o) {
  Canvas cv;
  cv.rgb = Img(c.w, c.h, 3);
  for (size_t i = 0; i < cv.rgb.d.size(); i += 3)
    for (size_t k = 0; k < 3; ++k) cv.rgb.d[i + k] = o.ground[k];
  cv.height = Img(c.w, c.h, 1, 0.0f);
  uint32_t layer = 0;
  for (float r : radii(c, o.maxFrac, o.minFrac)) {
    Brush b = o.brush;
    b.radius = r;
    if (!strokes(c, cv, ref, field, b, c.seed * 977U + ++layer, nullptr)) return cv;
  }
  // The face, with the finest brush, so it stays the sitter.
  if (o.faceDetail && !c.faces.d.empty()) {
    Brush b = o.brush;
    const float keep = mix(1.35f, 0.65f, c.ctl.likeness);  // finer brushes for a closer likeness
    b.radius = std::max(1.0f, o.minFrac * c.S * keep * (c.level == Level::Masterpiece ? 0.55f : c.level == Level::Painting ? 0.8f : 1.2f));
    b.threshold *= mix(0.9f, 0.3f, c.ctl.likeness);
    b.maxLen = std::min(b.maxLen, 4.0f);
    strokes(c, cv, ref, field, b, c.seed * 977U + 99U, &c.faces);
  }
  return cv;
}

void finishOil(Img& rgb, Canvas& cv, const Ctx& c, float weave, float weavePx, float relief, float gloss, float varnishAmt, float crack,
               float vignetteAmt, Rgb3 vignetteColour, bool panel = false) {
  weave *= c.ctl.texture, relief *= c.ctl.texture, gloss *= std::min(1.0f, c.ctl.texture);
  varnishAmt = clamp01(varnishAmt * c.ctl.age), crack = std::min(1.0f, crack * c.ctl.age);
  if (panel) woodPanel(cv.height, weave, c.seed + 41U);
  else if (weave > 0) canvasWeave(cv.height, std::max(2.5f, weavePx), weave, c.seed + 43U);
  if (relief > 0) lightRelief(rgb, cv.height, relief, gloss, c.token);
  if (varnishAmt > 0) varnish(rgb, varnishAmt);
  if (crack > 0 && c.level != Level::Study) craquelure(rgb, c.G / 40.0f, crack, c.seed + 47U, c.token);
  if (vignetteAmt > 0) vignette(rgb, vignetteAmt, vignetteColour);
}

Field formField(const Ctx& c, const Img& ref, float gradFrac = 1.0f / 300, float tensorFrac = 1.0f / 90) {
  return orientation(ref, std::max(0.8f, gradFrac * c.S), std::max(2.0f, tensorFrac * c.S), c.token);
}

// ---- the oil painters -------------------------------------------------------

Img leonardo(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.lift = 0.04f, .gain = 0.92f, .gamma = 1.05f, .contrast = 0.15f, .saturation = 0.55f, .warmth = 0.4f,
              .shadowTint = {0.48f, 0.44f, 0.32f}, .highlightTint = {0.56f, 0.52f, 0.42f}, .tintStrength = 0.1f}, c.token);
  palette(ref, pal({0x4A3423, 0x8A5A2B, 0xB8893F, 0x6B6A3A, 0x5E6650, 0xD8B48C, 0xE9D7B0, 0x1E1A14, 0x6F7F86}), 0.55f, 0.85f, c.token);
  const Img bg = background(c);
  // Aerial perspective: the land behind dissolves into a blue-green haze.
  mixInto(ref, blur(ref, c.S / 60.0f, c.token), 0.85f, &bg);
  mixColour(ref, hex(0x7D8C84), 0.35f, &bg);
  // Sfumato: no lines, no borders — light and shade blend like smoke.
  mixInto(ref, blur(ref, c.S / 220.0f, c.token), 0.5f);
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0x8C6E4A);
  o.maxFrac = 1.0f / 40, o.minFrac = 1.0f / 420;
  o.brush = {.maxLen = 6, .curvature = 0.6f, .opacity = 0.75f, .softness = 0.6f, .bristleAmp = 0.04f, .dryness = 0.1f, .taper = 0.3f,
             .thick = 0.08f, .threshold = 0.05f};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  mixInto(rgb, blur(rgb, c.S / 500.0f, c.token), 0.35f);  // glazes melt the strokes together
  vignette(rgb, 0.35f, hex(0x2A1E14));
  finishOil(rgb, cv, c, 0.15f, 0, 0.15f, 0.02f, 0.8f, 0.55f, 0, {}, true);
  return rgb;
}

Img raphael(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.lift = 0.02f, .contrast = 0.1f, .saturation = 1.05f, .warmth = 0.15f}, c.token);
  palette(ref, pal({0x2B4C8C, 0xC0392B, 0xE8C3A0, 0xD79A8A, 0xC99A45, 0xA9C4DD, 0x3E5B3A, 0x4E3626}), 0.4f, 0.9f, c.token);
  ref = flatten(c, ref, 1.0f / 350, 1);  // idealised, serene surfaces
  const Img bg = background(c);
  mixInto(ref, blur(ref, c.S / 120.0f, c.token), 0.6f, &bg);
  mixColour(ref, hex(0xB7C9D6), 0.25f, &bg);
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0xC9B58F);
  o.maxFrac = 1.0f / 45, o.minFrac = 1.0f / 430;
  o.brush = {.maxLen = 6, .curvature = 0.6f, .opacity = 0.8f, .softness = 0.55f, .bristleAmp = 0.035f, .dryness = 0.1f, .thick = 0.06f,
             .threshold = 0.05f};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  mixInto(rgb, blur(rgb, c.S / 700.0f, c.token), 0.25f);
  finishOil(rgb, cv, c, 0.12f, 0, 0.12f, 0.02f, 0.45f, 0.3f, 0.18f, hex(0x3A2E22), true);
  return rgb;
}

Img titian(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.lift = 0.03f, .gain = 0.97f, .contrast = 0.25f, .saturation = 1.15f, .warmth = 0.5f, .shadowTint = {0.55f, 0.42f, 0.38f},
              .tintStrength = 0.08f}, c.token);
  palette(ref, pal({0x8E1F1A, 0xA8322B, 0xC9963B, 0xE2B48D, 0x4B2E1E, 0x2C3E5C, 0x5F5B33, 0x1C130E}), 0.45f, 0.85f, c.token);
  const Img bg = background(c);
  multiplyBy(ref, [&] {
    Img m = bg;
    for (float& v : m.d) v = 1 - 0.25f * v;
    return m;
  }());
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0x7A4A2A);
  o.maxFrac = 1.0f / 30, o.minFrac = 1.0f / 360;
  o.brush = {.maxLen = 8, .curvature = 0.5f, .opacity = 0.82f, .softness = 0.45f, .bristleAmp = 0.09f, .dryness = 0.35f, .thick = 0.25f,
             .threshold = 0.06f};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  // A warm transparent glaze over the shadows.
  for (size_t i = 0; i < rgb.d.size(); i += 3) {
    const float a = 0.4f * (1 - smoothstep(0.1f, 0.6f, luma(&rgb.d[i])));
    rgb.d[i + 1] *= 1 - 0.07f * a, rgb.d[i + 2] *= 1 - 0.18f * a;
  }
  finishOil(rgb, cv, c, 0.18f, c.S / 220.0f, 0.35f, 0.04f, 0.55f, 0.3f, 0.3f, hex(0x1A100A));
  return rgb;
}

Img caravaggio(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.gamma = 1.15f, .contrast = 0.55f, .saturation = 0.95f, .warmth = 0.3f}, c.token);
  palette(ref, pal({0x0E0A07, 0x3B2618, 0x8C2A1C, 0xB8893F, 0xE8C29A, 0xEDE3CC, 0x4F4A2C}), 0.4f, 0.9f, c.token);
  // Tenebrism: one hard raking light; the rest falls into black.
  const Img light = lightPool(c, 1.0f, 0.06f);
  multiplyBy(ref, light);
  Img dark = background(c);
  if (c.subject.d.empty())
    for (size_t i = 0; i < dark.d.size(); ++i) dark.d[i] = smoothstep(0.6f, 0.2f, light.d[i]);
  darkGround(ref, c, hex(0x120D09), 0.92f, dark);
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0x2A1C12);
  o.maxFrac = 1.0f / 35, o.minFrac = 1.0f / 420;
  o.brush = {.maxLen = 6, .curvature = 0.6f, .opacity = 0.85f, .softness = 0.4f, .bristleAmp = 0.05f, .dryness = 0.2f, .thick = 0.15f,
             .threshold = 0.05f};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  finishOil(rgb, cv, c, 0.12f, c.S / 240.0f, 0.25f, 0.05f, 0.6f, 0.35f, 0.4f, hex(0x050302));
  return rgb;
}

Img rembrandt(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.gain = 0.95f, .contrast = 0.35f, .saturation = 0.8f, .warmth = 0.8f, .shadowTint = {0.45f, 0.38f, 0.30f},
              .highlightTint = {0.62f, 0.55f, 0.38f}, .tintStrength = 0.1f}, c.token);
  palette(ref, pal({0x15100B, 0x3E2A1A, 0x7A3E1E, 0xC08A3E, 0xEEDCB5, 0xD9A878, 0x5E5530}), 0.55f, 0.85f, c.token);
  // Golden light gathered on the face; everything else sinks into warm brown.
  const Img light = lightPool(c, 1.2f, 0.12f);
  multiplyBy(ref, light);
  Img dark = background(c);
  if (c.subject.d.empty())
    for (size_t i = 0; i < dark.d.size(); ++i) dark.d[i] = smoothstep(0.55f, 0.15f, light.d[i]);
  darkGround(ref, c, hex(0x2B1D12), 0.85f, dark);
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0x4A3524);
  o.maxFrac = 1.0f / 28, o.minFrac = 1.0f / 380;
  o.brush = {.maxLen = 7, .curvature = 0.45f, .opacity = 0.85f, .softness = 0.35f, .bristleAmp = 0.14f, .dryness = 0.45f, .thick = 0.25f,
             .thickLights = 1.0f, .ridge = 0.12f, .threshold = 0.06f};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  // Shadows glazed smooth; the lights stay thick and broken (impasto).
  const Img soft = blur(rgb, c.S / 300.0f, c.token);
  for (size_t i = 0, n = static_cast<size_t>(c.w) * static_cast<size_t>(c.h); i < n; ++i) {
    const float a = 0.5f * (1 - smoothstep(0.08f, 0.45f, luma(&rgb.d[i * 3])));
    for (size_t k = 0; k < 3; ++k) rgb.d[i * 3 + k] = mix(rgb.d[i * 3 + k], soft.d[i * 3 + k], a);
  }
  finishOil(rgb, cv, c, 0.14f, c.S / 200.0f, 0.3f, 0.06f, 0.85f, 0.35f, 0.45f, hex(0x150D07));
  return rgb;
}

Img vermeer(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.lift = 0.03f, .contrast = 0.2f, .shadowTint = {0.40f, 0.45f, 0.55f}, .highlightTint = {0.60f, 0.56f, 0.45f}, .tintStrength = 0.12f},
        c.token);
  palette(ref, pal({0x2A4D9B, 0xE3C35A, 0xF1EBDD, 0xA9A79A, 0x2A2722, 0xE3BFA0, 0x8E2C2C}), 0.45f, 0.88f, c.token);
  // Daylight from a window on the left.
  Img window(c.w, c.h, 1);
  for (int y = 0; y < c.h; ++y)
    for (int x = 0; x < c.w; ++x) window.at(x, y) = mix(1.08f, 0.62f, smoothstep(0.0f, 1.0f, static_cast<float>(x) / static_cast<float>(c.w)));
  multiplyBy(ref, window);
  const Img bg = background(c);
  mixColour(ref, hex(0xB9B4A4), 0.35f, &bg);  // a pale plaster wall
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0x8A7F6A);
  o.maxFrac = 1.0f / 40, o.minFrac = 1.0f / 450;
  o.brush = {.maxLen = 6, .curvature = 0.6f, .opacity = 0.82f, .softness = 0.55f, .bristleAmp = 0.04f, .dryness = 0.1f, .thick = 0.06f,
             .threshold = 0.05f};
  Canvas cv = paintOil(c, ref, f, o);
  // Pointillé: pearls of light on the highlights.
  Img lights(c.w, c.h, 1);
  for (size_t i = 0; i < lights.d.size(); ++i) lights.d[i] = smoothstep(0.72f, 0.85f, luma(&cv.rgb.d[i * 3]));
  const std::vector<Rgb3> pearls = pal({0xFBF6E8, 0xF4E3A6, 0xFFFFFF});
  Brush dots{.radius = std::max(1.0f, c.S / 700.0f), .opacity = 0.7f, .softness = 0.5f, .thick = 0.25f, .threshold = 0.0f, .grid = 3.0f,
             .blur = 0, .mode = StrokeMode::Dot, .dotPalette = &pearls};
  if (c.level != Level::Study) strokes(c, cv, cv.rgb, f, dots, c.seed + 7U, &lights);
  Img rgb = cv.rgb;
  mixInto(rgb, blur(rgb, c.S / 800.0f, c.token), 0.3f);  // the soft focus of a camera obscura
  finishOil(rgb, cv, c, 0.1f, c.S / 240.0f, 0.15f, 0.05f, 0.35f, 0.25f, 0.12f, hex(0x2A2620));
  return rgb;
}

Img velazquez(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.lift = 0.03f, .contrast = 0.25f, .saturation = 0.75f, .warmth = -0.05f, .shadowTint = {0.45f, 0.45f, 0.48f},
              .tintStrength = 0.06f}, c.token);
  palette(ref, pal({0x9A9893, 0xE8E4DA, 0x18171A, 0xC98B83, 0xA23B2C, 0x4A3A2C, 0x6C6A5A}), 0.5f, 0.9f, c.token);
  darkGround(ref, c, hex(0x3B3632), 0.6f, background(c));
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0x6E655A);
  o.maxFrac = 1.0f / 25, o.minFrac = 1.0f / 260;
  o.brush = {.maxLen = 10, .curvature = 0.35f, .opacity = 0.9f, .softness = 0.3f, .bristleAmp = 0.12f, .dryness = 0.55f, .thick = 0.18f,
             .thickLights = 0.4f, .valJitter = 0.06f, .threshold = 0.07f};
  o.brush.hueJitter = 0.01f;
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  finishOil(rgb, cv, c, 0.14f, c.S / 220.0f, 0.35f, 0.05f, 0.4f, 0.25f, 0.3f, hex(0x1E1B18));
  return rgb;
}

Img goya(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.gain = 0.9f, .gamma = 1.2f, .contrast = 0.45f, .saturation = 0.7f, .warmth = 0.25f}, c.token);
  palette(ref, pal({0x121010, 0x4D3424, 0xA57A3A, 0xD8CCB0, 0x7E1E16, 0x6B655C}), 0.6f, 0.85f, c.token);
  darkGround(ref, c, hex(0x1E1814), 0.6f, background(c));
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0x2E2620);
  o.maxFrac = 1.0f / 22, o.minFrac = 1.0f / 220;
  o.brush = {.maxLen = 7, .curvature = 0.3f, .opacity = 0.92f, .softness = 0.25f, .bristleAmp = 0.2f, .dryness = 0.6f, .thick = 0.3f,
             .ridge = 0.5f, .valJitter = 0.1f, .threshold = 0.07f};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  finishOil(rgb, cv, c, 0.2f, c.S / 180.0f, 0.5f, 0.03f, 0.5f, 0.35f, 0.5f, hex(0x0C0907));
  return rgb;
}

Img turner(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.lift = 0.08f, .gain = 1.05f, .saturation = 0.9f, .warmth = 0.5f}, c.token);
  palette(ref, pal({0xE7B83A, 0xF3E7C6, 0x9FB7C7, 0xA3532B, 0x5A4128, 0x8C8A80}), 0.55f, 0.75f, c.token);
  // Light dissolving form: the brightest place becomes a sun, a golden haze over all.
  const Img l = blur(lumaOf(ref), c.S / 20.0f, c.token);
  int bx = c.w / 2, by = c.h / 3;
  float best = -1;
  for (int y = 0; y < c.h; y += 4)
    for (int x = 0; x < c.w; x += 4)
      if (l.at(x, y) > best) best = l.at(x, y), bx = x, by = y;
  const Img bg = background(c);
  Img dissolve = bg;
  for (float& v : dissolve.d) v = 0.1f + 0.55f * v;
  mixInto(ref, blur(ref, c.S / 90.0f, c.token), 1.0f, &dissolve);
  Img haze = bg;
  for (float& v : haze.d) v = 0.08f + 0.35f * v;
  mixColour(ref, hex(0xE6D2A0), 1.0f, &haze);
  for (int y = 0; y < c.h; ++y)
    for (int x = 0; x < c.w; ++x) {
      const float d = std::hypot(static_cast<float>(x - bx), static_cast<float>(y - by)) / (c.G * 0.55f);
      const float g = 0.55f * std::exp(-d * d);
      float* p = ref.px(x, y);
      const Rgb3 sun = hex(0xFFE7A3);
      for (int k = 0; k < 3; ++k) p[k] = 1 - (1 - p[k]) * (1 - g * sun[static_cast<size_t>(k)]);  // screen
    }
  Field f = formField(c, ref, 1.0f / 200, 1.0f / 40);
  swirl(f, c.S / 5.0f, 0.6f, c.seed + 3U);
  Oil o;
  o.ground = hex(0xD9C59A);
  o.maxFrac = 1.0f / 12, o.minFrac = 1.0f / 300;
  o.brush = {.maxLen = 12, .curvature = 0.7f, .opacity = 0.55f, .softness = 0.7f, .bristleAmp = 0.08f, .dryness = 0.6f, .thick = 0.1f,
             .threshold = 0.05f};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  finishOil(rgb, cv, c, 0.1f, c.S / 220.0f, 0.2f, 0.02f, 0.3f, 0.2f, 0.2f, hex(0x6A4E2A));
  return rgb;
}

Img monet(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.lift = 0.08f, .gain = 1.04f, .saturation = 1.25f, .warmth = 0.1f, .shadowTint = {0.50f, 0.42f, 0.62f},
              .highlightTint = {0.58f, 0.55f, 0.42f}, .tintStrength = 0.2f}, c.token);
  palette(ref, pal({0x3D6FB6, 0x6FA8D6, 0x2E8B6E, 0x5DB37E, 0xF2C94C, 0xF4E27A, 0xE05A3C, 0xD9758A, 0x8A6BB5, 0xF5F0E6}), 0.35f, 0.9f, c.token);
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0xE8DCC4);
  o.maxFrac = 1.0f / 40, o.minFrac = 1.0f / 320;
  // Broken colour: short dabs of slightly different pure hues, side by side, never blended.
  o.brush = {.maxLen = 2.2f, .minLen = 0.6f, .step = 0.55f, .curvature = 0.5f, .opacity = 0.88f, .softness = 0.3f, .bristleAmp = 0.1f,
             .dryness = 0.25f, .thick = 0.22f, .hueJitter = 0.035f, .satJitter = 0.25f, .valJitter = 0.07f, .threshold = 0.05f, .grid = 0.9f,
             .mode = StrokeMode::Dab};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  finishOil(rgb, cv, c, 0.2f, c.S / 200.0f, 0.35f, 0.02f, 0.1f, 0.0f, 0, {});
  return rgb;
}

Img renoir(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.lift = 0.06f, .gain = 1.03f, .contrast = 0.05f, .saturation = 1.15f, .warmth = 0.35f, .shadowTint = {0.48f, 0.45f, 0.58f},
              .highlightTint = {0.62f, 0.52f, 0.48f}, .tintStrength = 0.12f}, c.token);
  palette(ref, pal({0xE39A9A, 0xF1C0A0, 0xF6EBDD, 0x5876B8, 0x4F9478, 0xEBC25C, 0xD8583E, 0xB49AC9}), 0.35f, 0.9f, c.token);
  mixInto(ref, blur(ref, c.S / 250.0f, c.token), 0.4f);
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0xEADBC8);
  o.maxFrac = 1.0f / 35, o.minFrac = 1.0f / 330;
  o.brush = {.maxLen = 7, .curvature = 0.75f, .opacity = 0.5f, .softness = 0.75f, .bristleAmp = 0.06f, .dryness = 0.4f, .taper = 0.6f,
             .thick = 0.08f, .hueJitter = 0.02f, .valJitter = 0.04f, .threshold = 0.04f};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  mixInto(rgb, blur(rgb, c.S / 600.0f, c.token), 0.3f);  // the shimmer of feathery strokes
  finishOil(rgb, cv, c, 0.12f, c.S / 220.0f, 0.15f, 0.02f, 0.15f, 0, 0, {});
  return rgb;
}

Img cezanne(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.contrast = 0.2f, .saturation = 1.05f, .warmth = 0.05f}, c.token);
  palette(ref, pal({0x3F7A5C, 0x5A9A6A, 0xC69A4A, 0xD27A3A, 0x2E4C78, 0x5A7FB4, 0x7C8A62, 0xEFEADB, 0x2A2A28}), 0.5f, 0.85f, c.token);
  ref = flatten(c, ref, 1.0f / 250, 1);
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0xE6DDC8);
  o.maxFrac = 1.0f / 70, o.minFrac = 1.0f / 300;
  // The constructive stroke: short parallel diagonal hatches, patch by patch.
  o.brush = {.maxLen = 4.0f, .minLen = 2.5f, .opacity = 0.82f, .softness = 0.45f, .bristleAmp = 0.1f, .dryness = 0.3f, .thick = 0.15f,
             .hueJitter = 0.02f, .valJitter = 0.04f, .threshold = 0.06f, .mode = StrokeMode::Fixed, .angle = -0.75f, .angleJitter = 0.12f};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  const Img edge = lines(blur(lumaOf(c.photo), 1.0f, c.token), std::max(1.0f, c.S / 500.0f), 1.6f, 0.015f, 0.04f, c.token);
  Img e = edge;
  for (float& v : e.d) v *= 0.55f;
  mixColour(rgb, hex(0x2E4C78), 1.0f, &e);  // blue contours
  finishOil(rgb, cv, c, 0.18f, c.S / 200.0f, 0.3f, 0.02f, 0.15f, 0, 0, {});
  return rgb;
}

Img seurat(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.lift = 0.04f, .gain = 1.03f, .contrast = 0.1f, .saturation = 1.3f}, c.token);
  const std::vector<Rgb3> pigments =
      pal({0x2E4FA6, 0x4F7FC9, 0x7FB6E0, 0x2F9E6E, 0xA8C85A, 0xF2D04A, 0xF09A3E, 0xE0503A, 0xE07A9A, 0x8A5FB0, 0xF7F3EA, 0x1F2A55});
  const Field f = formField(c, ref);
  Canvas cv;
  cv.rgb = Img(c.w, c.h, 3);
  const Rgb3 ground = hex(0xEDE6D6);
  for (size_t i = 0; i < cv.rgb.d.size(); i += 3)
    for (size_t k = 0; k < 3; ++k) cv.rgb.d[i + k] = ground[k];
  cv.height = Img(c.w, c.h, 1, 0.0f);
  // Dots of pure pigment, the eye mixes them.
  std::vector<float> rs = {c.S / 120.0f, c.S / 170.0f};
  if (c.level != Level::Study) rs.push_back(c.S / 240.0f);
  uint32_t layer = 0;
  for (float r : rs) {
    Brush b{.radius = std::max(1.0f, r), .opacity = 0.95f, .softness = 0.25f, .bristleAmp = 0.05f, .thick = 0.08f, .threshold = 0.04f,
            .grid = 0.9f, .blur = 0.4f, .mode = StrokeMode::Dot, .dotPalette = &pigments};
    if (!strokes(c, cv, ref, f, b, c.seed * 31U + ++layer, nullptr)) break;
  }
  Img rgb = cv.rgb;
  finishOil(rgb, cv, c, 0.08f, c.S / 240.0f, 0.15f, 0.0f, 0.0f, 0, 0, {});
  return rgb;
}

Img vangogh(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.gain = 1.02f, .contrast = 0.25f, .saturation = 1.45f, .warmth = 0.1f}, c.token);
  palette(ref, pal({0x2A3F8F, 0x3F6FC0, 0x1E3A5F, 0xF2C230, 0xF3E07A, 0xE0862E, 0x2F8F5A, 0x3C7A6A, 0xF4EFE0, 0xC8452E}), 0.5f, 0.85f, c.token);
  Field f = formField(c, ref, 1.0f / 300, 1.0f / 80);
  swirl(f, c.S / 10.0f, 0.85f, c.seed + 5U);  // the skies and walls swirl
  Oil o;
  o.ground = hex(0xC9B48A);
  o.maxFrac = 1.0f / 45, o.minFrac = 1.0f / 260;
  o.brush = {.maxLen = 4, .minLen = 1.5f, .step = 0.5f, .curvature = 0.85f, .opacity = 0.95f, .softness = 0.2f, .bristleAmp = 0.18f,
             .bristleFreq = 9, .dryness = 0.25f, .taper = 0.35f, .thick = 0.55f, .ridge = 0.6f, .hueJitter = 0.025f, .satJitter = 0.15f,
             .valJitter = 0.08f, .threshold = 0.06f};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  Img e = lines(blur(lumaOf(c.photo), 1.0f, c.token), std::max(1.0f, c.S / 500.0f), 1.6f, 0.012f, 0.035f, c.token);
  for (float& v : e.d) v *= 0.6f;
  mixColour(rgb, hex(0x1E3A5F), 1.0f, &e);  // forms outlined in Prussian blue
  finishOil(rgb, cv, c, 0.12f, c.S / 200.0f, 0.75f, 0.06f, 0.12f, 0, 0, {});
  return rgb;
}

// ---- flat colour: Gauguin, Hokusai ---------------------------------------------

// Nearest palette colour for each pixel (in Lab), then a majority filter so
// the fields are clean, as cut from a block or laid flat with a knife.
std::vector<uint8_t> quantise(const Img& rgb, const std::vector<Rgb3>& colours, const CancellationToken* token) {
  std::vector<std::array<float, 3>> labs(colours.size());
  for (size_t i = 0; i < colours.size(); ++i) rgbToLab(colours[i].data(), labs[i].data());
  std::vector<uint8_t> idx(static_cast<size_t>(rgb.w) * static_cast<size_t>(rgb.h));
  parallelFor(rgb.h, token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < rgb.w; ++x) {
        float lab[3];
        rgbToLab(rgb.px(x, y), lab);
        float best = 1e9f;
        uint8_t b = 0;
        for (size_t i = 0; i < labs.size(); ++i) {
          const float dl = lab[0] - labs[i][0], da = lab[1] - labs[i][1], db = lab[2] - labs[i][2];
          const float d = 1.3f * dl * dl + da * da + db * db;
          if (d < best) best = d, b = static_cast<uint8_t>(i);
        }
        idx[static_cast<size_t>(y) * static_cast<size_t>(rgb.w) + static_cast<size_t>(x)] = b;
      }
  });
  for (int pass = 0; pass < 2; ++pass) {
    std::vector<uint8_t> next = idx;
    parallelFor(rgb.h, token, [&](int32_t y0, int32_t y1) {
      std::vector<int> count(colours.size());
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < rgb.w; ++x) {
          std::fill(count.begin(), count.end(), 0);
          for (int j = -2; j <= 2; ++j)
            for (int i = -2; i <= 2; ++i) {
              const int xx = std::clamp(x + i, 0, rgb.w - 1), yy = std::clamp(y + j, 0, rgb.h - 1);
              ++count[idx[static_cast<size_t>(yy) * static_cast<size_t>(rgb.w) + static_cast<size_t>(xx)]];
            }
          next[static_cast<size_t>(y) * static_cast<size_t>(rgb.w) + static_cast<size_t>(x)] =
              static_cast<uint8_t>(std::max_element(count.begin(), count.end()) - count.begin());
        }
    });
    idx.swap(next);
  }
  return idx;
}

// Where neighbouring fields meet (for key lines and contours), 0..1.
Img borders(const std::vector<uint8_t>& idx, int w, int h, float width) {
  Img out(w, h, 1, 0.0f);
  const int r = std::max(1, static_cast<int>(width + 0.5f));
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const uint8_t me = idx[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
      float v = 0;
      for (int j = -r; j <= r && v < 1; ++j)
        for (int i = -r; i <= r; ++i) {
          const int xx = std::clamp(x + i, 0, w - 1), yy = std::clamp(y + j, 0, h - 1);
          if (idx[static_cast<size_t>(yy) * static_cast<size_t>(w) + static_cast<size_t>(xx)] != me) {
            v = std::max(v, 1 - std::hypot(static_cast<float>(i), static_cast<float>(j)) / (width + 1.0f));
          }
        }
      out.at(x, y) = clamp01(v * 1.6f);
    }
  return out;
}

Img gauguin(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.contrast = 0.2f, .saturation = 1.35f, .warmth = 0.35f}, c.token);
  ref = flatten(c, ref, 1.0f / 180, 2);
  const std::vector<Rgb3> colours =
      pal({0xE0892F, 0xD9A441, 0xD2557A, 0x7B4E9A, 0x23426E, 0x2E7A5E, 0x4FA36A, 0x9E2A22, 0x3A2418, 0xE6B79A, 0xF0E2C6, 0xB5502E});
  palette(ref, colours, 0.7f, 0.6f, c.token);
  const auto idx = quantise(ref, colours, c.token);
  Img flat(c.w, c.h, 3);
  for (size_t i = 0; i < idx.size(); ++i)
    for (size_t k = 0; k < 3; ++k) flat.d[i * 3 + k] = mix(ref.d[i * 3 + k], colours[idx[i]][k], 0.75f);
  // Painted, not printed: a coarse dry brush over the flat fields.
  Canvas cv;
  cv.rgb = flat;
  cv.height = Img(c.w, c.h, 1, 0.0f);
  cv.empty = false;
  const Field f = formField(c, flat);
  Brush b{.radius = c.S / 110.0f, .maxLen = 6, .curvature = 0.5f, .opacity = 0.35f, .softness = 0.4f, .bristleAmp = 0.15f, .dryness = 0.7f,
          .thick = 0.1f, .threshold = 0.0f, .blur = 0};
  strokes(c, cv, flat, f, b, c.seed + 11U, nullptr);
  Img rgb = cv.rgb;
  Img edge = borders(idx, c.w, c.h, std::max(1.0f, c.S / 450.0f));
  const Img l = lines(blur(lumaOf(c.photo), 1.0f, c.token), std::max(1.0f, c.S / 450.0f), 1.6f, 0.012f, 0.03f, c.token);
  for (size_t i = 0; i < edge.d.size(); ++i) edge.d[i] = 0.85f * std::max(edge.d[i] * 0.8f, l.d[i]);
  mixColour(rgb, hex(0x1F2A5A), 1.0f, &edge);  // bold dark-blue contours (cloisonnism)
  canvasWeave(cv.height, std::max(3.0f, c.S / 130.0f), 0.35f * c.ctl.texture, c.seed + 13U);  // coarse burlap
  lightRelief(rgb, cv.height, 0.4f * c.ctl.texture, 0.0f, c.token);
  grain(rgb, 0.03f * c.ctl.texture, c.seed + 17U);
  return rgb;
}

Img hokusai(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.contrast = 0.15f, .saturation = 0.9f}, c.token);
  ref = flatten(c, ref, 1.0f / 160, 2);
  const std::vector<Rgb3> inks =
      pal({0x1B2A4A, 0x2F5A8A, 0x8EB2C8, 0xEDE4CF, 0xE8C9A8, 0xD49A8C, 0xC9AE7E, 0x7C8B5A, 0x7A7A72, 0x2A2622});
  std::vector<Rgb3> blocks = inks;
  // The face and skin: one flat block in the sitter's own tone (as a printer would cut it), the features left to the key block.
  const Img skin = skinMask(c);
  double sk[3] = {0, 0, 0}, sw = 0;
  for (size_t i = 0; i < skin.d.size(); ++i)
    if (skin.d[i] > 0.6f) {
      for (size_t k = 0; k < 3; ++k) sk[k] += ref.d[i * 3 + k];
      sw += 1;
    }
  auto idx = quantise(ref, inks, c.token);
  if (sw > 50) {
    blocks.push_back({static_cast<float>(sk[0] / sw), static_cast<float>(sk[1] / sw), static_cast<float>(sk[2] / sw)});
    const auto skinBlock = static_cast<uint8_t>(blocks.size() - 1);
    for (size_t i = 0; i < idx.size(); ++i)
      if (skin.d[i] > 0.5f && luma(&c.photo.d[i * 3]) > 0.08f) idx[i] = skinBlock;
  }
  Img paperTooth;
  Img rgb = paper(c.w, c.h, hex(0xF1E8D6), 0.08f * c.ctl.texture, false, c.seed + 19U, &paperTooth);
  const Img bg = background(c);
  // Colour blocks, printed a hair off the key block, with the wood's grain in the flat areas.
  const int off = std::max(1, static_cast<int>(c.S / 900.0f));
  for (int y = 0; y < c.h; ++y)
    for (int x = 0; x < c.w; ++x) {
      const int sx = std::clamp(x - off, 0, c.w - 1), sy = std::clamp(y - off, 0, c.h - 1);
      const uint8_t k = idx[static_cast<size_t>(sy) * static_cast<size_t>(c.w) + static_cast<size_t>(sx)];
      if (k == 3) continue;  // the paper itself
      Rgb3 ink = blocks[k];
      // Bokashi: the sky's blue graded from the top.
      if ((k == 1 || k == 2) && bg.at(x, y) > 0.5f) {
        const float g = smoothstep(0.0f, 0.55f, static_cast<float>(y) / static_cast<float>(c.h));
        for (size_t ch = 0; ch < 3; ++ch) ink[ch] = mix(inks[1][ch], inks[2][ch], g);
      }
      const float wood = 1 + 0.06f * (valueNoise(static_cast<float>(x) * 0.02f, static_cast<float>(y) * 0.45f, c.seed + 23U) - 0.5f);
      const float absorb = 0.88f + 0.12f * paperTooth.at(x, y);
      float* p = rgb.px(x, y);
      for (size_t ch = 0; ch < 3; ++ch) p[ch] = clamp01(mix(p[ch], ink[ch] * wood, absorb));
    }
  // The key block: black lines where the fields meet and along the drawing.
  Img key = borders(idx, c.w, c.h, std::max(1.0f, c.S / 700.0f));
  const Img l = lines(blur(lumaOf(c.photo), 1.0f, c.token), std::max(1.0f, c.S / 550.0f), 1.6f, 0.014f, 0.03f, c.token);
  for (size_t i = 0; i < key.d.size(); ++i) key.d[i] = clamp01(std::max(key.d[i] * 0.75f, l.d[i]) * (0.85f + 0.15f * paperTooth.d[i]));
  mixColour(rgb, hex(0x1E1A18), 0.95f, &key);
  // Old, slightly faded.
  grade(rgb, {.lift = 0.03f, .saturation = 0.9f, .warmth = 0.1f}, c.token);
  return rgb;
}

// ---- drawings and prints -------------------------------------------------------

// Lines of a hatching set in direction a (radians): coverage 0..1 for a
// line weight 0..1, broken into strokes of a hand's length with tapered ends.
float hatchLine(float x, float y, float a, float period, float weight, float stroke, uint32_t seed) {
  if (weight <= 0) return 0;
  const float nx = -std::sin(a), ny = std::cos(a), tx = std::cos(a), ty = std::sin(a);
  const float v = (x * nx + y * ny) / period, u = x * tx + y * ty;
  const float line = std::floor(v);
  const int li = static_cast<int>(line);
  const float wob = 0.18f * (valueNoise(u / (stroke * 0.8f), line * 0.37f, seed) - 0.5f);  // a hand, not a ruler
  const float s = 0.5f + 0.5f * std::cos(2 * kPi * (v - line - 0.5f + wob));
  const float aa = 1.2f / period;
  float cover = smoothstep(1 - weight - aa, 1 - weight + aa, s);
  const float off = hash01(li, 7, seed) * stroke, f = (u + off) / stroke - std::floor((u + off) / stroke);
  cover *= smoothstep(0.0f, 0.12f, f) * (1 - smoothstep(0.8f, 0.97f, f));  // each stroke starts and lifts
  return cover;
}

Img michelangelo(Ctx& c) {
  Img l = lumaOf(c.photo);
  const Img coarse = blur(l, c.S / 50.0f, c.token);
  for (size_t i = 0; i < l.d.size(); ++i) l.d[i] = clamp01(l.d[i] + 0.6f * (l.d[i] - coarse.d[i]));
  l = blur(l, std::max(0.6f, c.S / 900.0f), c.token);
  Img tooth;
  const Rgb3 cream = hex(0xE9DCC0), chalk = hex(0xA2412B), deep = hex(0x7E2E1C);
  Img rgb = paper(c.w, c.h, cream, 0.1f * c.ctl.texture, true, c.seed + 29U, &tooth);
  // The study: the figure drawn; the paper left bare around it.
  Img fade = c.subject.d.empty() ? Img(c.w, c.h, 1, 1.0f) : c.subject;
  if (c.subject.d.empty())
    for (int y = 0; y < c.h; ++y)
      for (int x = 0; x < c.w; ++x) {
        const float dx = static_cast<float>(x) / static_cast<float>(c.w) - 0.5f, dy = static_cast<float>(y) / static_cast<float>(c.h) - 0.45f;
        fade.at(x, y) = 1 - smoothstep(0.3f, 0.6f, std::sqrt(dx * dx + dy * dy));
      }
  else
    fade = blur(fade, c.S / 60.0f, c.token);
  const Field f = orientation(c.photo, std::max(1.0f, c.S / 250.0f), std::max(3.0f, c.S / 50.0f), c.token);
  const float period = std::max(3.0f, c.S / 150.0f), stroke = c.S / 22.0f;
  constexpr int kDirs = 6;
  const bool cross = c.level != Level::Study;
  parallelFor(c.h, c.token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < c.w; ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(c.w) + static_cast<size_t>(x);
        const float fd = fade.d[i];
        const float T = std::pow(1 - l.d[i], 1.15f) * (0.15f + 0.85f * fd);
        const float fx = static_cast<float>(x), fy = static_cast<float>(y);
        // Hatching that wraps the form (blended between six directions), then the diagonal, then cross-hatching in the darks.
        const float theta = std::atan2(f.ty[i], f.tx[i]);
        float form = 0, wsum = 0;
        for (int b = 0; b < kDirs; ++b) {
          const float a = static_cast<float>(b) * kPi / kDirs;
          const float w = std::pow(std::max(0.0f, std::cos(2 * (theta + 0.35f - a))), 4.0f);
          if (w <= 0) continue;
          form += w * hatchLine(fx, fy, a, period, T * 0.75f, stroke, c.seed + static_cast<uint32_t>(b));
          wsum += w;
        }
        float cover = wsum > 0 ? form / wsum : 0;
        cover = std::max(cover, hatchLine(fx, fy, -0.8f, period * 0.9f, (T - 0.3f) * 1.1f, stroke * 0.8f, c.seed + 11U));
        if (cross) cover = std::max(cover, hatchLine(fx, fy, 0.55f, period * 0.85f, (T - 0.62f) * 1.3f, stroke * 0.7f, c.seed + 13U));
        // Chalk catches the paper's peaks; a little is rubbed in with the finger (sfumato of the study).
        const float grip = 0.25f + 0.75f * smoothstep(0.25f, 0.65f, tooth.d[i]);
        const float rubbed = 0.28f * T * fd;
        float* p = rgb.px(x, y);
        const float a = clamp01(rubbed + cover * grip * 0.9f);
        for (size_t k = 0; k < 3; ++k) p[k] = mix(p[k], chalk[k], a);
      }
  });
  Img contour = lines(l, std::max(1.0f, c.S / 650.0f), 1.6f, 0.012f, 0.04f, c.token);
  for (size_t i = 0; i < contour.d.size(); ++i) contour.d[i] *= 0.8f * (0.25f + 0.75f * fade.d[i]) * (0.35f + 0.65f * smoothstep(0.2f, 0.6f, tooth.d[i]));
  mixColour(rgb, deep, 1.0f, &contour);
  vignette(rgb, 0.12f, hex(0xB89C74));
  return rgb;
}

Img durer(Ctx& c) {
  Img l = lumaOf(c.photo);
  const Img coarse = blur(l, c.S / 40.0f, c.token);
  for (size_t i = 0; i < l.d.size(); ++i) l.d[i] = clamp01(l.d[i] + 0.5f * (l.d[i] - coarse.d[i]));
  l = blur(l, std::max(0.6f, c.S / 1200.0f), c.token);
  const Field f = orientation(c.photo, std::max(1.0f, c.S / 250.0f), std::max(3.0f, c.S / 60.0f), c.token);
  const Img bg = background(c);
  Img rgb = paper(c.w, c.h, hex(0xF2ECDD), 0.06f * c.ctl.texture, false, c.seed + 37U, nullptr);
  const float period = std::max(3.0f, c.S / 170.0f);
  const Rgb3 ink = hex(0x1A1612);
  constexpr int kDirs = 6;
  // Burin lines: stripes following the form (blended between six cut directions), swelling with the shadow.
  parallelFor(c.h, c.token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < c.w; ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(c.w) + static_cast<size_t>(x);
        float T = std::pow(1 - l.d[i], 0.9f);
        float theta = std::atan2(f.ty[i], f.tx[i]);
        if (bg.d[i] > 0.5f) theta = 0.0f, T *= 0.55f;  // a sky of level lines
        auto stripes = [&](float rot) {
          float s = 0, wsum = 0;
          for (int b = 0; b < kDirs; ++b) {
            const float a = static_cast<float>(b) * kPi / kDirs;
            const float w = std::pow(std::max(0.0f, std::cos(2 * (theta + rot - a))), 4.0f);
            if (w <= 0) continue;
            const float nx = -std::sin(a), ny = std::cos(a);
            s += w * (0.5f + 0.5f * std::cos(2 * kPi * (static_cast<float>(x) * nx + static_cast<float>(y) * ny) / period + static_cast<float>(b) * 1.7f));
            wsum += w;
          }
          return wsum > 0 ? s / wsum : 1.0f;
        };
        const float aa = 1.5f / period;
        auto inked = [&](float s, float width) { return 1 - smoothstep(width - aa, width + aa, s); };
        float cover = inked(stripes(0), T * 0.9f);
        if (T > 0.5f) cover = std::max(cover, inked(stripes(kPi / 3), (T - 0.5f) * 1.4f));
        if (T > 0.75f) cover = std::max(cover, inked(stripes(-kPi / 3), (T - 0.75f) * 1.6f));
        float* p = rgb.px(x, y);
        for (int k = 0; k < 3; ++k) p[k] = mix(p[k], ink[static_cast<size_t>(k)], cover * 0.95f);
      }
  });
  Img contour = lines(l, std::max(1.0f, c.S / 700.0f), 1.6f, 0.012f, 0.03f, c.token);
  mixColour(rgb, ink, 0.9f, &contour);
  return rgb;
}

Img degas(Ctx& c) {
  Img ref = c.photo;
  grade(ref, {.gain = 1.05f, .contrast = 0.2f, .saturation = 1.2f}, c.token);
  mixColour(ref, hex(0xFFFFFF), 0.12f);  // chalky
  palette(ref, pal({0xE7A6A0, 0xF0C08F, 0x6FC0C0, 0x4664A8, 0x7BAA6A, 0xD4A04C, 0xF5F2EA, 0x2B2A2A}), 0.35f, 0.9f, c.token);
  Canvas cv;
  // A toned paper in the picture's middle tone (as Degas chose his), so where it shows it belongs.
  double mean[3] = {0, 0, 0};
  for (size_t i = 0; i < ref.d.size(); i += 3)
    for (size_t k = 0; k < 3; ++k) mean[k] += ref.d[i + k];
  const double n = static_cast<double>(ref.d.size() / 3);
  const Rgb3 base = hex(0x9AA0A0);
  Rgb3 tone{};
  for (size_t k = 0; k < 3; ++k) tone[k] = mix(base[k], static_cast<float>(mean[k] / n) * 0.85f, 0.6f);
  cv.rgb = paper(c.w, c.h, tone, 0.12f * c.ctl.texture, false, c.seed + 43U, &cv.tooth);
  cv.height = Img(c.w, c.h, 1, 0.0f);
  cv.empty = false;  // the toned paper shows wherever it is already right
  const Field f = formField(c, ref);
  std::vector<float> rs = {c.S / 110.0f, c.S / 190.0f};
  if (c.level == Level::Masterpiece) rs.push_back(c.S / 300.0f);
  uint32_t layer = 0;
  for (float r : rs) {
    Brush b{.radius = r, .maxLen = 10, .minLen = 3, .curvature = 0.5f, .opacity = 0.9f, .softness = 0.45f, .bristleAmp = 0.15f, .dryness = 0.2f,
            .taper = 0.4f, .thick = 0, .threshold = 0.07f, .grid = 0.8f, .mode = layer % 2 == 0 ? StrokeMode::Fixed : StrokeMode::Flow,
            .angle = -1.0f, .angleJitter = 0.25f, .tooth = 0.25f};
    if (!strokes(c, cv, ref, f, b, c.seed * 13U + ++layer, nullptr)) break;
  }
  Img rgb = cv.rgb;
  Img contour = lines(blur(lumaOf(c.photo), 1.0f, c.token), std::max(1.0f, c.S / 600.0f), 1.6f, 0.015f, 0.04f, c.token);
  for (size_t i = 0; i < contour.d.size(); ++i) contour.d[i] *= 0.8f * smoothstep(0.25f, 0.6f, cv.tooth.d[i]);
  mixColour(rgb, hex(0x2B2A2A), 1.0f, &contour);  // charcoal
  grain(rgb, 0.02f * c.ctl.texture, c.seed + 47U);
  return rgb;
}

// ---- gold: Klimt -----------------------------------------------------------------

Img klimt(Ctx& c) {
  // The figure: a fine, pale, smooth painting.
  Img ref = c.photo;
  grade(ref, {.lift = 0.05f, .saturation = 0.9f, .warmth = 0.2f}, c.token);
  const Img skin = skinMask(c);  // the sitter's own skin, painted finely
  const Field f = formField(c, ref);
  Oil o;
  o.ground = hex(0xC9A86A);
  o.maxFrac = 1.0f / 45, o.minFrac = 1.0f / 420;
  o.brush = {.maxLen = 6, .curvature = 0.6f, .opacity = 0.82f, .softness = 0.55f, .bristleAmp = 0.04f, .dryness = 0.15f, .thick = 0.06f,
             .threshold = 0.05f};
  Canvas cv = paintOil(c, ref, f, o);
  Img rgb = cv.rgb;
  mixInto(rgb, blur(rgb, c.S / 600.0f, c.token), 0.3f);

  // Everywhere else, gold leaf and ornament.
  const Img bg = background(c);
  const Img tone = blur(lumaOf(c.photo), c.S / 300.0f, c.token);
  const Rgb3 goldDark = hex(0x6B4E1A), goldLight = hex(0xE9C66A), black = hex(0x151210), white = hex(0xEFE9DA), silver = hex(0xB8B6B0);
  const float tess = std::max(4.0f, c.S / 60.0f), motif = std::max(10.0f, c.S / 14.0f);
  Img height(c.w, c.h, 1, 0.0f);
  parallelFor(c.h, c.token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < c.w; ++x) {
        const float s = skin.at(x, y);
        if (s >= 0.999f) continue;
        const float fx = static_cast<float>(x), fy = static_cast<float>(y), t = tone.at(x, y);
        // Gold that keeps the form: darker where the photo is dark, shimmering.
        const float shimmer = 0.85f + 0.3f * fbm(fx / (c.S * 0.03f), fy / (c.S * 0.03f), c.seed + 51U, 3);
        Rgb3 col{};
        for (size_t k = 0; k < 3; ++k) col[k] = clamp01(mix(goldDark[k], goldLight[k], std::pow(t, 0.8f)) * shimmer);
        float hgt = 0.2f;
        const bool clothing = bg.at(x, y) < 0.5f && !c.subject.d.empty();
        if (!clothing) {
          // Background: a mosaic of small gold tesserae, a few coloured, with spirals here and there.
          // Hand-laid: each row shifted, each tessera its own size and tilt of the leaf.
          const int iy = static_cast<int>(fy / tess);
          const float rowShift = hash01(0, iy, c.seed + 52U) * tess;
          const int ix = static_cast<int>((fx + rowShift) / tess);
          const float u = (fx + rowShift) / tess - static_cast<float>(ix), v = fy / tess - static_cast<float>(iy);
          const float gap = std::min({u, v, 1 - u, 1 - v});
          const float h = hash01(ix, iy, c.seed + 53U);
          const float leaf = 0.82f + 0.3f * h + 0.12f * (fbm(fx / (c.S * 0.08f), fy / (c.S * 0.08f), c.seed + 54U, 3) - 0.5f);
          for (size_t k = 0; k < 3; ++k) col[k] = clamp01(col[k] * leaf);
          if (h > 0.97f) col = h > 0.99f ? silver : hex(0x8E3A2A);
          if (gap < 0.05f + 0.03f * h) {
            for (float& ch : col) ch *= 0.78f;
            hgt = 0.05f;
          }
          const int mx = static_cast<int>(fx / (motif * 1.4f)), my = static_cast<int>(fy / (motif * 1.4f));
          if (hash01(mx, my, c.seed + 55U) > 0.72f) {
            const float cx = (static_cast<float>(mx) + 0.5f) * motif * 1.4f, cy = (static_cast<float>(my) + 0.5f) * motif * 1.4f;
            const float dx = fx - cx, dy = fy - cy, rr = std::hypot(dx, dy) / (motif * 0.55f);
            if (rr < 1) {
              const float spiral = std::sin(2 * kPi * (rr * 3.2f) + std::atan2(dy, dx));
              if (spiral > 0.55f) col = black, hgt = 0.45f;
            }
          }
        } else {
          // The gown: ornaments — rings, black-and-white blocks, eyes, spirals — in gold and the gown's own colour.
          const int ix = static_cast<int>(fx / motif), iy = static_cast<int>(fy / motif);
          const float u = fx / motif - static_cast<float>(ix) - 0.5f, v = fy / motif - static_cast<float>(iy) - 0.5f;
          const float rr = std::hypot(u, v) * 2;
          const uint32_t kind = hash2(ix, iy, c.seed + 57U) % 4U;
          float hsv[3];
          rgbToHsv(c.photo.px(x, y), hsv);
          hsv[1] = std::max(hsv[1], 0.55f), hsv[2] = std::max(hsv[2], 0.55f);
          float tint[3];
          hsvToRgb(hsv, tint);
          if (kind == 0 && rr < 0.9f) {
            const int ring = static_cast<int>(rr * 5);
            const Rgb3 rings[4] = {black, white, {tint[0], tint[1], tint[2]}, goldLight};
            col = rings[ring % 4], hgt = 0.4f;
          } else if (kind == 1 && std::fabs(u) < 0.36f && std::fabs(v) < 0.44f) {
            const bool check = ((static_cast<int>((u + 0.5f) * 6) + static_cast<int>((v + 0.5f) * 6)) & 1) != 0;
            col = check ? black : white, hgt = 0.35f;
          } else if (kind == 2 && (u * u) / 0.16f + (v * v) / 0.05f < 1) {
            col = (u * u) / 0.02f + (v * v) / 0.02f < 1 ? black : Rgb3{tint[0], tint[1], tint[2]}, hgt = 0.4f;
          } else if (kind == 3 && rr < 0.9f && std::sin(2 * kPi * rr * 2.5f + std::atan2(v, u)) > 0.4f) {
            col = Rgb3{tint[0] * 0.8f, tint[1] * 0.8f, tint[2] * 0.8f}, hgt = 0.4f;
          }
        }
        if (hash01(x, y, c.seed + 59U) > 0.997f) col = {1, 0.97f, 0.85f};  // flecks of leaf catching light
        float* p = rgb.px(x, y);
        for (size_t k = 0; k < 3; ++k) p[k] = mix(col[k], p[k], s);
        height.at(x, y) = hgt * (1 - s);
      }
  });
  canvasWeave(height, std::max(2.5f, c.S / 240.0f), 0.08f * c.ctl.texture, c.seed + 61U);
  lightRelief(rgb, height, 0.35f * std::max(0.4f, c.ctl.texture), 0.25f, c.token);  // metal catches the light
  varnish(rgb, clamp01(0.15f * c.ctl.age));
  return rgb;
}

Img paintArtist(Ctx& c, Artist a) {
  switch (a) {
    case Artist::Leonardo: return leonardo(c);
    case Artist::Michelangelo: return michelangelo(c);
    case Artist::Raphael: return raphael(c);
    case Artist::Durer: return durer(c);
    case Artist::Titian: return titian(c);
    case Artist::Caravaggio: return caravaggio(c);
    case Artist::Rembrandt: return rembrandt(c);
    case Artist::Vermeer: return vermeer(c);
    case Artist::Velazquez: return velazquez(c);
    case Artist::Goya: return goya(c);
    case Artist::Hokusai: return hokusai(c);
    case Artist::Turner: return turner(c);
    case Artist::Monet: return monet(c);
    case Artist::Degas: return degas(c);
    case Artist::Renoir: return renoir(c);
    case Artist::Cezanne: return cezanne(c);
    case Artist::Seurat: return seurat(c);
    case Artist::VanGogh: return vangogh(c);
    case Artist::Gauguin: return gauguin(c);
    case Artist::Klimt: return klimt(c);
  }
  return c.photo;
}

// The person's colour and likeness choices, over the finished painting.
void personalise(Img& out, const Ctx& c) {
  const Controls& k = c.ctl;
  const bool colour = k.palette < 0.999f || std::fabs(k.vivid) > 0.001f;
  if (colour)
    for (int y = 0; y < c.h; ++y)
      for (int x = 0; x < c.w; ++x) {
        float lab[3], ph[3];
        float* p = out.px(x, y);
        rgbToLab(p, lab);
        if (k.palette < 0.999f) {
          rgbToLab(c.photo.px(x, y), ph);
          // The photo's hue and colourfulness, at the painting's own light (no glaring colour in its shadows).
          const float fit = std::clamp(lab[0] / std::max(ph[0], 5.0f), 0.0f, 1.1f);
          lab[1] = mix(ph[1] * fit, lab[1], k.palette), lab[2] = mix(ph[2] * fit, lab[2], k.palette);
        }
        if (std::fabs(k.vivid) > 0.001f) {
          // Vibrance: quiet colours gain most, strong ones (skin, a red dress) little, so skin stays natural.
          const float chroma = std::hypot(lab[1], lab[2]);
          const float sat = k.vivid > 0 ? 1.0f + 0.55f * k.vivid * (1.0f - smoothstep(15.0f, 55.0f, chroma)) : 1.0f + 0.6f * k.vivid;
          lab[1] *= sat, lab[2] *= sat;
        }
        labToRgb(lab, p);
        for (int i = 0; i < 3; ++i) p[i] = clamp01(p[i]);
      }
  // A closer likeness: the sitter's features (the photo's fine light and shade) drawn back into the face.
  if (k.likeness > 0.5f && !c.faces.d.empty()) {
    const Img l = lumaOf(c.photo);
    const Img soft = blur(l, std::max(1.0f, c.G / 160.0f), c.token);
    const float amt = (k.likeness - 0.5f) * 1.4f;
    for (int y = 0; y < c.h; ++y)
      for (int x = 0; x < c.w; ++x) {
        const float a = amt * c.faces.at(x, y);
        if (a <= 0) continue;
        const float d = std::clamp(l.at(x, y) - soft.at(x, y), -0.2f, 0.2f) * a;
        float* p = out.px(x, y);
        for (int i = 0; i < 3; ++i) p[i] = clamp01(p[i] + d);
      }
  }
}

// Separable min (erode) or max (dilate) over a square of radius r.
Img rankFilter(const Img& src, int r, bool max) {
  Img a(src.w, src.h, 1), b(src.w, src.h, 1);
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x) {
      float v = src.at(x, y);
      for (int k = std::max(0, x - r); k <= std::min(src.w - 1, x + r); ++k) v = max ? std::max(v, src.at(k, y)) : std::min(v, src.at(k, y));
      a.at(x, y) = v;
    }
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x) {
      float v = a.at(x, y);
      for (int k = std::max(0, y - r); k <= std::min(src.h - 1, y + r); ++k) v = max ? std::max(v, a.at(x, k)) : std::min(v, a.at(x, k));
      b.at(x, y) = v;
    }
  return b;
}

// Teeth and the whites of the eyes: found in the photo (bright, nearly
// colourless spots within a face, well above the skin's own light) and given
// back their light after the painting, at the painting's own brightness and
// in its tint. Without this, hatching, key lines, dark glazes and old
// varnish can blacken, cross out or yellow a smile.
Img facialWhites(const Ctx& c) {
  Img m(c.w, c.h, 1, 0.0f);
  if (c.faces.d.empty()) return m;
  for (const FaceBox& f : c.faceBoxes) {
    const int x0 = std::clamp(static_cast<int>(f.x * static_cast<float>(c.w)), 0, c.w - 1);
    const int y0 = std::clamp(static_cast<int>(f.y * static_cast<float>(c.h)), 0, c.h - 1);
    const int x1 = std::clamp(static_cast<int>((f.x + f.w) * static_cast<float>(c.w)), x0 + 1, c.w);
    const int y1 = std::clamp(static_cast<int>((f.y + f.h) * static_cast<float>(c.h)), y0 + 1, c.h);
    // The face's own light: the median of its brightness.
    std::vector<float> ys;
    ys.reserve(static_cast<size_t>((x1 - x0) * (y1 - y0)));
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x)
        if (c.faces.at(x, y) > 0.5f) ys.push_back(luma(c.photo.px(x, y)));
    if (ys.size() < 16) continue;
    std::nth_element(ys.begin(), ys.begin() + static_cast<std::ptrdiff_t>(ys.size() / 2), ys.end());
    const float skin = ys[ys.size() / 2];
    // The skin's own colourfulness: teeth and eye whites are far less colourful than it.
    std::vector<float> ss;
    ss.reserve(ys.capacity());
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x)
        if (c.faces.at(x, y) > 0.5f) {
          float hsv[3];
          rgbToHsv(c.photo.px(x, y), hsv);
          ss.push_back(hsv[1]);
        }
    std::nth_element(ss.begin(), ss.begin() + static_cast<std::ptrdiff_t>(ss.size() / 2), ss.end());
    const float sh = std::clamp(0.5f * ss[ss.size() / 2], 0.15f, 0.3f);
    // Clearly above the skin's light (on dark skin by half again; on light skin by a step), whatever the skin.
    const float lo = std::clamp(std::min(skin * 1.45f, skin + 0.14f), 0.28f, 0.85f);
    // Where they can be: the mouth and the eyes (from the face's landmarks, or
    // where they sit in a face box), never the forehead's or cheeks' shine.
    const float fw = f.w * static_cast<float>(c.w), fh = f.h * static_cast<float>(c.h);
    const float bx = f.x * static_cast<float>(c.w), by = f.y * static_cast<float>(c.h);
    struct Zone { float cx, cy, rx, ry; };
    std::vector<Zone> zones;
    const bool mouthKnown = f.mouth[2] > f.mouth[0] && f.mouth[3] >= f.mouth[1];
    if (mouthKnown) {
      // The opening between the lips: a closed mouth has none, so its shine stays the lips'.
      const float mx0 = f.mouth[0] * static_cast<float>(c.w), mx1 = f.mouth[2] * static_cast<float>(c.w);
      const float my0 = f.mouth[1] * static_cast<float>(c.h), my1 = f.mouth[3] * static_cast<float>(c.h);
      if (my1 - my0 > 0.02f * fw) zones.push_back({(mx0 + mx1) * 0.5f, (my0 + my1) * 0.5f, (mx1 - mx0) * 0.55f, (my1 - my0) * 0.6f + 0.01f * fw});
    } else {
      zones.push_back({bx + 0.5f * fw, by + 0.76f * fh, 0.24f * fw, 0.13f * fh});
    }
    const bool eyesKnown = f.eyes[0] > 0 && f.eyes[2] > 0;
    for (int e = 0; e < 2; ++e) {
      const float ex = eyesKnown ? f.eyes[e * 2] * static_cast<float>(c.w) : bx + (e == 0 ? 0.31f : 0.69f) * fw;
      const float ey = eyesKnown ? f.eyes[e * 2 + 1] * static_cast<float>(c.h) : by + 0.4f * fh;
      zones.push_back({ex, ey, 0.11f * fw, 0.05f * fw});
    }
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        float zone = 0;
        for (const Zone& z : zones) {
          const float dx = (static_cast<float>(x) - z.cx) / z.rx, dy = (static_cast<float>(y) - z.cy) / z.ry;
          zone = std::max(zone, 1 - smoothstep(0.8f, 1.0f, std::sqrt(dx * dx + dy * dy)));
        }
        if (zone <= 0) continue;
        const float* p = c.photo.px(x, y);
        float hsv[3];
        rgbToHsv(p, hsv);
        // Lip pinks and reds (a lip's gloss) aren't teeth.
        const float pink = (hsv[0] > 0.9f || hsv[0] < 0.045f) ? smoothstep(0.08f, 0.16f, hsv[1]) : 0.0f;
        const float v = smoothstep(lo, lo + 0.1f, luma(p)) * (1 - smoothstep(sh, sh + 0.12f, hsv[1])) * (1 - pink);
        m.at(x, y) = std::max(m.at(x, y), v * zone);
      }
  }
  // Whole features, not specks.
  float faceW = 0;
  for (const FaceBox& f : c.faceBoxes) faceW = std::max(faceW, f.w * static_cast<float>(c.w));
  // Close the gaps between teeth first, then open away specks.
  const int rc = std::max(1, static_cast<int>(faceW / 60.0f + 0.5f)), ro = std::max(1, static_cast<int>(faceW / 90.0f + 0.5f));
  m = rankFilter(rankFilter(m, rc, true), rc, false);
  m = rankFilter(rankFilter(m, ro, false), ro, true);
  m = blur(m, std::max(0.8f, faceW / 300.0f), c.token);
  for (float& v : m.d) v = smoothstep(0.2f, 0.55f, v);
  return m;
}

void keepWhites(Img& out, const Ctx& c) {
  const Img m = facialWhites(c);
  double po = 0, pp = 0, wsum = 0;
  for (size_t i = 0; i < m.d.size(); ++i) {
    const float f = c.faces.d.empty() ? 0.0f : c.faces.d[i];
    if (f < 0.5f) continue;
    po += luma(&out.d[i * 3]), pp += luma(&c.photo.d[i * 3]), wsum += 1;
  }
  if (wsum < 16) return;
  // How the painting lit the face against the photo: the whites follow it.
  const float ratio = std::clamp(static_cast<float>(po / std::max(pp, 1e-3)), 0.55f, 1.25f);
  const Img tint = blur(out, std::max(2.0f, c.G / 80.0f), c.token);  // the painting's colour around them
  const Img soft = blur(c.photo, std::max(0.6f, c.G / 1400.0f), c.token);
  for (int y = 0; y < c.h; ++y)
    for (int x = 0; x < c.w; ++x) {
      const float a = m.at(x, y);
      if (a <= 0.01f) continue;
      float lo[3], lp[3], lt[3];
      float* o = out.px(x, y);
      rgbToLab(o, lo);
      rgbToLab(soft.px(x, y), lp);
      rgbToLab(tint.px(x, y), lt);
      float target[3] = {std::max(lo[0], std::min(95.0f, lp[0] * ratio)), lt[1] * 0.35f + lp[1] * 0.15f, lt[2] * 0.35f + lp[2] * 0.15f};
      for (int k = 0; k < 3; ++k) lo[k] = mix(lo[k], target[k], a * 0.92f);
      labToRgb(lo, o);
      for (int k = 0; k < 3; ++k) o[k] = clamp01(o[k]);
    }
}

}  // namespace

Error paint(ImageBuffer& image, const Options& options, const CancellationToken* token) {
  if (image.empty() || (image.format() != PixelFormat::RGBA8 && image.format() != PixelFormat::BGRA8))
    return {PP_ERROR_INVALID_ARGUMENT, "Masters needs an RGBA or BGRA image"};
  if (image.width() < 16 || image.height() < 16) return {PP_ERROR_INVALID_ARGUMENT, "The photo is too small to paint"};
  const int a = static_cast<int>(options.artist);
  if (a < 0 || a >= kArtistCount) return {PP_ERROR_INVALID_ARGUMENT, "Unknown artist"};
  const int lv = static_cast<int>(options.level);
  if (lv < 0 || lv > 2) return {PP_ERROR_INVALID_ARGUMENT, "Unknown level"};
  if (options.subject != nullptr && (options.subject->empty() || options.subject->format() != PixelFormat::Gray8))
    return {PP_ERROR_INVALID_ARGUMENT, "The person mask must be a grey image"};

  const bool bgra = image.format() == PixelFormat::BGRA8;
  Ctx c;
  c.w = image.width(), c.h = image.height();
  {
    Controls k = options.controls;
    auto ok = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    if (!ok(k.brush, 0.5f, 2.0f) || !ok(k.palette, 0, 1) || !ok(k.vivid, -1, 1) || !ok(k.likeness, 0, 1) || !ok(k.texture, 0, 2) ||
        !ok(k.age, 0, 2) || !ok(k.background, 0, 1))
      return {PP_ERROR_INVALID_ARGUMENT, "A painting control is out of range"};
    c.ctl = k;
  }
  c.G = static_cast<float>(std::min(c.w, c.h));
  c.S = c.G * c.ctl.brush;
  c.level = options.level;
  c.seed = options.seed == 0 ? 1U : options.seed;
  c.token = token;
  c.photo = Img(c.w, c.h, 3);
  for (int y = 0; y < c.h; ++y) {
    const uint8_t* row = image.row(y);
    for (int x = 0; x < c.w; ++x) {
      float* p = c.photo.px(x, y);
      p[0] = static_cast<float>(row[x * 4 + (bgra ? 2 : 0)]) / 255.0f;
      p[1] = static_cast<float>(row[x * 4 + 1]) / 255.0f;
      p[2] = static_cast<float>(row[x * 4 + (bgra ? 0 : 2)]) / 255.0f;
    }
  }
  if (options.subject != nullptr) {
    const ImageBuffer& m = *options.subject;
    Img mask(m.width(), m.height(), 1);
    for (int y = 0; y < m.height(); ++y)
      for (int x = 0; x < m.width(); ++x) mask.at(x, y) = static_cast<float>(m.row(y)[x]) / 255.0f;
    c.subject = blur(resize(mask, c.w, c.h), std::max(1.0f, c.G / 300.0f), token);
  }
  for (const FaceBox& f : options.faces) {
    if (!(f.w > 0 && f.h > 0) || !std::isfinite(f.x) || !std::isfinite(f.y)) continue;
    c.faceBoxes.push_back(f);
    if (c.faces.d.empty()) c.faces = Img(c.w, c.h, 1, 0.0f);
    const float cx = (f.x + f.w * 0.5f) * static_cast<float>(c.w), cy = (f.y + f.h * 0.5f) * static_cast<float>(c.h);
    const float rx = f.w * static_cast<float>(c.w) * 0.65f, ry = f.h * static_cast<float>(c.h) * 0.75f;
    for (int y = 0; y < c.h; ++y)
      for (int x = 0; x < c.w; ++x) {
        const float dx = (static_cast<float>(x) - cx) / rx, dy = (static_cast<float>(y) - cy) / ry;
        c.faces.at(x, y) = std::max(c.faces.at(x, y), 1 - smoothstep(0.75f, 1.0f, std::sqrt(dx * dx + dy * dy)));
      }
  }
  // A face is never background, whatever the mask missed.
  if (!c.subject.d.empty() && !c.faces.d.empty())
    for (size_t i = 0; i < c.subject.d.size(); ++i) c.subject.d[i] = std::max(c.subject.d[i], c.faces.d[i]);
  if (isCancelled(token)) return {PP_ERROR_CANCELLED, "Cancelled"};

  Img out = paintArtist(c, options.artist);
  if (isCancelled(token)) return {PP_ERROR_CANCELLED, "Cancelled"};
  personalise(out, c);
  keepWhites(out, c);
  if (isCancelled(token)) return {PP_ERROR_CANCELLED, "Cancelled"};
  for (int y = 0; y < c.h; ++y) {
    uint8_t* row = image.row(y);
    for (int x = 0; x < c.w; ++x) {
      const float* p = out.px(x, y);
      auto q = [](float v) { return static_cast<uint8_t>(std::lround(clamp01(v) * 255.0f)); };
      row[x * 4 + (bgra ? 2 : 0)] = q(p[0]);
      row[x * 4 + 1] = q(p[1]);
      row[x * 4 + (bgra ? 0 : 2)] = q(p[2]);
    }
  }
  return {PP_OK, {}};
}

}  // namespace photoplus::art
