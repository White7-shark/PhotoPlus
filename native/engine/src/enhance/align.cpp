#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "photoplus/enhance.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::enhance {

namespace {

// Gaussian-ish pyramid: level 0 is full resolution.
Result<std::vector<Plane>> pyramid(const Plane& base, const CancellationToken* token) {
  std::vector<Plane> levels;
  auto first = clonePlane(base);
  if (!first) return failure(first.error());
  levels.push_back(std::move(first).value());
  while (std::min(levels.back().width(), levels.back().height()) >= 96 && levels.size() < 7) {
    // Light pre-blur limits aliasing before decimation.
    auto blurred = Plane::allocate(levels.back().width(), levels.back().height());
    if (!blurred) return failure(blurred.error());
    if (Error e = gaussianBlur(levels.back(), *blurred, 0.8f, token); e.status != PP_OK) return failure(e);
    auto next = downsample2(*blurred, token);
    if (!next) return failure(next.error());
    levels.push_back(std::move(next).value());
  }
  return levels;
}

}  // namespace

Result<Shift> alignTranslation(const Plane& reference, const Plane& frame, const CancellationToken* token) {
  ScopedTimer timer("enh.align");
  if (reference.width() != frame.width() || reference.height() != frame.height()) {
    return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Frames must share the same size"));
  }
  auto refPyr = pyramid(reference, token);
  if (!refPyr) return failure(refPyr.error());
  auto frmPyr = pyramid(frame, token);
  if (!frmPyr) return failure(frmPyr.error());

  double dx = 0.0, dy = 0.0;
  double rms = 0.0;
  for (int level = static_cast<int>(refPyr->size()) - 1; level >= 0; --level) {
    if (isCancelled(token)) return failure(cancelledError());
    const Plane& R = (*refPyr)[static_cast<size_t>(level)];
    const Plane& F = (*frmPyr)[static_cast<size_t>(level)];
    const int32_t w = R.width(), h = R.height();
    const int32_t margin = std::max<int32_t>(4, std::min(w, h) / 16);
    // Sample a bounded number of pixels per level so cost stays O(1e5).
    const int32_t step = std::max<int32_t>(1, static_cast<int32_t>(std::sqrt(static_cast<double>(w) * h / 120000.0)));

    // Gauss–Newton normal matrix depends only on reference gradients
    // (inverse-compositional approximation), so build it once per level.
    double hxx = 0.0, hxy = 0.0, hyy = 0.0;
    for (int32_t y = margin; y < h - margin; y += step) {
      for (int32_t x = margin; x < w - margin; x += step) {
        const double gx = 0.5 * (R.at(x + 1, y) - R.at(x - 1, y));
        const double gy = 0.5 * (R.at(x, y + 1) - R.at(x, y - 1));
        hxx += gx * gx;
        hxy += gx * gy;
        hyy += gy * gy;
      }
    }
    const double det = hxx * hyy - hxy * hxy;
    if (det <= 1e-12) continue;  // textureless level; keep previous estimate

    for (int iter = 0; iter < 12; ++iter) {
      double bx = 0.0, by = 0.0, err2 = 0.0;
      size_t n = 0;
      for (int32_t y = margin; y < h - margin; y += step) {
        for (int32_t x = margin; x < w - margin; x += step) {
          // frame(x) = ref(x + d)  =>  ref(x) ≈ frame(x - d)
          const double e = F.sample(static_cast<float>(x - dx), static_cast<float>(y - dy)) - R.row(y)[x];
          const double gx = 0.5 * (R.at(x + 1, y) - R.at(x - 1, y));
          const double gy = 0.5 * (R.at(x, y + 1) - R.at(x, y - 1));
          bx += gx * e;
          by += gy * e;
          err2 += e * e;
          ++n;
        }
      }
      const double ux = (hyy * bx - hxy * by) / det;
      const double uy = (hxx * by - hxy * bx) / det;
      dx += ux;
      dy += uy;
      rms = n ? std::sqrt(err2 / static_cast<double>(n)) : 0.0;
      if (ux * ux + uy * uy < 1e-6) break;
    }
    if (level > 0) {
      dx *= 2.0;
      dy *= 2.0;
    }
  }
  Shift s;
  s.dx = static_cast<float>(dx);
  s.dy = static_cast<float>(dy);
  s.residual = static_cast<float>(rms);
  s.valid = std::isfinite(s.dx) && std::isfinite(s.dy) &&
            std::fabs(s.dx) < static_cast<float>(reference.width()) / 8.0f &&
            std::fabs(s.dy) < static_cast<float>(reference.height()) / 8.0f;
  return s;
}

}  // namespace photoplus::enhance
