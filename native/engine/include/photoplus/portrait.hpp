#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/status.hpp"

// Portrait and look tools for social-media photos:
//
//   portrait — the chosen subject stays crisp and gently lifted while the
//              background melts into a lens-like blur; faces get natural
//              skin retouching (smoothing, spot removal, even tone) that
//              never changes the person's own skin colour.
//   subject  — a subject cut-out grown from tap points, used when no
//              on-device segmentation model is available.
//   style    — measures a reference photo's look (tone curve, colour by
//              tonal zone, saturation, vignette, grain) and transfers it.
namespace photoplus::portrait {

struct Params {
  float blur = 0.6f;      // background blur, 0..1 (0 = none)
  float retouch = 0.75f;  // skin smoothing and spot removal, 0..1 ("Smooth")
  float brighten = 0.4f;  // gentle lift of the subject's light and skin, 0..1
  float pop = 0.5f;       // subject clarity / separation from the background, 0..1
};

struct Report {
  float subjectCoverage = 0.0f;  // fraction of the frame that is subject
  float skinCoverage = 0.0f;     // fraction of the frame retouched as skin
  int32_t spotsRemoved = 0;      // blemishes healed
  float blurRadius = 0.0f;       // background blur radius in pixels
};

// Masks are single-channel GRAY8 images of any size (they are upsampled and
// aligned to the photo's edges). `subject` is required; `faces` marks face
// areas (enables skin retouching, calibrated to those faces' own colour);
// `protect` marks eyes, brows and lips, which are never smoothed.
// The image (RGBA8/BGRA8) is processed in place.
Error apply(ImageBuffer& image, const ImageBuffer& subject, const ImageBuffer* faces, const ImageBuffer* protect,
            const Params& params, const CancellationToken* token, Report* report);

// Subject cut-out seeded by points (normalised 0..1 image coordinates; empty
// = the frame centre). Returns a GRAY8 mask whose longest edge is ~320 px.
Result<ImageBuffer> subjectFromPoints(const ImageBuffer& image, const std::vector<std::array<float, 2>>& points,
                                      const CancellationToken* token);

// ------------------------------------------------------------------ style

struct Style {
  static constexpr int kQuantiles = 9;  // at 1, 5, 15, 30, 50, 70, 85, 95, 99 %
  std::array<float, kQuantiles> luma{};
  std::array<float, 3> zoneCb{};  // shadows, midtones, highlights
  std::array<float, 3> zoneCr{};
  std::array<float, 2> chromaStd{};
  float vignette = 1.0f;  // corner / centre mean luma
  float grain = 0.0f;     // noise std-dev
  float meanLuma = 0.5f;
};

Result<Style> analyzeStyle(const ImageBuffer& image, const CancellationToken* token);
// Transfers `target` onto the image in place; strength 0..1.
Error applyStyle(ImageBuffer& image, const Style& target, float strength, const CancellationToken* token);

// Edge-aware upsampling of a coarse mask to the guide's size (fast guided
// filter, He & Sun). `radius` is in guide pixels; result is in [0,1].
Result<Plane> refineMask(const Plane& coarse, const Plane& guide, int32_t radius, float eps,
                         const CancellationToken* token);

}  // namespace photoplus::portrait
