#include <algorithm>
#include <cmath>
#include <vector>

#include "photoplus/enhance.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::enhance {

namespace {
float percentile(const Plane& p, float q) {
  // 1024-bin histogram over [0,1] is plenty for tonal decisions.
  std::vector<size_t> hist(1024, 0);
  const size_t n = p.size();
  const float* d = p.data();
  for (size_t i = 0; i < n; ++i) {
    hist[static_cast<size_t>(std::clamp(d[i], 0.0f, 1.0f) * 1023.0f)]++;
  }
  const auto target = static_cast<size_t>(q * static_cast<float>(n));
  size_t cum = 0;
  for (size_t b = 0; b < hist.size(); ++b) {
    cum += hist[b];
    if (cum > target) return static_cast<float>(b) / 1023.0f;
  }
  return 1.0f;
}
}  // namespace

Error tone(YccImage& img, const ToneParams& params, const CancellationToken* token) {
  ScopedTimer timer("enh.tone");
  const int32_t w = img.width(), h = img.height();

  if (params.autoWhiteBalance) {
    // Grey-world on mid-tones only (ignore near-black/white and strongly
    // saturated pixels, which bias the estimate). Correction is partial and
    // bounded so intentional colour casts (sunsets) are softened, not erased.
    double sb = 0.0, sr = 0.0;
    size_t n = 0;
    for (int32_t y = 0; y < h; y += 2) {
      const float* py = img.y.row(y);
      const float* pb = img.cb.row(y);
      const float* pr = img.cr.row(y);
      for (int32_t x = 0; x < w; x += 2) {
        if (py[x] < 0.15f || py[x] > 0.9f) continue;
        if (std::fabs(pb[x]) + std::fabs(pr[x]) > 0.25f) continue;
        sb += pb[x];
        sr += pr[x];
        ++n;
      }
    }
    if (n > 256) {
      const float shiftB = std::clamp(-static_cast<float>(sb / static_cast<double>(n)) * 0.6f, -0.05f, 0.05f);
      const float shiftR = std::clamp(-static_cast<float>(sr / static_cast<double>(n)) * 0.6f, -0.05f, 0.05f);
      for (size_t i = 0; i < img.cb.size(); ++i) {
        img.cb.data()[i] += shiftB;
        img.cr.data()[i] += shiftR;
      }
    }
  }

  if (params.autoLevels) {
    const float lo = percentile(img.y, 0.004f);
    const float hi = percentile(img.y, 0.996f);
    const float black = std::min(lo, 0.08f);
    const float gain = std::clamp(0.98f / std::max(hi - black, 0.2f), 1.0f, 1.6f);
    // Lift dark images towards a natural mid-tone with a gentle gamma.
    const float median = percentile(img.y, 0.5f);
    const float m = std::clamp((median - black) * gain, 0.05f, 0.95f);
    const float gamma = m < 0.38f ? std::clamp(std::log(0.42f) / std::log(m), 0.6f, 1.0f) : 1.0f;
    const bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
      for (int32_t y = begin; y < end && !isCancelled(token); ++y) {
        float* py = img.y.row(y);
        float* pb = img.cb.row(y);
        float* pr = img.cr.row(y);
        for (int32_t x = 0; x < w; ++x) {
          const float before = std::max(py[x], 1e-4f);
          float v = std::clamp((py[x] - black) * gain, 0.0f, 1.0f);
          if (gamma != 1.0f) v = std::pow(v, gamma);
          // Keep saturation constant as luma changes.
          const float k = std::clamp(v / before, 0.5f, 2.0f);
          py[x] = v;
          pb[x] *= k;
          pr[x] *= k;
        }
      }
    });
    if (!done) return cancelledError();
  }

  if (params.localContrast > 0.0f) {
    // Clarity: boost mid-frequency contrast relative to a wide blur, with a
    // soft limiter so halos stay invisible.
    auto base = Plane::allocate(w, h);
    if (!base) return base.error();
    const float sigma = std::max(4.0f, static_cast<float>(std::max(w, h)) / 80.0f);
    if (Error e = gaussianBlur(img.y, *base, sigma, token); e.status != PP_OK) return e;
    const float amount = std::clamp(params.localContrast, 0.0f, 1.0f) * 0.8f;
    for (size_t i = 0; i < img.y.size(); ++i) {
      const float v = img.y.data()[i];
      const float d = v - base->data()[i];
      const float boost = amount * d / (1.0f + 6.0f * std::fabs(d));
      img.y.data()[i] = std::clamp(v + boost, 0.0f, 1.0f);
    }
  }

  if (params.vibrance > 0.0f) {
    const float v = std::clamp(params.vibrance, 0.0f, 1.0f) * 0.6f;
    for (size_t i = 0; i < img.cb.size(); ++i) {
      const float b = img.cb.data()[i], r = img.cr.data()[i];
      const float sat = std::sqrt(b * b + r * r);
      // Boost muted colours more than already-vivid ones (skin stays natural).
      const float k = 1.0f + v * std::clamp(1.0f - sat / 0.3f, 0.0f, 1.0f);
      img.cb.data()[i] = b * k;
      img.cr.data()[i] = r * k;
    }
  }
  return Error{PP_OK, {}};
}

}  // namespace photoplus::enhance
