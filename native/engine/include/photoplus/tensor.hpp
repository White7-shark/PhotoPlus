#pragma once

#include <cstddef>
#include <cstdint>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

// Model input/output conversion shared by every on-device model, so each
// model needs no per-pixel code of its own and preparation is fast (native,
// parallel) and identical everywhere.
namespace photoplus::ml {

enum class Layout : int32_t { NHWC = 0, NCHW = 1 };
// How the photo fills the model's input: stretched; fitted whole with padding
// (letterbox, padding is the mean colour = 0 after normalisation); or cropped
// to fill from the centre.
enum class Fit : int32_t { Stretch = 0, Letterbox = 1, CropCenter = 2 };

struct TensorSpec {
  int32_t width = 0, height = 0;
  Layout layout = Layout::NHWC;
  Fit fit = Fit::Stretch;
  float mean[3] = {0, 0, 0};  // on 0..1 values
  float std[3] = {1, 1, 1};
};

// Where the photo landed in the tensor (pixels of the tensor), to map
// outputs back: tensor = photo * scale + offset.
struct Placement {
  float scaleX = 1, scaleY = 1, offsetX = 0, offsetY = 0;
};

// RGB(A)/gray image -> float tensor of width*height*3 values (RGB order
// whatever the pixel format; gray is replicated).
Error toTensor(const ImageBuffer& image, const TensorSpec& spec, float* out, std::size_t outCount,
               const CancellationToken* token, Placement* placement);

enum class Activation : int32_t { None = 0, Sigmoid = 1, Softmax = 2 };

// A model's score map (NHWC, `channels` per pixel; or NCHW) -> GRAY8 mask of
// the given channel, at outWidth x outHeight (bilinear). None clamps 0..1.
// When `placement` is given (letterbox/crop inputs), only the photo's part
// of the map is used.
Result<ImageBuffer> toMask(const float* data, int32_t width, int32_t height, int32_t channels, Layout layout,
                           int32_t channel, Activation activation, int32_t outWidth, int32_t outHeight,
                           const Placement* placement, int32_t photoWidth, int32_t photoHeight,
                           const CancellationToken* token);

}  // namespace photoplus::ml
