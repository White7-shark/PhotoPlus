#pragma once

// Product photography (Phase 12): an object cut out of its photo, then set
// on a clean backdrop the way a catalogue shot is made: in a set place and
// size, with a shadow that grounds it and its colours corrected.
//
//  - cutout(): the object from the photo. On a plain backdrop (paper, a
//    table, a wall: what products are usually shot on) the backdrop is
//    measured across the photo, light fall-off and all, and the object is
//    whatever differs from it; the object's own shadow on the backdrop is
//    left out. An object mask from a model (`hint`) keeps stray specks and
//    reflections out and decides where the backdrop is busy. Edge colours
//    lose the old backdrop's colour.
//  - render(): the cut-out on a new backdrop, placed by a template (how much
//    of the frame it fills, where), with contact, soft and cast shadows, a
//    reflection, and colour correction measured from the old backdrop.

#include <array>
#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"
#include "photoplus/studio.hpp"

namespace photoplus::product {

struct MatteParams {
  float tolerance = 0.5f;  // 0..1: higher keeps colours closer to the backdrop's (a white bottle on white paper)
  float softness = 0.2f;   // 0..1: a softer edge
  float shift = 0.0f;      // -1 (grow) .. +1 (shrink) the edge
  bool shadowsOut = true;  // the object's own shadow on the backdrop is not part of it
};

struct MatteReport {
  float coverage = 0.0f;  // share of the frame that is the object (sum of alpha)
  float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;  // box of alpha > 0.5, normalised
  std::array<float, 3> backdrop{};  // the photographed backdrop's colour, sRGB 0..255
  float spread = 0.0f;              // how uneven the backdrop is (0 = one flat colour)
  bool plain = false;               // a plain backdrop: its colours decided the edge
  int32_t objects = 0;              // separate objects kept (a set of products)
};

// `photo` RGBA8/BGRA8. `hint`: optional GRAY8 object mask (any size).
// `out`: same size and format as `photo`, written with the object's
// colours and straight alpha.
Error cutout(const ImageBuffer& photo, const ImageBuffer* hint, const std::vector<studio::Stroke>& strokes,
             const MatteParams& params, ImageBuffer& out, const CancellationToken* token, MatteReport* report);

enum class BackdropKind : int32_t {
  Transparent = 0,  // nothing behind (a PNG); shadows stay, as soft black
  Solid = 1,        // one colour (`wall`)
  Sweep = 2,        // seamless paper: the wall curving into the floor, no line between
  Surface = 3,      // a table against a wall: `floor` below the horizon, `wall` above
  Radial = 4,       // a pool of `wall` behind the object, `floor` toward the edges
  Scene = 5,        // a photo (the `scene` image), filling the frame
};

struct Backdrop {
  BackdropKind kind = BackdropKind::Sweep;
  std::array<uint8_t, 3> wall{240, 240, 240};
  std::array<uint8_t, 3> floor{252, 252, 252};
  float horizon = 0.65f;   // Surface and Sweep: where the floor meets the wall (0..1 from the top)
  float softness = 0.5f;   // 0..1: how gently the wall turns into the floor
  float light = 0.5f;      // 0..1: a pool of light behind the object and darker corners
  uint32_t seed = 5;
};

struct Shadow {
  float contact = 0.6f;      // 0..1: the dark line where the object touches the surface
  float soft = 0.35f;        // 0..1: a soft pool of shade under it
  float cast = 0.0f;         // 0..1: a shadow thrown by one light
  float castAngle = 25.0f;   // degrees the cast shadow leans (+ to the right)
  float castLength = 0.45f;  // of the object's height; > 0 behind it (light in front), < 0 in front (light behind)
  float castSoftness = 0.5f; // 0..1: how quickly the cast shadow softens away from the object
  float reflection = 0.0f;   // 0..1: a mirror image on a glossy surface, fading out
};

struct Correction {
  bool autoWhite = true;     // the old backdrop's colour cast removed (it was meant to be neutral)
  bool autoExposure = false; // a light backdrop brought up to white, and the object with it
  std::array<float, 3> reference{-1.0f, -1.0f, -1.0f};  // the old backdrop (MatteReport::backdrop); negative: none
  float exposure = 0.0f;     // -1..1 (±1 stop)
  float contrast = 0.0f;     // -1..1
  float saturation = 0.0f;   // -1..1
  float warmth = 0.0f;       // -1..1
  float tint = 0.0f;         // -1 (green) .. 1 (magenta)
};

struct Layout {
  int32_t width = 2000, height = 2000;  // the frame, px
  float fill = 0.8f;   // 0.1..1: the object's larger side as a share of the frame's
  float x = 0.5f;      // where its box's centre goes, 0..1
  float y = 0.5f;      // its centre, or with `ground` its base, 0..1
  bool ground = false; // y places the base (on a table or the floor)
  bool flip = false;   // mirror
};

struct RenderReport {
  float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;  // the object's box in the frame, 0..1
  std::array<float, 3> gain{1.0f, 1.0f, 1.0f};  // colour gains the correction gave
};

// A new image (the cut-out's format) of layout.width x layout.height.
// `scene` (RGBA8/BGRA8, any size) is needed for BackdropKind::Scene only.
Result<ImageBuffer> render(const ImageBuffer& cutout, const ImageBuffer* scene, const Backdrop& backdrop, const Shadow& shadow,
                           const Correction& correction, const Layout& layout, const CancellationToken* token,
                           RenderReport* report);

}  // namespace photoplus::product
