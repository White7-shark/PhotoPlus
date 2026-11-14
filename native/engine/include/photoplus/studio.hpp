#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

// The photoshoot studio: a person is cut out of their photo and placed into
// another scene.
//
//   cutout  — a full-resolution matte from a person mask, refined along the
//             photo's own edges: the edge placed by colour where person and
//             background differ, soft partial alpha in hair zones, the
//             user's corrections (keep / remove / "fix hair" strokes), and
//             each edge pixel's colour freed of the old background
//             (foreground estimation), so the cut-out has no fringe. The result is an RGBA image with straight alpha: a
//             transparent-background photo.
//   compose — the cut-out placed into a background plate (position, size,
//             mirror, tilt for perspective), its colour and exposure brought
//             toward the scene's light, the scene's light wrapped onto its
//             edges, contact and cast shadows on the ground, the background
//             softened like a lens would, and the grain matched; composited
//             in linear light.
namespace photoplus::studio {

// Classes of a person-parts map (MediaPipe Selfie Multiclass order).
enum class Zone : uint8_t { Background = 0, Hair = 1, BodySkin = 2, FaceSkin = 3, Clothes = 4, Accessory = 5 };

enum class Brush : int32_t {
  Keep = 0,    // this is the person
  Remove = 1,  // this is not
  Hair = 2,    // fine detail here (hair, fur, lace, a veil): let the colours decide
};

struct Stroke {
  Brush brush = Brush::Keep;
  float radius = 0.02f;                          // share of the image's short side
  std::vector<std::array<float, 2>> points;      // normalised 0..1 image coordinates
};

struct CutoutParams {
  float softness = 0.0f;    // 0..1: a softer edge
  float shift = 0.0f;       // -1 (grow) .. +1 (shrink) the edge
  float hairDetail = 0.5f;  // 0..1: how far colour decides in hair (fine strands, curls)
  bool cleanColours = true; // free edge colours of the old background
};

struct CutoutReport {
  float coverage = 0.0f;   // share of the frame that is person (sum of alpha)
  float edge = 0.0f;       // share of the frame that is a partial (edge) pixel
  float hairEdge = 0.0f;   // share of the edge pixels that are in hair
  // Box of alpha > 0.5, normalised.
  float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
  // The person runs off this edge of the photo (e.g. a portrait cut at the chest).
  bool cutLeft = false, cutTop = false, cutRight = false, cutBottom = false;
  // The person's face (face skin in the parts map, inside the cut-out),
  // normalised: its height tells the person's real size. Needs `zones`.
  bool face = false;
  float faceLeft = 0.0f, faceTop = 0.0f, faceRight = 0.0f, faceBottom = 0.0f;
};

// `subject`: GRAY8 person mask (any size, required). `zones`: optional GRAY8
// person-parts map (Zone values, any size). `out`: same size and format as
// `image`, written with the person's colours and straight alpha.
Error cutout(const ImageBuffer& image, const ImageBuffer& subject, const ImageBuffer* zones,
             const std::vector<Stroke>& strokes, const CutoutParams& params, ImageBuffer& out,
             const CancellationToken* token, CutoutReport* report);

// Where the cut-out goes. The anchor is the bottom centre of the person's
// box (their feet, or where the photo cut them off), in the frame (0..1).
struct Placement {
  float x = 0.5f;
  float y = 0.95f;
  float height = 0.8f;  // the person's box height, as a share of the frame height
  bool flip = false;    // mirror
  float tilt = 0.0f;    // -1..1 vertical perspective: + narrows the top (camera looking up at the person)
  float angle = 0.0f;   // degrees, about the anchor
};

struct Look {
  float colorMatch = 0.7f;  // 0..1: the person's colour and exposure toward the scene's light
  float lightWrap = 0.35f;  // 0..1: the scene's light wrapping onto the person's edges
  float shadow = 0.6f;      // 0..1: contact shadow where the person meets the ground
  float castShadow = 0.0f;  // 0..1: shadow cast on the ground by a directional light
  float lightX = 0.0f;      // -1 (light from the left) .. 1 (from the right)
  float rim = 0.0f;         // 0..1: rim light from the scene on the side facing its light (backlit scenes)
  float horizon = 0.45f;    // the scene's horizon (0..1 from the top): shadows stay on the ground
  float bgBlur = 0.0f;      // 0..1: the background softened like a lens at a wide aperture
  float grain = 1.0f;       // 0..1: the person's grain matched to the scene's
  uint32_t seed = 11;
};

struct ComposeReport {
  // The person's box in the frame (0..1).
  float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
  bool grounded = false;   // feet in the photo: shadows were drawn
  float exposure = 1.0f;   // gain given to the person's light
  std::array<float, 3> balance{1.0f, 1.0f, 1.0f};  // per-channel colour gain
};

// `background` (RGBA8/BGRA8) is the output frame, edited in place. `cutout`
// is a cutout() result (straight alpha, same channel order).
Error compose(ImageBuffer& background, const ImageBuffer& cutout, const Placement& placement, const Look& look,
              const CancellationToken* token, ComposeReport* report);

}  // namespace photoplus::studio
