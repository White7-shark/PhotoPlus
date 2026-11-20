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
  // With two or more people (a couple), this is the largest face: one box
  // around every face would be as tall as the faces' height difference.
  bool face = false;
  float faceLeft = 0.0f, faceTop = 0.0f, faceRight = 0.0f, faceBottom = 0.0f;
  int32_t faces = 0;  // faces in the cut-out
  // The outfit's main colours (clothes in the parts map, inside the
  // cut-out), sRGB 0..255, largest first, with their share of the clothes.
  int32_t outfitCount = 0;
  std::array<std::array<float, 3>, 2> outfit{};
  std::array<float, 2> outfitShare{};
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
  // A fixed box (normalised to the cut-out) instead of the alpha's own: for
  // video, so a person who moves their arms does not change size or jump.
  bool fixedBox = false;
  float boxLeft = 0.0f, boxTop = 0.0f, boxRight = 1.0f, boxBottom = 1.0f;
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
  // 0..1: Spotlight. A pool of light on the person, the rest of the scene
  // dimmed and softened around them, the person lifted.
  float spotlight = 0.0f;
  // 0..1: the scene's colour bouncing back onto the person, as a coloured
  // paper backdrop warms (or cools) the people in front of it. Their
  // brightness is kept; only the tint follows the scene around them.
  float bounce = 0.0f;
  // Video: the colour match measured once (gain, balance, contrast from a
  // report) and kept, so it does not flicker from frame to frame.
  bool fixedHarmony = false;
  float gain = 1.0f, contrast = 1.0f;
  float balance[3] = {1.0f, 1.0f, 1.0f};
};

struct ComposeReport {
  // The person's box in the frame (0..1).
  float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
  bool grounded = false;   // feet in the photo: shadows were drawn
  float exposure = 1.0f;   // gain given to the person's light
  std::array<float, 3> balance{1.0f, 1.0f, 1.0f};  // per-channel colour gain
  float contrast = 1.0f;   // the person's contrast toward the scene's
};

// `background` (RGBA8/BGRA8) is the output frame, edited in place. `cutout`
// is a cutout() result (straight alpha, same channel order).
Error compose(ImageBuffer& background, const ImageBuffer& cutout, const Placement& placement, const Look& look,
              const CancellationToken* token, ComposeReport* report);

// Hair (Photoshoot Studio, Premium): a new colour for the person's own
// hair, or a new hairstyle in place of it. Works on a cut-out (straight
// alpha) with its parts map; the face box comes from the cut-out report.
// A photographer's own finishing of a hairstyle and its colour: every value
// at its default leaves the style and colour as the engine made them.
struct HairEdit {
  // The style's shape (-1..1, 0 as designed).
  float length = 0.0f;   // shorter .. longer
  float volume = 0.0f;   // closer to the head .. fuller at the sides
  float height = 0.0f;   // flatter .. more lift on top
  float fringe = 0.0f;   // shorter .. longer bangs (styles that have them)
  bool partSet = false;
  float part = 0.0f;     // the parting, -1 left .. 1 right (when partSet)
  float waves = 0.0f;    // straighter .. wavier / curlier
  float shine = 0.0f;    // matte .. glossy (the style and the colour)
  // Where the style sits: moved (face heights), sized and turned on the head.
  float moveX = 0.0f, moveY = 0.0f;  // -0.5..0.5
  float size = 1.0f;                 // 0.7..1.4
  float rotate = 0.0f;               // degrees, -30..30
  // The colour's finish.
  float depth = 0.0f;  // -1 softer .. 1 more contrast between strands
  float roots = 0.0f;  // 0..1 darker roots, as grown-out colour
  float ends = 0.0f;   // -1 darker .. 1 lighter ends (ombre)
};

// Where a photographer paints on the hair (strokes in the cut-out's own
// normalised coordinates, like the cut-out's brushes).
enum class HairBrush : int32_t {
  EraseNew = 0,   // no new hair here
  Restore = 1,    // the photo as it was here (their own hair, skin, clothes)
  ColourOn = 2,   // colour here too (hair the parts map missed)
  ColourOff = 3,  // no colour here (a brow, skin, a hat)
};
struct HairStroke {
  HairBrush brush = HairBrush::EraseNew;
  float radius = 0.02f;                      // share of the image's short side
  std::vector<std::array<float, 2>> points;  // normalised 0..1
};

struct HairParams {
  // A style id from hairStyles(), or empty to keep the person's own hair.
  const char* style = nullptr;
  // A new colour (sRGB 0..255); without it a new style takes the colour of
  // the person's own hair.
  bool recolor = false;
  std::array<uint8_t, 3> color{};
  float strength = 1.0f;  // 0..1, how fully the new colour replaces the old
  float faceLeft = 0.0f, faceTop = 0.0f, faceRight = 0.0f, faceBottom = 0.0f;  // normalised
  uint32_t seed = 5;
  HairEdit edit;
  std::vector<HairStroke> strokes;
};

struct HairStyleInfo {
  const char* id;
  bool women;  // in the women's list (else the men's)
};

// Every style the engine draws.
const std::vector<HairStyleInfo>& hairStyles();

// `cutout` (RGBA8/BGRA8, straight alpha) is edited in place. `zones`: the
// person-parts map (required: hair has to be found). A style needs the face.
Error restyleHair(ImageBuffer& cutout, const ImageBuffer& zones, const HairParams& params, const CancellationToken* token);

// A seamless paper backdrop in one colour, lit as in a studio: a soft pool
// of light behind the people, falling off toward the edges and corners, a
// floor a touch darker than the wall, and a faint paper texture. Worked in
// linear light (the colour's hue is kept everywhere) and dithered, so
// gradients on dark or saturated colours do not band.
struct BackdropParams {
  float light = 1.0f;    // 0 a flat colour .. 1 the full studio light
  float centerX = 0.5f;  // where the pool of light is (0..1)
  float centerY = 0.42f;
  uint32_t seed = 7;
};

// `out` (RGBA8/BGRA8) is filled, alpha 255. `color` is sRGB 0..255.
Error paintBackdrop(ImageBuffer& out, const std::array<uint8_t, 3>& color, const BackdropParams& params,
                    const CancellationToken* token);

// ---- Studio light and finishing (Phase 11).

// A person (a cut-out, edited in place) lit as if by studio lights. A photo
// has no depth, so the shape is estimated from the outline (and the face,
// when its box is given); the light is relative (their average brightness is
// kept) and `strength` eases it back toward the photo as it was.
struct LightParams {
  float strength = 1.0f;     // 0..1: 0 leaves the light as it was
  float keyAngle = -45.0f;   // degrees around the person: 0 from the camera, -90 from the frame's left, 90 from its right
  float keyHeight = 35.0f;   // degrees above the eyes
  float keySoftness = 0.5f;  // 0 a bare bulb (hard shadows) .. 1 a big softbox
  float key = 1.0f;          // the key light's intensity, 0..2
  float fill = 0.35f;        // the fill light from the other side, 0..1 of the key
  std::array<float, 3> keyColour{1.0f, 1.0f, 1.0f};   // linear gains: a gel, or the colour of the light
  std::array<float, 3> fillColour{1.0f, 1.0f, 1.0f};
  float rimLeft = 0.0f, rimRight = 0.0f;              // 0..1 rim lights behind, on either side
  std::array<float, 3> rimColour{1.0f, 1.0f, 1.0f};
  float faceLeft = 0.0f, faceTop = 0.0f, faceRight = 0.0f, faceBottom = 0.0f;  // the face box (normalised), optional
};
Error relight(ImageBuffer& cutout, const LightParams& params, const CancellationToken* token);

// The whole frame graded (edited in place).
struct FinishParams {
  float vignette = 0.0f;      // -1 lighter edges .. 1 darker edges
  float vignetteSize = 0.5f;  // 0 a small clear centre .. 1 a wide one
  float warmth = 0.0f, tint = 0.0f;  // -1..1: cooler / warmer; greener / more magenta
  float contrast = 0.0f;      // -1..1
  float saturation = 0.0f;    // -1..1 (-1: black and white)
  float fade = 0.0f;          // 0..1: lifted blacks, as on matte film
  std::array<float, 3> shadowTone{0.0f, 0.0f, 0.0f};     // split toning: a colour pushed into the shadows (-1..1 per channel)
  std::array<float, 3> highlightTone{0.0f, 0.0f, 0.0f};  // and another into the highlights
  float toneAmount = 0.0f;    // 0..1
  float grain = 0.0f, grainSize = 0.5f;  // 0..1
  bool mono = false;          // black and white
  uint32_t seed = 3;
};
Error finish(ImageBuffer& frame, const FinishParams& params, const CancellationToken* token);

// A backdrop blending from one colour to another (sRGB 0..255), dithered.
struct GradientParams {
  int32_t kind = 0;          // 0 linear, 1 radial (a pool of colour `from` at the centre)
  float angle = 90.0f;       // linear: the direction from `from` to `to`, degrees (90: top to bottom)
  float centerX = 0.5f, centerY = 0.4f, radius = 0.8f;  // radial (0..1 of the frame; radius in heights)
  uint32_t seed = 7;
};
Error paintGradient(ImageBuffer& out, const std::array<uint8_t, 3>& from, const std::array<uint8_t, 3>& to, const GradientParams& params,
                    const CancellationToken* token);

// Lips: the lips of each face (landmarks, see face.hpp: 131 x,y pairs
// normalised to the image) repainted in `rgb`, keeping their own shading,
// creases and gloss; dark lips are lifted so the colour shows on deep skin.
// `natural` (pink lips): the strength adapts to the person (already-pink lips
// and fair faces get very little); otherwise lipstick at `strength`.
// RGBA8/BGRA8 in place; strength 0..1.
constexpr size_t kLipLandmarkFloats = 262;
struct LipsReport {
  int32_t lips = 0;    // faces whose lips were coloured
  float lipLuma = 0;   // the last lips' own brightness
  float applied = 0;   // the last lips' strength as applied (natural: after adapting)
};
Error colourLips(ImageBuffer& image, const std::vector<std::array<float, kLipLandmarkFloats>>& faces,
                 const std::array<uint8_t, 3>& rgb, float strength, bool natural, const CancellationToken* token,
                 LipsReport* report);

}  // namespace photoplus::studio
