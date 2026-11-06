// Builds without GPU support: everything runs on the CPU.
#include "photoplus/gpu.hpp"

namespace photoplus::gpu {

Status init() { return status(); }
Status status() {
  Status s;
  s.reason = "Built without GPU support";
  return s;
}
bool available() { return false; }
void shutdown() {}
bool supports(const pp_op&) { return false; }
Result<ImageBuffer> run(const ImageBuffer&, const pp_op*, std::size_t, const CancellationToken*) {
  return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Built without GPU support"));
}

}  // namespace photoplus::gpu
