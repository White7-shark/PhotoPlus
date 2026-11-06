#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#include "photoplus/image.hpp"
#include "photoplus/memory.hpp"
#include "photoplus/status.hpp"

namespace photoplus::detail {

// Tracked scratch memory: counts toward the engine budget and is reused
// between operations by the memory tracker.
template <typename T>
class Scratch {
 public:
  Scratch() = default;
  ~Scratch() { reset(); }
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;
  Scratch(Scratch&& o) noexcept : data_(o.data_), count_(o.count_) {
    o.data_ = nullptr;
    o.count_ = 0;
  }
  Scratch& operator=(Scratch&& o) noexcept {
    if (this != &o) {
      reset();
      data_ = o.data_;
      count_ = o.count_;
      o.data_ = nullptr;
      o.count_ = 0;
    }
    return *this;
  }

  static Result<Scratch> allocate(std::size_t count) {
    Scratch s;
    if (count == 0) return s;
    pp_status status = PP_OK;
    void* mem = MemoryTracker::instance().allocate(count * sizeof(T), &status);
    if (mem == nullptr) {
      return failure(makeError(status, status == PP_ERROR_BUDGET_EXCEEDED ? "Allocation exceeds engine memory budget"
                                                                          : "Out of memory allocating scratch"));
    }
    s.data_ = static_cast<T*>(mem);
    s.count_ = count;
    return s;
  }

  T* data() noexcept { return data_; }
  const T* data() const noexcept { return data_; }
  std::size_t size() const noexcept { return count_; }
  T& operator[](std::size_t i) noexcept { return data_[i]; }
  const T& operator[](std::size_t i) const noexcept { return data_[i]; }

 private:
  void reset() noexcept {
    if (data_ != nullptr) MemoryTracker::instance().release(data_, count_ * sizeof(T));
    data_ = nullptr;
    count_ = 0;
  }
  T* data_ = nullptr;
  std::size_t count_ = 0;
};

// Channel layout of a pixel format.
struct Layout {
  int32_t bpp = 4;
  int32_t colors = 3;  // colour channels (1 for gray)
  int32_t r = 0, g = 1, b = 2;
  int32_t alpha = -1;  // index of alpha, or -1
};

inline Layout layoutOf(PixelFormat f) {
  switch (f) {
    case PixelFormat::RGBA8:
      return {4, 3, 0, 1, 2, 3};
    case PixelFormat::BGRA8:
      return {4, 3, 2, 1, 0, 3};
    case PixelFormat::RGB8:
      return {3, 3, 0, 1, 2, -1};
    case PixelFormat::Gray8:
      return {1, 1, 0, 0, 0, -1};
  }
  return {};
}

inline uint8_t toByte(float v) {
  v = v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
  return static_cast<uint8_t>(v + 0.5f);
}

// sRGB transfer (exact curve), on 0..1 values.
inline float srgbToLinear(float c) {
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float linearToSrgb(float c) {
  c = std::clamp(c, 0.0f, 1.0f);
  return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// The first error raised by any parallel task (others are ignored).
class FirstError {
 public:
  void set(Error e) {
    std::lock_guard<std::mutex> lock(m_);
    if (!has_) {
      error_ = std::move(e);
      has_ = true;
      failed_.store(true, std::memory_order_release);
    }
  }
  bool failed() const noexcept { return failed_.load(std::memory_order_acquire); }
  Error take() {
    std::lock_guard<std::mutex> lock(m_);
    return has_ ? error_ : Error{PP_OK, {}};
  }

 private:
  std::mutex m_;
  Error error_{PP_OK, {}};
  bool has_ = false;
  std::atomic<bool> failed_{false};
};

inline Error ok() { return Error{PP_OK, {}}; }
inline Error cancelledErr() { return makeError(PP_ERROR_CANCELLED, "Processing was cancelled"); }
inline Error invalid(const std::string& why) { return makeError(PP_ERROR_INVALID_ARGUMENT, why); }

}  // namespace photoplus::detail
