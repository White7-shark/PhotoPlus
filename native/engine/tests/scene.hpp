#pragma once

// Synthetic "natural-ish" scenes and a weak-camera simulator shared by the
// enhancement tests and the benchmark tool.

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "photoplus/plane.hpp"

namespace scene {

// Procedural scene with natural-image statistics: 1/f fractal texture,
// anti-aliased object edges, a resolution wedge and a text-like block. All
// content stays within what a real lens can pass (no pixel-level aliasing),
// so PSNR reflects recoverable quality rather than impossible detail.
inline photoplus::Plane makeScene(int w, int h, unsigned seed = 7) {
  auto p = photoplus::Plane::allocate(w, h);
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> U(0.0f, 1.0f);
  // Value-noise octaves on lattices of decreasing cell size, amplitude ~ 1/f.
  const int octaves = 5;
  std::vector<std::vector<float>> lattice(octaves);
  std::vector<int> cells(octaves);
  for (int o = 0; o < octaves; ++o) {
    cells[o] = 4 << o;
    lattice[o].resize(static_cast<size_t>((cells[o] + 2) * (cells[o] + 2)));
    for (float& v : lattice[o]) v = U(rng) - 0.5f;
  }
  auto noiseAt = [&](float fx, float fy) {
    fx -= std::floor(fx);  // tile, keeping lattice lookups in range
    fy -= std::floor(fy);
    float v = 0.0f, amp = 0.5f;
    for (int o = 0; o < octaves; ++o, amp *= 0.55f) {
      const float gx = fx * cells[o], gy = fy * cells[o];
      const int ix = static_cast<int>(gx), iy = static_cast<int>(gy);
      const float tx = gx - ix, ty = gy - iy;
      const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
      const int n = cells[o] + 2;
      auto L = [&](int a, int b) { return lattice[o][static_cast<size_t>(b * n + a)]; };
      const float top = L(ix, iy) + (L(ix + 1, iy) - L(ix, iy)) * sx;
      const float bot = L(ix, iy + 1) + (L(ix + 1, iy + 1) - L(ix, iy + 1)) * sx;
      v += amp * (top + (bot - top) * sy);
    }
    return v;
  };
  struct Disc { float x, y, r, v; };
  std::vector<Disc> discs;
  for (int i = 0; i < 12; ++i) discs.push_back({U(rng) * w, U(rng) * h, 6.0f + U(rng) * w / 9.0f, 0.15f + 0.7f * U(rng)});
  for (int y = 0; y < h; ++y) {
    float* row = p->row(y);
    for (int x = 0; x < w; ++x) {
      const float fx = (x + 0.5f) / w, fy = (y + 0.5f) / h;
      float v = 0.45f + 0.5f * noiseAt(fx, fy) + 0.15f * (fx - 0.5f);
      for (const auto& d : discs) {
        // Anti-aliased edge: coverage ramps over ~1 pixel.
        const float dist = std::sqrt((x + 0.5f - d.x) * (x + 0.5f - d.x) + (y + 0.5f - d.y) * (y + 0.5f - d.y));
        const float cov = std::fmin(1.0f, std::fmax(0.0f, d.r - dist + 0.5f));
        v = v * (1.0f - cov) + (d.v + 0.15f * noiseAt(fx * 3.0f, fy * 3.0f)) * cov;
      }
      // Resolution wedge: stripe frequency rises left to right, capped
      // below Nyquist (0.24 cycles/px peak local frequency, since the phase
      // term doubles it) so detail exists beyond a 2x-smaller sensor.
      if (fy > 0.78f) v = 0.5f + 0.3f * std::sin(6.2831853f * x * (0.04f + 0.20f * fx));
      // Text-like block of 6px glyph cells.
      if (fx > 0.08f && fx < 0.34f && fy > 0.08f && fy < 0.24f) v = (((x / 6) * 7 + (y / 6) * 3) % 5 < 2) ? 0.88f : 0.14f;
      row[x] = std::fmin(1.0f, std::fmax(0.0f, v));
    }
  }
  return std::move(p).value();
}

// Shift by (dx, dy) so that out(x) = src(x + d), then blur, decimate by
// `factor` (box), add Gaussian noise and quantise to 8 bits.
inline photoplus::Plane degrade(const photoplus::Plane& src, float dx, float dy, float blurSigma, int factor,
                                float noise, unsigned seed) {
  const int w = src.width(), h = src.height();
  auto shifted = photoplus::Plane::allocate(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) shifted->row(y)[x] = src.sample(static_cast<float>(x) + dx, static_cast<float>(y) + dy);
  auto blurred = photoplus::Plane::allocate(w, h);
  (void)photoplus::gaussianBlur(*shifted, *blurred, blurSigma, nullptr);
  const int ow = w / factor, oh = h / factor;
  auto out = photoplus::Plane::allocate(ow, oh);
  std::mt19937 rng(seed);
  std::normal_distribution<float> N(0.0f, noise);
  for (int y = 0; y < oh; ++y) {
    for (int x = 0; x < ow; ++x) {
      float acc = 0.0f;
      for (int j = 0; j < factor; ++j)
        for (int i = 0; i < factor; ++i) acc += blurred->row(y * factor + j)[x * factor + i];
      float v = acc / static_cast<float>(factor * factor) + (noise > 0 ? N(rng) : 0.0f);
      v = std::round(std::fmin(1.0f, std::fmax(0.0f, v)) * 255.0f) / 255.0f;
      out->row(y)[x] = v;
    }
  }
  return std::move(out).value();
}

// PSNR in dB over the interior (borders excluded: edge handling differs).
inline double psnr(const photoplus::Plane& a, const photoplus::Plane& b, int border = 8) {
  double se = 0.0;
  size_t n = 0;
  for (int y = border; y < a.height() - border; ++y)
    for (int x = border; x < a.width() - border; ++x) {
      const double d = static_cast<double>(a.row(y)[x]) - static_cast<double>(b.row(y)[x]);
      se += d * d;
      ++n;
    }
  const double mse = se / static_cast<double>(n);
  return 10.0 * std::log10(1.0 / std::fmax(mse, 1e-12));
}

}  // namespace scene
