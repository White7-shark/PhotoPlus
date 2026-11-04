#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "photoplus/enhance.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::enhance {

namespace {

struct Tap {
  int32_t index;  // sensor pixel along this axis
  float w;        // bilinear weight of its sub-samples on this output pixel
};

// For every output coordinate along one axis: the sensor pixels whose
// sub-samples (at (p + shift) * scale + i) land within one output pixel,
// with their summed bilinear weights. At most 2·scale + 2 taps each.
class TapTable {
 public:
  TapTable(int32_t outSize, int32_t inSize, int32_t scale, float shift) : stride_(2 * scale + 2) {
    taps_.resize(static_cast<size_t>(outSize) * static_cast<size_t>(stride_));
    counts_.assign(static_cast<size_t>(outSize), 0);
    covers_.assign(static_cast<size_t>(outSize), 0.0f);
    const float s = static_cast<float>(scale);
    for (int32_t p = 0; p < inSize; ++p) {
      for (int32_t i = 0; i < scale; ++i) {
        const float pos = (static_cast<float>(p) + shift) * s + static_cast<float>(i);
        const auto o0 = static_cast<int32_t>(std::floor(pos));
        for (int32_t o = o0; o <= o0 + 1; ++o) {
          if (o < 0 || o >= outSize) continue;
          const float w = 1.0f - std::fabs(pos - static_cast<float>(o));
          if (w <= 0.0f) continue;
          add(o, p, w);
        }
      }
    }
  }
  const Tap* at(int32_t o) const { return &taps_[static_cast<size_t>(o) * static_cast<size_t>(stride_)]; }
  int32_t count(int32_t o) const { return counts_[static_cast<size_t>(o)]; }
  float cover(int32_t o) const { return covers_[static_cast<size_t>(o)]; }

 private:
  void add(int32_t o, int32_t p, float w) {
    const size_t base = static_cast<size_t>(o) * static_cast<size_t>(stride_);
    int32_t& n = counts_[static_cast<size_t>(o)];
    covers_[static_cast<size_t>(o)] += w;
    for (int32_t k = 0; k < n; ++k) {
      if (taps_[base + static_cast<size_t>(k)].index == p) {
        taps_[base + static_cast<size_t>(k)].w += w;
        return;
      }
    }
    if (n < stride_) taps_[base + static_cast<size_t>(n++)] = Tap{p, w};
  }
  int32_t stride_;
  std::vector<Tap> taps_;
  std::vector<int32_t> counts_;
  std::vector<float> covers_;
};

}  // namespace

// Multi-frame super-resolution by iterative back-projection (Irani & Peleg).
//
// Observation model for frame k:  Y_k = D · H · S_k · X + noise, where X is the
// high-resolution scene, S_k the sub-pixel shift, H the optical blur and D the
// pixel integration (box average over scale×scale) of a smaller sensor.
// Starting from an initial estimate, each iteration simulates every frame from
// X, measures the error against what the camera really captured, and projects
// that error back onto X. Because every frame samples the scene at a different
// sub-pixel offset, the combined constraints pin down detail finer than any
// single frame's pixels.
//
// Both passes are row-parallel: the forward pass writes one error per sensor
// pixel; the back-projection is the exact transpose of a bilinear splat,
// computed as separable gathers through precomputed tap tables (no locks, a
// few multiply-adds per output pixel and frame).
Result<Plane> superResolve(const std::vector<const Plane*>& frames, const std::vector<Shift>& shifts,
                           const std::vector<const Plane*>& robustness, Plane initial, int32_t scale,
                           float psfSigma, int32_t iterations, const CancellationToken* token) {
  ScopedTimer timer("enh.superres");
  if (frames.empty() || frames.size() != shifts.size() || robustness.size() != frames.size()) {
    return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "superResolve: inconsistent inputs"));
  }
  if (scale < 1 || scale > 4) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "superResolve: bad scale"));
  const int32_t lw = frames[0]->width(), lh = frames[0]->height();
  const int32_t hw = initial.width(), hh = initial.height();
  if (hw != lw * scale || hh != lh * scale) {
    return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "superResolve: initial estimate has the wrong size"));
  }
  Plane X = std::move(initial);
  auto blurred = Plane::allocate(hw, hh);
  auto grad = Plane::allocate(hw, hh);
  auto smoothed = Plane::allocate(hw, hh);
  auto cover = Plane::allocate(hw, hh);
  auto rowsTmp = Plane::allocate(hw, lh);
  if (!blurred || !grad || !smoothed || !cover || !rowsTmp) {
    return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for super-resolution"));
  }
  std::vector<Plane> errors;
  for (size_t k = 0; k < frames.size(); ++k) {
    auto e = Plane::allocate(lw, lh);
    if (!e) return failure(e.error());
    errors.push_back(std::move(e).value());
  }
  const float s = static_cast<float>(scale);
  const float invArea = 1.0f / (s * s);
  std::vector<TapTable> colTaps, rowTaps;
  colTaps.reserve(frames.size());
  rowTaps.reserve(frames.size());
  for (size_t k = 0; k < frames.size(); ++k) {
    colTaps.emplace_back(hw, lw, scale, shifts[k].dx);
    rowTaps.emplace_back(hh, lh, scale, shifts[k].dy);
  }
  constexpr float kStep = 1.6f;  // over-relaxation: converges ~2x faster than 1.0, still stable

  for (int32_t it = 0; it < iterations; ++it) {
    if (isCancelled(token)) return failure(cancelledError());
    if (Error e = gaussianBlur(X, *blurred, psfSigma, token); e.status != PP_OK) return failure(e);

    // Forward model: simulate each sensor pixel and store the residual.
    for (size_t k = 0; k < frames.size(); ++k) {
      if (!shifts[k].valid) continue;
      const Plane& Y = *frames[k];
      const Plane* R = robustness[k];
      const float dx = shifts[k].dx, dy = shifts[k].dy;
      Plane& E = errors[k];
      const bool ok = parallelFor(lh, token, [&](int32_t begin, int32_t end) {
        for (int32_t y = begin; y < end && !isCancelled(token); ++y) {
          const float* yrow = Y.row(y);
          const float* rrow = R != nullptr ? R->row(y) : nullptr;
          float* erow = E.row(y);
          for (int32_t x = 0; x < lw; ++x) {
            float sim = 0.0f;
            for (int32_t j = 0; j < scale; ++j)
              for (int32_t i = 0; i < scale; ++i)
                sim += blurred->sample((static_cast<float>(x) + dx) * s + static_cast<float>(i),
                                       (static_cast<float>(y) + dy) * s + static_cast<float>(j));
            erow[x] = (yrow[x] - sim * invArea) * (rrow != nullptr ? rrow[x] : 1.0f);
          }
        }
      });
      if (!ok) return failure(cancelledError());
    }

    // Back-projection: the transpose of "bilinear-splat every sub-sample".
    // The splat weights are separable (wx · wy), so it is applied as two 1-D
    // gathers through precomputed tap tables, then normalised by coverage.
    std::fill(grad->data(), grad->data() + grad->size(), 0.0f);
    std::fill(cover->data(), cover->data() + cover->size(), 0.0f);
    for (size_t k = 0; k < frames.size(); ++k) {
      if (!shifts[k].valid) continue;
      const Plane& E = errors[k];
      const TapTable& cols = colTaps[k];
      const TapTable& rows = rowTaps[k];
      // Pass 1 (x): rowsTmp(y, Xh) = sum_x wx · E(y, x).
      bool ok = parallelFor(lh, token, [&](int32_t begin, int32_t end) {
        for (int32_t y = begin; y < end && !isCancelled(token); ++y) {
          const float* erow = E.row(y);
          float* trow = rowsTmp->row(y);
          for (int32_t Xh = 0; Xh < hw; ++Xh) {
            float acc = 0.0f;
            const Tap* t = cols.at(Xh);
            for (int32_t n = 0; n < cols.count(Xh); ++n) acc += t[n].w * erow[t[n].index];
            trow[Xh] = acc;
          }
        }
      });
      if (!ok) return failure(cancelledError());
      // Pass 2 (y): grad(Yh, Xh) += sum_y wy · rowsTmp(y, Xh); coverage is the
      // product of the 1-D coverages.
      ok = parallelFor(hh, token, [&](int32_t begin, int32_t end) {
        for (int32_t Yh = begin; Yh < end && !isCancelled(token); ++Yh) {
          float* grow = grad->row(Yh);
          float* crow = cover->row(Yh);
          const Tap* t = rows.at(Yh);
          const int32_t nt = rows.count(Yh);
          const float rowCover = rows.cover(Yh);
          for (int32_t n = 0; n < nt; ++n) {
            const float* trow = rowsTmp->row(t[n].index);
            const float wy = t[n].w;
            for (int32_t Xh = 0; Xh < hw; ++Xh) grow[Xh] += wy * trow[Xh];
          }
          for (int32_t Xh = 0; Xh < hw; ++Xh) crow[Xh] += rowCover * cols.cover(Xh);
        }
      });
      if (!ok) return failure(cancelledError());
    }
    for (size_t i = 0; i < grad->size(); ++i) {
      const float c = cover->data()[i];
      grad->data()[i] = c > 1e-4f ? grad->data()[i] / c : 0.0f;
    }

    // Apply the (symmetric) blur adjoint and update.
    if (Error e = gaussianBlur(*grad, *smoothed, psfSigma, token); e.status != PP_OK) return failure(e);
    for (size_t i = 0; i < X.size(); ++i) {
      X.data()[i] = std::clamp(X.data()[i] + kStep * smoothed->data()[i], -0.05f, 1.05f);
    }
  }
  return X;
}

}  // namespace photoplus::enhance
