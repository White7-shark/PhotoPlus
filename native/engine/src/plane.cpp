#include "photoplus/plane.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "photoplus/memory.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus {

Error cancelledError() { return makeError(PP_ERROR_CANCELLED, "Processing was cancelled"); }

namespace {
inline Error ok() { return Error{PP_OK, {}}; }
inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

Plane::~Plane() { reset(); }

Plane::Plane(Plane&& other) noexcept : data_(other.data_), width_(other.width_), height_(other.height_) {
  other.data_ = nullptr;
  other.width_ = other.height_ = 0;
}

Plane& Plane::operator=(Plane&& other) noexcept {
  if (this != &other) {
    reset();
    data_ = other.data_;
    width_ = other.width_;
    height_ = other.height_;
    other.data_ = nullptr;
    other.width_ = other.height_ = 0;
  }
  return *this;
}

void Plane::reset() noexcept {
  if (data_ != nullptr) MemoryTracker::instance().release(data_, size() * sizeof(float));
  data_ = nullptr;
  width_ = height_ = 0;
}

Result<Plane> Plane::allocate(int32_t width, int32_t height) {
  if (width <= 0 || height <= 0 || width > 2 * kMaxDimension || height > 2 * kMaxDimension) {
    return failure(makeError(PP_ERROR_INVALID_ARGUMENT,
                             "Invalid plane size " + std::to_string(width) + "x" + std::to_string(height)));
  }
  Plane p;
  p.width_ = width;
  p.height_ = height;
  pp_status status = PP_OK;
  void* mem = MemoryTracker::instance().allocate(p.size() * sizeof(float), &status);
  if (mem == nullptr) {
    p.width_ = p.height_ = 0;
    return failure(makeError(status, status == PP_ERROR_BUDGET_EXCEEDED ? "Allocation exceeds engine memory budget"
                                                                        : "Out of memory allocating working plane"));
  }
  p.data_ = static_cast<float*>(mem);
  return p;
}

float Plane::at(int32_t x, int32_t y) const noexcept {
  return row(clampi(y, 0, height_ - 1))[clampi(x, 0, width_ - 1)];
}

float Plane::sample(float x, float y) const noexcept {
  const float fx = std::clamp(x, 0.0f, static_cast<float>(width_ - 1));
  const float fy = std::clamp(y, 0.0f, static_cast<float>(height_ - 1));
  const auto x0 = static_cast<int32_t>(fx);
  const auto y0 = static_cast<int32_t>(fy);
  const int32_t x1 = std::min(x0 + 1, width_ - 1);
  const int32_t y1 = std::min(y0 + 1, height_ - 1);
  const float wx = fx - static_cast<float>(x0);
  const float wy = fy - static_cast<float>(y0);
  const float* r0 = row(y0);
  const float* r1 = row(y1);
  const float top = r0[x0] + (r0[x1] - r0[x0]) * wx;
  const float bot = r1[x0] + (r1[x1] - r1[x0]) * wx;
  return top + (bot - top) * wy;
}

Result<Plane> clonePlane(const Plane& src) {
  auto out = Plane::allocate(src.width(), src.height());
  if (!out) return failure(out.error());
  std::memcpy(out->data(), src.data(), src.size() * sizeof(float));
  return std::move(out).value();
}

Result<YccImage> toYcc(const ImageBuffer& rgba, const CancellationToken* token) {
  ScopedTimer timer("enh.toYcc");
  if (rgba.empty() || bytesPerPixel(rgba.format()) < 3) {
    return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Enhancement needs an RGB(A) image"));
  }
  const int32_t w = rgba.width(), h = rgba.height();
  auto y = Plane::allocate(w, h);
  if (!y) return failure(y.error());
  auto cb = Plane::allocate(w, h);
  if (!cb) return failure(cb.error());
  auto cr = Plane::allocate(w, h);
  if (!cr) return failure(cr.error());
  const int32_t bpp = bytesPerPixel(rgba.format());
  const bool bgr = rgba.format() == PixelFormat::BGRA8;
  const bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t row = begin; row < end && !isCancelled(token); ++row) {
      const uint8_t* s = rgba.row(row);
      float* py = y->row(row);
      float* pb = cb->row(row);
      float* pr = cr->row(row);
      for (int32_t x = 0; x < w; ++x) {
        const uint8_t* p = s + x * bpp;
        const float r = static_cast<float>(bgr ? p[2] : p[0]) / 255.0f;
        const float g = static_cast<float>(p[1]) / 255.0f;
        const float b = static_cast<float>(bgr ? p[0] : p[2]) / 255.0f;
        py[x] = 0.299f * r + 0.587f * g + 0.114f * b;
        pb[x] = -0.168736f * r - 0.331264f * g + 0.5f * b;
        pr[x] = 0.5f * r - 0.418688f * g - 0.081312f * b;
      }
    }
  });
  if (!done) return failure(cancelledError());
  return YccImage{std::move(y).value(), std::move(cb).value(), std::move(cr).value()};
}

Error fromYcc(const YccImage& ycc, ImageBuffer& rgba, const CancellationToken* token) {
  ScopedTimer timer("enh.fromYcc");
  if (rgba.width() != ycc.width() || rgba.height() != ycc.height() || bytesPerPixel(rgba.format()) < 3) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Output image does not match working size");
  }
  const int32_t w = ycc.width();
  const int32_t bpp = bytesPerPixel(rgba.format());
  const bool bgr = rgba.format() == PixelFormat::BGRA8;
  auto toByte = [](float v) {
    return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  };
  const bool done = parallelFor(ycc.height(), token, [&](int32_t begin, int32_t end) {
    for (int32_t row = begin; row < end && !isCancelled(token); ++row) {
      const float* py = ycc.y.row(row);
      const float* pb = ycc.cb.row(row);
      const float* pr = ycc.cr.row(row);
      uint8_t* d = rgba.row(row);
      for (int32_t x = 0; x < w; ++x) {
        const float yy = py[x], b = pb[x], r = pr[x];
        const float R = yy + 1.402f * r;
        const float G = yy - 0.344136f * b - 0.714136f * r;
        const float B = yy + 1.772f * b;
        uint8_t* p = d + x * bpp;
        p[bgr ? 2 : 0] = toByte(R);
        p[1] = toByte(G);
        p[bgr ? 0 : 2] = toByte(B);
        if (bpp == 4) p[3] = 255;
      }
    }
  });
  return done ? ok() : cancelledError();
}

namespace {
std::vector<float> gaussianKernel(float sigma) {
  const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0f)));
  std::vector<float> k(static_cast<size_t>(2 * radius + 1));
  float sum = 0.0f;
  for (int i = -radius; i <= radius; ++i) {
    const float v = std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma));
    k[static_cast<size_t>(i + radius)] = v;
    sum += v;
  }
  for (float& v : k) v /= sum;
  return k;
}
}  // namespace

Error gaussianBlur(const Plane& src, Plane& dst, float sigma, const CancellationToken* token) {
  ScopedTimer timer("enh.gaussian");
  if (src.width() != dst.width() || src.height() != dst.height()) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Blur planes differ in size");
  }
  if (!(sigma > 0.0f)) {
    if (&src != &dst) std::memcpy(dst.data(), src.data(), src.size() * sizeof(float));
    return ok();
  }
  const auto kernel = gaussianKernel(sigma);
  const int radius = static_cast<int>(kernel.size() / 2);
  const int32_t w = src.width(), h = src.height();
  auto tmp = Plane::allocate(w, h);
  if (!tmp) return tmp.error();
  // Horizontal pass: src -> tmp.
  bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    std::vector<float> line(static_cast<size_t>(w + 2 * radius));
    for (int32_t y = begin; y < end && !isCancelled(token); ++y) {
      const float* s = src.row(y);
      for (int32_t i = 0; i < w + 2 * radius; ++i) line[static_cast<size_t>(i)] = s[clampi(i - radius, 0, w - 1)];
      float* d = tmp->row(y);
      for (int32_t x = 0; x < w; ++x) {
        float acc = 0.0f;
        for (int k = 0; k <= 2 * radius; ++k) acc += line[static_cast<size_t>(x + k)] * kernel[static_cast<size_t>(k)];
        d[x] = acc;
      }
    }
  });
  if (!done) return cancelledError();
  // Vertical pass: tmp -> dst (row-wise accumulation for cache friendliness).
  done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end && !isCancelled(token); ++y) {
      float* d = dst.row(y);
      std::fill(d, d + w, 0.0f);
      for (int k = -radius; k <= radius; ++k) {
        const float kv = kernel[static_cast<size_t>(k + radius)];
        const float* s = tmp->row(clampi(y + k, 0, h - 1));
        for (int32_t x = 0; x < w; ++x) d[x] += s[x] * kv;
      }
    }
  });
  return done ? ok() : cancelledError();
}

Error boxBlur(const Plane& src, Plane& dst, int32_t radius, const CancellationToken* token) {
  ScopedTimer timer("enh.box");
  if (src.width() != dst.width() || src.height() != dst.height() || radius < 0) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Invalid box blur arguments");
  }
  const int32_t w = src.width(), h = src.height();
  auto tmp = Plane::allocate(w, h);
  if (!tmp) return tmp.error();
  const float norm = 1.0f / static_cast<float>(2 * radius + 1);
  bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end && !isCancelled(token); ++y) {
      const float* s = src.row(y);
      float* d = tmp->row(y);
      double acc = 0.0;
      for (int32_t i = -radius; i <= radius; ++i) acc += s[clampi(i, 0, w - 1)];
      for (int32_t x = 0; x < w; ++x) {
        d[x] = static_cast<float>(acc) * norm;
        acc += s[clampi(x + radius + 1, 0, w - 1)] - s[clampi(x - radius, 0, w - 1)];
      }
    }
  });
  if (!done) return cancelledError();
  done = parallelFor(w, token, [&](int32_t begin, int32_t end) {
    for (int32_t x = begin; x < end && !isCancelled(token); ++x) {
      double acc = 0.0;
      for (int32_t i = -radius; i <= radius; ++i) acc += tmp->row(clampi(i, 0, h - 1))[x];
      for (int32_t y = 0; y < h; ++y) {
        dst.row(y)[x] = static_cast<float>(acc) * norm;
        acc += tmp->row(clampi(y + radius + 1, 0, h - 1))[x] - tmp->row(clampi(y - radius, 0, h - 1))[x];
      }
    }
  });
  return done ? ok() : cancelledError();
}

namespace {
inline float cubic(float t) {  // Catmull-Rom
  const float a = std::fabs(t);
  if (a < 1.0f) return 1.5f * a * a * a - 2.5f * a * a + 1.0f;
  if (a < 2.0f) return -0.5f * a * a * a + 2.5f * a * a - 4.0f * a + 2.0f;
  return 0.0f;
}
}  // namespace

Result<Plane> resizeCubic(const Plane& src, int32_t width, int32_t height, const CancellationToken* token) {
  ScopedTimer timer("enh.resizeCubic");
  auto out = Plane::allocate(width, height);
  if (!out) return failure(out.error());
  const float sx = static_cast<float>(src.width()) / static_cast<float>(width);
  const float sy = static_cast<float>(src.height()) / static_cast<float>(height);
  const bool done = parallelFor(height, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end && !isCancelled(token); ++y) {
      const float fy = (static_cast<float>(y) + 0.5f) * sy - 0.5f;
      const auto iy = static_cast<int32_t>(std::floor(fy));
      float wy[4];
      for (int k = 0; k < 4; ++k) wy[k] = cubic(fy - static_cast<float>(iy - 1 + k));
      float* d = out->row(y);
      for (int32_t x = 0; x < width; ++x) {
        const float fx = (static_cast<float>(x) + 0.5f) * sx - 0.5f;
        const auto ix = static_cast<int32_t>(std::floor(fx));
        float acc = 0.0f, wsum = 0.0f;
        for (int j = 0; j < 4; ++j) {
          for (int k = 0; k < 4; ++k) {
            const float wgt = wy[j] * cubic(fx - static_cast<float>(ix - 1 + k));
            acc += src.at(ix - 1 + k, iy - 1 + j) * wgt;
            wsum += wgt;
          }
        }
        d[x] = wsum != 0.0f ? acc / wsum : 0.0f;
      }
    }
  });
  if (!done) return failure(cancelledError());
  return std::move(out).value();
}

Result<Plane> downsample2(const Plane& src, const CancellationToken* token) {
  const int32_t w = std::max(1, src.width() / 2), h = std::max(1, src.height() / 2);
  auto out = Plane::allocate(w, h);
  if (!out) return failure(out.error());
  for (int32_t y = 0; y < h; ++y) {
    if (isCancelled(token)) return failure(cancelledError());
    float* d = out->row(y);
    for (int32_t x = 0; x < w; ++x) {
      d[x] = 0.25f * (src.at(2 * x, 2 * y) + src.at(2 * x + 1, 2 * y) + src.at(2 * x, 2 * y + 1) +
                      src.at(2 * x + 1, 2 * y + 1));
    }
  }
  return std::move(out).value();
}

}  // namespace photoplus
