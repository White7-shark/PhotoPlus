#pragma once

#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/status.hpp"

// Computational photography: turn one or more frames from a modest phone
// camera into a cleaner, sharper, higher-resolution photo.
//
//   frames ─▶ quality analysis ─▶ sub-pixel alignment ─▶ robust fusion /
//   multi-frame super-resolution ─▶ edge-preserving denoise ─▶
//   deconvolution deblur ─▶ white balance, levels, local contrast ─▶ output
//
// Everything is classical signal processing (no learned model), so results
// are deterministic, explainable and run fully offline on any device.
namespace photoplus::enhance {

struct QualityReport {
  float noiseSigma = 0.0f;   // estimated Gaussian noise std-dev, luma in [0,1]
  float blurSigma = 0.0f;    // estimated Gaussian blur std-dev in pixels
  float sharpness = 0.0f;    // normalised variance of the Laplacian
  float meanLuma = 0.0f;
  float highlightsClipped = 0.0f;  // fraction of pixels >= 0.98
  float shadowsClipped = 0.0f;     // fraction of pixels <= 0.02
};

// Immerkær's fast noise estimate (robust to image content).
float estimateNoise(const Plane& luma);
float laplacianSharpness(const Plane& luma);
// Edge-based defocus estimate using the re-blur gradient ratio (Zhuo & Sim).
// Returns 0 both for a perfectly sharp image and for one with too few edges
// to judge; estimateBlur() tells the two apart.
Result<float> estimateBlurSigma(const Plane& luma, float noiseSigma, const CancellationToken* token);
struct BlurEstimate {
  float sigma = 0.0f;
  int32_t edges = 0;  // isolated step edges measured (fewer than 32: no estimate)
};
Result<BlurEstimate> estimateBlur(const Plane& luma, float noiseSigma, const CancellationToken* token);
Result<QualityReport> analyze(const Plane& luma, const CancellationToken* token);

struct Shift {
  float dx = 0.0f;  // frame(x) ≈ reference(x + d)
  float dy = 0.0f;
  float residual = 0.0f;  // RMS photometric error after alignment
  bool valid = false;
};

// Coarse-to-fine Lucas–Kanade translation estimate with sub-pixel accuracy.
Result<Shift> alignTranslation(const Plane& reference, const Plane& frame, const CancellationToken* token);

// Same, with the reference pyramid built once by alignmentPyramid() and
// reused for every frame of a burst.
Result<std::vector<Plane>> alignmentPyramid(const Plane& base, const CancellationToken* token);
Result<Shift> alignTranslation(const std::vector<Plane>& referencePyramid, const Plane& frame,
                               const CancellationToken* token);

// Per-frame weights in [0,1] (index 0, the reference, is left empty):
// low where a frame disagrees with the reference beyond the noise level
// (moving subjects, failed alignment), so those regions are not merged.
Result<std::vector<Plane>> robustnessMaps(const std::vector<const Plane*>& frames, const std::vector<Shift>& shifts,
                                          float noiseSigma, const CancellationToken* token);

// Robust kernel-regression merge of aligned frames onto a grid `scale` times
// larger than the reference. scale == 1 is burst denoising; scale == 2 is
// multi-frame super-resolution (hand-shake supplies sub-pixel samples).
// `kernelSigma` (output pixels) <= 0 selects the default footprint.
// `robustness` may pass maps already computed by robustnessMaps() for the
// same frames, so callers that need them again do not compute them twice.
Result<Plane> fuse(const std::vector<const Plane*>& frames, const std::vector<Shift>& shifts, int32_t scale,
                   float noiseSigma, const CancellationToken* token, float kernelSigma = 0.0f,
                   const std::vector<Plane>* robustness = nullptr);

// Iterative back-projection super-resolution. `robustness` holds optional
// per-frame weight maps (nullptr = 1). `psfSigma` is the optical blur in
// output pixels; `initial` (output size) seeds the solution.
Result<Plane> superResolve(const std::vector<const Plane*>& frames, const std::vector<Shift>& shifts,
                           const std::vector<const Plane*>& robustness, Plane initial, int32_t scale,
                           float psfSigma, int32_t iterations, const CancellationToken* token);

// Self-guided filter (He et al.): edge-preserving smoothing with an epsilon
// derived from the noise level. `keep` re-injects a fraction of fine grain.
Error denoiseGuided(Plane& plane, int32_t radius, float eps, float keep, const CancellationToken* token);

// Richardson–Lucy deconvolution of a Gaussian PSF with total-variation
// regularisation to suppress ringing and noise amplification.
Error deconvolve(Plane& plane, float psfSigma, int32_t iterations, float tvLambda, const CancellationToken* token);

struct ToneParams {
  bool autoWhiteBalance = true;
  bool autoLevels = true;
  float localContrast = 0.35f;  // "clarity", 0..1
  float vibrance = 0.25f;       // 0..1
};
Error tone(YccImage& image, const ToneParams& params, const CancellationToken* token);

// Mean absolute Laplacian on a sparse grid: fine texture (and noise).
float fineTexture(const Plane& p);
// Keeps restoration natural: if `after` holds more than `budget` times the
// fine texture of `before`, it is blended back toward `before` until it
// holds exactly that. Returns the share of the change kept (1 = untouched).
float applyDetailBudget(const Plane& before, Plane& after, float budget);

struct Params {
  int32_t upscale = 1;     // 1 or 2
  float denoise = 1.0f;    // 0..1 multiplier on the automatic strength (0 = off)
  float sharpen = 1.0f;    // 0..1 multiplier on the automatic deblur (0 = off)
  float detail = 0.35f;    // local contrast
  bool autoTone = true;
  float vibrance = 0.25f;
  // Lens-aware deblur (Transform): when > 0, this measured blur (px, input
  // scale) replaces the whole-image estimate, so the PSF follows the lens
  // where the photo is in focus rather than an out-of-focus background.
  float measuredBlur = 0.0f;
  // Extra blur (px, input scale) at the frame corners, removed by a second
  // deconvolution blended in with the squared distance from the centre.
  float cornerBlur = 0.0f;
  // Keep the full pre-denoise even when a strong deblur follows (by default
  // it backs off, leaving noise to the deconvolution's regulariser): for
  // photos that will be viewed enlarged, where leftover grain shows.
  bool fullDenoise = false;
  // Video (Glide): frame 0 stays the reference even if another frame is
  // sharper, and a noise level measured once for the whole clip replaces the
  // per-frame estimate, so every output frame is treated alike (no flicker).
  bool fixedReference = false;
  float noiseSigma = 0.0f;
};

struct Report {
  int32_t framesInput = 0;
  int32_t framesUsed = 0;
  int32_t referenceIndex = 0;
  float noiseBefore = 0.0f;
  float noiseAfter = 0.0f;
  float blurBefore = 0.0f;
  float deblurSigma = 0.0f;
};

// Output size is (width·upscale) x (height·upscale) of the frames.
int32_t outputWidth(const ImageBuffer& frame, const Params& params);
int32_t outputHeight(const ImageBuffer& frame, const Params& params);

// When `destination` is given (RGBA8/BGRA8 of the output size, e.g. a locked
// Android Bitmap) the result is written straight into it and an empty
// buffer is returned, saving a full-image copy.
Result<ImageBuffer> run(const std::vector<const ImageBuffer*>& frames, const Params& params,
                        const CancellationToken* token, Report* report, ImageBuffer* destination = nullptr);

}  // namespace photoplus::enhance
