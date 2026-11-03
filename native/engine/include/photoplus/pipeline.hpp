#pragma once

#include <cstddef>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/pp_engine.h"
#include "photoplus/status.hpp"

namespace photoplus {

// Executes a list of operations against an input image without mutating it.
//
// Memory strategy: the input is cloned once into a working buffer; in-place
// operations mutate that buffer; geometry operations replace it. At most two
// full-size buffers are alive at any time, which keeps peak memory
// predictable: roughly 2x the working image (plus the host's input buffer).
Result<ImageBuffer> runPipeline(const ImageBuffer& input, const pp_op* ops, std::size_t count,
                                const CancellationToken* token);

}  // namespace photoplus
