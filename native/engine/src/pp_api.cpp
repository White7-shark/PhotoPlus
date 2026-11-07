// C ABI implementation. Every entry point is a firewall: C++ exceptions must
// never escape into JNI or other C callers, so each function catches all and
// converts to a status code plus a thread-local message.

#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <utility>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/enhance.hpp"
#include "photoplus/face.hpp"
#include "photoplus/video.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/edit.hpp"
#include "photoplus/image.hpp"
#include "photoplus/lens.hpp"
#include "photoplus/memory.hpp"
#include "photoplus/pipeline.hpp"
#include "photoplus/tensor.hpp"
#include <cstdio>
#include <cstdlib>
#include "photoplus/gpu.hpp"
#include "photoplus/filters.hpp"
#include "photoplus/codec.hpp"
#include "photoplus/portrait.hpp"
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
    case PP_ERROR_IO: return "IO_ERROR";
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

float pp_cancel_token_progress(const pp_cancel_token* token) {
  return token == nullptr ? 0.0f : token->token.progress();
}

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

pp_status pp_process_ex(const pp_image* input, const pp_op* ops, size_t op_count, const pp_render_options* options,
                        const pp_cancel_token* token, pp_image** out) {
  return guarded([&]() -> pp_status {
    if (out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "out is null");
    *out = nullptr;
    if (input == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "input is null");
    photoplus::RenderOptions o;
    if (options != nullptr) {
      o.maxEdge = options->max_edge;
      o.backend = options->backend;
    }
    auto result = photoplus::runPipeline(input->buffer, ops, op_count, o, token ? &token->token : nullptr);
    if (!result) return fail(result.error());
    *out = new pp_image{std::move(result).value()};
    return succeed();
  });
}

pp_status pp_process_estimate(int32_t width, int32_t height, int32_t format, const pp_op* ops, size_t op_count,
                              const pp_render_options* options, uint64_t* peak_bytes) {
  return guarded([&]() -> pp_status {
    if (peak_bytes == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "peak_bytes is null");
    *peak_bytes = 0;
    if (!photoplus::isValidFormat(format)) return fail(PP_ERROR_UNSUPPORTED_FORMAT, "Unknown pixel format");
    photoplus::RenderOptions o;
    if (options != nullptr) {
      o.maxEdge = options->max_edge;
      o.backend = options->backend;
    }
    auto r = photoplus::estimatePipeline(width, height, static_cast<photoplus::PixelFormat>(format), ops, op_count, o);
    if (!r) return fail(r.error());
    *peak_bytes = *r;
    return succeed();
  });
}

void pp_memory_get_stats(pp_memory_stats* out) {
  if (out != nullptr) *out = photoplus::MemoryTracker::instance().stats();
}

void pp_memory_set_budget(uint64_t bytes) { photoplus::MemoryTracker::instance().setBudget(bytes); }

void pp_memory_reset_peak(void) { photoplus::MemoryTracker::instance().resetPeak(); }

void pp_memory_trim(void) { photoplus::MemoryTracker::instance().trim(); }

void pp_memory_set_reuse_limit(uint64_t bytes) { photoplus::MemoryTracker::instance().setReuseLimit(bytes); }

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
  out->fixed_reference = 0;
  out->measured_blur = 0.0f;
  out->noise_sigma = 0.0f;
  std::fill(std::begin(out->reserved), std::end(out->reserved), 0.0f);
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
    if (!std::isfinite(params->measured_blur) || !std::isfinite(params->noise_sigma)) {
      return fail(PP_ERROR_INVALID_ARGUMENT, "Enhancement parameters must be finite");
    }
    p.fixedReference = params->fixed_reference != 0;
    p.measuredBlur = std::clamp(params->measured_blur, 0.0f, 8.0f);
    p.noiseSigma = std::clamp(params->noise_sigma, 0.0f, 0.5f);
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

namespace {
void fillLens(const photoplus::lens::Analysis& a, pp_lens_report* out) {
  out->detail_blur = a.detailBlur;
  out->corner_blur = a.cornerBlur;
  out->fringe_px = a.fringePx;
  out->noise = a.noise;
  out->grade = a.grade;
  out->measured_tiles = a.measuredTiles;
  out->reliable = a.reliable ? 1 : 0;
}
}  // namespace

pp_status pp_place_prop(pp_image* image, const pp_image* prop, const pp_prop_params* params,
                        const pp_cancel_token* token, int64_t* covered_pixels) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || prop == nullptr || params == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null argument");
    photoplus::edit::PropParams pp;
    pp.cx = params->cx;
    pp.cy = params->cy;
    pp.width = params->width;
    pp.rotationDeg = params->rotation_deg;
    pp.flip = params->flip != 0;
    pp.opacity = params->opacity;
    pp.blend = params->blend;
    pp.shadow = params->shadow;
    photoplus::edit::PropReport r;
    if (photoplus::Error e = photoplus::edit::placeProp(image->buffer, prop->buffer, pp, token ? &token->token : nullptr, &r);
        e.status != PP_OK) {
      return fail(e);
    }
    if (covered_pixels != nullptr) *covered_pixels = r.coveredPixels;
    return succeed();
  });
}

pp_status pp_frame(const pp_image* photo, pp_image* canvas, const pp_frame_params* params,
                   const pp_cancel_token* token) {
  return guarded([&]() -> pp_status {
    if (photo == nullptr || canvas == nullptr || params == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null argument");
    if (params->backdrop < 0 || params->backdrop > 3) return fail(PP_ERROR_INVALID_ARGUMENT, "Unknown backdrop");
    photoplus::edit::FrameParams f;
    f.shape = static_cast<photoplus::edit::Shape>(params->shape);
    f.shapeParam = params->shape_param;
    f.rotationDeg = params->rotation_deg;
    for (int i = 0; i < 4; ++i) f.window[i] = params->window[i];
    f.backdrop = static_cast<photoplus::edit::Backdrop>(params->backdrop);
    f.colorA = params->color_a;
    f.colorB = params->color_b;
    f.border = params->border;
    f.borderColor = params->border_color;
    f.metallicBorder = params->metallic_border != 0;
    f.outlineGap = params->outline_gap;
    f.outlineWidth = params->outline_width;
    f.outlineColor = params->outline_color;
    f.shadow = params->shadow;
    f.shadowOffset = params->shadow_offset;
    f.shadowBlur = params->shadow_blur;
    f.glow = params->glow;
    f.glowColor = params->glow_color;
    f.zoom = params->zoom;
    f.offsetX = params->offset_x;
    f.offsetY = params->offset_y;
    f.decoration = params->decoration;
    if (photoplus::Error e = photoplus::edit::frame(photo->buffer, canvas->buffer, f, token ? &token->token : nullptr);
        e.status != PP_OK) {
      return fail(e);
    }
    return succeed();
  });
}

pp_status pp_overlay(pp_image* image, const pp_image* stamp, int32_t x, int32_t y, float opacity,
                     int64_t* visible_pixels) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || stamp == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null argument");
    photoplus::edit::OverlayReport r;
    if (photoplus::Error e = photoplus::edit::overlay(image->buffer, stamp->buffer, x, y, opacity, &r); e.status != PP_OK) {
      return fail(e);
    }
    if (visible_pixels != nullptr) *visible_pixels = r.visiblePixels;
    return succeed();
  });
}

pp_status pp_lens_analyze(const pp_image* image, const pp_cancel_token* token, pp_lens_report* out) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "image or out is null");
    auto a = photoplus::lens::analyze(image->buffer, token != nullptr ? &token->token : nullptr);
    if (!a) return fail(a.error());
    fillLens(*a, out);
    return succeed();
  });
}

void pp_transform_default_params(pp_transform_params* out) {
  if (out == nullptr) return;
  *out = pp_transform_params{1, 1, 1.0f, 0.4f};
}

pp_status pp_transform(const pp_image* input, pp_image* output, const pp_transform_params* params,
                       const pp_cancel_token* token, pp_transform_report* report) {
  return guarded([&]() -> pp_status {
    if (input == nullptr || output == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "input or output is null");
    pp_transform_params p{};
    pp_transform_default_params(&p);
    if (params != nullptr) p = *params;
    if (!std::isfinite(p.strength) || !std::isfinite(p.detail)) {
      return fail(PP_ERROR_INVALID_ARGUMENT, "strength and detail must be finite");
    }
    photoplus::lens::Params lp;
    lp.upscale = p.upscale;
    lp.enrich = p.enrich != 0;
    lp.strength = p.strength;
    lp.detail = std::clamp(p.detail, 0.0f, 1.0f);
    photoplus::lens::Report r;
    if (photoplus::Error e = photoplus::lens::restore(input->buffer, output->buffer, lp,
                                                      token != nullptr ? &token->token : nullptr, &r);
        e.status != PP_OK) {
      return fail(e);
    }
    if (report != nullptr) {
      fillLens(r.before, &report->before);
      report->fringe_fixed_px = r.fringeFixedPx;
      report->deblur_centre = r.deblurCentre;
      report->deblur_corner = r.deblurCorner;
      report->noise_before = r.noiseBefore;
      report->noise_after = r.noiseAfter;
    }
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

void pp_portrait_default_params(pp_portrait_params* out) {
  if (out == nullptr) return;
  const photoplus::portrait::Params d;
  out->blur = d.blur;
  out->retouch = d.retouch;
  out->brighten = d.brighten;
  out->pop = d.pop;
  out->smooth = d.smooth;
  out->blemish = d.blemish;
  out->even = d.even;
  out->bg_light = d.bgLight;
  out->bg_color = d.bgColor;
  out->edge = d.edge;
  std::fill(std::begin(out->reserved), std::end(out->reserved), 0.0f);
}

pp_status pp_portrait(pp_image* image, const pp_image* subject, const pp_image* faces, const pp_image* protect,
                      const pp_image* depth, const pp_portrait_params* params, const pp_cancel_token* token,
                      pp_portrait_report* report) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || subject == nullptr || params == nullptr) {
      return fail(PP_ERROR_INVALID_ARGUMENT, "image, subject and params are required");
    }
    photoplus::portrait::Params p;
    p.blur = params->blur;
    p.retouch = params->retouch;
    p.brighten = params->brighten;
    p.pop = params->pop;
    p.smooth = params->smooth;
    p.blemish = params->blemish;
    p.even = params->even;
    p.bgLight = params->bg_light;
    p.bgColor = params->bg_color;
    p.edge = params->edge;
    for (float v : {p.blur, p.retouch, p.brighten, p.pop, p.smooth, p.blemish, p.even, p.bgLight, p.bgColor, p.edge}) {
      if (!std::isfinite(v)) return fail(PP_ERROR_INVALID_ARGUMENT, "Non-finite portrait parameter");
    }
    photoplus::portrait::Report r;
    photoplus::Error e = photoplus::portrait::apply(image->buffer, subject->buffer, faces ? &faces->buffer : nullptr,
                                                    protect ? &protect->buffer : nullptr,
                                                    depth ? &depth->buffer : nullptr, p,
                                                    token ? &token->token : nullptr, &r);
    if (e.status != PP_OK) return fail(e);
    if (report != nullptr) {
      report->subject_coverage = r.subjectCoverage;
      report->skin_coverage = r.skinCoverage;
      report->spots_removed = r.spotsRemoved;
      report->blur_radius = r.blurRadius;
      report->focus_depth = r.focusDepth;
      report->depth_estimated = r.depthEstimated ? 1 : 0;
      report->edge_cleaned = r.edgeCleaned;
    }
    return succeed();
  });
}

pp_status pp_video_estimate_motion(const pp_image* previous, const pp_image* current, pp_video_motion* out) {
  return guarded([&]() -> pp_status {
    if (previous == nullptr || current == nullptr || out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null argument");
    auto m = photoplus::video::estimateMotion(previous->buffer, current->buffer, nullptr);
    if (!m) return fail(m.error());
    *out = pp_video_motion{};
    out->a = static_cast<float>(m->m.a);
    out->b = static_cast<float>(m->m.b);
    out->tx = static_cast<float>(m->m.tx);
    out->ty = static_cast<float>(m->m.ty);
    out->confidence = m->confidence;
    return succeed();
  });
}

pp_status pp_video_stabilize(const pp_video_motion* motions, size_t count, int32_t width, int32_t height, float strength,
                             float max_zoom, float* warps, float* report) {
  return guarded([&]() -> pp_status {
    if (motions == nullptr || warps == nullptr || count == 0 || count > 100000) {
      return fail(PP_ERROR_INVALID_ARGUMENT, "Need motions and room for the warps");
    }
    std::vector<photoplus::video::Motion> ms(count);
    for (size_t i = 0; i < count; ++i) {
      ms[i].m = {motions[i].a, motions[i].b, motions[i].tx, motions[i].ty};
      ms[i].confidence = motions[i].confidence;
    }
    if (count > 0) ms[0].m = {};
    auto plan = photoplus::video::stabilize(ms, width, height, strength, max_zoom);
    if (!plan) return fail(plan.error());
    for (size_t i = 0; i < count; ++i)
      for (int k = 0; k < 6; ++k) warps[i * 6 + static_cast<size_t>(k)] = static_cast<float>(plan->warps[i][static_cast<size_t>(k)]);
    if (report != nullptr) {
      report[0] = plan->zoom;
      report[1] = plan->shakeBefore;
      report[2] = plan->shakeAfter;
    }
    return succeed();
  });
}

pp_status pp_video_warp(const pp_image* src, const float warp[6], pp_image* dst, const pp_cancel_token* token) {
  return guarded([&]() -> pp_status {
    if (src == nullptr || warp == nullptr || dst == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null argument");
    photoplus::video::Affine m{};
    for (int k = 0; k < 6; ++k) m[static_cast<size_t>(k)] = warp[k];
    const photoplus::Error e = photoplus::video::warp(src->buffer, m, dst->buffer, token ? &token->token : nullptr);
    return e.status == PP_OK ? succeed() : fail(e);
  });
}

pp_status pp_video_fit_curves(const pp_image* before, const pp_image* after, uint8_t curves[768]) {
  return guarded([&]() -> pp_status {
    if (before == nullptr || after == nullptr || curves == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null argument");
    auto c = photoplus::video::fitCurves(before->buffer, after->buffer);
    if (!c) return fail(c.error());
    for (int ch = 0; ch < 3; ++ch) std::copy(c->rgb[static_cast<size_t>(ch)].begin(), c->rgb[static_cast<size_t>(ch)].end(), curves + ch * 256);
    return succeed();
  });
}

pp_status pp_video_apply_curves(pp_image* image, const uint8_t curves[768], const pp_cancel_token* token) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || curves == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null argument");
    photoplus::video::Curves c;
    for (int ch = 0; ch < 3; ++ch) std::copy(curves + ch * 256, curves + ch * 256 + 256, c.rgb[static_cast<size_t>(ch)].begin());
    const photoplus::Error e = photoplus::video::applyCurves(image->buffer, c, token ? &token->token : nullptr);
    return e.status == PP_OK ? succeed() : fail(e);
  });
}

pp_status pp_video_enhance_frame(const pp_image* frame, const pp_image* previous, const pp_image* next,
                                 const pp_video_motion* from_previous, const pp_video_motion* to_next,
                                 const pp_video_frame_params* params, const uint8_t curves[768], pp_image* dst,
                                 const pp_cancel_token* token) {
  return guarded([&]() -> pp_status {
    if (frame == nullptr || params == nullptr || dst == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null argument");
    auto sim = [](const pp_video_motion* m) {
      return m == nullptr ? photoplus::video::Similarity{} : photoplus::video::Similarity{m->a, m->b, m->tx, m->ty};
    };
    photoplus::video::FrameParams fp;
    fp.noiseSigma = params->noise_sigma;
    fp.blurSigma = params->blur_sigma;
    fp.denoise = params->denoise;
    fp.sharpen = params->sharpen;
    photoplus::video::Curves c;
    if (curves != nullptr) {
      for (int ch = 0; ch < 3; ++ch) std::copy(curves + ch * 256, curves + ch * 256 + 256, c.rgb[static_cast<size_t>(ch)].begin());
    }
    const photoplus::Error e = photoplus::video::enhanceFrame(
        frame->buffer, previous ? &previous->buffer : nullptr, next ? &next->buffer : nullptr, sim(from_previous),
        sim(to_next), fp, curves ? &c : nullptr, dst->buffer, token ? &token->token : nullptr);
    return e.status == PP_OK ? succeed() : fail(e);
  });
}

pp_status pp_video_steady_mask(pp_image* previous, const pp_image* current, float smoothing) {
  return guarded([&]() -> pp_status {
    if (previous == nullptr || current == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null argument");
    const photoplus::Error e = photoplus::video::steadyMask(previous->buffer, current->buffer, smoothing);
    return e.status == PP_OK ? succeed() : fail(e);
  });
}

void pp_face_default_params(pp_face_params* out) {
  if (out == nullptr) return;
  *out = pp_face_params{};
}

pp_status pp_face_adjust(pp_image* image, const pp_face* faces, size_t count, const pp_face_params* params,
                         const pp_cancel_token* token, pp_face_report* report) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || params == nullptr || (count > 0 && faces == nullptr)) {
      return fail(PP_ERROR_INVALID_ARGUMENT, "image and params are required");
    }
    if (count > 32) return fail(PP_ERROR_INVALID_ARGUMENT, "At most 32 faces");
    std::vector<photoplus::portrait::FaceLandmarks> lm(count);
    for (size_t i = 0; i < count; ++i) {
      std::copy(std::begin(faces[i].points), std::end(faces[i].points), lm[i].points.begin());
      lm[i].detailed = faces[i].detailed != 0;
    }
    photoplus::portrait::FaceParams p;
    p.eyes = params->eyes;
    p.teeth = params->teeth;
    p.lips = params->lips;
    p.underEye = params->under_eye;
    p.shine = params->shine;
    p.slim = params->slim;
    p.faceLight = params->face_light;
    p.studio = params->studio_light;
    p.lightAngle = params->light_angle;
    photoplus::portrait::FaceReport r;
    const photoplus::Error e = photoplus::portrait::adjustFaces(image->buffer, lm, p, token ? &token->token : nullptr, &r);
    if (e.status != PP_OK) return fail(e);
    if (report != nullptr) {
      *report = pp_face_report{};
      report->faces = r.faces;
      report->adjusted = r.adjusted;
      report->light_gain = r.lightGain;
    }
    return succeed();
  });
}

pp_status pp_subject_mask(const pp_image* image, const float* points, size_t count, const pp_cancel_token* token,
                          pp_image** out) {
  return guarded([&]() -> pp_status {
    if (out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "out is null");
    *out = nullptr;
    if (image == nullptr || (count > 0 && points == nullptr) || count > 64) {
      return fail(PP_ERROR_INVALID_ARGUMENT, "Invalid image or points");
    }
    std::vector<std::array<float, 2>> pts;
    for (size_t i = 0; i < count; ++i) {
      const float x = points[2 * i], y = points[2 * i + 1];
      if (!std::isfinite(x) || !std::isfinite(y)) return fail(PP_ERROR_INVALID_ARGUMENT, "Non-finite point");
      pts.push_back({std::clamp(x, 0.0f, 1.0f), std::clamp(y, 0.0f, 1.0f)});
    }
    auto mask = photoplus::portrait::subjectFromPoints(image->buffer, pts, token ? &token->token : nullptr);
    if (!mask) return fail(mask.error());
    *out = new pp_image{std::move(mask).value()};
    return succeed();
  });
}

pp_status pp_style_analyze(const pp_image* image, pp_style* out) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "image and out are required");
    auto s = photoplus::portrait::analyzeStyle(image->buffer, nullptr);
    if (!s) return fail(s.error());
    for (int i = 0; i < PP_STYLE_QUANTILES; ++i) out->luma[i] = s->luma[static_cast<size_t>(i)];
    for (int i = 0; i < 3; ++i) {
      out->zone_cb[i] = s->zoneCb[static_cast<size_t>(i)];
      out->zone_cr[i] = s->zoneCr[static_cast<size_t>(i)];
    }
    out->chroma_std[0] = s->chromaStd[0];
    out->chroma_std[1] = s->chromaStd[1];
    out->vignette = s->vignette;
    out->grain = s->grain;
    out->mean_luma = s->meanLuma;
    return succeed();
  });
}

pp_status pp_style_apply(pp_image* image, const pp_style* target, float strength, const pp_cancel_token* token) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || target == nullptr || !std::isfinite(strength)) {
      return fail(PP_ERROR_INVALID_ARGUMENT, "image and target are required");
    }
    photoplus::portrait::Style s;
    for (int i = 0; i < PP_STYLE_QUANTILES; ++i) {
      const float v = target->luma[i];
      if (!std::isfinite(v) || v < 0.0f || v > 1.0f) return fail(PP_ERROR_INVALID_ARGUMENT, "Invalid look");
      s.luma[static_cast<size_t>(i)] = v;
    }
    // std::clamp passes NaN through, so every field is checked first.
    for (int i = 0; i < 3; ++i) {
      if (!std::isfinite(target->zone_cb[i]) || !std::isfinite(target->zone_cr[i])) {
        return fail(PP_ERROR_INVALID_ARGUMENT, "Invalid look");
      }
      s.zoneCb[static_cast<size_t>(i)] = std::clamp(target->zone_cb[i], -0.5f, 0.5f);
      s.zoneCr[static_cast<size_t>(i)] = std::clamp(target->zone_cr[i], -0.5f, 0.5f);
    }
    if (!std::isfinite(target->chroma_std[0]) || !std::isfinite(target->chroma_std[1]) || !std::isfinite(target->mean_luma)) {
      return fail(PP_ERROR_INVALID_ARGUMENT, "Invalid look");
    }
    s.chromaStd = {std::clamp(target->chroma_std[0], 0.0f, 0.5f), std::clamp(target->chroma_std[1], 0.0f, 0.5f)};
    s.vignette = std::isfinite(target->vignette) ? std::clamp(target->vignette, 0.05f, 2.0f) : 1.0f;
    s.grain = std::isfinite(target->grain) ? std::clamp(target->grain, 0.0f, 0.2f) : 0.0f;
    s.meanLuma = target->mean_luma;
    photoplus::Error e = photoplus::portrait::applyStyle(image->buffer, s, strength, token ? &token->token : nullptr);
    if (e.status != PP_OK) return fail(e);
    return succeed();
  });
}


// ---- Codec ----------------------------------------------------------------------

namespace {

photoplus::codec::DecodeOptions decodeOptions(const pp_decode_options* o) {
  photoplus::codec::DecodeOptions d;
  if (o != nullptr) {
    d.maxEdge = o->max_edge;
    d.applyOrientation = o->apply_orientation != 0;
  }
  return d;
}

void fillDecodeInfo(const photoplus::codec::DecodeInfo& i, pp_decode_info* out) {
  if (out == nullptr) return;
  *out = pp_decode_info{};
  out->width = i.width;
  out->height = i.height;
  out->source_width = i.sourceWidth;
  out->source_height = i.sourceHeight;
  out->orientation = i.orientation;
  out->codec = static_cast<int32_t>(i.format);
  out->has_alpha = i.hasAlpha ? 1 : 0;
}

}  // namespace

pp_status pp_decode_memory(const void* data, size_t size, const pp_decode_options* options,
                           const pp_cancel_token* token, pp_image** out, pp_decode_info* info) {
  return guarded([&]() -> pp_status {
    if (out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "out is null");
    *out = nullptr;
    photoplus::codec::DecodeInfo i;
    auto r = photoplus::codec::decode(static_cast<const uint8_t*>(data), size, decodeOptions(options),
                                      token ? &token->token : nullptr, &i);
    if (!r) return fail(r.error());
    fillDecodeInfo(i, info);
    *out = new pp_image{std::move(r).value()};
    return succeed();
  });
}

pp_status pp_decode_file(const char* path, const pp_decode_options* options, const pp_cancel_token* token,
                         pp_image** out, pp_decode_info* info) {
  return guarded([&]() -> pp_status {
    if (out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "out is null");
    *out = nullptr;
    photoplus::codec::DecodeInfo i;
    auto r = photoplus::codec::decodeFile(path, decodeOptions(options), token ? &token->token : nullptr, &i);
    if (!r) return fail(r.error());
    fillDecodeInfo(i, info);
    *out = new pp_image{std::move(r).value()};
    return succeed();
  });
}

pp_status pp_encode_memory(const pp_image* image, int32_t codec, int32_t quality, void** data, size_t* size) {
  return guarded([&]() -> pp_status {
    if (data == nullptr || size == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "data or size is null");
    *data = nullptr;
    *size = 0;
    if (image == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "image is null");
    auto r = photoplus::codec::encode(image->buffer, static_cast<photoplus::codec::Format>(codec), quality);
    if (!r) return fail(r.error());
    void* mem = std::malloc(r->size());
    if (mem == nullptr) return fail(PP_ERROR_OUT_OF_MEMORY, "Out of memory");
    std::memcpy(mem, r->data(), r->size());
    *data = mem;
    *size = r->size();
    return succeed();
  });
}

pp_status pp_encode_file(const pp_image* image, const char* path, int32_t codec, int32_t quality) {
  return guarded([&]() -> pp_status {
    if (image == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "image is null");
    if (auto e = photoplus::codec::encodeFile(image->buffer, path, static_cast<photoplus::codec::Format>(codec), quality);
        e.status != PP_OK) {
      return fail(e);
    }
    return succeed();
  });
}

void pp_free(void* data) { std::free(data); }

// ---- Geometry, colour, compositing, masks --------------------------------------------

pp_status pp_warp_perspective(const pp_image* image, const float quad[8], int32_t width, int32_t height,
                              const pp_cancel_token* token, pp_image** out) {
  return guarded([&]() -> pp_status {
    if (out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "out is null");
    *out = nullptr;
    if (image == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "image is null");
    auto r = photoplus::ops::warpPerspective(image->buffer, quad, width, height, token ? &token->token : nullptr);
    if (!r) return fail(r.error());
    *out = new pp_image{std::move(r).value()};
    return succeed();
  });
}

pp_status pp_detect_keystone(const pp_image* image, const pp_cancel_token* token, float* vertical) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || vertical == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "Null argument");
    *vertical = photoplus::ops::detectVerticalKeystone(image->buffer, token ? &token->token : nullptr);
    if (token != nullptr && token->token.isCancelled()) return fail(PP_ERROR_CANCELLED, "Processing was cancelled");
    return succeed();
  });
}

pp_status pp_apply_lut3d(pp_image* image, const float* table, int32_t size, float strength,
                         const pp_cancel_token* token) {
  return guarded([&]() -> pp_status {
    if (image == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "image is null");
    if (auto e = photoplus::ops::applyLut3d(image->buffer, table, size, strength, token ? &token->token : nullptr);
        e.status != PP_OK) {
      return fail(e);
    }
    return succeed();
  });
}

pp_status pp_color_matrix(pp_image* image, const float matrix[12], int32_t linear_light, const pp_cancel_token* token) {
  return guarded([&]() -> pp_status {
    if (image == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "image is null");
    if (auto e = photoplus::ops::colorMatrix(image->buffer, matrix, linear_light != 0, token ? &token->token : nullptr);
        e.status != PP_OK) {
      return fail(e);
    }
    return succeed();
  });
}

pp_status pp_blend(pp_image* image, const pp_image* layer, const pp_image* mask, int32_t x, int32_t y,
                   int32_t mode, float opacity, const pp_cancel_token* token) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || layer == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "image or layer is null");
    if (mode < 0 || mode >= photoplus::ops::kBlendModeCount) return fail(PP_ERROR_INVALID_ARGUMENT, "Unknown blend mode");
    if (auto e = photoplus::ops::blend(image->buffer, layer->buffer, mask ? &mask->buffer : nullptr, x, y,
                                       static_cast<photoplus::ops::BlendMode>(mode), opacity,
                                       token ? &token->token : nullptr);
        e.status != PP_OK) {
      return fail(e);
    }
    return succeed();
  });
}

pp_status pp_mask_adjust(pp_image* mask, int32_t op, float amount, const pp_cancel_token* token) {
  return guarded([&]() -> pp_status {
    if (mask == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "mask is null");
    if (op < PP_MASK_INVERT || op > PP_MASK_THRESHOLD) return fail(PP_ERROR_INVALID_ARGUMENT, "Unknown mask operation");
    if (auto e = photoplus::ops::maskAdjust(mask->buffer, static_cast<photoplus::ops::MaskOp>(op), amount,
                                            token ? &token->token : nullptr);
        e.status != PP_OK) {
      return fail(e);
    }
    return succeed();
  });
}

pp_status pp_mask_refine(const pp_image* mask, const pp_image* guide, int32_t radius, float eps,
                         const pp_cancel_token* token, pp_image** out) {
  return guarded([&]() -> pp_status {
    if (out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "out is null");
    *out = nullptr;
    if (mask == nullptr || guide == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "mask or guide is null");
    auto r = photoplus::ops::maskRefine(mask->buffer, guide->buffer, radius, eps, token ? &token->token : nullptr);
    if (!r) return fail(r.error());
    *out = new pp_image{std::move(r).value()};
    return succeed();
  });
}

// ---- Machine learning --------------------------------------------------------------

pp_status pp_image_to_tensor(const pp_image* image, const pp_tensor_spec* spec, float* out, size_t out_count,
                             const pp_cancel_token* token, pp_tensor_placement* placement) {
  return guarded([&]() -> pp_status {
    if (image == nullptr || spec == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "image or spec is null");
    if (spec->layout != PP_TENSOR_NHWC && spec->layout != PP_TENSOR_NCHW) return fail(PP_ERROR_INVALID_ARGUMENT, "Unknown layout");
    if (spec->fit < PP_FIT_STRETCH || spec->fit > PP_FIT_CROP_CENTER) return fail(PP_ERROR_INVALID_ARGUMENT, "Unknown fit");
    photoplus::ml::TensorSpec s;
    s.width = spec->width;
    s.height = spec->height;
    s.layout = static_cast<photoplus::ml::Layout>(spec->layout);
    s.fit = static_cast<photoplus::ml::Fit>(spec->fit);
    for (int c = 0; c < 3; ++c) s.mean[c] = spec->mean[c], s.std[c] = spec->std[c];
    photoplus::ml::Placement p;
    if (auto e = photoplus::ml::toTensor(image->buffer, s, out, out_count, token ? &token->token : nullptr, &p); e.status != PP_OK) {
      return fail(e);
    }
    if (placement != nullptr) *placement = {p.scaleX, p.scaleY, p.offsetX, p.offsetY};
    return succeed();
  });
}

pp_status pp_tensor_to_mask(const float* data, int32_t width, int32_t height, int32_t channels, int32_t layout,
                            int32_t channel, int32_t activation, int32_t out_width, int32_t out_height,
                            const pp_tensor_placement* placement, int32_t photo_width, int32_t photo_height,
                            const pp_cancel_token* token, pp_image** out) {
  return guarded([&]() -> pp_status {
    if (out == nullptr) return fail(PP_ERROR_INVALID_ARGUMENT, "out is null");
    *out = nullptr;
    if (layout != PP_TENSOR_NHWC && layout != PP_TENSOR_NCHW) return fail(PP_ERROR_INVALID_ARGUMENT, "Unknown layout");
    if (activation < PP_ACTIVATION_NONE || activation > PP_ACTIVATION_SOFTMAX) return fail(PP_ERROR_INVALID_ARGUMENT, "Unknown activation");
    photoplus::ml::Placement p;
    if (placement != nullptr) p = {placement->scale_x, placement->scale_y, placement->offset_x, placement->offset_y};
    auto r = photoplus::ml::toMask(data, width, height, channels, static_cast<photoplus::ml::Layout>(layout), channel,
                                   static_cast<photoplus::ml::Activation>(activation), out_width, out_height,
                                   placement ? &p : nullptr, photo_width, photo_height, token ? &token->token : nullptr);
    if (!r) return fail(r.error());
    *out = new pp_image{std::move(r).value()};
    return succeed();
  });
}

// ---- GPU -------------------------------------------------------------------------

namespace {

void fillGpu(const photoplus::gpu::Status& s, pp_gpu_status* out) {
  if (out == nullptr) return;
  *out = pp_gpu_status{};
  out->compiled = s.compiled ? 1 : 0;
  out->available = s.available ? 1 : 0;
  out->max_difference = s.maxDifference;
  std::snprintf(out->renderer, sizeof(out->renderer), "%s", s.renderer.c_str());
  std::snprintf(out->reason, sizeof(out->reason), "%s", s.reason.c_str());
}

}  // namespace

pp_status pp_gpu_init(pp_gpu_status* out) {
  return guarded([&]() -> pp_status {
    const auto s = photoplus::gpu::init();
    fillGpu(s, out);
    if (!s.available) return fail(PP_ERROR_UNSUPPORTED_FORMAT, s.reason.empty() ? "GPU unavailable" : s.reason);
    return succeed();
  });
}

void pp_gpu_get_status(pp_gpu_status* out) { fillGpu(photoplus::gpu::status(), out); }

void pp_gpu_shutdown(void) { photoplus::gpu::shutdown(); }

}  // extern "C"
