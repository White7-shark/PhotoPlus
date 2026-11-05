#pragma once

// Small shared helpers for the portrait and style tools.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "photoplus/image.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/status.hpp"

namespace photoplus::portrait::detail {

inline Error ok() { return Error{PP_OK, {}}; }

inline float smoothstep(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

inline bool isColour(const ImageBuffer& img) {
  return img.format() == PixelFormat::RGBA8 || img.format() == PixelFormat::BGRA8;
}

// Channel offsets so BGRA and RGBA are handled alike.
struct Rgb {
  int r, g, b;
};
inline Rgb offsets(const ImageBuffer& img) {
  return img.format() == PixelFormat::BGRA8 ? Rgb{2, 1, 0} : Rgb{0, 1, 2};
}

// BT.601 full range, matching toYcc()/fromYcc().
inline void rgbToYcc(float r, float g, float b, float& y, float& cb, float& cr) {
  y = 0.299f * r + 0.587f * g + 0.114f * b;
  cb = -0.168736f * r - 0.331264f * g + 0.5f * b;
  cr = 0.5f * r - 0.418688f * g - 0.081312f * b;
}
inline void yccToRgb(float y, float cb, float cr, float& r, float& g, float& b) {
  r = y + 1.402f * cr;
  g = y - 0.344136f * cb - 0.714136f * cr;
  b = y + 1.772f * cb;
}

// sRGB transfer functions (the eye-friendly "gamma" encoding of photos).
inline float toLinear(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
inline float toGamma(float v) {
  v = std::max(v, 0.0f);
  return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// Deterministic, roughly Gaussian per-pixel noise (unit variance).
inline float hashNoise(int32_t x, int32_t y, uint32_t seed) {
  auto h = static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(y) * 0xD8163841u ^ seed * 0xCB1AB31Fu;
  float sum = 0.0f;
  for (int i = 0; i < 4; ++i) {
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    sum += static_cast<float>(h & 0xFFFFu) / 65535.0f;
  }
  return (sum - 2.0f) * 1.7320508f;  // 4 uniforms: variance 1/3 → scale to 1
}

// A GRAY8 (or colour, first channel) mask as a [0,1] plane.
inline Result<Plane> maskPlane(const ImageBuffer& mask) {
  auto p = Plane::allocate(mask.width(), mask.height());
  if (!p) return failure(p.error());
  const int32_t bpp = bytesPerPixel(mask.format());
  for (int32_t y = 0; y < mask.height(); ++y) {
    const uint8_t* src = mask.row(y);
    float* dst = p->row(y);
    for (int32_t x = 0; x < mask.width(); ++x) dst[x] = static_cast<float>(src[x * bpp]) / 255.0f;
  }
  return p;
}

// Bilinear resample of a plane to a new size (pixel-centre aligned).
inline Result<Plane> resizeBilinear(const Plane& src, int32_t w, int32_t h, const CancellationToken* token) {
  auto out = Plane::allocate(w, h);
  if (!out) return failure(out.error());
  const float sx = static_cast<float>(src.width()) / static_cast<float>(w);
  const float sy = static_cast<float>(src.height()) / static_cast<float>(h);
  const bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      float* row = out->row(y);
      const float fy = (static_cast<float>(y) + 0.5f) * sy - 0.5f;
      for (int32_t x = 0; x < w; ++x) row[x] = src.sample((static_cast<float>(x) + 0.5f) * sx - 0.5f, fy);
    }
  });
  if (!done) return failure(cancelledError());
  return out;
}

// Area-average downsample by an integer factor (good for building guides).
inline Result<Plane> boxDown(const Plane& src, int32_t f, const CancellationToken* token) {
  if (f <= 1) return clonePlane(src);
  const int32_t w = std::max(1, (src.width() + f - 1) / f), h = std::max(1, (src.height() + f - 1) / f);
  auto out = Plane::allocate(w, h);
  if (!out) return failure(out.error());
  const bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      for (int32_t x = 0; x < w; ++x) {
        float s = 0.0f;
        int32_t n = 0;
        for (int32_t yy = y * f; yy < std::min(src.height(), (y + 1) * f); ++yy) {
          const float* r = src.row(yy);
          for (int32_t xx = x * f; xx < std::min(src.width(), (x + 1) * f); ++xx, ++n) s += r[xx];
        }
        out->row(y)[x] = n ? s / static_cast<float>(n) : 0.0f;
      }
    }
  });
  if (!done) return failure(cancelledError());
  return out;
}

// Gamma-encoded luma of a colour image.
inline Result<Plane> lumaOf(const ImageBuffer& img, const CancellationToken* token) {
  auto p = Plane::allocate(img.width(), img.height());
  if (!p) return failure(p.error());
  const Rgb o = offsets(img);
  const bool done = parallelFor(img.height(), token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      const uint8_t* s = img.row(y);
      float* d = p->row(y);
      for (int32_t x = 0; x < img.width(); ++x) {
        const uint8_t* px = s + x * 4;
        d[x] = (0.299f * px[o.r] + 0.587f * px[o.g] + 0.114f * px[o.b]) / 255.0f;
      }
    }
  });
  if (!done) return failure(cancelledError());
  return p;
}

// Guided-filter coefficients (He et al.) for guide I and input p at radius r:
// output = mean(a)·I + mean(b). Defined in matte.cpp.
Error guidedCoefficients(const Plane& I, const Plane& p, int32_t r, float eps, Plane& meanA, Plane& meanB,
                         const CancellationToken* token);

inline uint8_t toByte(float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

}  // namespace photoplus::portrait::detail
