#pragma once

#include <cstdint>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

// The photo editor: adjustments, looks, focus effects, props, frames and the
// watermark. All of it works on 8-bit RGBA/BGRA (straight alpha), in place
// unless a new size is produced.
namespace photoplus::edit {

// Straighten: rotate by `degrees` (clockwise, -45..45) and crop to the
// largest centred rectangle of the same aspect that holds only photo.
Result<ImageBuffer> straighten(const ImageBuffer& src, float degrees, const CancellationToken* token);

// White balance nudge: temperature (-1 cool … 1 warm), tint (-1 green … 1 magenta).
Error warmth(ImageBuffer& image, float temperature, float tint, const CancellationToken* token);

// Look presets ("filters"). Ids match pp_filter_id; strength 0..1.
constexpr int32_t kFilterCount = 12;
Error filter(ImageBuffer& image, int32_t id, float strength, const CancellationToken* token);

// Focus effects: everything outside the focus area softens like a wide lens.
struct FocusParams {
  int32_t kind = 1;       // 1 radial (round area), 2 tilt (band)
  float cx = 0.5f;        // focus centre, 0..1 of width / height
  float cy = 0.5f;
  float size = 0.3f;      // radius (radial) or half band height (tilt), fraction of the shorter side
  float angleDeg = 0.0f;  // tilt band angle
  float strength = 0.6f;  // 0..1 (blur up to ~3% of the shorter side)
  float feather = 0.5f;   // 0..1 width of the transition
};
Error focus(ImageBuffer& image, const FocusParams& params, const CancellationToken* token);

// A prop (hat, glasses, accessory) placed on the photo so it looks like part
// of the scene: lit and tinted like the scene around it, casting a soft
// contact shadow, and as sharp and grainy as the photo itself.
struct PropParams {
  float cx = 0.0f, cy = 0.0f;  // centre of the prop in image pixels
  float width = 0.0f;          // prop width in image pixels
  float rotationDeg = 0.0f;    // clockwise
  bool flip = false;           // mirror horizontally
  float opacity = 1.0f;
  float blend = 1.0f;          // how strongly it takes on the photo's light, colour, sharpness and grain
  float shadow = 1.0f;         // contact shadow strength
};
struct PropReport {
  int64_t coveredPixels = 0;
};
Error placeProp(ImageBuffer& image, const ImageBuffer& prop, const PropParams& params, const CancellationToken* token,
                PropReport* report);

// Frames: the photo shown inside a shape on a designed card.
enum class Shape : int32_t { Rect = 0, Ellipse = 1, Arch = 2, Star = 3, Heart = 4, Hexagon = 5, Diamond = 6 };
enum class Backdrop : int32_t { Solid = 0, Gradient = 1, BlurredPhoto = 2, Paper = 3 };
struct FrameParams {
  Shape shape = Shape::Rect;
  float shapeParam = 0.0f;           // corner radius (rect, fraction of the smaller half size) / inner ratio (star)
  float rotationDeg = 0.0f;          // tilt of the window
  float window[4] = {0.1f, 0.1f, 0.8f, 0.8f};  // x, y, w, h in canvas fractions
  Backdrop backdrop = Backdrop::Solid;
  uint32_t colorA = 0xFFFFFFFF;      // 0xAARRGGBB
  uint32_t colorB = 0xFFFFFFFF;
  float border = 0.0f;               // inner border width, fraction of the window's shorter side
  uint32_t borderColor = 0xFFFFFFFF;
  bool metallicBorder = false;       // gold/silver sheen along the border
  float outlineGap = 0.0f;           // a thin outline around the shape, this far out (fraction)
  float outlineWidth = 0.0f;
  uint32_t outlineColor = 0xFFFFFFFF;
  float shadow = 0.0f;               // opacity 0..1
  float shadowOffset = 0.02f;        // downwards, fraction of the window's shorter side
  float shadowBlur = 0.04f;
  float glow = 0.0f;                 // opacity 0..1
  uint32_t glowColor = 0xFFFFFFFF;
  float zoom = 1.0f;                 // photo zoom inside the window (>= 1)
  float offsetX = 0.0f, offsetY = 0.0f;  // -1..1 pan inside the window
  int32_t decoration = 0;            // 1 = film sprocket holes
};
// `canvas` (already allocated at the final size) is fully overwritten.
Error frame(const ImageBuffer& photo, ImageBuffer& canvas, const FrameParams& params, const CancellationToken* token);

// Composites a straight-alpha stamp (e.g. a watermark) at (x, y) with an
// overall opacity. Reports how many stamp pixels landed inside the image
// with real visibility (effective alpha >= 0.25), so callers can verify a
// watermark was actually rendered.
struct OverlayReport {
  int64_t visiblePixels = 0;
};
Error overlay(ImageBuffer& image, const ImageBuffer& stamp, int32_t x, int32_t y, float opacity,
              OverlayReport* report);

}  // namespace photoplus::edit
