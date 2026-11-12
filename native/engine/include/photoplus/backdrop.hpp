#pragma once

#include <cstdint>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

// Backdrops: background designs behind the person. The subject is cut out
// along the photo's own edges, cleaned of the old background's colour at the
// edges, re-lit to belong to the new scene (rim light, light wrap, falloff)
// and composited in linear light. Every design is procedural: no stock
// images, no generated content, fully on the device.
namespace photoplus::backdrop {

enum class Design : int32_t {
  Signature = 0,  // depth-aware lens blur, gentle fall-off: the camera's default
  SoftStudio,     // creamy, neutralised studio blur
  Stage,          // black stage, light falling off the shoulders
  Noir,           // the same in black and white
  Porcelain,      // high-key white studio
  Canvas,         // hand-painted portrait backdrop
  GoldenHour,     // low sun, warm haze, warm rim light
  CityLights,     // night bokeh
  Neon,           // magenta / cyan glow and edge lights
  ColorPop,       // the person in colour, the world in black and white
  Velocity,       // zoom-burst motion
  Swirl,          // vintage swirling bokeh
  Aurora,         // ribbons of northern light
  Dream,          // pastel haze and bloom
  Count,
};

struct Params {
  Design design = Design::Signature;
  float strength = 1.0f;  // 0..1
  uint32_t seed = 7;      // varies the procedural elements (lights, canvas)
};

struct Report {
  float subjectCoverage = 0.0f;  // fraction of the frame that is subject
  float edgeCoverage = 0.0f;     // fraction that is a partial (edge) pixel
};

// `subject` is a GRAY8 mask of any size (required). `depth` (optional,
// 0 far .. 255 near) sharpens Signature's lens blur. In place on RGBA8/BGRA8.
Error apply(ImageBuffer& image, const ImageBuffer& subject, const ImageBuffer* depth, const Params& params,
            const CancellationToken* token, Report* report);

}  // namespace photoplus::backdrop
