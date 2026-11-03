#include "photoplus/image.hpp"

#include <cstring>
#include <string>
#include <utility>

#include "photoplus/memory.hpp"

namespace photoplus {

int32_t bytesPerPixel(PixelFormat format) noexcept {
  switch (format) {
    case PixelFormat::RGBA8:
    case PixelFormat::BGRA8:
      return 4;
    case PixelFormat::RGB8:
      return 3;
    case PixelFormat::Gray8:
      return 1;
  }
  return 0;
}

bool isValidFormat(int32_t raw) noexcept {
  return raw == PP_FORMAT_RGBA8 || raw == PP_FORMAT_BGRA8 || raw == PP_FORMAT_RGB8 ||
         raw == PP_FORMAT_GRAY8;
}

ImageBuffer::~ImageBuffer() { reset(); }

ImageBuffer::ImageBuffer(ImageBuffer&& other) noexcept
    : data_(other.data_),
      width_(other.width_),
      height_(other.height_),
      stride_(other.stride_),
      format_(other.format_),
      owns_(other.owns_) {
  other.data_ = nullptr;
  other.owns_ = false;
  other.width_ = other.height_ = other.stride_ = 0;
}

ImageBuffer& ImageBuffer::operator=(ImageBuffer&& other) noexcept {
  if (this != &other) {
    reset();
    data_ = other.data_;
    width_ = other.width_;
    height_ = other.height_;
    stride_ = other.stride_;
    format_ = other.format_;
    owns_ = other.owns_;
    other.data_ = nullptr;
    other.owns_ = false;
    other.width_ = other.height_ = other.stride_ = 0;
  }
  return *this;
}

void ImageBuffer::reset() noexcept {
  if (owns_ && data_ != nullptr) {
    MemoryTracker::instance().release(data_, byteSize());
  }
  data_ = nullptr;
  owns_ = false;
  width_ = height_ = stride_ = 0;
}

static Error validateDimensions(int32_t width, int32_t height) {
  if (width <= 0 || height <= 0) {
    return makeError(PP_ERROR_INVALID_ARGUMENT,
                     "Image dimensions must be positive (got " + std::to_string(width) + "x" +
                         std::to_string(height) + ")");
  }
  if (width > kMaxDimension || height > kMaxDimension) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Image dimension exceeds engine limit of " +
                                                    std::to_string(kMaxDimension) + " pixels");
  }
  return Error{PP_OK, {}};
}

Result<ImageBuffer> ImageBuffer::allocate(int32_t width, int32_t height, PixelFormat format) {
  Error dimError = validateDimensions(width, height);
  if (dimError.status != PP_OK) return dimError;
  const int32_t bpp = bytesPerPixel(format);
  if (bpp == 0) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Unknown pixel format");

  ImageBuffer image;
  image.width_ = width;
  image.height_ = height;
  image.stride_ = width * bpp;
  image.format_ = format;

  pp_status status = PP_OK;
  void* mem = MemoryTracker::instance().allocate(image.byteSize(), &status);
  if (mem == nullptr) {
    image.width_ = image.height_ = image.stride_ = 0;
    return makeError(status, status == PP_ERROR_BUDGET_EXCEEDED
                                 ? "Allocation exceeds engine memory budget"
                                 : "Out of memory allocating image buffer");
  }
  image.data_ = static_cast<uint8_t*>(mem);
  image.owns_ = true;
  return image;
}

Result<ImageBuffer> ImageBuffer::wrap(void* pixels, int32_t width, int32_t height, int32_t stride,
                                      PixelFormat format) {
  if (pixels == nullptr) return makeError(PP_ERROR_INVALID_ARGUMENT, "Pixel pointer is null");
  Error dimError = validateDimensions(width, height);
  if (dimError.status != PP_OK) return dimError;
  const int32_t bpp = bytesPerPixel(format);
  if (bpp == 0) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Unknown pixel format");
  if (stride < width * bpp) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Stride is smaller than one row of pixels");
  }
  ImageBuffer image;
  image.data_ = static_cast<uint8_t*>(pixels);
  image.width_ = width;
  image.height_ = height;
  image.stride_ = stride;
  image.format_ = format;
  image.owns_ = false;
  return image;
}

Error copyPixels(const ImageBuffer& src, ImageBuffer& dst) {
  if (src.empty() || dst.empty()) return makeError(PP_ERROR_INVALID_ARGUMENT, "Empty image");
  if (src.width() != dst.width() || src.height() != dst.height() ||
      src.format() != dst.format()) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Source and destination images do not match");
  }
  const std::size_t rowBytes = static_cast<std::size_t>(src.width()) * bytesPerPixel(src.format());
  for (int32_t y = 0; y < src.height(); ++y) {
    std::memcpy(dst.row(y), src.row(y), rowBytes);
  }
  return Error{PP_OK, {}};
}

Result<ImageBuffer> cloneImage(const ImageBuffer& src) {
  auto out = ImageBuffer::allocate(src.width(), src.height(), src.format());
  if (!out) return out.error();
  Error err = copyPixels(src, out.value());
  if (err.status != PP_OK) return err;
  return std::move(out).value();
}

namespace {
struct Rgba {
  uint8_t r, g, b, a;
};

inline Rgba readPixel(const uint8_t* p, PixelFormat f) {
  switch (f) {
    case PixelFormat::RGBA8:
      return {p[0], p[1], p[2], p[3]};
    case PixelFormat::BGRA8:
      return {p[2], p[1], p[0], p[3]};
    case PixelFormat::RGB8:
      return {p[0], p[1], p[2], 255};
    case PixelFormat::Gray8:
      return {p[0], p[0], p[0], 255};
  }
  return {0, 0, 0, 255};
}

inline void writePixel(uint8_t* p, PixelFormat f, Rgba c) {
  switch (f) {
    case PixelFormat::RGBA8:
      p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
      return;
    case PixelFormat::BGRA8:
      p[0] = c.b; p[1] = c.g; p[2] = c.r; p[3] = c.a;
      return;
    case PixelFormat::RGB8:
      p[0] = c.r; p[1] = c.g; p[2] = c.b;
      return;
    case PixelFormat::Gray8:
      // Rec. 709 luma, integer approximation.
      p[0] = static_cast<uint8_t>((c.r * 54 + c.g * 183 + c.b * 19) >> 8);
      return;
  }
}
}  // namespace

Result<ImageBuffer> convertFormat(const ImageBuffer& src, PixelFormat target) {
  if (src.empty()) return makeError(PP_ERROR_INVALID_ARGUMENT, "Empty image");
  if (src.format() == target) return cloneImage(src);
  auto out = ImageBuffer::allocate(src.width(), src.height(), target);
  if (!out) return out.error();
  const int32_t sbpp = bytesPerPixel(src.format());
  const int32_t dbpp = bytesPerPixel(target);
  ImageBuffer& dst = out.value();
  for (int32_t y = 0; y < src.height(); ++y) {
    const uint8_t* s = src.row(y);
    uint8_t* d = dst.row(y);
    for (int32_t x = 0; x < src.width(); ++x) {
      writePixel(d + x * dbpp, target, readPixel(s + x * sbpp, src.format()));
    }
  }
  return std::move(out).value();
}

}  // namespace photoplus
