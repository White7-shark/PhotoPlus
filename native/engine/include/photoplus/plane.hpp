#pragma once

#include <cstddef>
#include <cstdint>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

namespace photoplus {

// Single-channel float32 image with tracked memory (counts toward the engine
// budget like every pixel buffer). Values are nominally in [0, 1].
class Plane {
 public:
  Plane() = default;
  ~Plane();
  Plane(const Plane&) = delete;
  Plane& operator=(const Plane&) = delete;
  Plane(Plane&& other) noexcept;
  Plane& operator=(Plane&& other) noexcept;

  // Contents are uninitialised: every caller must write before reading.
  static Result<Plane> allocate(int32_t width, int32_t height);

  int32_t width() const noexcept { return width_; }
  int32_t height() const noexcept { return height_; }
  bool empty() const noexcept { return data_ == nullptr; }
  std::size_t size() const noexcept { return static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_); }

  float* data() noexcept { return data_; }
  const float* data() const noexcept { return data_; }
  float* row(int32_t y) noexcept { return data_ + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_); }
  const float* row(int32_t y) const noexcept {
    return data_ + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
  }
  // Clamped (edge-replicating) access.
  float at(int32_t x, int32_t y) const noexcept;
  // Bilinear sample with edge clamping; x/y in pixel-centre coordinates.
  float sample(float x, float y) const noexcept;

 private:
  void reset() noexcept;
  float* data_ = nullptr;
  int32_t width_ = 0;
  int32_t height_ = 0;
};

Result<Plane> clonePlane(const Plane& src);

// Luma/chroma working representation (BT.601 full range, gamma-encoded,
// normalised to [0,1]; chroma centred on 0). Restoration operates mostly on
// luma, where the eye resolves detail, and smooths chroma more aggressively.
struct YccImage {
  Plane y;
  Plane cb;
  Plane cr;
  int32_t width() const noexcept { return y.width(); }
  int32_t height() const noexcept { return y.height(); }
};

Result<YccImage> toYcc(const ImageBuffer& rgba, const CancellationToken* token);
// Writes into an allocated RGBA8 image of the same size (alpha = 255).
Error fromYcc(const YccImage& ycc, ImageBuffer& rgba, const CancellationToken* token);

// Separable Gaussian blur (sigma in pixels; sigma <= 0 copies).
Error gaussianBlur(const Plane& src, Plane& dst, float sigma, const CancellationToken* token);
// Mean over a (2r+1)^2 window via running sums, O(1) per pixel.
Error boxBlur(const Plane& src, Plane& dst, int32_t radius, const CancellationToken* token);
// Bicubic (Catmull-Rom) resample to an arbitrary size.
Result<Plane> resizeCubic(const Plane& src, int32_t width, int32_t height, const CancellationToken* token);
// 2x box downsample (used for pyramids).
Result<Plane> downsample2(const Plane& src, const CancellationToken* token);

Error cancelledError();

}  // namespace photoplus
