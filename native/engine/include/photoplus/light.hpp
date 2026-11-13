#pragma once

#include <cstdint>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

// Smart lighting: how the camera handles dark rooms, night, flash and harsh
// light. Built on what the best phone cameras do (see
// docs/PHASE9_LIGHTING_REPORT.md for sources):
//
//   meter   — reads the scene (brightness from the camera's own exposure,
//             contrast from the preview) and picks a plan: a normal shot,
//             a Moonlight burst (dim), flash or screen flash (very dark), or
//             Bright Balance (a highlight-safe burst for harsh or backlit
//             light). Night Sight's operating points: a single shot struggles
//             below ~30 lux, merged bursts work to ~3 lux, flash below ~0.3.
//   tone    — local tone mapping after the burst merge (HDR+ style):
//             highlights compressed, shadows lifted where they hold detail,
//             detail kept. Night rendering keeps night looking like night
//             (lower key, gentle S-curve, true blacks), as Night Sight does.
//   flashFuse — Natural Flash: a flash frame and the ambient (no-flash)
//             frames combined (Petschnigg et al. / Eisemann & Durand 2004):
//             the ambient light's colour and mood, the flash frame's clean
//             detail; flash shadows and hot spots taken from the ambient.
//   screenFlashColour — the front "flash" (the screen) tinted toward the
//             room's light, so a warm room stays warm (like True Tone flash).
namespace photoplus::light {

enum class Plan : int32_t {
  Normal = 0,
  Moonlight = 1,      // dim: merged burst, night rendering
  Flash = 2,          // very dark: flash (rear: Natural Flash fusion; front: screen flash)
  BrightBalance = 3,  // harsh or backlit: highlight-safe burst, local tone
};

struct Exposure {
  float iso = 0;           // 0 = unknown
  float exposureS = 0;     // seconds
  float fNumber = 0;       // 0 = unknown (1.8 assumed)
};

struct MeterInput {
  Exposure exposure;
  bool front = false;
  bool hasFlash = false;
  float faceLuma = -1.0f;  // mean luma (0..1, sRGB) of the main face, -1 = no face
  float focusDiopters = -1.0f;  // the lens's focus (1/metres), -1 = unknown
};

struct Meter {
  float ev100 = 0;            // scene brightness (exposure value at ISO 100)
  float lux = 0;              // ≈ 2.5 · 2^EV100
  bool exposureKnown = false;
  float key = 0;              // the preview's log-average luma (0..1)
  float highlights = 0;       // share of the preview at or near white
  float shadows = 0;          // share of the preview near black
  float rangeStops = 0;       // visible dynamic range of the preview
  bool backlit = false;       // a face much darker than the scene
  Plan plan = Plan::Normal;
  int32_t frames = 1;         // burst length for the plan
  float exposureBias = 0;     // EV for the burst (negative: protect highlights)
  float night = 0;            // 0..1 night rendering for tone()
  uint8_t screenFlash[3] = {255, 255, 255};  // overlay colour for a front "flash"
};

// `preview`: a small RGBA/BGRA frame of the scene (any size ≥ 16 px).
Meter meter(const ImageBuffer& preview, const MeterInput& in);

struct ToneParams {
  bool autoExposure = true;  // bring the scene's key to a pleasing level
  float exposure = 0;        // stops, added to it
  float shadows = -1;        // 0..1 shadow lift; -1 = automatic from the scene
  float highlights = -1;     // 0..1 highlight compression; -1 = automatic
  float night = 0;           // 0..1 night rendering
  float detail = 0.35f;      // 0..1 local contrast kept / added
  float whiteBalance = 0;    // 0..1 low-light colour correction (grey-edge, kept a touch warm)
  float captureBias = 0;     // EV the frames were deliberately underexposed by (≤ 0), restored in full
  float strength = 1;        // 0..1
};

struct ToneReport {
  float keyBefore = 0, keyAfter = 0;  // log-average luma, linear (0..1)
  float rangeBefore = 0;              // dynamic range (stops, 2nd–98th percentile)
  float shadows = 0, highlights = 0;  // the amounts used
  float clippedAfter = 0;             // share of pixels at white
};

Error tone(ImageBuffer& image, const ToneParams& params, const CancellationToken* token, ToneReport* report);

struct FuseParams {
  float fill = 0.3f;      // 0..1 how much of the flash's own light is kept (0: ambient look only)
  float strength = 1.0f;  // 0..1
};

struct FuseReport {
  bool aligned = false;     // the flash frame was matched to the ambient one
  float flashGain = 0;      // how much brighter the flash frame is (linear)
  float shadowShare = 0;    // share of the frame in flash shadow (kept from the ambient)
  float hotShare = 0;       // share in flash hot spots (kept from the ambient)
};

// `ambient` is the no-flash photo (ideally a merged burst), `flash` the
// flash one, same size. The result is written to `out` (same size).
Error flashFuse(const ImageBuffer& ambient, const ImageBuffer& flash, ImageBuffer& out, const FuseParams& params,
                const CancellationToken* token, FuseReport* report);

}  // namespace photoplus::light
