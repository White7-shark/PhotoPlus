// Look transfer ("make my photo feel like this one").
//
// A look is captured as statistics that describe how a photo was graded:
//  - tone curve: luma quantiles (lifted blacks, crushed shadows, soft or
//    punchy highlights all show up here);
//  - colour by tonal zone: average chroma of shadows, midtones and
//    highlights (split-toning such as teal shadows and warm highlights);
//  - saturation (chroma spread), vignette (corner vs centre light) and grain.
// Transfer maps the photo's own statistics onto the target's with a smooth,
// monotone tone curve and bounded colour moves, so it never posterises or
// produces garish colour, whatever the reference.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "common.hpp"
#include "photoplus/enhance.hpp"
#include "photoplus/portrait.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::portrait {

using namespace detail;

namespace {

constexpr std::array<float, Style::kQuantiles> kAt = {0.01f, 0.05f, 0.15f, 0.30f, 0.50f, 0.70f, 0.85f, 0.95f, 0.99f};
constexpr int32_t kAnalysisEdge = 1024;  // statistics measured at a common size

inline void zoneWeights(float y, float& s, float& m, float& h) {
  s = smoothstep(0.45f, 0.15f, y);
  h = smoothstep(0.55f, 0.85f, y);
  m = std::max(0.0f, 1.0f - s - h);
}

}  // namespace

Result<Style> analyzeStyle(const ImageBuffer& image, const CancellationToken* token) {
  ScopedTimer timer("style.analyze");
  if (!isColour(image)) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Look needs an RGBA image"));
  const int32_t W = image.width(), H = image.height();
  if (W < 8 || H < 8) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Image too small"));
  const int32_t f = std::max<int32_t>(1, (std::max(W, H) + kAnalysisEdge - 1) / kAnalysisEdge);
  const int32_t w = W / f, h = H / f;
  auto Y = Plane::allocate(w, h);
  if (!Y) return failure(Y.error());
  const Rgb o = offsets(image);

  std::vector<uint32_t> hist(1024, 0);
  std::array<double, 3> zcb{}, zcr{}, zw{};
  double sumCb = 0, sumCr = 0, sumCb2 = 0, sumCr2 = 0, sumY = 0;
  double centre = 0, corner = 0;
  int64_t nCentre = 0, nCorner = 0;
  const float cx = static_cast<float>(w) * 0.5f, cy = static_cast<float>(h) * 0.5f;
  const float rmax = std::sqrt(cx * cx + cy * cy);
  for (int32_t y = 0; y < h; ++y) {
    if (isCancelled(token)) return failure(cancelledError());
    for (int32_t x = 0; x < w; ++x) {
      float r = 0, g = 0, b = 0;
      for (int32_t yy = y * f; yy < (y + 1) * f; ++yy) {
        const uint8_t* row = image.row(yy);
        for (int32_t xx = x * f; xx < (x + 1) * f; ++xx) {
          r += row[xx * 4 + o.r];
          g += row[xx * 4 + o.g];
          b += row[xx * 4 + o.b];
        }
      }
      const float n = 255.0f * static_cast<float>(f * f);
      float yv, cb, cr;
      rgbToYcc(r / n, g / n, b / n, yv, cb, cr);
      Y->row(y)[x] = yv;
      hist[static_cast<size_t>(std::clamp(static_cast<int>(yv * 1023.0f + 0.5f), 0, 1023))]++;
      float ws, wm, wh;
      zoneWeights(yv, ws, wm, wh);
      const float zws[3] = {ws, wm, wh};
      for (int z = 0; z < 3; ++z) {
        zcb[static_cast<size_t>(z)] += zws[z] * cb;
        zcr[static_cast<size_t>(z)] += zws[z] * cr;
        zw[static_cast<size_t>(z)] += zws[z];
      }
      sumCb += cb;
      sumCr += cr;
      sumCb2 += static_cast<double>(cb) * cb;
      sumCr2 += static_cast<double>(cr) * cr;
      sumY += yv;
      const float dx = static_cast<float>(x) + 0.5f - cx, dy = static_cast<float>(y) + 0.5f - cy;
      const float rr = std::sqrt(dx * dx + dy * dy) / rmax;
      if (rr < 0.4f) {
        centre += yv;
        ++nCentre;
      } else if (rr > 0.85f) {
        corner += yv;
        ++nCorner;
      }
    }
  }
  Style s;
  const double n = static_cast<double>(w) * h;
  uint64_t acc = 0;
  size_t q = 0;
  for (size_t i = 0; i < hist.size() && q < kAt.size(); ++i) {
    acc += hist[i];
    while (q < kAt.size() && static_cast<double>(acc) >= kAt[q] * n) {
      s.luma[q] = static_cast<float>(i) / 1023.0f;
      ++q;
    }
  }
  for (; q < kAt.size(); ++q) s.luma[q] = 1.0f;
  // Zones with too little support inherit the overall mean.
  const float meanCb = static_cast<float>(sumCb / n), meanCr = static_cast<float>(sumCr / n);
  for (size_t z = 0; z < 3; ++z) {
    const bool enough = zw[z] > 0.02 * n;
    s.zoneCb[z] = enough ? static_cast<float>(zcb[z] / zw[z]) : meanCb;
    s.zoneCr[z] = enough ? static_cast<float>(zcr[z] / zw[z]) : meanCr;
  }
  s.chromaStd[0] = static_cast<float>(std::sqrt(std::max(0.0, sumCb2 / n - meanCb * meanCb)));
  s.chromaStd[1] = static_cast<float>(std::sqrt(std::max(0.0, sumCr2 / n - meanCr * meanCr)));
  s.meanLuma = static_cast<float>(sumY / n);
  s.vignette = nCentre && nCorner && centre > 0.0
                   ? static_cast<float>((corner / static_cast<double>(nCorner)) / (centre / static_cast<double>(nCentre)))
                   : 1.0f;
  s.grain = enhance::estimateNoise(*Y);
  return s;
}

namespace {

// Monotone cubic (Fritsch–Carlson) through the knots, sampled into a LUT,
// with the slope bounded so shadows or highlights never get torn apart.
std::vector<float> toneCurve(const Style& src, const Style& tgt, float strength) {
  std::vector<float> xs{0.0f}, ys{std::clamp(tgt.luma[0] - src.luma[0], 0.0f, 0.25f)};
  for (int i = 0; i < Style::kQuantiles; ++i) {
    const float x = src.luma[static_cast<size_t>(i)], y = tgt.luma[static_cast<size_t>(i)];
    if (x <= xs.back() + 0.01f || x >= 0.995f) continue;
    xs.push_back(x);
    ys.push_back(std::max(y, ys.back()));
  }
  xs.push_back(1.0f);
  ys.push_back(std::max(ys.back(), std::clamp(1.0f - (src.luma[8] - tgt.luma[8]), 0.75f, 1.0f)));
  const size_t n = xs.size();
  std::vector<float> d(n - 1), m(n);
  for (size_t i = 0; i + 1 < n; ++i) d[i] = (ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]);
  m[0] = d[0];
  m[n - 1] = d[n - 2];
  for (size_t i = 1; i + 1 < n; ++i) m[i] = d[i - 1] * d[i] <= 0.0f ? 0.0f : 0.5f * (d[i - 1] + d[i]);
  for (size_t i = 0; i + 1 < n; ++i) {
    if (d[i] == 0.0f) {
      m[i] = m[i + 1] = 0.0f;
      continue;
    }
    const float a = m[i] / d[i], b = m[i + 1] / d[i];
    const float t = a * a + b * b;
    if (t > 9.0f) {
      const float k = 3.0f / std::sqrt(t);
      m[i] = k * a * d[i];
      m[i + 1] = k * b * d[i];
    }
  }
  std::vector<float> lut(1024);
  size_t seg = 0;
  for (size_t i = 0; i < lut.size(); ++i) {
    const float x = static_cast<float>(i) / 1023.0f;
    while (seg + 2 < n && x > xs[seg + 1]) ++seg;
    const float hseg = xs[seg + 1] - xs[seg];
    const float t = std::clamp((x - xs[seg]) / hseg, 0.0f, 1.0f);
    const float t2 = t * t, t3 = t2 * t;
    const float v = (2 * t3 - 3 * t2 + 1) * ys[seg] + (t3 - 2 * t2 + t) * hseg * m[seg] + (-2 * t3 + 3 * t2) * ys[seg + 1] +
                    (t3 - t2) * hseg * m[seg + 1];
    lut[i] = v;
  }
  // Bound the slope (max 3x, min 0.3x) so nothing posterises or flattens.
  constexpr float kStep = 1.0f / 1023.0f;
  for (size_t i = 1; i < lut.size(); ++i) lut[i] = std::clamp(lut[i], lut[i - 1] + 0.3f * kStep, lut[i - 1] + 3.0f * kStep);
  for (size_t i = 0; i < lut.size(); ++i) {
    const float x = static_cast<float>(i) / 1023.0f;
    lut[i] = std::clamp(x + strength * (lut[i] - x), 0.0f, 1.0f);
  }
  return lut;
}

}  // namespace

Error applyStyle(ImageBuffer& image, const Style& target, float strength, const CancellationToken* token) {
  ScopedTimer timer("style.apply");
  strength = std::clamp(strength, 0.0f, 1.0f);
  if (strength <= 0.0f) return ok();
  auto srcR = analyzeStyle(image, token);
  if (!srcR) return srcR.error();
  const Style& src = *srcR;
  const auto lut = toneCurve(src, target, strength);

  float scaleCb = std::clamp(target.chromaStd[0] / std::max(src.chromaStd[0], 1e-3f), 0.4f, 1.8f);
  float scaleCr = std::clamp(target.chromaStd[1] / std::max(src.chromaStd[1], 1e-3f), 0.4f, 1.8f);
  scaleCb = 1.0f + strength * (scaleCb - 1.0f);
  scaleCr = 1.0f + strength * (scaleCr - 1.0f);
  std::array<float, 3> shiftCb{}, shiftCr{};
  for (size_t z = 0; z < 3; ++z) {
    shiftCb[z] = strength * std::clamp(target.zoneCb[z] - src.zoneCb[z], -0.12f, 0.12f);
    shiftCr[z] = strength * std::clamp(target.zoneCr[z] - src.zoneCr[z], -0.12f, 0.12f);
  }
  const float vigRatio = 1.0f + strength * (std::clamp(target.vignette / std::max(src.vignette, 0.05f), 0.5f, 1.5f) - 1.0f);

  const int32_t W = image.width(), H = image.height();
  const int32_t f = std::max<int32_t>(1, (std::max(W, H) + kAnalysisEdge - 1) / kAnalysisEdge);
  // Grain is measured at the analysis size; downsampling by f divides it by f.
  const float addGrain =
      target.grain > src.grain + 0.002f
          ? std::min(0.04f, strength * std::sqrt(target.grain * target.grain - src.grain * src.grain) * static_cast<float>(f))
          : 0.0f;
  const float cx = static_cast<float>(W) * 0.5f, cy = static_cast<float>(H) * 0.5f;
  const float rmax = std::sqrt(cx * cx + cy * cy);
  const Rgb o = offsets(image);

  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* row = image.row(y);
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* px = row + x * 4;
        float yv, cb, cr;
        rgbToYcc(px[o.r] / 255.0f, px[o.g] / 255.0f, px[o.b] / 255.0f, yv, cb, cr);
        float ws, wm, wh;
        zoneWeights(yv, ws, wm, wh);
        const float centreCb = ws * src.zoneCb[0] + wm * src.zoneCb[1] + wh * src.zoneCb[2];
        const float centreCr = ws * src.zoneCr[0] + wm * src.zoneCr[1] + wh * src.zoneCr[2];
        cb = (cb - centreCb) * scaleCb + centreCb + ws * shiftCb[0] + wm * shiftCb[1] + wh * shiftCb[2];
        cr = (cr - centreCr) * scaleCr + centreCr + ws * shiftCr[0] + wm * shiftCr[1] + wh * shiftCr[2];
        const float li = std::clamp(yv, 0.0f, 1.0f) * 1023.0f;
        const auto i0 = static_cast<size_t>(li);
        const size_t i1 = std::min<size_t>(i0 + 1, 1023);
        const float t = li - static_cast<float>(i0);
        float ny = lut[i0] + t * (lut[i1] - lut[i0]);
        const float dx = static_cast<float>(x) + 0.5f - cx, dy = static_cast<float>(y) + 0.5f - cy;
        const float rr = std::sqrt(dx * dx + dy * dy) / rmax;
        ny *= 1.0f + (vigRatio - 1.0f) * smoothstep(0.35f, 1.0f, rr);
        if (addGrain > 0.0f) ny += addGrain * hashNoise(x, y, 0x6A41u);
        // Keep colourfulness proportional when the curve changes brightness.
        const float k = yv > 0.02f ? std::clamp(std::sqrt(std::max(ny, 0.0f) / yv), 0.6f, 1.5f) : 1.0f;
        float r, g, b;
        yccToRgb(ny, cb * k, cr * k, r, g, b);
        px[o.r] = toByte(r);
        px[o.g] = toByte(g);
        px[o.b] = toByte(b);
      }
    }
  });
  return done ? ok() : cancelledError();
}

}  // namespace photoplus::portrait
