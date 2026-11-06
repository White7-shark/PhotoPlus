#pragma once

#include <cstddef>
#include <cstdint>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/pp_engine.h"
#include "photoplus/status.hpp"

namespace photoplus {

struct RenderOptions {
  // > 0: render a preview. The input is first reduced (area filter) so its
  // longer edge fits, and every setting measured in pixels (crop rectangle,
  // resize target, blur and sharpen radii) is scaled by the same factor, so
  // the preview looks like the final render, only smaller.
  int32_t maxEdge = 0;
  int32_t backend = PP_BACKEND_AUTO;
};

// Executes a list of operations against an input image without mutating it.
//
// Memory strategy: the input is cloned once into a working buffer; in-place
// operations mutate that buffer; geometry operations replace it. At most two
// full-size buffers (plus bounded scratch) are alive at any time, so peak
// memory is predictable; estimatePipeline() computes the bound up front.
Result<ImageBuffer> runPipeline(const ImageBuffer& input, const pp_op* ops, std::size_t count,
                                const CancellationToken* token);
Result<ImageBuffer> runPipeline(const ImageBuffer& input, const pp_op* ops, std::size_t count,
                                const RenderOptions& options, const CancellationToken* token);

// Upper bound of the engine memory the pipeline will hold at its peak (the
// caller's input included), without running it; also validates the ops.
Result<std::uint64_t> estimatePipeline(int32_t width, int32_t height, PixelFormat format, const pp_op* ops,
                                       std::size_t count, const RenderOptions& options);

}  // namespace photoplus
