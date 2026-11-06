// Detail (luma unsharp mask), noise reduction and colour transforms.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "common.hpp"
#include "photoplus/enhance.hpp"
#include "photoplus/filters.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::ops {

using detail::Layout;
using detail::layoutOf;
using detail::toByte;

namespace {

float lumaOf(const uint8_t* p, const Layout& L) {
  return L.colors == 1 ? p[0] : 0.299f * p[L.r] + 0.587f * p[L.g] + 0.114f * p[L.b];
}

}  // namespace

Result<ImageBuffer> sharpenLuma(const ImageBuffer& src, float amount, float radius, float threshold,
                                const CancellationToken* token) {
  ScopedTimer timer("op.sharpenLuma");
  if (src.empty()) return failure(detail::invalid("Empty image"));
  if (!std::isfinite(amount) || !std::isfinite(radius) || !std::isfinite(threshold)) {
    return failure(detail::invalid("Sharpen settings must be finite"));
  }
  amount = std::clamp(amount, 0.0f, 3.0f);
  radius = std::clamp(radius <= 0.0f ? 1.0f : radius, 0.3f, 50.0f);
  threshold = std::clamp(threshold, 0.0f, 0.2f) * 255.0f;
  auto out = cloneImage(src);
  if (!out) return failure(out.error());
  if (amount == 0.0f) return out;
  const Layout L = layoutOf(src.format());
  const int32_t w = src.width(), h = src.height();
  auto luma = Plane::allocate(w, h);
  if (!luma) return failure(luma.error());
  parallelFor(h, nullptr, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) {
      const uint8_t* r = src.row(y);
      float* d = luma->row(y);
      for (int32_t x = 0; x < w; ++x) d[x] = lumaOf(r + x * L.bpp, L);
    }
  });
  auto soft = Plane::allocate(w, h);
  if (!soft) return failure(soft.error());
  if (Error e = gaussianBlur(luma.value(), soft.value(), radius, token); e.status != PP_OK) return failure(e);
  ImageBuffer& dst = out.value();
  const bool ok = parallelFor(h, token, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) {
      const float* l = luma->row(y);
      const float* s = soft->row(y);
      uint8_t* d = dst.row(y);
      for (int32_t x = 0; x < w; ++x) {
        float detail = l[x] - s[x];
        // Below the threshold (flat areas, noise) the detail fades out smoothly.
        const float mag = std::fabs(detail);
        detail = mag <= threshold ? 0.0f : std::copysign(mag - threshold, detail);
        const float add = amount * detail;
        uint8_t* p = d + x * L.bpp;
        for (int32_t c = 0; c < L.colors; ++c) {
          const int32_t idx = L.colors == 1 ? 0 : (c == 0 ? L.r : c == 1 ? L.g : L.b);
          p[idx] = toByte(p[idx] + add);
        }
      }
    }
  });
  if (!ok) return failure(detail::cancelledErr());
  return out;
}

Error denoise(ImageBuffer& image, float strength, const CancellationToken* token) {
  ScopedTimer timer("op.denoise");
  if (image.empty()) return detail::invalid("Empty image");
  if (!std::isfinite(strength)) return detail::invalid("Strength must be finite");
  strength = std::clamp(strength, 0.0f, 1.0f);
  if (strength == 0.0f) return detail::ok();
  const Layout L = layoutOf(image.format());
  if (L.colors == 1) {
    auto p = Plane::allocate(image.width(), image.height());
    if (!p) return p.error();
    for (int32_t y = 0; y < image.height(); ++y) {
      for (int32_t x = 0; x < image.width(); ++x) p->row(y)[x] = image.row(y)[x] / 255.0f;
    }
    const float noise = std::max(enhance::estimateNoise(p.value()), 0.006f * strength);
    const float k = 1.5f * strength;
    if (Error e = enhance::denoiseGuided(p.value(), 2, (k * noise) * (k * noise), 0.12f, token); e.status != PP_OK) return e;
    for (int32_t y = 0; y < image.height(); ++y) {
      for (int32_t x = 0; x < image.width(); ++x) image.row(y)[x] = toByte(p->row(y)[x] * 255.0f);
    }
    return detail::ok();
  }
  // Work in luma/chroma; keep alpha aside (the YCC round trip is opaque).
  ImageBuffer rgba;
  bool converted = false;
  if (image.format() != PixelFormat::RGBA8) {
    auto c = convertFormat(image, PixelFormat::RGBA8);
    if (!c) return c.error();
    rgba = std::move(c).value();
    converted = true;
  }
  ImageBuffer& work = converted ? rgba : image;
  std::vector<uint8_t> alpha;
  if (L.alpha >= 0) {
    alpha.resize(static_cast<std::size_t>(work.width()) * work.height());
    for (int32_t y = 0; y < work.height(); ++y) {
      for (int32_t x = 0; x < work.width(); ++x) alpha[static_cast<std::size_t>(y) * work.width() + x] = work.row(y)[x * 4 + 3];
    }
  }
  auto ycc = toYcc(work, token);
  if (!ycc) return ycc.error();
  // Scaled to the noise the photo has, with a floor so a requested strength
  // always does something.
  const float noise = std::max(enhance::estimateNoise(ycc->y), 0.006f * strength);
  const float k = 1.5f * strength;
  const float eps = (k * noise) * (k * noise);
  if (Error e = enhance::denoiseGuided(ycc->y, 2, eps, 0.12f, token); e.status != PP_OK) return e;
  const float ceps = std::max(eps, (1.5f * noise) * (1.5f * noise)) * 6.0f * strength + 1e-5f;
  if (Error e = enhance::denoiseGuided(ycc->cb, 4, ceps, 0.0f, token); e.status != PP_OK) return e;
  if (Error e = enhance::denoiseGuided(ycc->cr, 4, ceps, 0.0f, token); e.status != PP_OK) return e;
  if (Error e = fromYcc(ycc.value(), work, token); e.status != PP_OK) return e;
  if (!alpha.empty()) {
    for (int32_t y = 0; y < work.height(); ++y) {
      for (int32_t x = 0; x < work.width(); ++x) work.row(y)[x * 4 + 3] = alpha[static_cast<std::size_t>(y) * work.width() + x];
    }
  }
  if (converted) {
    auto back = convertFormat(work, image.format());
    if (!back) return back.error();
    return copyPixels(back.value(), image);
  }
  return detail::ok();
}

// ---- colour ----------------------------------------------------------------------

Error applyLut3d(ImageBuffer& image, const float* table, int32_t size, float strength, const CancellationToken* token) {
  ScopedTimer timer("op.lut3d");
  if (image.empty()) return detail::invalid("Empty image");
  if (table == nullptr || size < 2 || size > 65) return detail::invalid("A LUT needs 2..65 points per side");
  if (!std::isfinite(strength)) return detail::invalid("Strength must be finite");
  const Layout L = layoutOf(image.format());
  if (L.colors != 3) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "LUTs need a colour image");
  const std::size_t entries = static_cast<std::size_t>(size) * size * size * 3;
  for (std::size_t i = 0; i < entries; ++i) {
    if (!std::isfinite(table[i])) return detail::invalid("LUT values must be finite");
  }
  strength = std::clamp(strength, 0.0f, 1.0f);
  if (strength == 0.0f) return detail::ok();
  const float n1 = static_cast<float>(size - 1);
  auto at = [&](int32_t r, int32_t g, int32_t b, int32_t c) {
    return table[((static_cast<std::size_t>(b) * size + g) * size + r) * 3 + c];
  };
  const bool ok = parallelFor(image.height(), token, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) {
      uint8_t* row = image.row(y);
      for (int32_t x = 0; x < image.width(); ++x) {
        uint8_t* p = row + x * L.bpp;
        const float fr = p[L.r] / 255.0f * n1, fg = p[L.g] / 255.0f * n1, fb = p[L.b] / 255.0f * n1;
        const int32_t r0 = std::min(size - 2, static_cast<int32_t>(fr));
        const int32_t g0 = std::min(size - 2, static_cast<int32_t>(fg));
        const int32_t b0 = std::min(size - 2, static_cast<int32_t>(fb));
        const float dr = fr - static_cast<float>(r0), dg = fg - static_cast<float>(g0), db = fb - static_cast<float>(b0);
        // Tetrahedral interpolation: exact on the grey axis, smooth, 4 lookups.
        float out[3];
        for (int c = 0; c < 3; ++c) {
          const float c000 = at(r0, g0, b0, c), c111 = at(r0 + 1, g0 + 1, b0 + 1, c);
          float v;
          if (dr >= dg) {
            if (dg >= db) {
              v = c000 + dr * (at(r0 + 1, g0, b0, c) - c000) + dg * (at(r0 + 1, g0 + 1, b0, c) - at(r0 + 1, g0, b0, c)) +
                  db * (c111 - at(r0 + 1, g0 + 1, b0, c));
            } else if (dr >= db) {
              v = c000 + dr * (at(r0 + 1, g0, b0, c) - c000) + db * (at(r0 + 1, g0, b0 + 1, c) - at(r0 + 1, g0, b0, c)) +
                  dg * (c111 - at(r0 + 1, g0, b0 + 1, c));
            } else {
              v = c000 + db * (at(r0, g0, b0 + 1, c) - c000) + dr * (at(r0 + 1, g0, b0 + 1, c) - at(r0, g0, b0 + 1, c)) +
                  dg * (c111 - at(r0 + 1, g0, b0 + 1, c));
            }
          } else {
            if (db >= dg) {
              v = c000 + db * (at(r0, g0, b0 + 1, c) - c000) + dg * (at(r0, g0 + 1, b0 + 1, c) - at(r0, g0, b0 + 1, c)) +
                  dr * (c111 - at(r0, g0 + 1, b0 + 1, c));
            } else if (db >= dr) {
              v = c000 + dg * (at(r0, g0 + 1, b0, c) - c000) + db * (at(r0, g0 + 1, b0 + 1, c) - at(r0, g0 + 1, b0, c)) +
                  dr * (c111 - at(r0, g0 + 1, b0 + 1, c));
            } else {
              v = c000 + dg * (at(r0, g0 + 1, b0, c) - c000) + dr * (at(r0 + 1, g0 + 1, b0, c) - at(r0, g0 + 1, b0, c)) +
                  db * (c111 - at(r0 + 1, g0 + 1, b0, c));
            }
          }
          out[c] = v * 255.0f;
        }
        const int32_t idx[3] = {L.r, L.g, L.b};
        for (int c = 0; c < 3; ++c) p[idx[c]] = toByte(p[idx[c]] + (out[c] - p[idx[c]]) * strength);
      }
    }
  });
  return ok ? detail::ok() : detail::cancelledErr();
}

Error colorMatrix(ImageBuffer& image, const float m[12], bool linear, const CancellationToken* token) {
  ScopedTimer timer("op.colorMatrix");
  if (image.empty()) return detail::invalid("Empty image");
  if (m == nullptr) return detail::invalid("Matrix is null");
  for (int i = 0; i < 12; ++i) {
    if (!std::isfinite(m[i])) return detail::invalid("Matrix values must be finite");
  }
  const Layout L = layoutOf(image.format());
  if (L.colors != 3) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "A colour matrix needs a colour image");
  std::array<float, 256> toLin{};
  for (int i = 0; i < 256; ++i) toLin[i] = linear ? detail::srgbToLinear(static_cast<float>(i) / 255.0f) : static_cast<float>(i) / 255.0f;
  const bool ok = parallelFor(image.height(), token, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) {
      uint8_t* row = image.row(y);
      for (int32_t x = 0; x < image.width(); ++x) {
        uint8_t* p = row + x * L.bpp;
        const float r = toLin[p[L.r]], g = toLin[p[L.g]], b = toLin[p[L.b]];
        float o[3];
        for (int i = 0; i < 3; ++i) o[i] = m[4 * i] * r + m[4 * i + 1] * g + m[4 * i + 2] * b + m[4 * i + 3];
        const int32_t idx[3] = {L.r, L.g, L.b};
        for (int i = 0; i < 3; ++i) p[idx[i]] = toByte((linear ? detail::linearToSrgb(o[i]) : std::clamp(o[i], 0.0f, 1.0f)) * 255.0f);
      }
    }
  });
  return ok ? detail::ok() : detail::cancelledErr();
}

}  // namespace photoplus::ops
