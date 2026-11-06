#include "photoplus/tensor.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "../ops/common.hpp"
#include "photoplus/filters.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::ml {


Error toTensor(const ImageBuffer& image, const TensorSpec& spec, float* out, std::size_t outCount,
               const CancellationToken* token, Placement* placement) {
  ScopedTimer timer("ml.toTensor");
  if (image.empty()) return detail::invalid("Empty image");
  if (out == nullptr) return detail::invalid("Output is null");
  if (spec.width <= 0 || spec.height <= 0 || spec.width > 4096 || spec.height > 4096) {
    return detail::invalid("Tensor size must be 1..4096");
  }
  const std::size_t needed = static_cast<std::size_t>(spec.width) * spec.height * 3;
  if (outCount < needed) return detail::invalid("Output buffer is too small");
  for (int c = 0; c < 3; ++c) {
    if (!std::isfinite(spec.mean[c]) || !std::isfinite(spec.std[c]) || std::fabs(spec.std[c]) < 1e-8f) {
      return detail::invalid("Normalisation must be finite with non-zero std");
    }
  }
  // Where the photo goes inside the tensor.
  const float iw = static_cast<float>(image.width()), ih = static_cast<float>(image.height());
  const float tw = static_cast<float>(spec.width), th = static_cast<float>(spec.height);
  float sx = tw / iw, sy = th / ih;
  if (spec.fit == Fit::Letterbox) sx = sy = std::min(tw / iw, th / ih);
  if (spec.fit == Fit::CropCenter) sx = sy = std::max(tw / iw, th / ih);
  const int32_t rw = std::max(1, static_cast<int32_t>(std::lround(iw * sx)));
  const int32_t rh = std::max(1, static_cast<int32_t>(std::lround(ih * sy)));
  const int32_t ox = (spec.width - rw) / 2, oy = (spec.height - rh) / 2;
  if (placement != nullptr) *placement = {static_cast<float>(rw) / iw, static_cast<float>(rh) / ih, static_cast<float>(ox), static_cast<float>(oy)};
  auto resized = ops::resize(image, rw, rh, ops::ResizeFilter::Auto, token);
  if (!resized) return resized.error();
  const ImageBuffer& r = resized.value();
  const detail::Layout L = detail::layoutOf(r.format());
  const std::size_t plane = static_cast<std::size_t>(spec.width) * spec.height;
  float k[3], b[3];
  for (int c = 0; c < 3; ++c) k[c] = 1.0f / (255.0f * spec.std[c]), b[c] = spec.mean[c] / spec.std[c];
  const bool done = parallelFor(spec.height, token, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) {
      const int32_t sy2 = y - oy;
      for (int32_t x = 0; x < spec.width; ++x) {
        const int32_t sx2 = x - ox;
        float v[3] = {0, 0, 0};  // padding: the mean colour, 0 after normalisation
        if (sx2 >= 0 && sx2 < rw && sy2 >= 0 && sy2 < rh) {
          const uint8_t* p = r.row(sy2) + static_cast<std::size_t>(sx2) * L.bpp;
          const float rgb[3] = {static_cast<float>(p[L.r]), static_cast<float>(p[L.g]), static_cast<float>(p[L.b])};
          for (int c = 0; c < 3; ++c) v[c] = rgb[c] * k[c] - b[c];
        }
        const std::size_t i = static_cast<std::size_t>(y) * spec.width + x;
        for (int c = 0; c < 3; ++c) {
          if (spec.layout == ml::Layout::NCHW) {
            out[c * plane + i] = v[c];
          } else {
            out[i * 3 + c] = v[c];
          }
        }
      }
    }
  });
  return done ? detail::ok() : detail::cancelledErr();
}

Result<ImageBuffer> toMask(const float* data, int32_t width, int32_t height, int32_t channels, ml::Layout layout,
                           int32_t channel, Activation activation, int32_t outWidth, int32_t outHeight,
                           const Placement* placement, int32_t photoWidth, int32_t photoHeight,
                           const CancellationToken* token) {
  ScopedTimer timer("ml.toMask");
  if (data == nullptr) return failure(detail::invalid("Data is null"));
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || channels <= 0 || channels > 1024) {
    return failure(detail::invalid("Score map size is out of range"));
  }
  if (channel < 0 || channel >= channels) return failure(detail::invalid("Channel is out of range"));
  if (outWidth <= 0 || outHeight <= 0 || outWidth > kMaxDimension || outHeight > kMaxDimension) {
    return failure(detail::invalid("Mask size is out of range"));
  }
  // The photo's part of the map.
  int32_t x0 = 0, y0 = 0, cw = width, ch = height;
  if (placement != nullptr && photoWidth > 0 && photoHeight > 0) {
    x0 = std::clamp(static_cast<int32_t>(std::lround(placement->offsetX)), 0, width - 1);
    y0 = std::clamp(static_cast<int32_t>(std::lround(placement->offsetY)), 0, height - 1);
    cw = std::clamp(static_cast<int32_t>(std::lround(placement->scaleX * static_cast<float>(photoWidth))), 1, width - x0);
    ch = std::clamp(static_cast<int32_t>(std::lround(placement->scaleY * static_cast<float>(photoHeight))), 1, height - y0);
  }
  auto small = ImageBuffer::allocate(cw, ch, PixelFormat::Gray8);
  if (!small) return failure(small.error());
  const std::size_t plane = static_cast<std::size_t>(width) * height;
  auto at = [&](int32_t x, int32_t y, int32_t c) {
    const std::size_t i = static_cast<std::size_t>(y) * width + x;
    return layout == ml::Layout::NCHW ? data[c * plane + i] : data[i * channels + c];
  };
  bool finite = true;
  for (int32_t y = 0; y < ch; ++y) {
    uint8_t* d = small->row(y);
    for (int32_t x = 0; x < cw; ++x) {
      float v = at(x0 + x, y0 + y, channel);
      if (activation == Activation::Sigmoid) {
        v = 1.0f / (1.0f + std::exp(-v));
      } else if (activation == Activation::Softmax) {
        float mx = -INFINITY;
        for (int32_t c = 0; c < channels; ++c) mx = std::max(mx, at(x0 + x, y0 + y, c));
        float sum = 0.0f;
        for (int32_t c = 0; c < channels; ++c) sum += std::exp(at(x0 + x, y0 + y, c) - mx);
        v = std::exp(v - mx) / sum;
      }
      if (!std::isfinite(v)) finite = false, v = 0.0f;
      d[x] = detail::toByte(std::clamp(v, 0.0f, 1.0f) * 255.0f);
    }
  }
  if (!finite) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "The model produced invalid values"));
  if (cw == outWidth && ch == outHeight) return small;
  return ops::resize(small.value(), outWidth, outHeight, ops::ResizeFilter::Bilinear, token);
}

}  // namespace photoplus::ml
