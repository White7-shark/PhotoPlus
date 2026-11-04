// C ABI implementation. Every entry point is a firewall: C++ exceptions must
// never escape into JNI or other C callers, so each function catches all and
// converts to a status code plus a thread-local message.

#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <utility>

#include <algorithm>
#include <cmath>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/enhance.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/image.hpp"
#include "photoplus/memory.hpp"
#include "photoplus/pipeline.hpp"
#include "photoplus/pp_engine.h"
#include "photoplus/profiler.hpp"

struct pp_image {
  photoplus::ImageBuffer buffer;
};

struct pp_cancel_token {
  photoplus::CancellationToken token;
};

namespace {

thread_local std::string g_lastError;

pp_status fail(pp_status status, std::string message) {
  g_lastError = std::move(message);
  return status;
}

pp_status fail(const photoplus::Error& error) { return fail(error.status, error.message); }

pp_status succeed() {
  g_lastError.clear();
  return PP_OK;
}

template <typename Fn>
pp_status guarded(Fn&& fn) {
  try {
    return fn();
  } catch (const std::bad_alloc&) {
    return fail(PP_ERROR_OUT_OF_MEMORY, "Out of memory");
  } catch (const std::exception& e) {
    return fail(PP_ERROR_INTERNAL, std::string("Internal error: ") + e.what());
  } catch (...) {
    return fail(PP_ERROR_INTERNAL, "Unknown internal error");
  }
}

#define PP_STR2(x) #x
#define PP_STR(x) PP_STR2(x)

}  // namespace

extern "C" {

const char* pp_version(void) {
  return PP_STR(PP_ENGINE_VERSION_MAJOR) "." PP_STR(PP_ENGINE_VERSION_MINOR) "." PP_STR(
      PP_ENGINE_VERSION_PATCH);
}

const char* pp_status_name(pp_status status) {
  switch (status) {
    case PP_OK: return "OK";
    case PP_ERROR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case PP_ERROR_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case PP_ERROR_BUDGET_EXCEEDED: return "BUDGET_EXCEEDED";
    case PP_ERROR_CANCELLED: return "CANCELLED";
    case PP_ERROR_UNSUPPORTED_FORMAT: return "UNSUPPORTED_FORMAT";
    case PP_ERROR_INTERNAL: return "INTERNAL";
  }
  return "UNKNOWN";
}

const char* pp_last_error_message(void) { return g_lastError.c_str(); }

pp_status pp_image_create(int32_t width, int32_t height, int32_t format, pp_image** out) {
  return guarded([&]() -> pp_status {
    if (out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "out is null");
    *out = nullptr;
    if (!photoplus::isValidFormat(format)) {
      return fail(PP_ERROR_UNSUPPORTED_FORMAT, "Unknown pixel format");
    }
    auto result = photoplus::ImageBuffer::allocate(width, height,
                                                   static_cast<photoplus::PixelFormat>(format));
    if (!result) return fail(result.error());
    *out = new pp_image{std::move(result).value()};
    return succeed();
  });
}

pp_status pp_image_wrap(void* pixels, int32_t width, int32_t height, int32_t stride,
                        int32_t format, pp_image** out) {
  return guarded([&]() -> pp_status {
    if (out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "out is null");
    *out = nullptr;
    if (!photoplus::isValidFormat(format)) {
      return fail(PP_ERROR_UNSUPPORTED_FORMAT, "Unknown pixel format");
    }
    auto result = photoplus::ImageBuffer::wrap(pixels, width, height, stride,
                                               static_cast<photoplus::PixelFormat>(format));
    if (!result) return fail(result.error());
    *out = new pp_image{std::move(result).value()};
    return succeed();
  });
}

void pp_image_destroy(pp_image* image) { delete image; }

pp_status pp_image_get_info(const pp_image* image, pp_image_info* out) {
  if (image == nullptr || out == nullptr) {
    return fail(PP_ERROR_INVALID_ARGUMENT, "image or out is null");
  }
  out->width = image->buffer.width();
  out->height = image->buffer.height();
  out->stride = image->buffer.stride();
  out->format = static_cast<int32_t>(image->buffer.format());
  out->owns_pixels = image->buffer.ownsPixels() ? 1 : 0;
  return succeed();
}

void* pp_image_pixels(pp_image* image) {
  return image == nullptr ? nullptr : image->buffer.data();
}

pp_status pp_image_copy_pixels(const pp_image* src, pp_image* dst) {
  return guarded([&]() -> pp_status {
    if (src == nullptr || dst == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "image is null");
    photoplus::Error err = photoplus::copyPixels(src->buffer, dst->buffer);
    if (err.status != PP_OK) return fail(err);
    return succeed();
  });
}

pp_cancel_token* pp_cancel_token_create(void) {
  return new (std::nothrow) pp_cancel_token{};
}

void pp_cancel_token_cancel(pp_cancel_token* token) {
  if (token != nullptr) token->token.cancel();
}

int32_t pp_cancel_token_is_cancelled(const pp_cancel_token* token) {
  return token != nullptr && token->token.isCancelled() ? 1 : 0;
}

void pp_cancel_token_destroy(pp_cancel_token* token) { delete token; }

pp_status pp_process(const pp_image* input, const pp_op* ops, size_t op_count,
                     const pp_cancel_token* token, pp_image** out) {
  return guarded([&]() -> pp_status {
    if (out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "out is null");
    *out = nullptr;
    if (input == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "input is null");
    auto result =
        photoplus::runPipeline(input->buffer, ops, op_count, token ? &token->token : nullptr);
    if (!result) return fail(result.error());
    *out = new pp_image{std::move(result).value()};
    return succeed();
  });
}

void pp_memory_get_stats(pp_memory_stats* out) {
  if (out != nullptr) *out = photoplus::MemoryTracker::instance().stats();
}

void pp_memory_set_budget(uint64_t bytes) { photoplus::MemoryTracker::instance().setBudget(bytes); }

void pp_memory_reset_peak(void) { photoplus::MemoryTracker::instance().resetPeak(); }

size_t pp_profiler_report_json(char* buffer, size_t capacity) {
  std::string json;
  try {
    json = photoplus::Profiler::instance().toJson();
  } catch (...) {
    json = "{}";
  }
  if (buffer != nullptr && capacity > 0) {
    const size_t n = json.size() < capacity - 1 ? json.size() : capacity - 1;
    std::memcpy(buffer, json.data(), n);
    buffer[n] = '\0';
  }
  return json.size();
}

void pp_profiler_reset(void) { photoplus::Profiler::instance().reset(); }

void pp_enhance_default_params(pp_enhance_params* out) {
  if (out == nullptr) return;
  const photoplus::enhance::Params d;
  out->upscale = d.upscale;
  out->denoise = d.denoise;
  out->sharpen = d.sharpen;
  out->detail = d.detail;
  out->auto_tone = d.autoTone ? 1 : 0;
  out->vibrance = d.vibrance;
}

namespace {
pp_status enhanceImpl(const pp_image* const* frames, size_t count, const pp_enhance_params* params,
                      const pp_cancel_token* token, pp_image** out, pp_image* destination, pp_enhance_report* report) {
  if (frames == nullptr || count == 0) return fail(PP_ERROR_INVALID_ARGUMENT, "No frames supplied");
  photoplus::enhance::Params p;
  if (params != nullptr) {
    if (!std::isfinite(params->denoise) || !std::isfinite(params->sharpen) || !std::isfinite(params->detail) ||
        !std::isfinite(params->vibrance)) {
      return fail(PP_ERROR_INVALID_ARGUMENT, "Enhancement parameters must be finite");
    }
    p.upscale = params->upscale;
    p.denoise = params->denoise;
    p.sharpen = params->sharpen;
    p.detail = params->detail;
    p.autoTone = params->auto_tone != 0;
    p.vibrance = params->vibrance;
  }
  std::vector<const photoplus::ImageBuffer*> buffers;
  buffers.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    if (frames[i] == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null frame in burst");
    buffers.push_back(&frames[i]->buffer);
  }
  photoplus::enhance::Report r;
  auto result = photoplus::enhance::run(buffers, p, token ? &token->token : nullptr, &r,
                                        destination != nullptr ? &destination->buffer : nullptr);
  if (!result) return fail(result.error());
  if (out != nullptr) *out = new pp_image{std::move(result).value()};
  if (report != nullptr) {
    report->frames_input = r.framesInput;
    report->frames_used = r.framesUsed;
    report->reference_index = r.referenceIndex;
    report->noise_before = r.noiseBefore;
    report->noise_after = r.noiseAfter;
    report->blur_before = r.blurBefore;
    report->deblur_sigma = r.deblurSigma;
  }
  return succeed();
}
}  // namespace

pp_status pp_enhance(const pp_image* const* frames, size_t count, const pp_enhance_params* params,
                     const pp_cancel_token* token, pp_image** out, pp_enhance_report* report) {
  return guarded([&]() -> pp_status {
    if (out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "out is null");
    *out = nullptr;
    return enhanceImpl(frames, count, params, token, out, nullptr, report);
  });
}

pp_status pp_enhance_into(const pp_image* const* frames, size_t count, const pp_enhance_params* params,
                          const pp_cancel_token* token, pp_image* destination, pp_enhance_report* report) {
  return guarded([&]() -> pp_status {
    if (destination == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "destination is null");
    return enhanceImpl(frames, count, params, token, nullptr, destination, report);
  });
}

pp_status pp_analyze_quality(const pp_image* image, pp_quality_report* out) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "image or out is null");
    auto ycc = photoplus::toYcc(image->buffer, nullptr);
    if (!ycc) return fail(ycc.error());
    auto q = photoplus::enhance::analyze(ycc->y, nullptr);
    if (!q) return fail(q.error());
    out->noise_sigma = q->noiseSigma;
    out->blur_sigma = q->blurSigma;
    out->sharpness = q->sharpness;
    out->mean_luma = q->meanLuma;
    out->highlights_clipped = q->highlightsClipped;
    out->shadows_clipped = q->shadowsClipped;
    return succeed();
  });
}

pp_status pp_focus_peaking(const uint8_t* luma, int32_t width, int32_t height, int32_t row_stride,
                           int32_t pixel_stride, float threshold, uint32_t argb, uint32_t* out, int32_t out_width,
                           int32_t out_height, float* sharpness) {
  return guarded([&]() -> pp_status {
    if (luma == nullptr || out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null buffer");
    if (width < 8 || height < 8 || out_width <= 0 || out_height <= 0 || out_width > width || out_height > height ||
        pixel_stride < 1 || row_stride < width * pixel_stride || !std::isfinite(threshold)) {
      return fail(PP_ERROR_INVALID_ARGUMENT, "Invalid focus peaking geometry");
    }
    // Sobel magnitude, max-pooled over each overlay cell so thin edges
    // between sample points are never missed. Reads the camera plane in place.
    const float t = std::clamp(threshold, 0.01f, 1.0f) * 4.0f * 255.0f;
    auto px = [&](int32_t x, int32_t y) -> int32_t {
      x = std::clamp(x, 0, width - 1);
      y = std::clamp(y, 0, height - 1);
      return luma[static_cast<size_t>(y) * static_cast<size_t>(row_stride) +
                  static_cast<size_t>(x) * static_cast<size_t>(pixel_stride)];
    };
    double centreEnergy = 0.0;
    size_t centreCount = 0;
    for (int32_t oy = 0; oy < out_height; ++oy) {
      const int32_t ya = oy * height / out_height, yb = std::max(ya + 1, (oy + 1) * height / out_height);
      uint32_t* row = out + static_cast<size_t>(oy) * static_cast<size_t>(out_width);
      for (int32_t ox = 0; ox < out_width; ++ox) {
        const int32_t xa = ox * width / out_width, xb = std::max(xa + 1, (ox + 1) * width / out_width);
        float best = 0.0f;
        for (int32_t y = ya; y < yb; ++y) {
          for (int32_t x = xa; x < xb; ++x) {
            const int32_t gx = -px(x - 1, y - 1) - 2 * px(x - 1, y) - px(x - 1, y + 1) + px(x + 1, y - 1) +
                               2 * px(x + 1, y) + px(x + 1, y + 1);
            const int32_t gy = -px(x - 1, y - 1) - 2 * px(x, y - 1) - px(x + 1, y - 1) + px(x - 1, y + 1) +
                               2 * px(x, y + 1) + px(x + 1, y + 1);
            best = std::max(best, std::sqrt(static_cast<float>(gx * gx + gy * gy)));
          }
        }
        row[ox] = best > t ? argb : 0u;
        if (ox > out_width / 3 && ox < 2 * out_width / 3 && oy > out_height / 3 && oy < 2 * out_height / 3) {
          centreEnergy += static_cast<double>(best) * best;
          ++centreCount;
        }
      }
    }
    if (sharpness != nullptr) {
      const double rms = centreCount ? std::sqrt(centreEnergy / static_cast<double>(centreCount)) : 0.0;
      *sharpness = static_cast<float>(std::clamp(rms / 300.0, 0.0, 1.0));
    }
    return succeed();
  });
}

}  // extern "C"
