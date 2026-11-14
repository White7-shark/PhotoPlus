#pragma once

// Matte tools shared by Backdrops and the Studio cut-out: colour triplets,
// blurs, "clean plate" colours (normalised convolution), hair the person
// mask missed, and stray islands. Internal to the engine.

#include <algorithm>
#include <array>
#include <cmath>

#include "../portrait/common.hpp"
#include "photoplus/plane.hpp"

namespace photoplus::matte {

using namespace portrait::detail;

struct V3 {
  float r = 0, g = 0, b = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline V3 operator-(V3 a, V3 b) { return {a.r - b.r, a.g - b.g, a.b - b.b}; }
inline V3 operator*(V3 a, V3 b) { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
inline V3 operator*(V3 a, float k) { return {a.r * k, a.g * k, a.b * k}; }
inline V3 mix(V3 a, V3 b, float t) { return a + (b - a) * t; }
inline float lum(V3 c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
inline V3 grey(float v) { return {v, v, v}; }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline V3 lin(float r, float g, float b) { return {toLinear(r), toLinear(g), toLinear(b)}; }
// Adding light without clipping.
inline V3 screen(V3 a, V3 b) { return {a.r + b.r * (1 - a.r), a.g + b.g * (1 - a.g), a.b + b.b * (1 - a.b)}; }

struct Rgb3 {
  Plane r, g, b;
  V3 sample(float x, float y) const { return {r.sample(x, y), g.sample(x, y), b.sample(x, y)}; }
  V3 at(size_t i) const { return {r.data()[i], g.data()[i], b.data()[i]}; }
  void set(size_t i, V3 c) { r.data()[i] = c.r, g.data()[i] = c.g, b.data()[i] = c.b; }
};

Result<Rgb3> allocRgb(int32_t w, int32_t h);
// A Gaussian along one axis only (three running boxes).
Result<Plane> blurredAlong(const Plane& src, float sigma, bool horizontal, const CancellationToken* token);
// A Gaussian (running boxes for wide ones).
Result<Plane> blurred(const Plane& src, float sigma, const CancellationToken* token);
// Normalised convolution: the colour of `src` where `weight` is high,
// spread smoothly over the frame (axis 1 / 2: along rows / columns only).
// Far from any weighted pixel, `fallbackPlate` if given, else `fallback`.
Result<Rgb3> weightedPlate(const Rgb3& src, const Plane& weight, float sigma, V3 fallback, const CancellationToken* token,
                           int axis = 0, const Rgb3* fallbackPlate = nullptr);
// The background without the subject: blur(I·(1-a)) / blur(1-a).
Result<Rgb3> cleanPlate(const Rgb3& low, const Plane& alpha, float sigma, V3 fallback, const CancellationToken* token);
// Hair (and notches at the crown) the person mask missed, recovered by
// colour and texture around the silhouette; `gam` is the photo in gamma.
Error recoverHair(const Rgb3& gam, Plane& mask, const CancellationToken* token);
// Patches of "person" not connected to anyone are dropped.
Error dropIslands(Plane& mask, const CancellationToken* token);

}  // namespace photoplus::matte
