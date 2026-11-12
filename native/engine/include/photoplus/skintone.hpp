#pragma once

#include <cstdint>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

// Skin Tone: the person's skin — face and the visible body (neck, arms,
// hands) — moved toward a chosen shade. Unlike the beauty filters (which
// light and retouch), this changes the colour of the skin itself, nothing
// else: hair, eyes, brows, lips, teeth, clothes and the background keep
// their colours.
//
// How:
//   - the person's own skin colour is measured on the face (median, spread);
//   - skin is found by colour similarity to it, in the face and in the
//     subject, with features (eyes, brows, lips) protected and hair kept out
//     by brightness (dark hair) and texture (fair hair: strands, not skin); the weight follows the photo's edges (guided filter);
//   - the chroma moves by the difference between the shade and the person's
//     own skin, so their natural variation (flush, shadows, freckles) stays;
//   - brightness moves part of the way only (a shade, not a different
//     face): half the way in stops, eased off toward about 0.7 stop either
//     way, with highlights rolled off so nothing new clips. The undertone
//     goes all the way; saturation follows the brightness (a pale shade on a
//     face that stays deep would otherwise look ashen). Texture and shading
//     are untouched.
namespace photoplus::skintone {

struct Params {
  float r = 0.8f, g = 0.6f, b = 0.5f;  // the shade, sRGB 0..1 (a mid-tone of that skin)
  float glow = 0.0f;                   // 0..1 a soft luminous sheen on the skin's highlights
  float strength = 1.0f;               // 0..1
};

struct Report {
  bool hasSkin = false;       // false: no face found, nothing changed
  float skinCoverage = 0.0f;  // share of the frame recoloured (weight > 0.5)
};

// `faces` (face area from landmarks) is needed to measure the skin; without
// it nothing changes (Report::hasSkin is false). `subject` widens the search
// from the face to the body; `protect` keeps eyes, brows and lips out.
Error apply(ImageBuffer& image, const ImageBuffer* subject, const ImageBuffer* faces, const ImageBuffer* protect,
            const Params& params, const CancellationToken* token, Report* report);

}  // namespace photoplus::skintone
