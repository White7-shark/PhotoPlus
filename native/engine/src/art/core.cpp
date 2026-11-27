#include "core.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "photoplus/parallel.hpp"

namespace photoplus::art::core {

namespace {

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// sRGB <-> linear, through tables (colour work runs per pixel).
struct SrgbLut {
  std::array<float, 4097> toLinear{};
  SrgbLut() {
    for (int i = 0; i <= 4096; ++i) {
      const float s = static_cast<float>(i) / 4096.0f;
      toLinear[static_cast<size_t>(i)] = s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    }
  }
};
const SrgbLut& lut() {
  static const SrgbLut l;
  return l;
}
inline float linearOf(float s) { return lut().toLinear[static_cast<size_t>(clampi(static_cast<int>(clamp01(s) * 4096.0f + 0.5f), 0, 4096))]; }
inline float srgbOf(float l) {
  l = std::max(0.0f, l);
  return l <= 0.0031308f ? 12.92f * l : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
}

void gaussPass(const Img& src, Img& dst, float sigma, bool horizontal, const CancellationToken* token) {
  const int r = std::max(1, static_cast<int>(std::ceil(sigma * 3.0f)));
  std::vector<float> k(static_cast<size_t>(2 * r + 1));
  float sum = 0;
  for (int i = -r; i <= r; ++i) {
    k[static_cast<size_t>(i + r)] = std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma));
    sum += k[static_cast<size_t>(i + r)];
  }
  for (float& v : k) v /= sum;
  const int c = src.c;
  if (horizontal) {
    parallelFor(src.h, token, [&](int32_t y0, int32_t y1) {
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < src.w; ++x) {
          float acc[3] = {0, 0, 0};
          for (int i = -r; i <= r; ++i) {
            const float* p = src.px(clampi(x + i, 0, src.w - 1), y);
            const float kw = k[static_cast<size_t>(i + r)];
            for (int ch = 0; ch < c; ++ch) acc[ch] += p[ch] * kw;
          }
          float* o = dst.px(x, y);
          for (int ch = 0; ch < c; ++ch) o[ch] = acc[ch];
        }
    });
  } else {
    parallelFor(src.h, token, [&](int32_t y0, int32_t y1) {
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < src.w; ++x) {
          float acc[3] = {0, 0, 0};
          for (int i = -r; i <= r; ++i) {
            const float* p = src.px(x, clampi(y + i, 0, src.h - 1));
            const float kw = k[static_cast<size_t>(i + r)];
            for (int ch = 0; ch < c; ++ch) acc[ch] += p[ch] * kw;
          }
          float* o = dst.px(x, y);
          for (int ch = 0; ch < c; ++ch) o[ch] = acc[ch];
        }
    });
  }
}

}  // namespace

float Img::sample(float x, float y, int k) const {
  x = std::clamp(x, 0.0f, static_cast<float>(w - 1));
  y = std::clamp(y, 0.0f, static_cast<float>(h - 1));
  const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
  const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
  const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
  const float a = px(x0, y0)[k], b = px(x1, y0)[k], cc = px(x0, y1)[k], dd = px(x1, y1)[k];
  return mix(mix(a, b, fx), mix(cc, dd, fx), fy);
}

void Img::sample3(float x, float y, float out[3]) const {
  for (int k = 0; k < 3; ++k) out[k] = sample(x, y, k);
}

float valueNoise(float x, float y, uint32_t seed) {
  const float fx = std::floor(x), fy = std::floor(y);
  const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
  float tx = x - fx, ty = y - fy;
  tx = tx * tx * (3 - 2 * tx);
  ty = ty * ty * (3 - 2 * ty);
  const float a = hash01(ix, iy, seed), b = hash01(ix + 1, iy, seed), c = hash01(ix, iy + 1, seed), d = hash01(ix + 1, iy + 1, seed);
  return mix(mix(a, b, tx), mix(c, d, tx), ty);
}

float fbm(float x, float y, uint32_t seed, int octaves) {
  float sum = 0, amp = 0.5f, norm = 0;
  for (int o = 0; o < octaves; ++o) {
    sum += amp * valueNoise(x, y, seed + static_cast<uint32_t>(o) * 101U);
    norm += amp;
    x *= 2.03f;
    y *= 2.03f;
    amp *= 0.5f;
  }
  return sum / norm;
}

float noise1(float x, uint32_t seed) {
  const float fx = std::floor(x);
  const int ix = static_cast<int>(fx);
  float t = x - fx;
  t = t * t * (3 - 2 * t);
  return mix(hash01(ix, 0, seed), hash01(ix + 1, 0, seed), t);
}

Img down2(const Img& src) {
  Img out(std::max(1, src.w / 2), std::max(1, src.h / 2), src.c);
  parallelFor(out.h, nullptr, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < out.w; ++x) {
        float* o = out.px(x, y);
        const int sx = std::min(2 * x, src.w - 1), sy = std::min(2 * y, src.h - 1);
        const int sx1 = std::min(sx + 1, src.w - 1), sy1 = std::min(sy + 1, src.h - 1);
        for (int k = 0; k < src.c; ++k)
          o[k] = 0.25f * (src.px(sx, sy)[k] + src.px(sx1, sy)[k] + src.px(sx, sy1)[k] + src.px(sx1, sy1)[k]);
      }
  });
  return out;
}

Img resize(const Img& src, int w, int h) {
  if (w == src.w && h == src.h) return src;
  const Img* s = &src;
  Img tmp;
  while (s->w > 2 * w && s->h > 2 * h) {
    tmp = down2(*s);
    s = &tmp;
  }
  Img out(w, h, src.c);
  const float sx = static_cast<float>(s->w) / static_cast<float>(w), sy = static_cast<float>(s->h) / static_cast<float>(h);
  parallelFor(h, nullptr, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        float* o = out.px(x, y);
        const float fx = (static_cast<float>(x) + 0.5f) * sx - 0.5f, fy = (static_cast<float>(y) + 0.5f) * sy - 0.5f;
        for (int k = 0; k < src.c; ++k) o[k] = s->sample(fx, fy, k);
      }
  });
  return out;
}

Img blur(const Img& src, float sigma, const CancellationToken* token) {
  if (sigma < 0.35f) return src;
  if (sigma > 3.0f && src.w > 16 && src.h > 16) {
    const Img small = blur(down2(src), sigma * 0.5f, token);
    return resize(small, src.w, src.h);
  }
  Img tmp(src.w, src.h, src.c), out(src.w, src.h, src.c);
  gaussPass(src, tmp, sigma, true, token);
  gaussPass(tmp, out, sigma, false, token);
  return out;
}

Img lumaOf(const Img& rgb) {
  Img out(rgb.w, rgb.h, 1);
  for (size_t i = 0, n = static_cast<size_t>(rgb.w) * static_cast<size_t>(rgb.h); i < n; ++i) out.d[i] = luma(&rgb.d[i * 3]);
  return out;
}

Rgb3 hex(uint32_t rgb) {
  return {static_cast<float>((rgb >> 16) & 255) / 255.0f, static_cast<float>((rgb >> 8) & 255) / 255.0f,
          static_cast<float>(rgb & 255) / 255.0f};
}

void rgbToLab(const float* rgb, float* lab) {
  const float r = linearOf(rgb[0]), g = linearOf(rgb[1]), b = linearOf(rgb[2]);
  float X = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f;
  float Y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
  float Z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f;
  auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
  X = f(X), Y = f(Y), Z = f(Z);
  lab[0] = 116.0f * Y - 16.0f;
  lab[1] = 500.0f * (X - Y);
  lab[2] = 200.0f * (Y - Z);
}

void labToRgb(const float* lab, float* rgb) {
  const float fy = (lab[0] + 16.0f) / 116.0f, fx = fy + lab[1] / 500.0f, fz = fy - lab[2] / 200.0f;
  auto inv = [](float t) { return t * t * t > 0.008856f ? t * t * t : (t - 16.0f / 116.0f) / 7.787f; };
  const float X = inv(fx) * 0.95047f, Y = inv(fy), Z = inv(fz) * 1.08883f;
  rgb[0] = clamp01(srgbOf(3.2406f * X - 1.5372f * Y - 0.4986f * Z));
  rgb[1] = clamp01(srgbOf(-0.9689f * X + 1.8758f * Y + 0.0415f * Z));
  rgb[2] = clamp01(srgbOf(0.0557f * X - 0.2040f * Y + 1.0570f * Z));
}

void rgbToHsv(const float* rgb, float* hsv) {
  const float mx = std::max({rgb[0], rgb[1], rgb[2]}), mn = std::min({rgb[0], rgb[1], rgb[2]}), d = mx - mn;
  float h = 0;
  if (d > 1e-6f) {
    if (mx == rgb[0]) h = std::fmod((rgb[1] - rgb[2]) / d + 6.0f, 6.0f);
    else if (mx == rgb[1]) h = (rgb[2] - rgb[0]) / d + 2.0f;
    else h = (rgb[0] - rgb[1]) / d + 4.0f;
  }
  hsv[0] = h / 6.0f;
  hsv[1] = mx > 1e-6f ? d / mx : 0.0f;
  hsv[2] = mx;
}

void hsvToRgb(const float* hsv, float* rgb) {
  const float h = (hsv[0] - std::floor(hsv[0])) * 6.0f, s = clamp01(hsv[1]), v = clamp01(hsv[2]);
  const int i = static_cast<int>(h) % 6;
  const float f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  const float c[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
  rgb[0] = c[i][0], rgb[1] = c[i][1], rgb[2] = c[i][2];
}

void grade(Img& rgb, const Grade& g, const CancellationToken* token) {
  parallelFor(rgb.h, token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < rgb.w; ++x) {
        float* p = rgb.px(x, y);
        for (int k = 0; k < 3; ++k) {
          float v = p[k];
          v = g.lift + (g.gain - g.lift) * v;
          v = std::pow(clamp01(v), g.gamma);
          if (g.contrast != 0) {
            const float s = v * v * (3 - 2 * v);
            v = mix(v, s, g.contrast);
          }
          p[k] = v;
        }
        const float l = luma(p);
        for (int k = 0; k < 3; ++k) p[k] = clamp01(l + (p[k] - l) * g.saturation);
        if (g.warmth != 0) {
          p[0] = clamp01(p[0] * (1 + 0.10f * g.warmth));
          p[2] = clamp01(p[2] * (1 - 0.14f * g.warmth));
        }
        if (g.tintStrength > 0) {
          const float sh = 1 - smoothstep(0.0f, 0.55f, l), hi = smoothstep(0.45f, 1.0f, l);
          for (int k = 0; k < 3; ++k)
            p[k] = clamp01(p[k] + g.tintStrength * (sh * (g.shadowTint[static_cast<size_t>(k)] - 0.5f) + hi * (g.highlightTint[static_cast<size_t>(k)] - 0.5f)));
        }
      }
  });
}

void palette(Img& rgb, const std::vector<Rgb3>& colours, float strength, float lumaKeep, const CancellationToken* token) {
  if (colours.empty() || strength <= 0) return;
  std::vector<std::array<float, 3>> labs(colours.size());
  for (size_t i = 0; i < colours.size(); ++i) rgbToLab(colours[i].data(), labs[i].data());
  parallelFor(rgb.h, token, [&](int32_t y0, int32_t y1) {
    std::vector<float> w(colours.size());
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < rgb.w; ++x) {
        float* p = rgb.px(x, y);
        float lab[3];
        rgbToLab(p, lab);
        float sum = 0, out[3] = {0, 0, 0};
        float best = 1e9f;
        for (size_t i = 0; i < labs.size(); ++i) {
          const float dl = (lab[0] - labs[i][0]) * 0.6f, da = lab[1] - labs[i][1], db = lab[2] - labs[i][2];
          best = std::min(best, dl * dl + da * da + db * db);
          w[i] = dl * dl + da * da + db * db;
        }
        for (size_t i = 0; i < labs.size(); ++i) {
          const float wi = std::exp(-(w[i] - best) / 350.0f);
          sum += wi;
          for (int k = 0; k < 3; ++k) out[k] += wi * labs[i][static_cast<size_t>(k)];
        }
        for (float& v : out) v /= sum;
        out[0] = mix(out[0], lab[0], lumaKeep);
        float mapped[3];
        labToRgb(out, mapped);
        for (int k = 0; k < 3; ++k) p[k] = mix(p[k], mapped[k], strength);
      }
  });
}

void Field::at(float x, float y, float& ox, float& oy) const {
  const int ix = clampi(static_cast<int>(x), 0, w - 1), iy = clampi(static_cast<int>(y), 0, h - 1);
  const size_t i = static_cast<size_t>(iy) * static_cast<size_t>(w) + static_cast<size_t>(ix);
  ox = tx[i], oy = ty[i];
}

float Field::coherent(float x, float y) const {
  const int ix = clampi(static_cast<int>(x), 0, w - 1), iy = clampi(static_cast<int>(y), 0, h - 1);
  return coherence[static_cast<size_t>(iy) * static_cast<size_t>(w) + static_cast<size_t>(ix)];
}

Field orientation(const Img& rgb, float sigmaGrad, float sigmaTensor, const CancellationToken* token) {
  const Img l = blur(lumaOf(rgb), sigmaGrad, token);
  Img t(rgb.w, rgb.h, 3);
  for (int y = 0; y < rgb.h; ++y)
    for (int x = 0; x < rgb.w; ++x) {
      auto L = [&](int dx, int dy) { return l.at(clampi(x + dx, 0, l.w - 1), clampi(y + dy, 0, l.h - 1)); };
      const float gx = (L(1, -1) + 2 * L(1, 0) + L(1, 1)) - (L(-1, -1) + 2 * L(-1, 0) + L(-1, 1));
      const float gy = (L(-1, 1) + 2 * L(0, 1) + L(1, 1)) - (L(-1, -1) + 2 * L(0, -1) + L(1, -1));
      float* p = t.px(x, y);
      p[0] = gx * gx, p[1] = gx * gy, p[2] = gy * gy;
    }
  const Img s = blur(t, sigmaTensor, token);
  Field f;
  f.w = rgb.w, f.h = rgb.h;
  const size_t n = static_cast<size_t>(rgb.w) * static_cast<size_t>(rgb.h);
  f.tx.resize(n), f.ty.resize(n), f.coherence.resize(n);
  for (size_t i = 0; i < n; ++i) {
    const float E = s.d[i * 3], F = s.d[i * 3 + 1], G = s.d[i * 3 + 2];
    const float phi = 0.5f * std::atan2(2 * F, E - G);  // the gradient's direction
    f.tx[i] = -std::sin(phi), f.ty[i] = std::cos(phi);  // along the forms
    const float tr = E + G;
    f.coherence[i] = tr > 1e-7f ? std::sqrt((E - G) * (E - G) + 4 * F * F) / tr : 0.0f;
  }
  return f;
}

void swirl(Field& f, float scale, float amount, uint32_t seed) {
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) {
      const size_t i = static_cast<size_t>(y) * static_cast<size_t>(f.w) + static_cast<size_t>(x);
      const float fx = static_cast<float>(x) / scale, fy = static_cast<float>(y) / scale, e = 0.02f;
      const float dx = fbm(fx + e, fy, seed, 3) - fbm(fx - e, fy, seed, 3);
      const float dy = fbm(fx, fy + e, seed, 3) - fbm(fx, fy - e, seed, 3);
      float cx = dy, cy = -dx;  // curl: flowing around the noise's hills
      const float len = std::hypot(cx, cy);
      if (len < 1e-7f) continue;
      cx /= len, cy /= len;
      float tx = f.tx[i], ty = f.ty[i];
      if (tx * cx + ty * cy < 0) tx = -tx, ty = -ty;
      const float w = amount * (1 - f.coherence[i]);
      const float ox = mix(tx, cx, w), oy = mix(ty, cy, w), ol = std::hypot(ox, oy);
      if (ol > 1e-6f) f.tx[i] = ox / ol, f.ty[i] = oy / ol;
    }
}

Img kuwahara(const Img& rgb, int radius, const CancellationToken* token) {
  constexpr int kSectors = 8;
  struct Tap {
    int dx, dy;
    float w[kSectors];
  };
  std::vector<Tap> taps;
  const float r = static_cast<float>(radius);
  for (int dy = -radius; dy <= radius; ++dy)
    for (int dx = -radius; dx <= radius; ++dx) {
      const float d2 = static_cast<float>(dx * dx + dy * dy);
      if (d2 > r * r) continue;
      Tap t{dx, dy, {}};
      const float radial = std::exp(-d2 / (2.0f * (r * 0.5f) * (r * 0.5f)));
      const float ang = std::atan2(static_cast<float>(dy), static_cast<float>(dx));
      for (int s = 0; s < kSectors; ++s) {
        if (dx == 0 && dy == 0) {
          t.w[s] = radial / kSectors;
          continue;
        }
        float da = std::fabs(std::remainder(ang - static_cast<float>(s) * 2.0f * kPi / kSectors, 2.0f * kPi));
        const float sw = std::max(0.0f, 1.0f - da / (kPi / kSectors * 1.5f));
        t.w[s] = radial * sw * sw;
      }
      taps.push_back(t);
    }
  Img out(rgb.w, rgb.h, 3);
  parallelFor(rgb.h, token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < rgb.w; ++x) {
        float sw[kSectors] = {}, m[kSectors][3] = {}, q[kSectors][3] = {};
        for (const Tap& t : taps) {
          const float* p = rgb.px(clampi(x + t.dx, 0, rgb.w - 1), clampi(y + t.dy, 0, rgb.h - 1));
          for (int s = 0; s < kSectors; ++s) {
            const float w = t.w[s];
            if (w <= 0) continue;
            sw[s] += w;
            for (int k = 0; k < 3; ++k) m[s][k] += w * p[k], q[s][k] += w * p[k] * p[k];
          }
        }
        float total = 0, acc[3] = {0, 0, 0};
        for (int s = 0; s < kSectors; ++s) {
          if (sw[s] <= 0) continue;
          float var = 0;
          for (int k = 0; k < 3; ++k) {
            const float mean = m[s][k] / sw[s];
            var += std::max(0.0f, q[s][k] / sw[s] - mean * mean);
          }
          const float v = var * 255.0f * 255.0f;
          const float a = 1.0f / (1.0f + v * v * v * v * 1e-8f);
          total += a;
          for (int k = 0; k < 3; ++k) acc[k] += a * m[s][k] / sw[s];
        }
        float* o = out.px(x, y);
        for (int k = 0; k < 3; ++k) o[k] = total > 0 ? acc[k] / total : rgb.px(x, y)[k];
      }
  });
  return out;
}

Img lines(const Img& l, float sigma, float k, float threshold, float softness, const CancellationToken* token) {
  const Img a = blur(l, sigma, token), b = blur(l, sigma * k, token);
  Img out(l.w, l.h, 1);
  for (size_t i = 0; i < out.d.size(); ++i) out.d[i] = smoothstep(threshold, threshold + softness, b.d[i] - a.d[i]);
  return out;
}

namespace {

struct Stroke {
  std::vector<float> x, y, cum;  // the path and its length so far
  float len = 0;
  float r = 1;
  float col[3] = {0, 0, 0};
  float thick = 0;
  uint32_t seed = 0;
  float minX = 0, minY = 0, maxX = 0, maxY = 0;
};

float colourDiff(const float* a, const float* b) {
  const float dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
  return std::sqrt(0.3f * dr * dr + 0.55f * dg * dg + 0.15f * db * db);
}

}  // namespace

bool strokeLayer(Canvas& canvas, const Img& ref, const Field& field, const Brush& br, uint32_t seed, const Img* where,
                 const CancellationToken* token) {
  const int W = ref.w, H = ref.h;
  const float R = std::max(0.6f, br.radius);
  const Img refB = br.blur > 0 ? blur(ref, std::max(0.5f, br.blur * R), token) : ref;
  if (isCancelled(token)) return false;
  const int cell = std::max(1, static_cast<int>(R * br.grid));
  const int cols = (W + cell - 1) / cell, rows = (H + cell - 1) / cell;
  const bool first = canvas.empty;
  std::vector<std::vector<Stroke>> perRow(static_cast<size_t>(rows));

  // Where to put strokes, and the strokes themselves (each cell on its own, in parallel).
  if (!parallelFor(rows, token, [&](int32_t r0, int32_t r1) {
        for (int cy = r0; cy < r1; ++cy) {
          auto& out = perRow[static_cast<size_t>(cy)];
          for (int cx = 0; cx < cols; ++cx) {
            const int x0 = cx * cell, y0 = cy * cell, x1 = std::min(W, x0 + cell), y1 = std::min(H, y0 + cell);
            const uint32_t cs = hash2(cx, cy, seed);
            Rng rng(cs);
            if (where != nullptr && where->sample(0.5f * static_cast<float>(x0 + x1), 0.5f * static_cast<float>(y0 + y1)) < 0.25f) continue;
            float px = 0, py = 0;
            if (first || br.mode == StrokeMode::Dot) {
              px = static_cast<float>(x0) + rng.uni() * static_cast<float>(x1 - x0);
              py = static_cast<float>(y0) + rng.uni() * static_cast<float>(y1 - y0);
              if (!first) {
                const float e = colourDiff(canvas.rgb.px(static_cast<int>(px), static_cast<int>(py)), refB.px(static_cast<int>(px), static_cast<int>(py)));
                if (e < br.threshold) continue;
              }
            } else {
              const int step = std::max(1, cell / 5);
              float sum = 0, best = -1;
              int n = 0;
              for (int y = y0; y < y1; y += step)
                for (int x = x0; x < x1; x += step) {
                  const float e = colourDiff(canvas.rgb.px(x, y), refB.px(x, y));
                  sum += e, ++n;
                  if (e > best) best = e, px = static_cast<float>(x), py = static_cast<float>(y);
                }
              if (n == 0 || sum / static_cast<float>(n) < br.threshold) continue;
              px += rng.range(-0.5f, 0.5f) * static_cast<float>(step), py += rng.range(-0.5f, 0.5f) * static_cast<float>(step);
              px = std::clamp(px, 0.0f, static_cast<float>(W - 1)), py = std::clamp(py, 0.0f, static_cast<float>(H - 1));
            }
            Stroke s;
            s.seed = cs;
            s.r = R * rng.range(0.85f, 1.15f);
            refB.sample3(px, py, s.col);
            if (br.mode == StrokeMode::Dot && br.dotPalette != nullptr && !br.dotPalette->empty()) {
              // Pure pigments mixed in the eye: the nearest few, by chance.
              const auto& pal = *br.dotPalette;
              std::vector<std::pair<float, size_t>> d;
              for (size_t i = 0; i < pal.size(); ++i) d.push_back({colourDiff(s.col, pal[i].data()), i});
              std::sort(d.begin(), d.end());
              const float u = rng.uni();
              const size_t pick = d[std::min(d.size() - 1, static_cast<size_t>(u < 0.5f ? 0 : u < 0.78f ? 1 : u < 0.93f ? 2 : 3))].second;
              const float l = luma(s.col);
              float c[3] = {pal[pick][0], pal[pick][1], pal[pick][2]};
              const float lc = luma(c);
              for (int k = 0; k < 3; ++k) s.col[k] = clamp01(mix(c[k] * (lc > 1e-3f ? std::sqrt(l / lc) : 1.0f), s.col[k], 0.15f));
            }
            if (br.hueJitter > 0 || br.satJitter > 0 || br.valJitter > 0) {
              float hsv[3];
              rgbToHsv(s.col, hsv);
              hsv[0] += br.hueJitter * rng.range(-1, 1);
              hsv[1] = clamp01(hsv[1] * (1 + br.satJitter * rng.range(-1, 1)));
              hsv[2] = clamp01(hsv[2] * (1 + br.valJitter * rng.range(-1, 1)));
              hsvToRgb(hsv, s.col);
            }
            s.thick = br.thick + br.thickLights * luma(s.col) * luma(s.col);
            s.x.push_back(px), s.y.push_back(py);
            if (br.mode != StrokeMode::Dot) {
              float dx = 0, dy = 0;
              if (br.mode == StrokeMode::Fixed) {
                const float a = br.angle + br.angleJitter * rng.range(-1, 1);
                dx = std::cos(a), dy = std::sin(a);
              } else {
                field.at(px, py, dx, dy);
                if (br.mode == StrokeMode::Dab) {
                  const float a = std::atan2(dy, dx) + rng.range(-0.7f, 0.7f);
                  dx = std::cos(a), dy = std::sin(a);
                }
              }
              if (rng.uni() < 0.5f) dx = -dx, dy = -dy;
              const int maxSteps = std::max(1, static_cast<int>(br.maxLen / br.step + 0.5f));
              const int minSteps = std::max(0, static_cast<int>(br.minLen / br.step + 0.5f));
              float pdx = dx, pdy = dy, x = px, y = py;
              for (int i = 0; i < maxSteps; ++i) {
                if (br.mode == StrokeMode::Flow && i > 0) {
                  float fx = 0, fy = 0;
                  field.at(x, y, fx, fy);
                  if (fx * pdx + fy * pdy < 0) fx = -fx, fy = -fy;
                  // Bend with the form only where it has a direction; in flat areas (skies, walls) keep going straight.
                  const float bend = br.curvature * smoothstep(0.05f, 0.35f, field.coherent(x, y));
                  dx = mix(pdx, fx, bend), dy = mix(pdy, fy, bend);
                  const float l = std::hypot(dx, dy);
                  if (l < 1e-6f) break;
                  dx /= l, dy /= l;
                }
                const float nx = x + dx * br.step * s.r, ny = y + dy * br.step * s.r;
                if (nx < 0 || ny < 0 || nx > static_cast<float>(W - 1) || ny > static_cast<float>(H - 1)) break;
                if (i >= minSteps) {
                  // Hertzmann: stop where the reference no longer matches this stroke's colour better than the canvas does.
                  float rc[3];
                  refB.sample3(nx, ny, rc);
                  const float* cc = canvas.rgb.px(static_cast<int>(nx), static_cast<int>(ny));
                  if (!first && colourDiff(rc, s.col) > colourDiff(cc, s.col)) break;
                  if (first && colourDiff(rc, s.col) > br.threshold * 2.5f) break;
                }
                x = nx, y = ny, pdx = dx, pdy = dy;
                s.x.push_back(x), s.y.push_back(y);
              }
            }
            s.cum.assign(s.x.size(), 0.0f);
            for (size_t i = 1; i < s.x.size(); ++i) s.cum[i] = s.cum[i - 1] + std::hypot(s.x[i] - s.x[i - 1], s.y[i] - s.y[i - 1]);
            s.len = s.cum.back();
            s.minX = *std::min_element(s.x.begin(), s.x.end()) - s.r - 1, s.maxX = *std::max_element(s.x.begin(), s.x.end()) + s.r + 1;
            s.minY = *std::min_element(s.y.begin(), s.y.end()) - s.r - 1, s.maxY = *std::max_element(s.y.begin(), s.y.end()) + s.r + 1;
            out.push_back(std::move(s));
          }
        }
      }))
    return false;

  std::vector<Stroke> strokes;
  for (auto& r : perRow)
    for (auto& s : r) strokes.push_back(std::move(s));
  // A painter's order within a layer: no grid, no sweep.
  std::vector<uint32_t> order(strokes.size());
  std::iota(order.begin(), order.end(), 0U);
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return hash32(a ^ seed) < hash32(b ^ seed); });

  // Rendered by tiles in parallel, each tile drawing its strokes in the same order.
  constexpr int kTile = 96;
  const int tx = (W + kTile - 1) / kTile, ty = (H + kTile - 1) / kTile;
  std::vector<std::vector<uint32_t>> tiles(static_cast<size_t>(tx * ty));
  for (uint32_t o : order) {
    const Stroke& s = strokes[o];
    const int a0 = clampi(static_cast<int>(s.minX) / kTile, 0, tx - 1), a1 = clampi(static_cast<int>(s.maxX) / kTile, 0, tx - 1);
    const int b0 = clampi(static_cast<int>(s.minY) / kTile, 0, ty - 1), b1 = clampi(static_cast<int>(s.maxY) / kTile, 0, ty - 1);
    for (int b = b0; b <= b1; ++b)
      for (int a = a0; a <= a1; ++a) tiles[static_cast<size_t>(b * tx + a)].push_back(o);
  }
  const bool hasTooth = !canvas.tooth.d.empty() && br.tooth > 0;
  const bool ok = parallelFor(tx * ty, token, [&](int32_t t0, int32_t t1) {
    for (int t = t0; t < t1; ++t) {
      const int bx = (t % tx) * kTile, by = (t / tx) * kTile;
      const int ex = std::min(W, bx + kTile), ey = std::min(H, by + kTile);
      for (uint32_t o : tiles[static_cast<size_t>(t)]) {
        const Stroke& s = strokes[o];
        const int x0 = std::max(bx, static_cast<int>(s.minX)), x1 = std::min(ex - 1, static_cast<int>(s.maxX));
        const int y0 = std::max(by, static_cast<int>(s.minY)), y1 = std::min(ey - 1, static_cast<int>(s.maxY));
        const size_t n = s.x.size();
        const float bristleSeed = static_cast<float>(s.seed % 1000U);
        for (int y = y0; y <= y1; ++y)
          for (int x = x0; x <= x1; ++x) {
            const float fx = static_cast<float>(x), fy = static_cast<float>(y);
            float best = 1e18f, u = 0, sgn = 1;
            if (n == 1) {
              best = (fx - s.x[0]) * (fx - s.x[0]) + (fy - s.y[0]) * (fy - s.y[0]);
            } else {
              for (size_t i = 0; i + 1 < n; ++i) {
                const float ax = s.x[i], ay = s.y[i], dx = s.x[i + 1] - ax, dy = s.y[i + 1] - ay;
                const float l2 = dx * dx + dy * dy;
                const float tt = l2 > 1e-9f ? std::clamp(((fx - ax) * dx + (fy - ay) * dy) / l2, 0.0f, 1.0f) : 0.0f;
                const float qx = ax + tt * dx - fx, qy = ay + tt * dy - fy, dd = qx * qx + qy * qy;
                if (dd < best) {
                  best = dd;
                  u = s.cum[i] + tt * std::sqrt(l2);
                  sgn = dx * (fy - ay) - dy * (fx - ax) >= 0 ? 1.0f : -1.0f;
                }
              }
            }
            const float along = s.len > 1e-3f ? u / s.len : 0.5f;
            const float e = std::fabs(2 * along - 1);
            const float rr = s.r * (1 - br.taper * e * e * e);
            const float d = std::sqrt(best);
            if (d >= rr) continue;
            const float v = sgn * d / rr;
            float a = (1 - smoothstep(1 - br.softness, 1.0f, d / rr)) * br.opacity;
            // Bristle streaks along the stroke, breaking up at the tail as the brush runs dry.
            const float bristle = n == 1 ? hash01(x, y, s.seed) * 0.6f + 0.4f * noise1(v * br.bristleFreq + bristleSeed, s.seed)
                                         : noise1(v * br.bristleFreq + bristleSeed, s.seed);
            a *= 1 - br.dryness * smoothstep(0.5f, 1.0f, along) * (1 - smoothstep(0.3f, 0.6f, bristle));
            if (hasTooth) a *= smoothstep(br.tooth * 0.7f, br.tooth * 0.7f + 0.35f, canvas.tooth.at(x, y));
            if (a <= 0.002f) continue;
            float* p = canvas.rgb.px(x, y);
            const float shade = 1 + br.bristleAmp * (bristle - 0.5f) * 2;
            for (int k = 0; k < 3; ++k) p[k] = mix(p[k], clamp01(s.col[k] * shade), a);
            if (s.thick > 0) {
              const float av = std::fabs(v);
              const float target = s.thick * (0.75f + 0.25f * bristle) + br.ridge * s.thick * smoothstep(0.55f, 0.85f, av) * (1 - smoothstep(0.85f, 1.0f, av));
              float& hgt = canvas.height.at(x, y);
              hgt = hgt * (1 - 0.55f * a) + a * target;
            }
          }
      }
    }
  });
  canvas.empty = false;
  return ok;
}

void canvasWeave(Img& height, float p, float amount, uint32_t seed) {
  for (int y = 0; y < height.h; ++y)
    for (int x = 0; x < height.w; ++x) {
      const float fx = static_cast<float>(x) / p, fy = static_cast<float>(y) / p;
      const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
      const float wx = std::sin(kPi * (fx - std::floor(fx))), wy = std::sin(kPi * (fy - std::floor(fy)));
      // Over-under: warp and weft take turns on top.
      const float w = ((ix + iy) & 1) ? wy : wx;
      const float irregular = 0.75f + 0.5f * valueNoise(fx * 0.37f, fy * 0.41f, seed) + 0.15f * (hash01(ix, iy, seed) - 0.5f);
      height.at(x, y) += amount * w * irregular;
    }
}

void woodPanel(Img& height, float amount, uint32_t seed) {
  for (int y = 0; y < height.h; ++y)
    for (int x = 0; x < height.w; ++x) {
      const float fx = static_cast<float>(x), fy = static_cast<float>(y);
      height.at(x, y) += amount * (fbm(fx * 0.004f, fy * 0.06f, seed, 3) - 0.5f + 0.4f * (valueNoise(fx * 0.05f, fy * 0.9f, seed + 7) - 0.5f));
    }
}

void lightRelief(Img& rgb, const Img& height, float strength, float gloss, const CancellationToken* token) {
  const float lx = -0.55f, ly = -0.6f, lz = 0.58f;
  parallelFor(rgb.h, token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < rgb.w; ++x) {
        const float hx = height.at(std::min(x + 1, rgb.w - 1), y) - height.at(std::max(x - 1, 0), y);
        const float hy = height.at(x, std::min(y + 1, rgb.h - 1)) - height.at(x, std::max(y - 1, 0));
        float nx = -hx * 2.2f, ny = -hy * 2.2f, nz = 1;
        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= nl, ny /= nl, nz /= nl;
        const float diff = (nx * lx + ny * ly + nz * lz) / lz - 1;
        // Glints where a ridge faces the light (reflection towards the viewer).
        const float rz = 2 * (nx * lx + ny * ly + nz * lz) * nz - lz;
        const float spec = gloss * std::pow(std::max(0.0f, rz), 24.0f);
        float* p = rgb.px(x, y);
        for (int k = 0; k < 3; ++k) p[k] = clamp01(p[k] * (1 + strength * diff) + spec);
      }
  });
}

void varnish(Img& rgb, float a) {
  for (size_t i = 0; i < rgb.d.size(); i += 3) {
    float* p = &rgb.d[i];
    const float l = luma(p);
    p[0] = clamp01(p[0] * (1 - 0.03f * a) * (1 - 0.06f * a) + 0.015f * a * (1 - l));
    p[1] = clamp01(p[1] * (1 - 0.08f * a) * (1 - 0.06f * a) + 0.01f * a * (1 - l));
    p[2] = clamp01(p[2] * (1 - 0.24f * a) * (1 - 0.06f * a));
  }
}

void craquelure(Img& rgb, float cell, float amount, uint32_t seed, const CancellationToken* token) {
  parallelFor(rgb.h, token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < rgb.w; ++x) {
        // Cracks: the borders of irregular (warped) cells, in two sizes.
        float crack = 0;
        for (int level = 0; level < 2; ++level) {
          const float c = cell * (level == 0 ? 1.0f : 0.42f);
          const float wx = static_cast<float>(x) + c * 0.35f * (fbm(static_cast<float>(x) / c * 0.7f, static_cast<float>(y) / c * 0.7f, seed + 3U, 3) - 0.5f);
          const float wy = static_cast<float>(y) + c * 0.35f * (fbm(static_cast<float>(x) / c * 0.7f + 9.1f, static_cast<float>(y) / c * 0.7f, seed + 5U, 3) - 0.5f);
          const float fx = wx / c, fy = wy / c;
          const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
          float d1 = 9, d2 = 9;
          for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i) {
              const float px = static_cast<float>(ix + i) + hash01(ix + i, iy + j, seed + static_cast<uint32_t>(level) * 17U);
              const float py = static_cast<float>(iy + j) + hash01(ix + i, iy + j, seed + 1U + static_cast<uint32_t>(level) * 17U);
              const float d = std::hypot(fx - px, fy - py);
              if (d < d1) d2 = d1, d1 = d;
              else if (d < d2) d2 = d;
            }
          const float edge = (d2 - d1) * c;  // px from the crack
          crack = std::max(crack, (1 - smoothstep(0.15f, 0.75f, edge)) * (level == 0 ? 1.0f : 0.45f));
        }
        if (crack <= 0) continue;
        float* p = rgb.px(x, y);
        const float dark = amount * crack;
        for (int k = 0; k < 3; ++k) p[k] = clamp01(p[k] * (1 - 0.3f * dark) + (k == 0 ? 0.01f : 0.0f) * dark);
      }
  });
}

void vignette(Img& rgb, float amount, Rgb3 colour, float cx, float cy) {
  for (int y = 0; y < rgb.h; ++y)
    for (int x = 0; x < rgb.w; ++x) {
      const float dx = (static_cast<float>(x) / static_cast<float>(rgb.w) - cx) * 1.15f, dy = static_cast<float>(y) / static_cast<float>(rgb.h) - cy;
      const float v = amount * smoothstep(0.25f, 0.85f, std::sqrt(dx * dx + dy * dy));
      float* p = rgb.px(x, y);
      for (int k = 0; k < 3; ++k) p[k] = mix(p[k], colour[static_cast<size_t>(k)], v);
    }
}

Img paper(int w, int h, Rgb3 colour, float grainAmt, bool laid, uint32_t seed, Img* tooth) {
  Img out(w, h, 3);
  if (tooth != nullptr) *tooth = Img(w, h, 1);
  const float chain = static_cast<float>(std::max(w, h)) / 11.0f;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float fx = static_cast<float>(x), fy = static_cast<float>(y);
      float v = 1 + grainAmt * (0.6f * (fbm(fx * 0.012f, fy * 0.012f, seed, 4) - 0.5f) + 0.5f * (valueNoise(fx * 0.25f, fy * 0.04f, seed + 9) - 0.5f) +
                                0.35f * (valueNoise(fx * 0.04f, fy * 0.25f, seed + 13) - 0.5f));
      if (laid) {
        v *= 1 - 0.035f * (0.5f + 0.5f * std::sin(fy * 2 * kPi / 3.2f));
        const float cd = std::fabs(std::remainder(fx, chain));
        v *= 1 - 0.05f * (1 - smoothstep(0.0f, 2.0f, cd));
      }
      float* p = out.px(x, y);
      for (int k = 0; k < 3; ++k) p[k] = clamp01(colour[static_cast<size_t>(k)] * v);
      if (tooth != nullptr) tooth->at(x, y) = clamp01(0.55f + 1.2f * (fbm(fx * 0.12f, fy * 0.12f, seed + 21, 3) - 0.5f));
    }
  return out;
}

void grain(Img& rgb, float amount, uint32_t seed) {
  for (int y = 0; y < rgb.h; ++y)
    for (int x = 0; x < rgb.w; ++x) {
      const float n = amount * (hash01(x, y, seed) - 0.5f);
      float* p = rgb.px(x, y);
      for (int k = 0; k < 3; ++k) p[k] = clamp01(p[k] + n);
    }
}

}  // namespace photoplus::art::core
