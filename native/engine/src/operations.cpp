#include "photoplus/operations.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include "photoplus/profiler.hpp"

namespace photoplus::ops {

namespace {

inline Error kOkStatus() { return Error{PP_OK, {}}; }

Error cancelled() { return makeError(PP_ERROR_CANCELLED, "Processing was cancelled"); }

inline uint8_t clampByte(float v) {
  return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 255.0f)));
}

// Number of colour channels that tonal operations should touch (alpha is
// never modified).
inline int32_t colorChannels(PixelFormat f) { return f == PixelFormat::Gray8 ? 1 : 3; }

// Channel index of R, G, B for formats with colour.
inline std::array<int, 3> rgbIndex(PixelFormat f) {
  if (f == PixelFormat::BGRA8) return {2, 1, 0};
  return {0, 1, 2};
}

inline float srgbToLinear(float c) {
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float linearToSrgb(float c) {
  return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

Error applyLut(ImageBuffer& image, const std::array<uint8_t, 256>& lut,
               const CancellationToken* token) {
  const int32_t bpp = bytesPerPixel(image.format());
  const int32_t channels = colorChannels(image.format());
  for (int32_t y = 0; y < image.height(); ++y) {
    if (isCancelled(token)) return cancelled();
    uint8_t* row = image.row(y);
    for (int32_t x = 0; x < image.width(); ++x) {
      uint8_t* p = row + x * bpp;
      for (int32_t c = 0; c < channels; ++c) p[c] = lut[p[c]];
    }
  }
  return kOkStatus();
}

}  // namespace

Error tone(ImageBuffer& image, const ToneParams& params, const CancellationToken* token) {
  ScopedTimer timer("op.tone");
  if (image.empty()) return makeError(PP_ERROR_INVALID_ARGUMENT, "Empty image");
  if (!std::isfinite(params.exposureEv) || !std::isfinite(params.brightness) ||
      !std::isfinite(params.contrast)) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Tone parameters must be finite");
  }
  const float ev = std::clamp(params.exposureEv, -5.0f, 5.0f);
  const float brightness = std::clamp(params.brightness, -1.0f, 1.0f);
  const float contrast = std::clamp(params.contrast, -1.0f, 1.0f);
  const float gain = std::exp2(ev);
  // Contrast pivots on mid grey; factor in [0, 2].
  const float contrastFactor = 1.0f + contrast;

  std::array<uint8_t, 256> lut{};
  for (int i = 0; i < 256; ++i) {
    float v = static_cast<float>(i) / 255.0f;
    v = linearToSrgb(std::min(1.0f, srgbToLinear(v) * gain));
    v += brightness * 0.5f;
    v = (v - 0.5f) * contrastFactor + 0.5f;
    lut[i] = clampByte(v * 255.0f);
  }
  return applyLut(image, lut, token);
}

Error grayscale(ImageBuffer& image, const CancellationToken* token) {
  ScopedTimer timer("op.grayscale");
  if (image.empty()) return makeError(PP_ERROR_INVALID_ARGUMENT, "Empty image");
  if (image.format() == PixelFormat::Gray8) return kOkStatus();
  const int32_t bpp = bytesPerPixel(image.format());
  const auto idx = rgbIndex(image.format());
  for (int32_t y = 0; y < image.height(); ++y) {
    if (isCancelled(token)) return cancelled();
    uint8_t* row = image.row(y);
    for (int32_t x = 0; x < image.width(); ++x) {
      uint8_t* p = row + x * bpp;
      const auto luma =
          static_cast<uint8_t>((p[idx[0]] * 54 + p[idx[1]] * 183 + p[idx[2]] * 19) >> 8);
      p[idx[0]] = p[idx[1]] = p[idx[2]] = luma;
    }
  }
  return kOkStatus();
}

Error saturation(ImageBuffer& image, float delta, const CancellationToken* token) {
  ScopedTimer timer("op.saturation");
  if (image.empty()) return makeError(PP_ERROR_INVALID_ARGUMENT, "Empty image");
  if (!std::isfinite(delta)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Saturation must be finite");
  if (image.format() == PixelFormat::Gray8) return kOkStatus();
  const float s = 1.0f + std::clamp(delta, -1.0f, 1.0f);
  const int32_t bpp = bytesPerPixel(image.format());
  const auto idx = rgbIndex(image.format());
  for (int32_t y = 0; y < image.height(); ++y) {
    if (isCancelled(token)) return cancelled();
    uint8_t* row = image.row(y);
    for (int32_t x = 0; x < image.width(); ++x) {
      uint8_t* p = row + x * bpp;
      const float r = p[idx[0]], g = p[idx[1]], b = p[idx[2]];
      const float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
      p[idx[0]] = clampByte(l + (r - l) * s);
      p[idx[1]] = clampByte(l + (g - l) * s);
      p[idx[2]] = clampByte(l + (b - l) * s);
    }
  }
  return kOkStatus();
}

Error flip(ImageBuffer& image, bool horizontal, bool vertical, const CancellationToken* token) {
  ScopedTimer timer("op.flip");
  if (image.empty()) return makeError(PP_ERROR_INVALID_ARGUMENT, "Empty image");
  const int32_t bpp = bytesPerPixel(image.format());
  if (horizontal) {
    uint8_t tmp[4];
    for (int32_t y = 0; y < image.height(); ++y) {
      if (isCancelled(token)) return cancelled();
      uint8_t* row = image.row(y);
      for (int32_t l = 0, r = image.width() - 1; l < r; ++l, --r) {
        std::memcpy(tmp, row + l * bpp, bpp);
        std::memcpy(row + l * bpp, row + r * bpp, bpp);
        std::memcpy(row + r * bpp, tmp, bpp);
      }
    }
  }
  if (vertical) {
    const std::size_t rowBytes = static_cast<std::size_t>(image.width()) * bpp;
    for (int32_t t = 0, b = image.height() - 1; t < b; ++t, --b) {
      if (isCancelled(token)) return cancelled();
      uint8_t* top = image.row(t);
      uint8_t* bottom = image.row(b);
      // Swap in small chunks to avoid a full-row scratch allocation.
      uint8_t chunk[256];
      for (std::size_t off = 0; off < rowBytes; off += sizeof(chunk)) {
        const std::size_t n = std::min(sizeof(chunk), rowBytes - off);
        std::memcpy(chunk, top + off, n);
        std::memcpy(top + off, bottom + off, n);
        std::memcpy(bottom + off, chunk, n);
      }
    }
  }
  return kOkStatus();
}

Result<ImageBuffer> rotate90(const ImageBuffer& src, int quarterTurns,
                             const CancellationToken* token) {
  ScopedTimer timer("op.rotate90");
  if (src.empty()) return makeError(PP_ERROR_INVALID_ARGUMENT, "Empty image");
  const int turns = ((quarterTurns % 4) + 4) % 4;
  if (turns == 0) return cloneImage(src);
  if (turns == 2) {
    auto out = cloneImage(src);
    if (!out) return out.error();
    Error err = flip(out.value(), true, true, token);
    if (err.status != PP_OK) return err;
    return std::move(out).value();
  }
  const int32_t w = src.width(), h = src.height();
  auto out = ImageBuffer::allocate(h, w, src.format());
  if (!out) return out.error();
  ImageBuffer& dst = out.value();
  const int32_t bpp = bytesPerPixel(src.format());
  for (int32_t y = 0; y < h; ++y) {
    if (isCancelled(token)) return cancelled();
    const uint8_t* srow = src.row(y);
    for (int32_t x = 0; x < w; ++x) {
      // Clockwise: (x, y) -> (h - 1 - y, x). Counter-clockwise: (y, w - 1 - x).
      const int32_t dx = turns == 1 ? h - 1 - y : y;
      const int32_t dy = turns == 1 ? x : w - 1 - x;
      std::memcpy(dst.row(dy) + dx * bpp, srow + x * bpp, bpp);
    }
  }
  return std::move(out).value();
}

Result<ImageBuffer> crop(const ImageBuffer& src, int32_t x, int32_t y, int32_t width,
                         int32_t height, const CancellationToken* token) {
  ScopedTimer timer("op.crop");
  if (src.empty()) return makeError(PP_ERROR_INVALID_ARGUMENT, "Empty image");
  if (x < 0 || y < 0 || width <= 0 || height <= 0 || x > src.width() - width ||
      y > src.height() - height) {
    return makeError(PP_ERROR_INVALID_ARGUMENT,
                     "Crop rectangle " + std::to_string(width) + "x" + std::to_string(height) +
                         "+" + std::to_string(x) + "+" + std::to_string(y) +
                         " is outside the image");
  }
  auto out = ImageBuffer::allocate(width, height, src.format());
  if (!out) return out.error();
  const int32_t bpp = bytesPerPixel(src.format());
  for (int32_t row = 0; row < height; ++row) {
    if (isCancelled(token)) return cancelled();
    std::memcpy(out.value().row(row), src.row(y + row) + x * bpp,
                static_cast<std::size_t>(width) * bpp);
  }
  return std::move(out).value();
}

Result<ImageBuffer> resizeBilinear(const ImageBuffer& src, int32_t width, int32_t height,
                                   const CancellationToken* token) {
  ScopedTimer timer("op.resize");
  if (src.empty()) return makeError(PP_ERROR_INVALID_ARGUMENT, "Empty image");
  auto out = ImageBuffer::allocate(width, height, src.format());
  if (!out) return out.error();
  ImageBuffer& dst = out.value();
  const int32_t bpp = bytesPerPixel(src.format());
  const float sx = static_cast<float>(src.width()) / static_cast<float>(width);
  const float sy = static_cast<float>(src.height()) / static_cast<float>(height);
  const int32_t maxX = src.width() - 1, maxY = src.height() - 1;

  for (int32_t y = 0; y < height; ++y) {
    if (isCancelled(token)) return cancelled();
    // Pixel-centre mapping keeps the image from shifting by half a pixel.
    const float fy = std::clamp((static_cast<float>(y) + 0.5f) * sy - 0.5f, 0.0f,
                                static_cast<float>(maxY));
    const int32_t y0 = static_cast<int32_t>(fy);
    const int32_t y1 = std::min(y0 + 1, maxY);
    const float wy = fy - static_cast<float>(y0);
    const uint8_t* r0 = src.row(y0);
    const uint8_t* r1 = src.row(y1);
    uint8_t* d = dst.row(y);
    for (int32_t x = 0; x < width; ++x) {
      const float fx = std::clamp((static_cast<float>(x) + 0.5f) * sx - 0.5f, 0.0f,
                                  static_cast<float>(maxX));
      const int32_t x0 = static_cast<int32_t>(fx);
      const int32_t x1 = std::min(x0 + 1, maxX);
      const float wx = fx - static_cast<float>(x0);
      for (int32_t c = 0; c < bpp; ++c) {
        const float top = r0[x0 * bpp + c] * (1.0f - wx) + r0[x1 * bpp + c] * wx;
        const float bot = r1[x0 * bpp + c] * (1.0f - wx) + r1[x1 * bpp + c] * wx;
        d[x * bpp + c] = clampByte(top * (1.0f - wy) + bot * wy);
      }
    }
  }
  return std::move(out).value();
}

Result<ImageBuffer> sharpen(const ImageBuffer& src, float amount, const CancellationToken* token) {
  ScopedTimer timer("op.sharpen");
  if (src.empty()) return makeError(PP_ERROR_INVALID_ARGUMENT, "Empty image");
  if (!std::isfinite(amount)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Amount must be finite");
  const float k = std::clamp(amount, 0.0f, 2.0f);
  auto out = ImageBuffer::allocate(src.width(), src.height(), src.format());
  if (!out) return out.error();
  ImageBuffer& dst = out.value();
  const int32_t bpp = bytesPerPixel(src.format());
  const int32_t channels = colorChannels(src.format());
  const int32_t w = src.width(), h = src.height();

  for (int32_t y = 0; y < h; ++y) {
    if (isCancelled(token)) return cancelled();
    const uint8_t* rows[3] = {src.row(std::max(0, y - 1)), src.row(y), src.row(std::min(h - 1, y + 1))};
    uint8_t* d = dst.row(y);
    for (int32_t x = 0; x < w; ++x) {
      const int32_t xs[3] = {std::max(0, x - 1), x, std::min(w - 1, x + 1)};
      for (int32_t c = 0; c < bpp; ++c) {
        const uint8_t centre = rows[1][x * bpp + c];
        if (c >= channels) {  // alpha passes through untouched
          d[x * bpp + c] = centre;
          continue;
        }
        int sum = 0;
        for (const uint8_t* r : rows) {
          for (int32_t xi : xs) sum += r[xi * bpp + c];
        }
        const float blur = static_cast<float>(sum) / 9.0f;
        d[x * bpp + c] = clampByte(centre + (centre - blur) * k);
      }
    }
  }
  return std::move(out).value();
}

}  // namespace photoplus::ops
