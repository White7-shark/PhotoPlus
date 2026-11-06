// Subject mattes: edge-aware refinement of coarse masks (from an on-device
// segmentation model or from tap points) and a tap-seeded cut-out.

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "common.hpp"
#include "photoplus/portrait.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::portrait {

using namespace detail;

namespace detail {

Error guidedCoefficients(const Plane& I, const Plane& p, int32_t r, float eps, Plane& meanA, Plane& meanB,
                         const CancellationToken* token) {
  const int32_t w = I.width(), h = I.height();
  auto mI = Plane::allocate(w, h), mP = Plane::allocate(w, h), mIP = Plane::allocate(w, h), mII = Plane::allocate(w, h);
  auto tmp = Plane::allocate(w, h);
  if (!mI || !mP || !mIP || !mII || !tmp) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
  if (Error e = boxBlur(I, *mI, r, token); e.status != PP_OK) return e;
  if (Error e = boxBlur(p, *mP, r, token); e.status != PP_OK) return e;
  for (size_t i = 0; i < I.size(); ++i) tmp->data()[i] = I.data()[i] * p.data()[i];
  if (Error e = boxBlur(*tmp, *mIP, r, token); e.status != PP_OK) return e;
  for (size_t i = 0; i < I.size(); ++i) tmp->data()[i] = I.data()[i] * I.data()[i];
  if (Error e = boxBlur(*tmp, *mII, r, token); e.status != PP_OK) return e;
  // a -> tmp, b -> mIP (buffers reused)
  for (size_t i = 0; i < I.size(); ++i) {
    const float var = std::max(0.0f, mII->data()[i] - mI->data()[i] * mI->data()[i]);
    const float cov = mIP->data()[i] - mI->data()[i] * mP->data()[i];
    const float a = cov / (var + eps);
    tmp->data()[i] = a;
    mIP->data()[i] = mP->data()[i] - a * mI->data()[i];
  }
  if (Error e = boxBlur(*tmp, meanA, r, token); e.status != PP_OK) return e;
  return boxBlur(*mIP, meanB, r, token);
}

// Colour guided filter (He et al., section 3.5): with three guide channels a
// boundary is found wherever colour changes, even where brightness does not
// (dark hair against a dark background of another hue). Per pixel the 3x3
// guide covariance (+ eps) is inverted; out = a·I + b.
Error guidedCoefficientsColor(const std::array<const Plane*, 3>& I, const Plane& p, int32_t r, float eps,
                              std::array<Plane, 3>& meanA, Plane& meanB, const CancellationToken* token) {
  const int32_t w = p.width(), h = p.height();
  const size_t n = p.size();
  auto alloc = [&]() { return Plane::allocate(w, h); };
  std::array<Result<Plane>, 3> mI{alloc(), alloc(), alloc()}, mIp{alloc(), alloc(), alloc()};
  std::array<Result<Plane>, 6> cov{alloc(), alloc(), alloc(), alloc(), alloc(), alloc()};  // rr rg rb gg gb bb
  auto mP = alloc(), tmp = alloc();
  for (auto* q : {&mI[0], &mI[1], &mI[2], &mIp[0], &mIp[1], &mIp[2], &mP, &tmp}) {
    if (!*q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
  }
  for (auto& q : cov)
    if (!q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
  if (Error e = boxBlur(p, *mP, r, token); e.status != PP_OK) return e;
  for (int c = 0; c < 3; ++c) {
    if (Error e = boxBlur(*I[c], *mI[c], r, token); e.status != PP_OK) return e;
    for (size_t i = 0; i < n; ++i) tmp->data()[i] = I[c]->data()[i] * p.data()[i];
    if (Error e = boxBlur(*tmp, *mIp[c], r, token); e.status != PP_OK) return e;
  }
  const int pairs[6][2] = {{0, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 2}, {2, 2}};
  for (int k = 0; k < 6; ++k) {
    const Plane& A = *I[pairs[k][0]];
    const Plane& B = *I[pairs[k][1]];
    for (size_t i = 0; i < n; ++i) tmp->data()[i] = A.data()[i] * B.data()[i];
    if (Error e = boxBlur(*tmp, *cov[k], r, token); e.status != PP_OK) return e;
  }
  if (isCancelled(token)) return cancelledError();
  // a (3 channels) overwrites mIp; b overwrites mP.
  for (size_t i = 0; i < n; ++i) {
    const float m0 = mI[0]->data()[i], m1 = mI[1]->data()[i], m2 = mI[2]->data()[i], mp = mP->data()[i];
    const float s00 = cov[0]->data()[i] - m0 * m0 + eps, s01 = cov[1]->data()[i] - m0 * m1,
                s02 = cov[2]->data()[i] - m0 * m2, s11 = cov[3]->data()[i] - m1 * m1 + eps,
                s12 = cov[4]->data()[i] - m1 * m2, s22 = cov[5]->data()[i] - m2 * m2 + eps;
    const float c0 = mIp[0]->data()[i] - m0 * mp, c1 = mIp[1]->data()[i] - m1 * mp, c2 = mIp[2]->data()[i] - m2 * mp;
    // Inverse of the symmetric 3x3 matrix via cofactors.
    const float i00 = s11 * s22 - s12 * s12, i01 = s02 * s12 - s01 * s22, i02 = s01 * s12 - s02 * s11;
    const float i11 = s00 * s22 - s02 * s02, i12 = s01 * s02 - s00 * s12, i22 = s00 * s11 - s01 * s01;
    const float det = s00 * i00 + s01 * i01 + s02 * i02;
    float a0 = 0, a1 = 0, a2 = 0;
    if (std::fabs(det) > 1e-12f) {
      const float inv = 1.0f / det;
      a0 = (i00 * c0 + i01 * c1 + i02 * c2) * inv;
      a1 = (i01 * c0 + i11 * c1 + i12 * c2) * inv;
      a2 = (i02 * c0 + i12 * c1 + i22 * c2) * inv;
    }
    mIp[0]->data()[i] = a0;
    mIp[1]->data()[i] = a1;
    mIp[2]->data()[i] = a2;
    mP->data()[i] = mp - a0 * m0 - a1 * m1 - a2 * m2;
  }
  for (int c = 0; c < 3; ++c) {
    if (Error e = boxBlur(*mIp[c], meanA[c], r, token); e.status != PP_OK) return e;
  }
  return boxBlur(*mP, meanB, r, token);
}

}  // namespace detail

Result<Plane> refineMask(const Plane& coarse, const Plane& guide, int32_t radius, float eps,
                         const CancellationToken* token) {
  ScopedTimer timer("portrait.matte");
  if (coarse.empty() || guide.empty()) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Empty mask or guide"));
  // Fast guided filter: solve on a subsampled grid, apply at full resolution.
  const int32_t s = std::max<int32_t>(1, radius / 4);
  auto Il = boxDown(guide, s, token);
  if (!Il) return failure(Il.error());
  auto pl = resizeBilinear(coarse, Il->width(), Il->height(), token);
  if (!pl) return failure(pl.error());
  auto a = Plane::allocate(Il->width(), Il->height());
  auto b = Plane::allocate(Il->width(), Il->height());
  if (!a || !b) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out"));
  const int32_t rl = std::max<int32_t>(1, static_cast<int32_t>(std::lround(static_cast<float>(radius) / static_cast<float>(s))));
  if (Error e = guidedCoefficients(*Il, *pl, rl, eps, *a, *b, token); e.status != PP_OK) return failure(e);

  auto out = Plane::allocate(guide.width(), guide.height());
  if (!out) return failure(out.error());
  const float inv = 1.0f / static_cast<float>(s);
  const bool done = parallelFor(guide.height(), token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      const float fy = (static_cast<float>(y) + 0.5f) * inv - 0.5f;
      const float* g = guide.row(y);
      float* o = out->row(y);
      for (int32_t x = 0; x < guide.width(); ++x) {
        const float fx = (static_cast<float>(x) + 0.5f) * inv - 0.5f;
        o[x] = std::clamp(a->sample(fx, fy) * g[x] + b->sample(fx, fy), 0.0f, 1.0f);
      }
    }
  });
  if (!done) return failure(cancelledError());
  return out;
}

// ------------------------------------------------------------------------
// Tap-seeded cut-out. Colour models of "subject" (around the taps) and
// "background" (the frame border) are learnt and iteratively re-estimated,
// combined with a spatial prior around the taps, smoothed along image edges,
// and restricted to regions connected to a tap. A lightweight cousin of
// GrabCut that runs in milliseconds at ~320 px.

namespace {

constexpr int kBins = 8;
constexpr int kHist = kBins * kBins * kBins;

inline int binOf(float y, float cb, float cr) {
  auto q = [](float v, float lo, float hi) {
    return std::clamp(static_cast<int>((v - lo) / (hi - lo) * kBins), 0, kBins - 1);
  };
  return (q(y, 0.0f, 1.0f) * kBins + q(cb, -0.3f, 0.3f)) * kBins + q(cr, -0.3f, 0.3f);
}

void smoothHistogram(std::array<float, kHist>& h) {
  std::array<float, kHist> out{};
  for (int a = 0; a < kBins; ++a)
    for (int b = 0; b < kBins; ++b)
      for (int c = 0; c < kBins; ++c) {
        float s = 0.0f, n = 0.0f;
        for (int da = -1; da <= 1; ++da)
          for (int db = -1; db <= 1; ++db)
            for (int dc = -1; dc <= 1; ++dc) {
              const int A = a + da, B = b + db, C = c + dc;
              if (A < 0 || B < 0 || C < 0 || A >= kBins || B >= kBins || C >= kBins) continue;
              const float wgt = (da == 0 && db == 0 && dc == 0) ? 4.0f : 1.0f;
              s += wgt * h[static_cast<size_t>((A * kBins + B) * kBins + C)];
              n += wgt;
            }
        out[static_cast<size_t>((a * kBins + b) * kBins + c)] = s / n;
      }
  float total = 0.0f;
  for (float v : out) total += v;
  if (total > 0.0f)
    for (auto& v : out) v /= total;
  h = out;
}

}  // namespace

Result<ImageBuffer> subjectFromPoints(const ImageBuffer& image, const std::vector<std::array<float, 2>>& points,
                                      const CancellationToken* token) {
  ScopedTimer timer("portrait.subjectFromPoints");
  if (!isColour(image)) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Cut-out needs an RGBA image"));
  const int32_t f = std::max<int32_t>(1, (std::max(image.width(), image.height()) + 319) / 320);
  const int32_t w = std::max(1, image.width() / f), h = std::max(1, image.height() / f);
  if (w < 8 || h < 8) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Image too small"));

  auto Y = Plane::allocate(w, h), Cb = Plane::allocate(w, h), Cr = Plane::allocate(w, h);
  auto P = Plane::allocate(w, h), prior = Plane::allocate(w, h);
  if (!Y || !Cb || !Cr || !P || !prior) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out"));
  const Rgb o = offsets(image);
  for (int32_t y = 0; y < h; ++y)
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
      rgbToYcc(r / n, g / n, b / n, Y->row(y)[x], Cb->row(y)[x], Cr->row(y)[x]);
    }
  if (isCancelled(token)) return failure(cancelledError());

  std::vector<std::array<float, 2>> seeds = points;
  if (seeds.empty()) seeds.push_back({0.5f, 0.5f});
  const float shortSide = static_cast<float>(std::min(w, h));
  const float seedR = std::max(3.0f, 0.06f * shortSide);
  const float sigma = 0.35f * shortSide;
  const int32_t band = std::max<int32_t>(2, static_cast<int32_t>(0.04f * shortSide));
  std::vector<uint8_t> isSeed(static_cast<size_t>(w) * h, 0), isBorder(static_cast<size_t>(w) * h, 0);
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      float best = 0.0f;
      bool seed = false;
      for (const auto& s : seeds) {
        const float dx = static_cast<float>(x) - s[0] * static_cast<float>(w);
        const float dy = static_cast<float>(y) - s[1] * static_cast<float>(h);
        const float d2 = dx * dx + dy * dy;
        best = std::max(best, std::exp(-d2 / (2.0f * sigma * sigma)));
        seed = seed || d2 <= seedR * seedR;
      }
      prior->row(y)[x] = best;
      const size_t i = static_cast<size_t>(y) * w + x;
      isSeed[i] = seed ? 1 : 0;
      isBorder[i] = !seed && (x < band || y < band || x >= w - band || y >= h - band) ? 1 : 0;
    }

  std::array<float, kHist> fg{}, bg{};
  for (int iter = 0; iter < 4; ++iter) {
    fg.fill(0.0f);
    bg.fill(0.0f);
    for (int32_t y = 0; y < h; ++y)
      for (int32_t x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * w + x;
        const int bin = binOf(Y->row(y)[x], Cb->row(y)[x], Cr->row(y)[x]);
        if (isSeed[i]) fg[static_cast<size_t>(bin)] += 3.0f;
        if (isBorder[i]) bg[static_cast<size_t>(bin)] += 1.0f;
        if (iter > 0) {
          const float p = P->row(y)[x];
          if (p > 0.65f && !isBorder[i]) fg[static_cast<size_t>(bin)] += 1.0f;
          if (p < 0.25f && !isSeed[i]) bg[static_cast<size_t>(bin)] += 1.0f;
        }
      }
    smoothHistogram(fg);
    smoothHistogram(bg);
    for (int32_t y = 0; y < h; ++y)
      for (int32_t x = 0; x < w; ++x) {
        const size_t bin = static_cast<size_t>(binOf(Y->row(y)[x], Cb->row(y)[x], Cr->row(y)[x]));
        const float like = (fg[bin] + 1e-5f) / (fg[bin] + bg[bin] + 2e-5f);
        const float pr = 0.3f + 0.7f * prior->row(y)[x];
        const float a = like * pr, b = (1.0f - like) * (1.0f - pr);
        P->row(y)[x] = a / (a + b + 1e-6f);
      }
  }
  if (isCancelled(token)) return failure(cancelledError());

  // Edge-aware smoothing, then a soft decision.
  auto refined = refineMask(*P, *Y, 4, 2e-3f, token);
  if (!refined) return failure(refined.error());
  Plane& m = *refined;
  for (size_t i = 0; i < m.size(); ++i) m.data()[i] = smoothstep(0.4f, 0.62f, m.data()[i]);

  // Keep only regions connected to a tap (a distant look-alike stays out).
  std::vector<uint8_t> reached(static_cast<size_t>(w) * h, 0);
  std::vector<int32_t> stack;
  for (const auto& s : seeds) {
    const int32_t sx = std::clamp(static_cast<int32_t>(s[0] * static_cast<float>(w)), 0, w - 1);
    const int32_t sy = std::clamp(static_cast<int32_t>(s[1] * static_cast<float>(h)), 0, h - 1);
    stack.push_back(sy * w + sx);
  }
  while (!stack.empty()) {
    const int32_t i = stack.back();
    stack.pop_back();
    if (reached[static_cast<size_t>(i)]) continue;
    const int32_t x = i % w, y = i / w;
    // Seed pixels always count; elsewhere follow the subject.
    if (m.row(y)[x] < 0.5f && !isSeed[static_cast<size_t>(i)]) continue;
    reached[static_cast<size_t>(i)] = 1;
    if (x > 0) stack.push_back(i - 1);
    if (x < w - 1) stack.push_back(i + 1);
    if (y > 0) stack.push_back(i - w);
    if (y < h - 1) stack.push_back(i + w);
  }
  auto keep = Plane::allocate(w, h), keepBlur = Plane::allocate(w, h);
  if (!keep || !keepBlur) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out"));
  for (size_t i = 0; i < keep->size(); ++i) keep->data()[i] = reached[i] ? 1.0f : 0.0f;
  if (Error e = boxBlur(*keep, *keepBlur, 2, token); e.status != PP_OK) return failure(e);

  // Reliability: a colour-only cut-out that swallows most of the frame or
  // runs along its edges has confused subject and background. A patchy blur
  // would look worse than none, so fall back to a soft, deliberate focus
  // fall-off around the taps (it always looks intentional).
  double coverage = 0.0, borderFg = 0.0, borderN = 0.0;
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      const float v = m.row(y)[x] * std::min(1.0f, keepBlur->row(y)[x] * 3.0f);
      m.row(y)[x] = v;
      coverage += v;
      const bool edge = x < band || y < band || x >= w - band;  // the bottom edge may legitimately hold the subject
      if (edge) {
        borderFg += v;
        borderN += 1.0;
      }
    }
  coverage /= static_cast<double>(w) * h;
  const bool reliable = coverage < 0.6 && (borderN == 0.0 || borderFg / borderN < 0.3);

  auto out = ImageBuffer::allocate(w, h, PixelFormat::Gray8);
  if (!out) return failure(out.error());
  if (reliable) {
    for (int32_t y = 0; y < h; ++y)
      for (int32_t x = 0; x < w; ++x) out->row(y)[x] = toByte(m.row(y)[x]);
    return out;
  }
  float minX = 1, maxX = 0, minY = 1, maxY = 0;
  for (const auto& s : seeds) {
    minX = std::min(minX, s[0]);
    maxX = std::max(maxX, s[0]);
    minY = std::min(minY, s[1]);
    maxY = std::max(maxY, s[1]);
  }
  const float cx = 0.5f * (minX + maxX) * static_cast<float>(w), cy = 0.5f * (minY + maxY) * static_cast<float>(h);
  const float rx = 0.5f * (maxX - minX) * static_cast<float>(w) + 0.22f * shortSide;
  const float ry = 0.5f * (maxY - minY) * static_cast<float>(h) + 0.28f * shortSide;
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      const float dx = (static_cast<float>(x) - cx) / rx, dy = (static_cast<float>(y) - cy) / ry;
      out->row(y)[x] = toByte(1.0f - smoothstep(0.75f, 1.6f, std::sqrt(dx * dx + dy * dy)));
    }
  return out;
}

}  // namespace photoplus::portrait
