#include "photoplus/pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "photoplus/edit.hpp"
#include "photoplus/filters.hpp"
#include "photoplus/gpu.hpp"
#include "photoplus/operations.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus {

namespace {

bool toInt(float v, int32_t* out) {
  if (!std::isfinite(v) || v < static_cast<float>(std::numeric_limits<int32_t>::min()) ||
      v > static_cast<float>(std::numeric_limits<int32_t>::max())) {
    return false;
  }
  *out = static_cast<int32_t>(std::lround(v));
  return true;
}

Error invalidOp(std::size_t index, const std::string& why) {
  return makeError(PP_ERROR_INVALID_ARGUMENT, "Operation #" + std::to_string(index) + ": " + why);
}

bool finiteParams(const pp_op& op) {
  for (float v : op.p) {
    if (!std::isfinite(v)) return false;
  }
  return true;
}

// Preview scale for an input: 1 at full resolution.
double previewScale(int32_t w, int32_t h, const RenderOptions& options) {
  const int32_t longest = std::max(w, h);
  if (options.maxEdge <= 0 || longest <= options.maxEdge) return 1.0;
  return static_cast<double>(options.maxEdge) / longest;
}

int32_t scaled(int32_t v, double s) { return static_cast<int32_t>(std::lround(v * s)); }

// The op with its pixel-measured settings scaled to the preview.
pp_op scaleOp(const pp_op& op, double s) {
  if (s == 1.0) return op;
  pp_op o = op;
  const float fs = static_cast<float>(s);
  switch (op.type) {
    case PP_OP_CROP:
      for (int i = 0; i < 4; ++i) o.p[i] = static_cast<float>(std::lround(op.p[i] * s));
      o.p[2] = std::max(1.0f, o.p[2]);
      o.p[3] = std::max(1.0f, o.p[3]);
      break;
    case PP_OP_RESIZE:
      o.p[0] = std::max(1.0f, static_cast<float>(std::lround(op.p[0] * s)));
      o.p[1] = std::max(1.0f, static_cast<float>(std::lround(op.p[1] * s)));
      break;
    case PP_OP_SHARPEN:
      o.p[1] = (op.p[1] <= 0.0f ? 1.0f : op.p[1]) * fs;
      break;
    case PP_OP_BLUR:
      o.p[0] = op.p[0] * fs;
      break;
    default:
      break;
  }
  return o;
}

}  // namespace

Result<ImageBuffer> runPipeline(const ImageBuffer& input, const pp_op* ops, std::size_t count,
                                const CancellationToken* token) {
  return runPipeline(input, ops, count, RenderOptions{}, token);
}

Result<ImageBuffer> runPipeline(const ImageBuffer& input, const pp_op* ops, std::size_t count,
                                const RenderOptions& options, const CancellationToken* token) {
  ScopedTimer timer("pipeline.total");
  if (input.empty()) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Input image is empty"));
  if (count > 0 && ops == nullptr) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "ops is null"));
  if (options.maxEdge < 0) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "max_edge must be 0 or more"));
  if (options.backend < PP_BACKEND_AUTO || options.backend > PP_BACKEND_GPU) {
    return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Unknown backend"));
  }
  if (isCancelled(token)) return failure(makeError(PP_ERROR_CANCELLED, "Processing was cancelled"));
  // Validate everything before doing any work: errors are predictable and cheap.
  if (auto check = estimatePipeline(input.width(), input.height(), input.format(), ops, count, options); !check) {
    return failure(check.error());
  }

  const double s = previewScale(input.width(), input.height(), options);
  // One extra step for the preview reduction.
  const float steps = static_cast<float>(count + (s < 1.0 ? 1 : 0));
  float step = 0.0f;
  ImageBuffer current;
  if (s < 1.0) {
    if (token != nullptr) token->beginStep(0.0f, 1.0f / std::max(1.0f, steps));
    auto reduced = ops::resize(input, std::max(1, scaled(input.width(), s)), std::max(1, scaled(input.height(), s)),
                               ops::ResizeFilter::Area, token);
    if (!reduced) return failure(reduced.error());
    current = std::move(reduced).value();
    step = 1.0f;
  } else {
    auto working = cloneImage(input);
    if (!working) return failure(working.error());
    current = std::move(working).value();
  }

  // Settings measured in pixels, scaled to the preview (both paths use these).
  std::vector<pp_op> scaledOps(count);
  for (std::size_t i = 0; i < count; ++i) scaledOps[i] = scaleOp(ops[i], s);

  // GPU: runs of operations the GPU supports go there (previews by default);
  // anything else, and every failure, falls back to the CPU.
  const bool wantGpu = options.backend == PP_BACKEND_GPU || (options.backend == PP_BACKEND_AUTO && options.maxEdge > 0);
  if (options.backend == PP_BACKEND_GPU && !gpu::available()) {
    return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "No verified GPU is available"));
  }

  for (std::size_t i = 0; i < count; ++i) {
    if (isCancelled(token)) return failure(makeError(PP_ERROR_CANCELLED, "Processing was cancelled"));
    if (token != nullptr) token->beginStep(step / std::max(1.0f, steps), (step + 1.0f) / std::max(1.0f, steps));

    if (wantGpu && gpu::available() && gpu::supports(scaledOps[i])) {
      std::size_t end = i;
      while (end < count && gpu::supports(scaledOps[end])) ++end;
      auto ran = gpu::run(current, scaledOps.data() + i, end - i, token);
      if (ran) {
        current = std::move(ran).value();
        step += static_cast<float>(end - i);
        i = end - 1;
        continue;
      }
      if (ran.error().status == PP_ERROR_CANCELLED) return failure(ran.error());
      if (options.backend == PP_BACKEND_GPU) return failure(ran.error());
      // AUTO: the CPU does it instead.
    }

    const pp_op& op = scaledOps[i];
    Error err{PP_OK, {}};
    Result<ImageBuffer> replaced{ImageBuffer{}};
    bool replaces = false;

    switch (op.type) {
      case PP_OP_TONE:
        err = ops::tone(current, {op.p[0], op.p[1], op.p[2]}, token);
        break;
      case PP_OP_GRAYSCALE:
        err = ops::grayscale(current, token);
        break;
      case PP_OP_SATURATION:
        err = ops::saturation(current, op.p[0], token);
        break;
      case PP_OP_FLIP:
        err = ops::flip(current, op.p[0] != 0.0f, op.p[1] != 0.0f, token);
        break;
      case PP_OP_ROTATE90: {
        int32_t turns = 0;
        toInt(op.p[0], &turns);
        replaced = ops::rotate90(current, turns, token);
        replaces = true;
        break;
      }
      case PP_OP_CROP: {
        int32_t x, y, w, h;
        toInt(op.p[0], &x), toInt(op.p[1], &y), toInt(op.p[2], &w), toInt(op.p[3], &h);
        if (s < 1.0) {
          // Rounding in the preview must never push the rectangle off the image.
          x = std::clamp(x, 0, current.width() - 1);
          y = std::clamp(y, 0, current.height() - 1);
          w = std::clamp(w, 1, current.width() - x);
          h = std::clamp(h, 1, current.height() - y);
        }
        replaced = ops::crop(current, x, y, w, h, token);
        replaces = true;
        break;
      }
      case PP_OP_RESIZE: {
        int32_t w, h, f = 0;
        toInt(op.p[0], &w), toInt(op.p[1], &h), toInt(op.p[2], &f);
        replaced = ops::resize(current, w, h, static_cast<ops::ResizeFilter>(f), token);
        replaces = true;
        break;
      }
      case PP_OP_SHARPEN:
        replaced = ops::sharpenLuma(current, op.p[0], op.p[1], op.p[2], token);
        replaces = true;
        break;
      case PP_OP_STRAIGHTEN:
        if (op.p[0] == 0.0f) break;
        replaced = edit::straighten(current, op.p[0], token);
        replaces = true;
        break;
      case PP_OP_WARMTH:
        err = edit::warmth(current, op.p[0], op.p[1], token);
        break;
      case PP_OP_FILTER: {
        int32_t id = 0;
        toInt(op.p[0], &id);
        err = edit::filter(current, id, op.p[1], token);
        break;
      }
      case PP_OP_FOCUS: {
        int32_t kind = 0;
        toInt(op.p[0], &kind);
        err = edit::focus(current, {kind, op.p[1], op.p[2], op.p[3], op.p[4], op.p[5], op.p[6]}, token);
        break;
      }
      case PP_OP_BLUR:
        replaced = ops::blur(current, op.p[0], token);
        replaces = true;
        break;
      case PP_OP_DENOISE:
        err = ops::denoise(current, op.p[0], token);
        break;
      case PP_OP_PERSPECTIVE:
        replaced = ops::keystone(current, op.p[0], op.p[1], token);
        replaces = true;
        break;
      default:
        return failure(invalidOp(i, "unknown operation type " + std::to_string(op.type)));
    }

    if (replaces) {
      if (!replaced) return failure(replaced.error());
      current = std::move(replaced).value();  // frees the previous buffer
    } else if (err.status != PP_OK) {
      return failure(err);
    }
    step += 1.0f;
  }
  if (token != nullptr) token->finish();
  return current;
}

Result<std::uint64_t> estimatePipeline(int32_t width, int32_t height, PixelFormat format, const pp_op* ops,
                                       std::size_t count, const RenderOptions& options) {
  if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension) {
    return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Image size is out of range"));
  }
  if (!isValidFormat(static_cast<int32_t>(format))) return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Unknown format"));
  if (count > 0 && ops == nullptr) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "ops is null"));
  if (count > 256) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Too many operations (256 at most)"));
  const std::uint64_t bpp = static_cast<std::uint64_t>(bytesPerPixel(format));
  // Resampling scratch for all threads: a ring of filtered rows plus one source
  // row each (generous: Lanczos taps when shrinking a lot).
  const std::uint64_t strips = 4ull * (4ull << 20) + 4ull * kMaxDimension * 16ull;
  const std::uint64_t input = static_cast<std::uint64_t>(width) * height * bpp;
  const double s = previewScale(width, height, options);
  std::int64_t w = std::max(1, scaled(width, s)), h = std::max(1, scaled(height, s));
  auto bytes = [&](std::int64_t ww, std::int64_t hh) { return static_cast<std::uint64_t>(ww) * hh * bpp; };
  auto plane = [&](std::int64_t ww, std::int64_t hh) { return static_cast<std::uint64_t>(ww) * hh * 4u; };
  // The caller's input stays alive throughout.
  std::uint64_t peak = input + bytes(w, h) + (s < 1.0 ? strips : 0);
  for (std::size_t i = 0; i < count; ++i) {
    const pp_op& raw = ops[i];
    if (!finiteParams(raw)) return failure(invalidOp(i, "settings must be finite"));
    const pp_op op = scaleOp(raw, s);
    const std::uint64_t cur = bytes(w, h);
    std::uint64_t extra = 0;
    switch (op.type) {
      case PP_OP_TONE:
      case PP_OP_GRAYSCALE:
      case PP_OP_SATURATION:
      case PP_OP_FLIP:
      case PP_OP_WARMTH:
      case PP_OP_FILTER:
        break;
      case PP_OP_ROTATE90: {
        int32_t turns = 0;
        if (!toInt(op.p[0], &turns)) return failure(invalidOp(i, "rotation must be finite"));
        extra = cur;
        if (((turns % 4) + 4) % 4 % 2 == 1) std::swap(w, h);
        break;
      }
      case PP_OP_CROP: {
        int32_t x, y, cw, ch;
        if (!toInt(op.p[0], &x) || !toInt(op.p[1], &y) || !toInt(op.p[2], &cw) || !toInt(op.p[3], &ch)) {
          return failure(invalidOp(i, "crop rectangle must be finite"));
        }
        if (s == 1.0 && (x < 0 || y < 0 || cw <= 0 || ch <= 0 || x + static_cast<std::int64_t>(cw) > w ||
                         y + static_cast<std::int64_t>(ch) > h)) {
          return failure(invalidOp(i, "crop rectangle is outside the image"));
        }
        cw = std::max(1, cw), ch = std::max(1, ch);
        w = std::min<std::int64_t>(cw, w), h = std::min<std::int64_t>(ch, h);
        extra = bytes(w, h);
        break;
      }
      case PP_OP_RESIZE: {
        int32_t rw, rh, f;
        if (!toInt(op.p[0], &rw) || !toInt(op.p[1], &rh) || !toInt(op.p[2], &f)) {
          return failure(invalidOp(i, "resize settings must be finite"));
        }
        if (rw <= 0 || rh <= 0 || rw > kMaxDimension || rh > kMaxDimension) return failure(invalidOp(i, "resize target is out of range"));
        if (f < PP_RESIZE_AUTO || f > PP_RESIZE_LANCZOS) return failure(invalidOp(i, "unknown resize filter"));
        w = rw, h = rh;
        extra = bytes(w, h) + strips;
        break;
      }
      case PP_OP_SHARPEN:
        extra = cur + 2 * plane(w, h) + strips;
        break;
      case PP_OP_STRAIGHTEN:
        if (op.p[0] < -45.0f || op.p[0] > 45.0f) return failure(invalidOp(i, "straighten angle must be -45..45"));
        extra = 2 * cur;  // the rotated image, then the crop of it
        break;
      case PP_OP_FOCUS: {
        int32_t kind = 0;
        if (!toInt(op.p[0], &kind) || (kind != 1 && kind != 2)) return failure(invalidOp(i, "unknown focus kind"));
        extra = cur + plane(w, h);
        break;
      }
      case PP_OP_BLUR:
        if (op.p[0] < 0.0f) return failure(invalidOp(i, "blur radius must be 0 or more"));
        extra = cur + strips + cur / 4;
        break;
      case PP_OP_DENOISE:
        // Luma/chroma planes, the guided filter's working planes, alpha and format copies.
        extra = 3 * plane(w, h) + 6 * plane(w, h) + static_cast<std::uint64_t>(w) * h + 2 * cur;
        break;
      case PP_OP_PERSPECTIVE:
        if (op.p[0] < -1.0f || op.p[0] > 1.0f || op.p[1] < -1.0f || op.p[1] > 1.0f) {
          return failure(invalidOp(i, "perspective must be -1..1"));
        }
        extra = cur;
        break;
      default:
        return failure(invalidOp(i, "unknown operation type " + std::to_string(op.type)));
    }
    peak = std::max(peak, input + cur + extra);
  }
  return peak;
}

}  // namespace photoplus
