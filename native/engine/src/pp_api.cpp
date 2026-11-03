// C ABI implementation. Every entry point is a firewall: C++ exceptions must
// never escape into JNI or other C callers, so each function catches all and
// converts to a status code plus a thread-local message.

#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <utility>

#include "photoplus/cancellation.hpp"
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

}  // extern "C"
