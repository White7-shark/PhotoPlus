#pragma once

#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

// Core image-processing building blocks (Phase 4): resampling, filtering,
// geometry, colour, compositing and masks. Every function works on RGBA8,
// BGRA8, RGB8 and (where it makes sense) Gray8, polls `token`, reports
// progress through it, and returns an Error instead of throwing.
//
// Colour images with alpha are filtered with premultiplied alpha internally,
// so transparent pixels never bleed their (meaningless) colour into edges;
// buffers stay straight-alpha in and out, as everywhere in the engine.
namespace photoplus::ops {

enum class ResizeFilter : int32_t {
  Auto = 0,      // area when shrinking (no aliasing), Lanczos-3 when enlarging
  Bilinear = 1,  // fastest
  Area = 2,      // box average over the source footprint
  Lanczos = 3,   // sharpest; anti-aliased when shrinking
};

Result<ImageBuffer> resize(const ImageBuffer& src, int32_t width, int32_t height, ResizeFilter filter,
                           const CancellationToken* token);

// The separable Gaussian kernel blur() uses for sigma <= 8 (for the GPU).
struct GaussianKernel {
  int32_t first = 0;            // offset of weights[0]
  std::vector<float> weights;   // normalised
};
GaussianKernel gaussianKernel(float sigma);

// Gaussian blur, sigma in pixels. Large blurs run on a reduced copy (the
// result is smooth either way), so cost stays flat as sigma grows.
Result<ImageBuffer> blur(const ImageBuffer& src, float sigma, const CancellationToken* token);

// Unsharp mask on luminance only (no colour fringes): amount 0..3, radius in
// pixels (Gaussian sigma), threshold 0..0.2 keeps flat areas and noise calm.
Result<ImageBuffer> sharpenLuma(const ImageBuffer& src, float amount, float radius, float threshold,
                                const CancellationToken* token);

// Edge-preserving noise reduction, strength 0..1, scaled to the noise the
// photo actually has (luma gently, chroma blotches firmly). Alpha is kept.
Error denoise(ImageBuffer& image, float strength, const CancellationToken* token);

// Perspective. The homography maps output pixel centres to source pixels.
bool solveHomography(const float from[8], const float to[8], double h[9]);
// The quadrilateral `quad` (x, y of top-left, top-right, bottom-right,
// bottom-left, in source pixels) is straightened into a width x height image.
Result<ImageBuffer> warpPerspective(const ImageBuffer& src, const float quad[8], int32_t width, int32_t height,
                                    const CancellationToken* token);
// Keystone correction: vertical > 0 widens the top (lines converging upwards,
// e.g. a building shot from below, become parallel); horizontal > 0 the same
// for the right side. -1..1. The output keeps the size and is fully covered.
void keystoneQuad(int32_t width, int32_t height, float vertical, float horizontal, float quad[8]);
Result<ImageBuffer> keystone(const ImageBuffer& src, float vertical, float horizontal, const CancellationToken* token);
// How much vertical keystone straightens the photo's near-vertical lines
// (-1..1, 0 if none were found), from a line-direction vote.
float detectVerticalKeystone(const ImageBuffer& src, const CancellationToken* token);

// Colour. 3D LUT: size^3 RGB float triples, red fastest (the .cube order),
// domain and range 0..1, tetrahedral interpolation, blended by strength.
Error applyLut3d(ImageBuffer& image, const float* table, int32_t size, float strength, const CancellationToken* token);
// out = M * (r, g, b, 1), M row-major 3x4; in linear light when `linear`.
Error colorMatrix(ImageBuffer& image, const float m[12], bool linear, const CancellationToken* token);

// Compositing (W3C Compositing and Blending Level 1, straight alpha).
enum class BlendMode : int32_t {
  Normal = 0,
  Multiply = 1,
  Screen = 2,
  Overlay = 3,
  SoftLight = 4,
  HardLight = 5,
  Darken = 6,
  Lighten = 7,
  Difference = 8,
  Add = 9,
  Color = 10,
  Luminosity = 11,
};
constexpr int32_t kBlendModeCount = 12;
// Draws `src` over `dst` at (x, y) (clipped). `mask` (Gray8, src-sized, may
// be null) scales the source alpha, so it doubles as a matte for background
// replacement.
Error blend(ImageBuffer& dst, const ImageBuffer& src, const ImageBuffer* mask, int32_t x, int32_t y, BlendMode mode,
            float opacity, const CancellationToken* token);

// Masks (Gray8, 0 = outside, 255 = inside).
enum class MaskOp : int32_t {
  Invert = 1,
  Feather = 2,    // amount = Gaussian sigma in pixels
  Grow = 3,       // amount = pixels (negative shrinks)
  Threshold = 4,  // amount = level 0..1
};
Error maskAdjust(ImageBuffer& mask, MaskOp op, float amount, const CancellationToken* token);
// Edge-aware refinement (fast guided filter): a coarse mask, e.g. a model's
// low-resolution output, is snapped to the edges of `guide` (hair, fur) and
// returned at the guide's size. radius in guide pixels, eps 1e-6..0.1.
Result<ImageBuffer> maskRefine(const ImageBuffer& mask, const ImageBuffer& guide, int32_t radius, float eps,
                               const CancellationToken* token);

}  // namespace photoplus::ops
