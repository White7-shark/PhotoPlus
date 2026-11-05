#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "photoplus/enhance.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::enhance {

float estimateNoise(const Plane& luma) {
  ScopedTimer timer("enh.noise");
  const int32_t w = luma.width(), h = luma.height();
  if (w < 3 || h < 3) return 0.0f;
  // Immerkær (1996): convolve with the difference of two Laplacians, which
  // cancels smooth image structure and leaves the noise. Edges and fine
  // texture still leak through, so (after Tai & Yang 2008) only the
  // flattest pixels by Sobel magnitude contribute.
  const int32_t step = std::max<int32_t>(1, static_cast<int32_t>(std::sqrt(static_cast<double>(w) * h / 250000.0)));
  std::vector<std::pair<float, float>> samples;  // (sobel magnitude, |M * I|)
  samples.reserve(static_cast<size_t>((w / step + 1) * (h / step + 1)));
  for (int32_t y = 1; y < h - 1; y += step) {
    const float* a = luma.row(y - 1);
    const float* b = luma.row(y);
    const float* c = luma.row(y + 1);
    for (int32_t x = 1; x < w - 1; x += step) {
      const float v = a[x - 1] - 2.0f * a[x] + a[x + 1] - 2.0f * b[x - 1] + 4.0f * b[x] - 2.0f * b[x + 1] + c[x - 1] -
                      2.0f * c[x] + c[x + 1];
      const float gx = (a[x + 1] + 2.0f * b[x + 1] + c[x + 1]) - (a[x - 1] + 2.0f * b[x - 1] + c[x - 1]);
      const float gy = (c[x - 1] + 2.0f * c[x] + c[x + 1]) - (a[x - 1] + 2.0f * a[x] + a[x + 1]);
      samples.emplace_back(gx * gx + gy * gy, std::fabs(v));
    }
  }
  if (samples.empty()) return 0.0f;
  // Keep the flattest 50%. Gradient magnitude and the Immerkær response are
  // nearly independent on pure noise, so this subset needs no correction
  // (verified on Gaussian noise: estimate/true = 1.00).
  auto mid = samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2);
  std::nth_element(samples.begin(), mid, samples.end(),
                   [](const auto& l, const auto& r) { return l.first < r.first; });
  double sum = 0.0;
  for (auto it = samples.begin(); it != mid; ++it) sum += it->second;
  const double n = static_cast<double>(mid - samples.begin());
  if (n <= 0.0) return 0.0f;
  return static_cast<float>(std::sqrt(std::numbers::pi / 2.0) * sum / (6.0 * n));
}

float laplacianSharpness(const Plane& luma) {
  const int32_t w = luma.width(), h = luma.height();
  if (w < 3 || h < 3) return 0.0f;
  double s = 0.0, s2 = 0.0;
  for (int32_t y = 1; y < h - 1; ++y) {
    for (int32_t x = 1; x < w - 1; ++x) {
      const float l = luma.at(x - 1, y) + luma.at(x + 1, y) + luma.at(x, y - 1) + luma.at(x, y + 1) - 4.0f * luma.at(x, y);
      s += l;
      s2 += static_cast<double>(l) * l;
    }
  }
  const double n = static_cast<double>(w - 2) * static_cast<double>(h - 2);
  const double mean = s / n;
  return static_cast<float>((s2 / n - mean * mean) * 1000.0);
}

namespace {

constexpr float kReblur = 1.6f;  // large enough to stay sensitive to heavy blur

std::vector<float> discreteGaussian(float sigma) {
  if (sigma <= 0.0f) return {1.0f};
  const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0f)));
  std::vector<float> k(static_cast<size_t>(2 * radius + 1));
  float sum = 0.0f;
  for (int i = -radius; i <= radius; ++i) {
    k[static_cast<size_t>(i + radius)] = std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma));
    sum += k[static_cast<size_t>(i + radius)];
  }
  for (float& v : k) v /= sum;
  return k;
}

std::vector<float> convolve1d(const std::vector<float>& s, const std::vector<float>& k) {
  const int r = static_cast<int>(k.size() / 2), n = static_cast<int>(s.size());
  std::vector<float> out(s.size());
  for (int i = 0; i < n; ++i) {
    float acc = 0.0f;
    for (int j = -r; j <= r; ++j) acc += s[static_cast<size_t>(std::clamp(i + j, 0, n - 1))] * k[static_cast<size_t>(j + r)];
    out[static_cast<size_t>(i)] = acc;
  }
  return out;
}

// Calibration table: gradient ratio (edge / re-blurred edge) for an ideal
// pixel-sampled step blurred by sigma, computed with the very same discrete
// filters used on images, so discretisation needs no analytic correction.
const std::vector<std::pair<float, float>>& ratioTable() {
  static const std::vector<std::pair<float, float>> table = [] {
    std::vector<std::pair<float, float>> t;  // (sigma, ratio), ratio decreasing
    const auto reblur = discreteGaussian(kReblur);
    for (float sigma = 0.0f; sigma <= 6.0f; sigma += 0.02f) {
      std::vector<float> step(129);
      for (size_t i = 0; i < step.size(); ++i) step[i] = i < 64 ? 0.0f : 1.0f;
      const auto edge = convolve1d(step, discreteGaussian(sigma));
      const auto edge2 = convolve1d(edge, reblur);
      float g = 0.0f, g2 = 0.0f;
      for (size_t i = 1; i + 1 < step.size(); ++i) {
        g = std::max(g, 0.5f * (edge[i + 1] - edge[i - 1]));
        g2 = std::max(g2, 0.5f * (edge2[i + 1] - edge2[i - 1]));
      }
      t.emplace_back(sigma, g / g2);
    }
    return t;
  }();
  return table;
}

float sigmaForRatio(float ratio) {
  const auto& t = ratioTable();
  if (ratio >= t.front().second) return t.front().first;
  for (size_t i = 1; i < t.size(); ++i) {
    if (ratio >= t[i].second) {
      const float a = (ratio - t[i].second) / (t[i - 1].second - t[i].second);
      return t[i].first + a * (t[i - 1].first - t[i].first);
    }
  }
  return t.back().first;
}

}  // namespace

Result<float> estimateBlurSigma(const Plane& luma, float noiseSigma, const CancellationToken* token) {
  auto e = estimateBlur(luma, noiseSigma, token);
  if (!e) return failure(e.error());
  return e->sigma;
}

Result<BlurEstimate> estimateBlur(const Plane& luma, float noiseSigma, const CancellationToken* token) {
  ScopedTimer timer("enh.blurEstimate");
  // Measure on a lightly pre-smoothed copy: noise otherwise inflates the
  // selected gradient peaks (biasing blur low). The known pre-blur is removed
  // in quadrature at the end.
  // Stronger pre-smoothing for noisier images (noise in the gradient falls
  // ~1/sigma^2); the exact amount is removed again below.
  const float kPreBlur = std::clamp(1.0f + 25.0f * noiseSigma, 1.0f, 2.5f);
  auto smoothed = Plane::allocate(luma.width(), luma.height());
  if (!smoothed) return failure(smoothed.error());
  if (Error e = gaussianBlur(luma, *smoothed, kPreBlur, token); e.status != PP_OK) return failure(e);
  auto reblurred = Plane::allocate(luma.width(), luma.height());
  if (!reblurred) return failure(reblurred.error());
  if (Error e = gaussianBlur(*smoothed, *reblurred, kReblur, token); e.status != PP_OK) return failure(e);
  const Plane& src = *smoothed;

  const int32_t w = luma.width(), h = luma.height();
  auto gradient = [](const Plane& p, int32_t x, int32_t y, float& gx, float& gy) {
    gx = 0.5f * (p.at(x + 1, y) - p.at(x - 1, y));
    gy = 0.5f * (p.at(x, y + 1) - p.at(x, y - 1));
    return std::sqrt(gx * gx + gy * gy);
  };
  // Gradient noise after pre-smoothing falls ~1/kPreBlur^2, so thresholds are
  // set relative to the smoothed noise, not the raw noise.
  const float threshold = std::max(0.008f, 0.4f * noiseSigma / (kPreBlur * kPreBlur));
  const float minHeight = std::max(0.06f, 3.0f * noiseSigma / kPreBlur);
  struct Sample {
    float magnitude;
    float ratio;
  };
  std::vector<Sample> samples;
  samples.reserve(8192);
  const int32_t step = std::max(1, std::min(w, h) / 768);
  const int32_t margin = 10;
  for (int32_t y = margin; y < h - margin; y += step) {
    for (int32_t x = margin; x < w - margin; x += step) {
      float gx, gy;
      const float g = gradient(src, x, y, gx, gy);
      if (g < threshold) continue;
      // Non-maximum suppression along the (quantised) gradient direction, so
      // only edge centres are measured, never their flanks.
      const int32_t ox = std::fabs(gx) >= 0.4142f * std::fabs(gy) ? (gx > 0 ? 1 : -1) : 0;
      const int32_t oy = std::fabs(gy) >= 0.4142f * std::fabs(gx) ? (gy > 0 ? 1 : -1) : 0;
      float tx, ty;
      if (g < gradient(src, x + ox, y + oy, tx, ty) || g < gradient(src, x - ox, y - oy, tx, ty)) continue;
      // Isolation test: the re-blur (radius ~3·kReblur px) must see a single
      // step, so the profile has to be flat on both sides out to that reach.
      // Thin bars, stripes and texture, where neighbouring edges interfere
      // with the re-blur, are rejected.
      const float ux = gx / g, uy = gy / g;
      auto at = [&](float d) {
        return src.sample(static_cast<float>(x) + ux * d, static_cast<float>(y) + uy * d);
      };
      const float height = at(5.0f) - at(-5.0f);
      if (height <= minHeight) continue;
      if (std::fabs(at(8.0f) - at(5.0f)) > 0.12f * height || std::fabs(at(-8.0f) - at(-5.0f)) > 0.12f * height) continue;
      if (at(2.0f) - at(-2.0f) <= 0.0f) continue;
      const float gr = gradient(*reblurred, x, y, tx, ty);
      if (gr <= 1e-6f) continue;
      samples.push_back({g, g / gr});
    }
  }
  BlurEstimate result;
  result.edges = static_cast<int32_t>(samples.size());
  if (samples.size() < 32) return result;  // too few edges to judge
  // Object boundaries dominate the strongest gradients; texture and shading
  // (which violate the step-edge model) fall below. Keep the top quartile.
  auto cut = samples.begin() + static_cast<std::ptrdiff_t>(samples.size() * 3 / 4);
  std::nth_element(samples.begin(), cut, samples.end(),
                   [](const Sample& a, const Sample& b) { return a.magnitude < b.magnitude; });
  std::vector<float> sigmas;
  for (auto it = cut; it != samples.end(); ++it) sigmas.push_back(sigmaForRatio(it->ratio));
  auto mid = sigmas.begin() + static_cast<std::ptrdiff_t>(sigmas.size() / 2);
  std::nth_element(sigmas.begin(), mid, sigmas.end());
  result.sigma = std::sqrt(std::max(*mid * *mid - kPreBlur * kPreBlur, 0.0f));
  return result;
}

Result<QualityReport> analyze(const Plane& luma, const CancellationToken* token) {
  QualityReport r;
  r.noiseSigma = estimateNoise(luma);
  auto blur = estimateBlurSigma(luma, r.noiseSigma, token);
  if (!blur) return failure(blur.error());
  r.blurSigma = *blur;
  {
    // Raw Laplacian variance mostly measures noise; smoothing first makes the
    // score respond to detail instead (noise falls far faster than edges).
    auto smooth = Plane::allocate(luma.width(), luma.height());
    if (!smooth) return failure(smooth.error());
    if (Error e = gaussianBlur(luma, *smooth, 1.2f, token); e.status != PP_OK) return failure(e);
    r.sharpness = laplacianSharpness(*smooth);
  }
  double sum = 0.0;
  size_t hi = 0, lo = 0;
  const size_t n = luma.size();
  const float* d = luma.data();
  for (size_t i = 0; i < n; ++i) {
    sum += d[i];
    if (d[i] >= 0.98f) ++hi;
    if (d[i] <= 0.02f) ++lo;
  }
  r.meanLuma = static_cast<float>(sum / static_cast<double>(n));
  r.highlightsClipped = static_cast<float>(static_cast<double>(hi) / static_cast<double>(n));
  r.shadowsClipped = static_cast<float>(static_cast<double>(lo) / static_cast<double>(n));
  return r;
}

}  // namespace photoplus::enhance
