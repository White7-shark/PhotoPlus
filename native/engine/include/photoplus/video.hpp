#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

// Video ("Glide", Phase 6): the pieces that turn a short handheld clip into
// a steady, clean one. Frames are processed one at a time (memory stays
// bounded whatever the clip length):
//
//   pass 1  estimateMotion(previous, current) per frame, on a small copy
//   plan    stabilize(motions): the camera path is smoothed and every frame
//           gets the warp that moves it onto the smooth path, with a crop
//           zoom just large enough to hide the edges (capped)
//   pass 2  per frame: multi-frame enhancement of a short window (see
//           enhance::Params::fixedReference), tone curves fitted once for
//           the whole clip, portrait tools, then warp()
namespace photoplus::video {

// x' = a·x − b·y + tx, y' = b·x + a·y + ty, in full-resolution pixels with
// the origin at the image centre: where content at (x, y) in the previous
// frame appears in the current one.
struct Similarity {
  double a = 1, b = 0, tx = 0, ty = 0;
};

struct Motion {
  Similarity m;
  float confidence = 0;  // share of image blocks that agree (0..1)
};

Result<Motion> estimateMotion(const ImageBuffer& previous, const ImageBuffer& current, const CancellationToken* token);

// Output-pixel → source-pixel affine map (top-left origin, pixel centres at
// +0.5): sx = m[0]·x + m[1]·y + m[2], sy = m[3]·x + m[4]·y + m[5].
using Affine = std::array<double, 6>;

struct Plan {
  std::vector<Affine> warps;
  float zoom = 1.0f;         // crop zoom applied to every frame
  float shakeBefore = 0.0f;  // mean frame-to-frame jitter of the path (px), before
  float shakeAfter = 0.0f;   // ... and after stabilisation
};

// motions[k] relates frame k-1 to frame k (motions[0] is ignored).
// strength 0..1: how smooth the path becomes (1 ≈ a tripod-like glide).
// maxZoom: largest crop zoom allowed (e.g. 1.15); frames that would need
// more keep part of their motion instead of showing an edge.
Result<Plan> stabilize(const std::vector<Motion>& motions, int32_t width, int32_t height, float strength,
                       float maxZoom);

// dst (same format as src, any size) receives src through the warp.
Error warp(const ImageBuffer& src, const Affine& m, ImageBuffer& dst, const CancellationToken* token);

// Per-channel tone curves that turn `before` into `after` (e.g. a frame and
// its auto-toned version), fitted once and applied to every frame so colour
// and brightness never flicker.
struct Curves {
  std::array<std::array<uint8_t, 256>, 3> rgb{};
};
Result<Curves> fitCurves(const ImageBuffer& before, const ImageBuffer& after);
Error applyCurves(ImageBuffer& image, const Curves& curves, const CancellationToken* token);

// Temporal smoothing of a per-frame subject mask (GRAY8, same size): where
// the new mask agrees with the previous one it is averaged with it (edges
// stop flickering); where it changed a lot (the person moved) the new mask
// wins at once (no ghosting). `previous` is updated to the result.
Error steadyMask(ImageBuffer& previous, const ImageBuffer& current, float smoothing);

// Consensus of three neighbouring frames' subject masks (GRAY8, same size):
// the per-pixel median. A segmentation mistake in a single frame (a patch
// of wall taken for the person, a bite out of the hair) is outvoted by its
// neighbours; real movement, which changes smoothly, passes through.
// `previous` / `next` may be null at the ends of the clip.
Error consensusMask(const ImageBuffer* previous, const ImageBuffer& current, const ImageBuffer* next, ImageBuffer& out);

// The per-frame "pro video" enhancement, fast enough for every frame of a
// clip (the slow analysis is done once per clip: noise, blur, tone curves):
//  - temporal denoise: the previous and next frames, aligned by the camera
//    motion already measured, are averaged in only where they agree with
//    this frame (moving people never ghost);
//  - lens deblur: a luminance sharpen sized to the clip's measured blur;
//  - tone curves fitted once for the clip (no flicker).
struct FrameParams {
  float noiseSigma = 0.0f;  // the clip's noise level (0..1 luma)
  float blurSigma = 0.0f;   // the clip's measured blur (px)
  float denoise = 1.0f;     // 0..1
  float sharpen = 1.0f;     // 0..1
};
// `prev`/`next` may be null (first/last frame). `fromPrev` is the motion of
// this frame (previous → this), `toNext` the next frame's (this → next).
Error enhanceFrame(const ImageBuffer& cur, const ImageBuffer* prev, const ImageBuffer* next, const Similarity& fromPrev,
                   const Similarity& toNext, const FrameParams& params, const Curves* curves, ImageBuffer& dst,
                   const CancellationToken* token);

}  // namespace photoplus::video
