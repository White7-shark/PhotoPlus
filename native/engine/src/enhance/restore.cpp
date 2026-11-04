#include <algorithm>
#include <cmath>
#include <utility>

#include "photoplus/enhance.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::enhance {

namespace {
inline Error ok() { return Error{PP_OK, {}}; }

template <typename Fn>
Error forEachPixel(Plane& dst, const CancellationToken* token, Fn&& fn) {
  const int32_t w = dst.width();
  const bool done = parallelFor(dst.height(), token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end && !isCancelled(token); ++y) {
      for (int32_t x = 0; x < w; ++x) fn(x, y);
    }
  });
  return done ? ok() : cancelledError();
}
}  // namespace

Error denoiseGuided(Plane& I, int32_t radius, float eps, float keep, const CancellationToken* token) {
  ScopedTimer timer("enh.denoise");
  const int32_t w = I.width(), h = I.height();
  auto meanI = Plane::allocate(w, h);
  auto sq = Plane::allocate(w, h);
  auto meanSq = Plane::allocate(w, h);
  if (!meanI || !sq || !meanSq) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to denoise");
  if (Error e = boxBlur(I, *meanI, radius, token); e.status != PP_OK) return e;
  if (Error e = forEachPixel(*sq, token, [&](int32_t x, int32_t y) { sq->row(y)[x] = I.row(y)[x] * I.row(y)[x]; });
      e.status != PP_OK) {
    return e;
  }
  if (Error e = boxBlur(*sq, *meanSq, radius, token); e.status != PP_OK) return e;
  // Reuse buffers: sq <- a, meanSq <- b.
  if (Error e = forEachPixel(*sq, token,
                             [&](int32_t x, int32_t y) {
                               const float m = meanI->row(y)[x];
                               const float var = std::max(0.0f, meanSq->row(y)[x] - m * m);
                               const float a = var / (var + eps);
                               sq->row(y)[x] = a;
                               meanSq->row(y)[x] = m - a * m;
                             });
      e.status != PP_OK) {
    return e;
  }
  // meanI <- mean(a), then smooth b in place via a scratch copy.
  if (Error e = boxBlur(*sq, *meanI, radius, token); e.status != PP_OK) return e;
  if (Error e = boxBlur(*meanSq, *sq, radius, token); e.status != PP_OK) return e;
  return forEachPixel(I, token, [&](int32_t x, int32_t y) {
    const float v = I.row(y)[x];
    const float q = meanI->row(y)[x] * v + sq->row(y)[x];
    I.row(y)[x] = q + keep * (v - q);
  });
}

Error deconvolve(Plane& u, float psfSigma, int32_t iterations, float tvLambda, const CancellationToken* token) {
  ScopedTimer timer("enh.deconvolve");
  if (!(psfSigma > 0.05f) || iterations <= 0) return ok();
  const int32_t w = u.width(), h = u.height();
  constexpr float kOffset = 0.02f;  // RL needs strictly positive data
  auto observed = clonePlane(u);
  auto est = Plane::allocate(w, h);
  auto ratio = Plane::allocate(w, h);
  if (!observed || !est || !ratio) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to deblur");
  for (size_t i = 0; i < u.size(); ++i) {
    observed->data()[i] += kOffset;
    u.data()[i] += kOffset;
  }

  // Biggs–Andrews vector extrapolation: predict along the direction of recent
  // progress before each RL step. Needs three more planes; if the memory
  // budget can't spare them, plain RL runs instead (same result, slower).
  auto prevX = Plane::allocate(w, h);
  auto gOld = Plane::allocate(w, h);
  auto gNew = Plane::allocate(w, h);
  const bool accelerate = prevX && gOld && gNew;
  if (!accelerate) {
    prevX = Plane();
    gOld = Plane();
    gNew = Plane();
  } else {
    // Planes are not zero-initialised; seed history so the first
    // extrapolation (alpha = 0) never multiplies stale bytes (0 * NaN = NaN).
    std::copy(u.data(), u.data() + u.size(), prevX->data());
    std::fill(gOld->data(), gOld->data() + gOld->size(), 0.0f);
  }
  float alpha = 0.0f;
  const size_t n = u.size();

  for (int32_t it = 0; it < iterations; ++it) {
    if (isCancelled(token)) return cancelledError();
    if (accelerate) {
      float* x = u.data();
      float* px = prevX->data();
      for (size_t i = 0; i < n; ++i) {
        const float xi = x[i];
        x[i] = std::max(1e-4f, xi + alpha * (xi - px[i]));  // prediction y_k
        px[i] = xi;                                          // x_k becomes x_{k-1}
      }
      std::copy(x, x + n, gNew->data());  // remember y_k
    }
    if (Error e = gaussianBlur(u, *est, psfSigma, token); e.status != PP_OK) return e;
    if (Error e = forEachPixel(*ratio, token,
                               [&](int32_t x, int32_t y) {
                                 ratio->row(y)[x] = observed->row(y)[x] / std::max(est->row(y)[x], 1e-4f);
                               });
        e.status != PP_OK) {
      return e;
    }
    // The Gaussian PSF is symmetric, so the adjoint blur is the same blur.
    if (Error e = gaussianBlur(*ratio, *est, psfSigma, token); e.status != PP_OK) return e;
    // TV term: divergence of the normalised gradient (computed from u before
    // updating, into `ratio`, which is free again).
    if (tvLambda > 0.0f) {
      constexpr float kEps = 1e-3f;
      if (Error e = forEachPixel(*ratio, token,
                                 [&](int32_t x, int32_t y) {
                                   auto nx = [&](int32_t xx, int32_t yy) {
                                     const float gx = u.at(xx + 1, yy) - u.at(xx, yy);
                                     const float gy = u.at(xx, yy + 1) - u.at(xx, yy);
                                     return gx / std::sqrt(gx * gx + gy * gy + kEps * kEps);
                                   };
                                   auto ny = [&](int32_t xx, int32_t yy) {
                                     const float gx = u.at(xx + 1, yy) - u.at(xx, yy);
                                     const float gy = u.at(xx, yy + 1) - u.at(xx, yy);
                                     return gy / std::sqrt(gx * gx + gy * gy + kEps * kEps);
                                   };
                                   ratio->row(y)[x] = (nx(x, y) - nx(x - 1, y)) + (ny(x, y) - ny(x, y - 1));
                                 });
          e.status != PP_OK) {
        return e;
      }
    }
    if (Error e = forEachPixel(u, token,
                               [&](int32_t x, int32_t y) {
                                 const float div = tvLambda > 0.0f ? ratio->row(y)[x] : 0.0f;
                                 const float denom = std::max(0.2f, 1.0f - tvLambda * div);
                                 // Damp each step to avoid overshoot on noisy data.
                                 const float corr = std::clamp(est->row(y)[x], 0.5f, 2.0f);
                                 u.row(y)[x] = std::clamp(u.row(y)[x] * corr / denom, 1e-4f, 1.3f);
                               });
        e.status != PP_OK) {
      return e;
    }
    if (accelerate) {
      // g_k = x_{k+1} - y_k;  alpha = <g_k, g_{k-1}> / <g_{k-1}, g_{k-1}>
      double num = 0.0, den = 0.0;
      float* g = gNew->data();
      const float* go = gOld->data();
      const float* x = u.data();
      for (size_t i = 0; i < n; ++i) {
        g[i] = x[i] - g[i];
        num += static_cast<double>(g[i]) * go[i];
        den += static_cast<double>(go[i]) * go[i];
      }
      alpha = it == 0 || den <= 0.0 ? 0.0f : static_cast<float>(std::clamp(num / den, 0.0, 0.9));
      std::swap(*gNew, *gOld);
    }
  }
  for (size_t i = 0; i < u.size(); ++i) u.data()[i] -= kOffset;
  return ok();
}

}  // namespace photoplus::enhance
