#include "photoplus/pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

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
  return makeError(PP_ERROR_INVALID_ARGUMENT,
                   "Operation #" + std::to_string(index) + ": " + why);
}

}  // namespace

Result<ImageBuffer> runPipeline(const ImageBuffer& input, const pp_op* ops, std::size_t count,
                                const CancellationToken* token) {
  ScopedTimer timer("pipeline.total");
  if (input.empty()) return makeError(PP_ERROR_INVALID_ARGUMENT, "Input image is empty");
  if (count > 0 && ops == nullptr) return makeError(PP_ERROR_INVALID_ARGUMENT, "ops is null");
  if (isCancelled(token)) return makeError(PP_ERROR_CANCELLED, "Processing was cancelled");

  auto working = cloneImage(input);
  if (!working) return working.error();
  ImageBuffer current = std::move(working).value();

  for (std::size_t i = 0; i < count; ++i) {
    if (isCancelled(token)) return makeError(PP_ERROR_CANCELLED, "Processing was cancelled");
    const pp_op& op = ops[i];
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
        if (!toInt(op.p[0], &turns)) return invalidOp(i, "rotation must be finite");
        replaced = ops::rotate90(current, turns, token);
        replaces = true;
        break;
      }
      case PP_OP_CROP: {
        int32_t x, y, w, h;
        if (!toInt(op.p[0], &x) || !toInt(op.p[1], &y) || !toInt(op.p[2], &w) ||
            !toInt(op.p[3], &h)) {
          return invalidOp(i, "crop rectangle must be finite");
        }
        replaced = ops::crop(current, x, y, w, h, token);
        replaces = true;
        break;
      }
      case PP_OP_RESIZE: {
        int32_t w, h;
        if (!toInt(op.p[0], &w) || !toInt(op.p[1], &h)) {
          return invalidOp(i, "resize dimensions must be finite");
        }
        replaced = ops::resizeBilinear(current, w, h, token);
        replaces = true;
        break;
      }
      case PP_OP_SHARPEN:
        replaced = ops::sharpen(current, op.p[0], token);
        replaces = true;
        break;
      default:
        return invalidOp(i, "unknown operation type " + std::to_string(op.type));
    }

    if (replaces) {
      if (!replaced) return replaced.error();
      current = std::move(replaced).value();  // frees the previous buffer
    } else if (err.status != PP_OK) {
      return err;
    }
  }
  return current;
}

std::size_t estimatePeakBytes(const ImageBuffer& input, const pp_op* ops, std::size_t count) {
  const std::size_t bpp = static_cast<std::size_t>(bytesPerPixel(input.format()));
  std::size_t w = static_cast<std::size_t>(input.width());
  std::size_t h = static_cast<std::size_t>(input.height());
  std::size_t peak = w * h * bpp;  // working clone
  for (std::size_t i = 0; ops != nullptr && i < count; ++i) {
    std::size_t nw = w, nh = h;
    const pp_op& op = ops[i];
    if (op.type == PP_OP_ROTATE90) {
      const long turns = std::lround(op.p[0]);
      if (turns % 2 != 0) std::swap(nw, nh);
    } else if (op.type == PP_OP_CROP || op.type == PP_OP_RESIZE) {
      const int idx = op.type == PP_OP_CROP ? 2 : 0;
      if (std::isfinite(op.p[idx]) && std::isfinite(op.p[idx + 1]) && op.p[idx] > 0 &&
          op.p[idx + 1] > 0) {
        nw = static_cast<std::size_t>(op.p[idx]);
        nh = static_cast<std::size_t>(op.p[idx + 1]);
      }
    }
    peak = std::max(peak, (w * h + nw * nh) * bpp);
    w = nw;
    h = nh;
  }
  return peak;
}

}  // namespace photoplus
