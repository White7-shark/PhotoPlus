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
//     by brightness (dark hair) and texture (fair hair: strands, not skin);
//     the weight follows the photo's edges (guided filter) and the face area
//     is feathered, so no outline shows;
//   - a shade is a change of the skin's own colour (albedo) under the same
//     light: per-channel gains in linear light. The scene's light colour and
//     the person's shading, pores and flush all stay, so the new shade sits
//     naturally in the photo; shine keeps the light's colour;
//   - colour is evened a little around the skin's local colour (blotches,
//     redness, colour noise); brightness detail is untouched;
//   - fair skin is lifted only as far as its headroom allows, and highlights
//     roll off, so nothing new clips and texture is kept.
namespace photoplus::skintone {

struct Params {
  // The shade: gains on the skin's own colour, linear light (1 = unchanged).
  // E.g. 1.35/1.5/1.05 is a lighter, golden skin; 0.84/0.76/0.70 a deeper,
  // warmer one.
  float gainR = 1.0f, gainG = 1.0f, gainB = 1.0f;
  float glow = 0.0f;      // 0..1 a soft luminous sheen on the skin's highlights
  float evenness = 0.5f;  // 0..1 how much blotchy colour is evened
  float strength = 1.0f;  // 0..1
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
