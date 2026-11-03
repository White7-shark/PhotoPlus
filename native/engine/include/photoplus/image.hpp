#pragma once

#include <cstddef>
#include <cstdint>

#include "photoplus/pp_engine.h"
#include "photoplus/status.hpp"

namespace photoplus {

enum class PixelFormat : int32_t {
  RGBA8 = PP_FORMAT_RGBA8,
  BGRA8 = PP_FORMAT_BGRA8,
  RGB8 = PP_FORMAT_RGB8,
  Gray8 = PP_FORMAT_GRAY8,
};

int32_t bytesPerPixel(PixelFormat format) noexcept;
bool isValidFormat(int32_t raw) noexcept;

// Largest edge the engine accepts. 16384 covers 200 MP sensors with headroom
// while keeping width*height*4 well inside 32-bit stride arithmetic.
constexpr int32_t kMaxDimension = 16384;

// Move-only pixel container. Either owns tracked memory or is a non-owning
// view over host memory (e.g. a locked Android Bitmap).
class ImageBuffer {
 public:
  ImageBuffer() = default;
  ~ImageBuffer();

  ImageBuffer(const ImageBuffer&) = delete;
  ImageBuffer& operator=(const ImageBuffer&) = delete;
  ImageBuffer(ImageBuffer&& other) noexcept;
  ImageBuffer& operator=(ImageBuffer&& other) noexcept;

  static Result<ImageBuffer> allocate(int32_t width, int32_t height, PixelFormat format);
  static Result<ImageBuffer> wrap(void* pixels, int32_t width, int32_t height, int32_t stride,
                                  PixelFormat format);

  int32_t width() const noexcept { return width_; }
  int32_t height() const noexcept { return height_; }
  int32_t stride() const noexcept { return stride_; }
  PixelFormat format() const noexcept { return format_; }
  bool ownsPixels() const noexcept { return owns_; }
  bool empty() const noexcept { return data_ == nullptr; }
  std::size_t byteSize() const noexcept { return static_cast<std::size_t>(stride_) * height_; }

  uint8_t* row(int32_t y) noexcept { return data_ + static_cast<std::size_t>(y) * stride_; }
  const uint8_t* row(int32_t y) const noexcept {
    return data_ + static_cast<std::size_t>(y) * stride_;
  }
  uint8_t* data() noexcept { return data_; }
  const uint8_t* data() const noexcept { return data_; }

 private:
  void reset() noexcept;

  uint8_t* data_ = nullptr;
  int32_t width_ = 0;
  int32_t height_ = 0;
  int32_t stride_ = 0;
  PixelFormat format_ = PixelFormat::RGBA8;
  bool owns_ = false;
};

Result<ImageBuffer> cloneImage(const ImageBuffer& src);
Error copyPixels(const ImageBuffer& src, ImageBuffer& dst);
Result<ImageBuffer> convertFormat(const ImageBuffer& src, PixelFormat target);

}  // namespace photoplus
