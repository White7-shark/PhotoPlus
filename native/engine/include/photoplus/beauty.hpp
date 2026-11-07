#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/face.hpp"
#include "photoplus/image.hpp"
#include "photoplus/portrait.hpp"
#include "photoplus/status.hpp"

// The beauty engine: six layers combined by one compositor.
//
//   A skin        portrait::apply   smoothing, spots, even tone, lift (texture kept)
//   B face        adjustFaces       eyes, lips, teeth, under-eye, shine, blush,
//                                   contour, highlight, studio key light
//   C background  grade             light, colour, tint, glow, haze, vignette
//   D lighting    grade             softbox light on the subject (direction from
//                                   the face's own shading), rim light, bloom,
//                                   soft focus, shadow recovery, skin sheen
//   E colour      grade             white balance, contrast, blacks, highlights,
//                                   saturation, vibrance, split toning, one
//                                   selective hue band, skin-only colour; the
//                                   scene grade is kept off skin (skinProtect)
//   F compositor  apply             runs A → B → C/D/E with one recipe
//
// The grade is a per-pixel function of the photo, the subject alpha, a skin
// map and one low-resolution blurred copy, so the live camera shader
// (android assets beauty/*.frag) computes the same look on the GPU.
namespace photoplus::beauty {

using Color = std::array<float, 3>;  // gamma-encoded sRGB, 0..1

struct Grade {
  // ---- E: colour (whole frame) -------------------------------------------
  float temperature = 0;  // -1 cool .. 1 warm
  float tint = 0;         // -1 green .. 1 magenta
  float contrast = 0;     // -1 .. 1 (S-curve; ends fixed)
  float blacks = 0;       // -1 deeper .. 1 lifted (matte)
  float highlights = 0;   // -1 recovered .. 1 brighter (never clipped)
  float saturation = 0;   // -1 .. 1
  float vibrance = 0;     // -1 .. 1 (muted colours first)
  Color shadowColor{0.5f, 0.5f, 0.5f};  // split toning
  float shadowAmount = 0;               // 0..1
  Color highlightColor{0.5f, 0.5f, 0.5f};
  float highlightAmount = 0;
  Color bandColor{0.5f, 0.5f, 0.5f};  // selective hue: colours near this hue…
  float bandWidth = 0;                 // …within ± this many degrees (0 = off)
  float bandHueShift = 0;              // degrees
  float bandSaturation = 0;            // -1 .. 1
  float skinProtect = 0.75f;  // 0..1: share of the scene grade kept off skin
  float skinWarmth = 0;       // -1 .. 1 (golden/peach vs neutral), skin only
  float skinRosy = 0;         // 0..1 pink flush, skin only
  float skinLuminance = 0;    // -1 .. 1, skin only (exposure-like)
  // ---- D: lighting (subject) ---------------------------------------------
  float subjectLight = 0;  // -1 .. 1 softbox exposure on the subject (stops/2)
  float lightWarmth = 0;   // -1 cool .. 1 golden key light
  float shadowLift = 0;    // 0..1 local shadow recovery
  float rim = 0;           // 0..1 rim light along the subject's edge
  Color rimColor{1.0f, 0.92f, 0.8f};
  float bloom = 0;      // 0..1 highlights glow
  float softFocus = 0;  // 0..1 dreamy diffusion
  float skinSheen = 0;  // 0..1 dewy highlights on skin
  // ---- C: background ------------------------------------------------------
  float bgExposure = 0;    // -1 .. 1 stops
  float bgSaturation = 0;  // -1 .. 1
  Color bgTint{0.5f, 0.5f, 0.5f};
  float bgTintAmount = 0;  // 0..1 (luminance kept)
  float bgGlow = 0;        // 0..1 a soft light in the background…
  float bgGlowX = 0.8f, bgGlowY = 0.2f, bgGlowSize = 0.35f;  // …centre (0..1) and radius (of the diagonal)
  Color bgGlowColor{1.0f, 0.8f, 0.5f};
  float bgVignette = 0;  // 0..1
  float bgHaze = 0;      // 0..1 creamy veil (uses the glow colour)
  float bgWarmth = 0;    // -1 cool .. 1 warm (Portrait mode's Background Tone)
};

// What the grade needs to know about the scene (measured by `measure`, or
// supplied by the live camera's analyser).
struct Scene {
  bool hasSkin = false;
  float skinY = 0.5f, skinCb = -0.05f, skinCr = 0.08f, skinSpread = 0.03f;  // the person's own skin
  float subjectX = 0.5f, subjectW = 0.5f;  // subject centre and width (0..1 of the image width)
  float lightAngle = 0.35f;                // -1 picture left .. 1 right
  float skinYSpread = 0.06f;               // spread of the skin's own luma (MAD-based)
};

struct GradeReport {
  float subjectCoverage = 0;
  float skinCoverage = 0;
  float clippedBefore = 0;  // fraction of pixels at 255 in any channel
  float clippedAfter = 0;
};

// Measures skin colour (inside `faces`, outside `protect`), the subject's
// extent and the key light's side. Any mask may be null.
Scene measure(const ImageBuffer& image, const ImageBuffer* subject, const ImageBuffer* faces, const ImageBuffer* protect,
              const std::vector<portrait::FaceLandmarks>& landmarks);

bool active(const Grade& g);

// Layers C, D and E in one pass. `subject` may be null (the whole frame is
// then treated as the subject and background effects do nothing). `faces`
// (optional) always counts as subject: segmentation can miss a brightly lit
// cheek against a pale wall, and background effects must never reach a face.
Error grade(ImageBuffer& image, const ImageBuffer* subject, const ImageBuffer* faces, const Scene& scene, const Grade& g,
            const CancellationToken* token, GradeReport* report);

struct Recipe {
  Recipe();                    // neutral: changes nothing
  portrait::Params portrait;   // A (+ Portrait mode's blur, background and edge)
  portrait::FaceParams face;   // B
  Grade grade;                 // C, D, E
  bool autoLight = true;       // key light side from the face's shading
};

struct Report {
  portrait::Report portrait;
  portrait::FaceReport face;
  GradeReport grade;
  float lightAngle = 0;
  int32_t layers = 0;  // bit 0 skin/portrait, 1 face, 2 grade
};

// The compositor: A → B → C/D/E. Masks are GRAY8 of any size; `subject`
// may be null (no background separation), the others too.
Error apply(ImageBuffer& image, const ImageBuffer* subject, const ImageBuffer* faces, const ImageBuffer* protect,
            const ImageBuffer* depth, const std::vector<portrait::FaceLandmarks>& landmarks, const Recipe& recipe,
            const CancellationToken* token, Report* report);

}  // namespace photoplus::beauty
