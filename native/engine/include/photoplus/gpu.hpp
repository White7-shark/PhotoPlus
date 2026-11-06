#pragma once

#include <cstddef>
#include <string>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/pp_engine.h"
#include "photoplus/status.hpp"

// GPU acceleration (OpenGL ES 3.1 compute shaders, headless EGL).
//
// The GPU path is used only after it has been verified on the device: at
// start-up it renders a test image with every supported operation and
// compares the result with the CPU's. If anything differs by more than a
// level or two (driver bugs, precision shortcuts), the GPU stays off and
// everything runs on the CPU, so results are consistent on every device.
namespace photoplus::gpu {

struct Status {
  bool compiled = false;   // built with GPU support
  bool available = false;  // initialised and verified
  std::string renderer;    // GL_RENDERER
  std::string reason;      // why it is unavailable
  int maxDifference = -1;  // largest CPU/GPU difference in the self-test (0..255)
};

// Initialises (once) and verifies the GPU; later calls return the result.
Status init();
Status status();
bool available();
// Releases the GPU context and its buffers (memory pressure, shutdown).
void shutdown();

// Whether `op` runs on the GPU (per-pixel colour work, blur, resize).
bool supports(const pp_op& op);
// Runs a run of supported operations. On any failure the caller falls back
// to the CPU.
Result<ImageBuffer> run(const ImageBuffer& input, const pp_op* ops, std::size_t count, const CancellationToken* token);

}  // namespace photoplus::gpu
