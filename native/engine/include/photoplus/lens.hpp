#pragma once

#include <cstdint>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

// Lens quality ("Transform").
//
// A basic phone lens leaves recognisable traces in every photo:
//   - soft fine detail, even where the photo is in focus;
//   - corners softer than the centre;
//   - colour fringes along edges towards the corners (red and blue land a
//     little apart from green);
//   - more noise from its small sensor.
// (Darker corners are left alone: phones already correct them, and from a
// single photo they cannot be told apart from a scene that is darker at the
// edges, such as a night shot or a low-key portrait.)
//
// analyze() measures each of these on the full-resolution photo. restore()
// corrects them, so the photo looks as if a high-end lens had taken it:
// fringes are re-aligned, detail is recovered by
// deconvolution whose strength follows the measured lens blur across the
// frame, and the result is finished by the enhancement pipeline (noise,
// local contrast, optional 2x detail). White balance and exposure are left
// as they are: a better lens does not change the colour of the light.
namespace photoplus::lens {

struct Analysis {
  float detailBlur = 0.0f;   // lens blur (px) where the photo is sharpest
  float cornerBlur = 0.0f;   // lens blur (px) at the corners
  float fringeRed = 0.0f;    // radial scale of red vs green (fraction of radius)
  float fringeBlue = 0.0f;   // same for blue
  float fringePx = 0.0f;     // colour fringe width at the corners (px)
  float noise = 0.0f;        // noise std-dev, luma 0..1
  int32_t measuredTiles = 0; // regions with enough edges to judge sharpness
  float grade = 1.0f;        // 0 basic … 1 flagship-like
  bool reliable = false;     // enough content to judge the lens at all
};

Result<Analysis> analyze(const ImageBuffer& image, const CancellationToken* token);

struct Params {
  int32_t upscale = 1;           // 1, or 2 for twice the detail
  bool enrich = true;            // a touch of vibrance (off keeps a styled photo's colours exactly)
  float strength = 1.0f;         // 0..1
  float detail = 0.4f;           // finishing local contrast, 0..1
};

struct Report {
  Analysis before;
  float fringeFixedPx = 0.0f;
  float deblurCentre = 0.0f;     // PSF sigma used (px)
  float deblurCorner = 0.0f;     // extra PSF sigma at the corners (px)
  float noiseBefore = 0.0f;
  float noiseAfter = 0.0f;
};

// `output` must be RGBA8/BGRA8 of (width·upscale) x (height·upscale). It may
// be `input` itself when upscale == 1.
Error restore(const ImageBuffer& input, ImageBuffer& output, const Params& params, const CancellationToken* token,
              Report* report);

}  // namespace photoplus::lens
