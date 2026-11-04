#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "photoplus/enhance.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::enhance {

// Robustness maps. Motion and misalignment change a region's *local mean*;
// aliasing (which differs from frame to frame and is exactly the extra
// information super-resolution needs) changes only fine detail. So frames
// are compared after a low-pass, and per-pixel differences are ignored.
// Noise in the low-passed images is ~noise / (2·sqrt(pi)·sigma).
Result<std::vector<Plane>> robustnessMaps(const std::vector<const Plane*>& frames, const std::vector<Shift>& shifts,
                                          float noiseSigma, const CancellationToken* token) {
  ScopedTimer timer("enh.robustness");
  const Plane& ref = *frames[0];
  const int32_t w = ref.width(), h = ref.height();
  constexpr float kLowPass = 1.5f;
  const float lpNoise = noiseSigma / (2.0f * 1.7725f * kLowPass);
  const float robustSigma = std::max(0.02f, 4.0f * lpNoise);
  const float inv2r = 1.0f / (2.0f * robustSigma * robustSigma);
  auto refLow = Plane::allocate(w, h);
  if (!refLow) return failure(refLow.error());
  if (Error e = gaussianBlur(ref, *refLow, kLowPass, token); e.status != PP_OK) return failure(e);
  std::vector<Plane> weights(frames.size());
  for (size_t k = 1; k < frames.size(); ++k) {
    if (!shifts[k].valid) continue;
    auto low = Plane::allocate(w, h);
    if (!low) return failure(low.error());
    if (Error e = gaussianBlur(*frames[k], *low, kLowPass, token); e.status != PP_OK) return failure(e);
    const float dx = shifts[k].dx, dy = shifts[k].dy;
    const bool ok = parallelFor(h, token, [&](int32_t begin, int32_t end) {
      for (int32_t y = begin; y < end && !isCancelled(token); ++y) {
        float* row = low->row(y);
        for (int32_t x = 0; x < w; ++x) {
          const float diff = row[x] - refLow->sample(static_cast<float>(x) + dx, static_cast<float>(y) + dy);
          row[x] = std::exp(-diff * diff * inv2r);  // reuse the buffer for the weight
        }
      }
    });
    if (!ok) return failure(cancelledError());
    weights[k] = std::move(low).value();
  }
  return weights;
}

// Kernel-regression merge, the core of burst photography:
//
// Every pixel of every frame is a sample of the scene at a slightly different
// position (hand-shake moves the sensor by fractions of a pixel). Mapping each
// sample to its true position on a finer output grid and accumulating with a
// Gaussian footprint recovers detail no single frame contains, while averaging
// N samples cuts noise by ~sqrt(N). Samples that disagree with the reference
// beyond the noise level (moving subjects, misalignment) are down-weighted so
// motion never ghosts.
//
// Implementation note: rather than scattering (which needs atomics or locks),
// each output row gathers from the input rows that can reach it, which is
// embarrassingly parallel and cache friendly.
Result<Plane> fuse(const std::vector<const Plane*>& frames, const std::vector<Shift>& shifts, int32_t scale,
                   float noiseSigma, const CancellationToken* token, float kernelSigma,
                   const std::vector<Plane>* robustness) {
  ScopedTimer timer("enh.fuse");
  if (frames.empty() || frames.size() != shifts.size()) {
    return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "fuse: frames and shifts must match"));
  }
  if (scale != 1 && scale != 2) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "fuse: scale must be 1 or 2"));
  const Plane& ref = *frames[0];
  const int32_t w = ref.width(), h = ref.height();
  const int32_t ow = w * scale, oh = h * scale;
  auto out = Plane::allocate(ow, oh);
  if (!out) return failure(out.error());

  // Prior: upsampled reference, weighted lightly, fills any sparse cells.
  Plane prior;
  if (scale == 1) {
    auto copy = clonePlane(ref);
    if (!copy) return failure(copy.error());
    prior = std::move(copy).value();
  } else {
    auto up = resizeCubic(ref, ow, oh, token);
    if (!up) return failure(up.error());
    prior = std::move(up).value();
  }

  const float s = static_cast<float>(scale);
  // Footprint in output pixels: wide enough that samples from N frames overlap.
  const float kSigma = kernelSigma > 0.0f ? kernelSigma : (scale == 1 ? 0.45f : 0.9f);
  const int32_t kRadius = std::max<int32_t>(1, static_cast<int32_t>(std::ceil(2.2f * kSigma)));
  const float inv2k = 1.0f / (2.0f * kSigma * kSigma);
  constexpr float kPriorWeight = 0.08f;

  std::vector<Plane> computed;
  if (robustness == nullptr || robustness->size() != frames.size()) {
    auto maps = robustnessMaps(frames, shifts, noiseSigma, token);
    if (!maps) return failure(maps.error());
    computed = std::move(maps).value();
    robustness = &computed;
  }
  const std::vector<Plane>& weights = *robustness;

  const bool done = parallelFor(oh, token, [&](int32_t begin, int32_t end) {
    std::vector<float> acc(static_cast<size_t>(ow)), wsum(static_cast<size_t>(ow));
    for (int32_t oy = begin; oy < end && !isCancelled(token); ++oy) {
      std::fill(acc.begin(), acc.end(), 0.0f);
      std::fill(wsum.begin(), wsum.end(), 0.0f);
      for (size_t k = 0; k < frames.size(); ++k) {
        if (!shifts[k].valid) continue;
        const Plane& f = *frames[k];
        const float dx = shifts[k].dx, dy = shifts[k].dy;
        // Input rows whose samples land within kRadius of output row oy:
        // u = (y + dy + 0.5) * s - 0.5  =>  y = (u + 0.5)/s - 0.5 - dy
        const float yc = (static_cast<float>(oy) + 0.5f) / s - 0.5f - dy;
        const int32_t y0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(yc - static_cast<float>(kRadius) / s)) - 1);
        const int32_t y1 = std::min<int32_t>(h - 1, static_cast<int32_t>(std::ceil(yc + static_cast<float>(kRadius) / s)) + 1);
        for (int32_t y = y0; y <= y1; ++y) {
          const float uy = (static_cast<float>(y) + dy + 0.5f) * s - 0.5f;
          const float ddy = uy - static_cast<float>(oy);
          if (std::fabs(ddy) > static_cast<float>(kRadius)) continue;
          const float wy = std::exp(-ddy * ddy * inv2k);
          const float* frow = f.row(y);
          const float* wrow = k != 0 ? weights[k].row(y) : nullptr;
          for (int32_t x = 0; x < w; ++x) {
            const float v = frow[x];
            const float wr = wrow != nullptr ? wrow[x] : 1.0f;
            const float ux = (static_cast<float>(x) + dx + 0.5f) * s - 0.5f;
            const auto cx = static_cast<int32_t>(std::lround(ux));
            for (int32_t ox = std::max(0, cx - kRadius); ox <= std::min(ow - 1, cx + kRadius); ++ox) {
              const float ddx = ux - static_cast<float>(ox);
              const float wgt = wy * std::exp(-ddx * ddx * inv2k) * wr;
              acc[static_cast<size_t>(ox)] += wgt * v;
              wsum[static_cast<size_t>(ox)] += wgt;
            }
          }
        }
      }
      const float* p = prior.row(oy);
      float* d = out->row(oy);
      for (int32_t ox = 0; ox < ow; ++ox) {
        d[ox] = (acc[static_cast<size_t>(ox)] + kPriorWeight * p[ox]) / (wsum[static_cast<size_t>(ox)] + kPriorWeight);
      }
    }
  });
  if (!done) return failure(cancelledError());
  return std::move(out).value();
}

}  // namespace photoplus::enhance
