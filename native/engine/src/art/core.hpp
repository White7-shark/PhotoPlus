#pragma once

// Masters (art.hpp): the painter's tools shared by every artist — float
// images, blur, noise, colour (Lab, palettes, tone), the form-following
// orientation field, edge-preserving smoothing, the brush-stroke renderer,
// and the finishes (canvas, impasto light, varnish, craquelure, paper).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"

namespace photoplus::art::core {

constexpr float kPi = 3.14159265358979f;

// A float image with c channels (1 or 3), values 0..1.
struct Img {
  int w = 0, h = 0, c = 3;
  std::vector<float> d;
  Img() = default;
  Img(int width, int height, int channels, float fill = 0.0f)
      : w(width), h(height), c(channels), d(static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels), fill) {}
  float* px(int x, int y) { return &d[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * static_cast<size_t>(c)]; }
  const float* px(int x, int y) const {
    return &d[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * static_cast<size_t>(c)];
  }
  float& at(int x, int y) { return d[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
  float at(int x, int y) const { return d[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
  // Bilinear, edge-clamped, channel k.
  float sample(float x, float y, int k = 0) const;
  void sample3(float x, float y, float out[3]) const;
};

// ---- random and noise (deterministic) -------------------------------------
inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}
inline uint32_t hash2(int x, int y, uint32_t s) {
  return hash32(static_cast<uint32_t>(x) * 0x9E3779B1U ^ hash32(static_cast<uint32_t>(y) * 0x85EBCA77U ^ s));
}
inline float hash01(int x, int y, uint32_t s) { return static_cast<float>(hash2(x, y, s) & 0xFFFFFF) / 16777216.0f; }
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 1) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return static_cast<uint32_t>(s >> 11);
  }
  float uni() { return static_cast<float>(next() & 0xFFFFFF) / 16777216.0f; }
  float range(float a, float b) { return a + (b - a) * uni(); }
};
float valueNoise(float x, float y, uint32_t seed);
float fbm(float x, float y, uint32_t seed, int octaves = 4);
float noise1(float x, uint32_t seed);

inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float smoothstep(float a, float b, float x) {
  const float t = clamp01((x - a) / (b - a));
  return t * t * (3.0f - 2.0f * t);
}
inline float mix(float a, float b, float t) { return a + (b - a) * t; }
inline float luma(const float* p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; }

// ---- images --------------------------------------------------------------
Img blur(const Img& src, float sigma, const CancellationToken* token);
Img resize(const Img& src, int w, int h);
Img lumaOf(const Img& rgb);
Img down2(const Img& src);

// ---- colour ----------------------------------------------------------------
using Rgb3 = std::array<float, 3>;
Rgb3 hex(uint32_t rgb);
void rgbToLab(const float* rgb, float* lab);
void labToRgb(const float* lab, float* rgb);
void rgbToHsv(const float* rgb, float* hsv);
void hsvToRgb(const float* hsv, float* rgb);

// Grade: tone (lift shadows, gain highlights, gamma, S-contrast), saturation,
// warmth (+ warm / - cool), and split toning of shadows and highlights.
struct Grade {
  float lift = 0, gain = 1, gamma = 1, contrast = 0, saturation = 1, warmth = 0;
  Rgb3 shadowTint{0, 0, 0}, highlightTint{0, 0, 0};
  float tintStrength = 0;
};
void grade(Img& rgb, const Grade& g, const CancellationToken* token);

// Pulls colours towards the artist's palette (soft nearest colours in Lab),
// keeping the photo's light (lumaKeep 1 keeps every value) — like painting
// it with those pigments.
void palette(Img& rgb, const std::vector<Rgb3>& colours, float strength, float lumaKeep, const CancellationToken* token);

// ---- form ------------------------------------------------------------------
// Unit tangent of the forms at each pixel (along edges and contours, from a
// smoothed structure tensor) and how strongly a direction exists (0..1).
struct Field {
  int w = 0, h = 0;
  std::vector<float> tx, ty, coherence;
  void at(float x, float y, float& ox, float& oy) const;
  float coherent(float x, float y) const;
};
Field orientation(const Img& rgb, float sigmaGrad, float sigmaTensor, const CancellationToken* token);
// Mixes a large swirling flow into weak-direction areas (skies, walls): for painters whose strokes swirl.
void swirl(Field& f, float scale, float amount, uint32_t seed);

// Edge-preserving smoothing (generalised Kuwahara, 8 sectors): flat, painterly colour areas that keep edges.
Img kuwahara(const Img& rgb, int radius, const CancellationToken* token);
// Difference-of-Gaussians outlines (0 none .. 1 a full line) — for an artist's contours and key lines.
Img lines(const Img& luma, float sigma, float k, float threshold, float softness, const CancellationToken* token);

// ---- brush strokes -----------------------------------------------------------
enum class StrokeMode { Flow, Dab, Fixed, Dot };
struct Brush {
  float radius = 8;           // px
  float maxLen = 8, minLen = 2;  // in radii
  float step = 0.6f;          // in radii
  float curvature = 0.5f;     // 0 rigid .. 1 follows the field fully
  float opacity = 0.9f;
  float softness = 0.35f;     // edge falloff (fraction of the radius)
  float bristleAmp = 0.12f;   // value variation along the bristle streaks
  float bristleFreq = 6.0f;   // streaks across the brush
  float dryness = 0.3f;       // the tail breaks up as paint runs out
  float taper = 0.25f;        // pointed ends
  float thick = 0.5f;         // impasto height
  float thickLights = 0.0f;   // extra thickness in the light (Rembrandt)
  float ridge = 0.3f;         // paint piled at the stroke's edges
  float hueJitter = 0.0f, satJitter = 0.0f, valJitter = 0.0f;
  float threshold = 0.08f;    // repaint where canvas and reference differ by more
  float grid = 1.0f;          // seed spacing, in radii
  float blur = 0.5f;          // reference blur, in radii
  StrokeMode mode = StrokeMode::Flow;
  float angle = 0;            // Fixed mode: radians (with jitter below)
  float angleJitter = 0.15f;
  float tooth = 0;            // chalk/pastel: strokes skip over the paper's valleys (0..1)
  const std::vector<Rgb3>* dotPalette = nullptr;  // Dot mode: pure hues mixed in the eye
};

struct Canvas {
  Img rgb;     // the paint
  Img height;  // paint thickness (impasto), 1 channel
  Img tooth;   // paper/canvas tooth (0..1), 1 channel, optional
  bool empty = true;
};

// One layer of strokes from `ref` onto the canvas (only where the canvas
// still differs from it; everywhere on the first layer). `where` (optional,
// 1 channel) limits it to an area (faces, the person). Returns false if cancelled.
bool strokeLayer(Canvas& canvas, const Img& ref, const Field& field, const Brush& brush, uint32_t seed,
                 const Img* where, const CancellationToken* token);

// ---- finishes ----------------------------------------------------------------
void canvasWeave(Img& height, float threadPx, float amount, uint32_t seed);
void woodPanel(Img& height, float amount, uint32_t seed);
// Lights the paint's relief from the upper left (impasto ridges catch the light); gloss adds glints.
void lightRelief(Img& rgb, const Img& height, float strength, float gloss, const CancellationToken* token);
void varnish(Img& rgb, float amount);
void craquelure(Img& rgb, float cellPx, float amount, uint32_t seed, const CancellationToken* token);
void vignette(Img& rgb, float amount, Rgb3 colour, float cx = 0.5f, float cy = 0.45f);
// Paper: its colour, fibres and tooth (into canvas.tooth); laid lines for old papers.
Img paper(int w, int h, Rgb3 colour, float grain, bool laid, uint32_t seed, Img* tooth);
void grain(Img& rgb, float amount, uint32_t seed);

}  // namespace photoplus::art::core
