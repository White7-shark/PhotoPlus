#pragma once

// Toon's image tools (toon.cpp): box and guided filters, a noise estimate,
// contrast-limited adaptive histogram equalisation, k-means in Lab, small
// line-speck removal, and anti-aliased shapes (filled polygons, tapered
// strokes) for drawing a face's features from its contour points.

#include <array>
#include <cstdint>
#include <vector>

#include "../art/core.hpp"
#include "photoplus/cancellation.hpp"

namespace photoplus::toon::filters {

using art::core::Img;

// Mean over a (2r+1)² window, edge-clamped (running sums: the same cost at any radius). 1-channel.
Img boxMean(const Img& src, int r, const CancellationToken* token);

// He, Sun and Tang's guided filter: `src` smoothed so that its edges follow
// `guide` (both 1-channel, the same size). eps: in the guide's units squared.
Img guided(const Img& guide, const Img& src, int r, float eps, const CancellationToken* token);

// The same, its coefficients found at half size (He and Sun's fast guided filter): for large radii.
Img guidedFast(const Img& guide, const Img& src, int r, float eps, const CancellationToken* token);

// The sensor noise (standard deviation, in the units of `l`) by Immerkær's
// Laplacian-difference estimate over the flattest half of the picture.
float noiseSigma(const Img& l);

// CLAHE on lightness L (0..100): tiles about `tile` px wide, each tile's
// histogram clipped at `clip` times the mean bin and equalised, the mappings
// interpolated between tile centres.
void clahe(Img& L, int tile, float clip, const CancellationToken* token);

// A colour palette in Lab by k-means (k-means++ seeding, deterministic for
// the seed), from the pixels of `lab` (3-channel) where `where` > 0.5 (null:
// all), at most `samples` of them; `lWeight` scales lightness against hue.
std::vector<std::array<float, 3>> kmeansLab(const Img& lab, const Img* where, int k, float lWeight, int samples, uint32_t seed);

// Each pixel's colour from the palette: the nearest, blended with the
// second nearest across a boundary `soft` Lab units wide (anti-aliased cel edges).
void assignPalette(const Img& lab, const std::vector<std::array<float, 3>>& palette, float lWeight, float soft, Img& out,
                   const CancellationToken* token);

// Removes the pieces of `ink` (0..1) smaller than `minArea` pixels (connected
// at 8 neighbours, where ink > 0.35): specks and broken dashes, not lines.
void dropSpecks(Img& ink, int minArea);

// A point in pixels.
struct Pt {
  float x = 0, y = 0;
};

// A smooth curve through the points (Catmull-Rom), `steps` segments between each pair; closed or open.
std::vector<Pt> smoothCurve(const std::vector<Pt>& pts, int steps, bool closed);

// Coverage (0..1, anti-aliased over `feather` px) of a closed polygon, max-blended into `alpha`.
void fillPolygon(Img& alpha, const std::vector<Pt>& poly, float feather);

// A stroke along the points, its width going from w0 to w1 (px), rounded
// ends, coverage max-blended into `alpha` (scaled by `opacity`).
void stroke(Img& alpha, const std::vector<Pt>& pts, float w0, float w1, float opacity);

// Is the point inside the polygon (even-odd)?
bool inside(const std::vector<Pt>& poly, float x, float y);

}  // namespace photoplus::toon::filters
