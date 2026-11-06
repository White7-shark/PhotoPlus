#pragma once

#include <array>
#include <cstdint>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

namespace photoplus::ops {

// In-place operations mutate `image` and return PP_OK / an error.
// Geometry operations allocate and return a new image.
//
// All operations poll `token` between rows and return PP_ERROR_CANCELLED
// promptly when it is signalled; `image` is then in an unspecified (but
// valid, fully allocated) state and must be discarded by the caller.

struct ToneParams {
  float exposureEv = 0.0f;  // -5..5 stops, applied in linear light
  float brightness = 0.0f;  // -1..1
  float contrast = 0.0f;    // -1..1
};

Error tone(ImageBuffer& image, const ToneParams& params, const CancellationToken* token);
// The 256-entry table tone() applies to every colour channel (shared with the GPU path).
std::array<uint8_t, 256> toneLut(const ToneParams& params);
Error grayscale(ImageBuffer& image, const CancellationToken* token);
Error saturation(ImageBuffer& image, float delta, const CancellationToken* token);
Error flip(ImageBuffer& image, bool horizontal, bool vertical, const CancellationToken* token);

Result<ImageBuffer> rotate90(const ImageBuffer& src, int quarterTurns,
                             const CancellationToken* token);
Result<ImageBuffer> crop(const ImageBuffer& src, int32_t x, int32_t y, int32_t width,
                         int32_t height, const CancellationToken* token);
Result<ImageBuffer> resizeBilinear(const ImageBuffer& src, int32_t width, int32_t height,
                                   const CancellationToken* token);
Result<ImageBuffer> sharpen(const ImageBuffer& src, float amount, const CancellationToken* token);

}  // namespace photoplus::ops
