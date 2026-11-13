// Smart lighting: see light.hpp.
#include "photoplus/light.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "../portrait/common.hpp"
#include "photoplus/profiler.hpp"
#include "photoplus/video.hpp"

namespace photoplus::light {

using namespace portrait::detail;

namespace {

inline float lum(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

// Linear RGB averaged over f×f cells (a working grid).
struct Grid {
  int32_t w = 0, h = 0, f = 1;
  std::array<Plane, 3> c;  // linear R, G, B
};

Result<Grid> gridOf(const ImageBuffer& img, int32_t maxSide, const CancellationToken* token) {
  Grid g;
  const int32_t W = img.width(), H = img.height();
  g.f = std::max<int32_t>(1, (std::max(W, H) + maxSide - 1) / maxSide);
  g.w = (W + g.f - 1) / g.f, g.h = (H + g.f - 1) / g.f;
  for (auto& p : g.c) {
    auto q = Plane::allocate(g.w, g.h);
    if (!q) return failure(q.error());
    p = std::move(q).value();
  }
  std::array<float, 256> lut{};
  for (int i = 0; i < 256; ++i) lut[static_cast<size_t>(i)] = toLinear(static_cast<float>(i) / 255.0f);
  const Rgb o = offsets(img);
  const bool done = parallelFor(g.h, token, [&](int32_t begin, int32_t end) {
    for (int32_t ly = begin; ly < end; ++ly)
      for (int32_t lx = 0; lx < g.w; ++lx) {
        float s[3] = {0, 0, 0};
        int32_t n = 0;
        for (int32_t y = ly * g.f; y < std::min(H, (ly + 1) * g.f); ++y) {
          const uint8_t* row = img.row(y);
          for (int32_t x = lx * g.f; x < std::min(W, (lx + 1) * g.f); ++x, ++n) {
            const uint8_t* q = row + x * 4;
            s[0] += lut[q[o.r]], s[1] += lut[q[o.g]], s[2] += lut[q[o.b]];
          }
        }
        const float inv = n ? 1.0f / static_cast<float>(n) : 0.0f;
        for (int c = 0; c < 3; ++c) g.c[static_cast<size_t>(c)].row(ly)[lx] = s[c] * inv;
      }
  });
  if (!done) return failure(cancelledError());
  return g;
}

float percentile(std::vector<float> v, float q) {
  if (v.empty()) return 0.0f;
  const auto k = static_cast<size_t>(std::clamp(q, 0.0f, 1.0f) * static_cast<float>(v.size() - 1));
  std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
  return v[k];
}

constexpr float kLogFloor = 1e-4f;  // linear luminance floor for log2 (~-13.3 stops)

}  // namespace

// ------------------------------------------------------------------ meter

Meter meter(const ImageBuffer& preview, const MeterInput& in) {
  Meter m;
  if (!isColour(preview) || preview.width() < 16 || preview.height() < 16) return m;
  auto g = gridOf(preview, 160, nullptr);
  if (!g) return m;
  std::vector<float> logs;
  logs.reserve(static_cast<size_t>(g->w) * static_cast<size_t>(g->h));
  double hi = 0, lo = 0, n = 0, warm[3] = {0, 0, 0}, wn = 0;
  for (int32_t y = 0; y < g->h; ++y)
    for (int32_t x = 0; x < g->w; ++x) {
      const float r = g->c[0].row(y)[x], gg = g->c[1].row(y)[x], b = g->c[2].row(y)[x];
      const float yl = lum(r, gg, b), yg = toGamma(yl);
      logs.push_back(std::log2(yl + kLogFloor));
      hi += yg >= 0.96f ? 1.0 : 0.0;
      lo += yg <= 0.05f ? 1.0 : 0.0;
      n += 1.0;
      if (yg > 0.12f && yg < 0.9f) warm[0] += r, warm[1] += gg, warm[2] += b, wn += 1.0;
    }
  double sum = 0;
  for (float v : logs) sum += v;
  m.key = std::exp2(static_cast<float>(sum / std::max<double>(1.0, static_cast<double>(logs.size()))));
  m.highlights = static_cast<float>(hi / n);
  m.shadows = static_cast<float>(lo / n);
  m.rangeStops = percentile(logs, 0.98f) - percentile(logs, 0.02f);
  // Scene brightness from the camera's own exposure, corrected by how bright
  // the preview actually came out (in the dark the exposure runs out of room
  // and the picture is darker than the meter's target).
  const Exposure& e = in.exposure;
  if (e.iso > 0 && e.exposureS > 0) {
    const float fn = e.fNumber > 0 ? e.fNumber : 1.8f;
    const float set = std::log2(fn * fn / e.exposureS) - std::log2(e.iso / 100.0f);
    m.ev100 = set + std::log2(std::max(m.key, 1e-4f) / 0.18f);
    m.lux = 2.5f * std::exp2(m.ev100);
    m.exposureKnown = true;
  }
  m.backlit = in.faceLuma >= 0 && m.highlights > 0.02f && in.faceLuma < 0.55f * toGamma(m.key) + 0.02f;
  const bool dim = m.exposureKnown ? m.lux < 40.0f : m.key < 0.04f;
  const bool veryDark = m.exposureKnown ? m.lux < 1.0f : m.key < 0.008f;
  // Harsh: a lot already burnt to white (a sunset, a window), or both ends
  // of the range in use at once, or a face darker than its background.
  const bool harsh = m.highlights > 0.06f || (m.highlights > 0.02f && m.shadows > 0.10f) || m.rangeStops > 7.0f || m.backlit;
  // Flash only helps what is near: a face, a selfie, or something the lens
  // is focused within ~2.5 m. A night street or skyline gets a long burst.
  const bool near = in.front || in.faceLuma >= 0 || in.focusDiopters >= 0.4f;
  if (veryDark && near && (in.hasFlash || in.front)) {
    m.plan = Plan::Flash;
    m.frames = 6;  // ambient frames merged before the flash frame is fused in
    m.night = 1.0f;
  } else if (dim) {
    m.plan = Plan::Moonlight;
    const float t = m.exposureKnown ? std::clamp(std::log2(40.0f / std::max(m.lux, 0.05f)) / std::log2(40.0f), 0.0f, 1.0f)
                                    : std::clamp((0.04f - m.key) / 0.035f, 0.0f, 1.0f);
    m.frames = static_cast<int32_t>(std::lround(4.0f + 8.0f * t));
    // Night rendering grows as the light fades (full by ~3 lux).
    m.night = m.exposureKnown ? std::clamp(std::log2(40.0f / std::max(m.lux, 0.05f)) / std::log2(40.0f / 3.0f), 0.0f, 1.0f) : t;
    // A portrait in a dim room should be clear and bright, not a night scene.
    if (in.faceLuma >= 0) m.night *= 0.4f;
    // Bright lamps in a dark scene: keep them from clipping, a little.
    m.exposureBias = m.highlights > 0.01f ? -0.7f : 0.0f;
  } else if (harsh) {
    m.plan = Plan::BrightBalance;
    m.frames = 5;
    m.exposureBias = -std::clamp(0.8f + 12.0f * m.highlights, 1.0f, 2.0f);
  }
  // The screen "flash": white tinted half way toward the room's own light,
  // never strongly coloured (the face must still read as itself).
  if (wn > 4) {
    float c[3] = {static_cast<float>(warm[0] / wn), static_cast<float>(warm[1] / wn), static_cast<float>(warm[2] / wn)};
    const float mx = std::max({c[0], c[1], c[2], 1e-6f});
    for (int k = 0; k < 3; ++k) {
      const float t = std::max(0.78f, 0.5f + 0.5f * c[k] / mx);
      m.screenFlash[k] = static_cast<uint8_t>(std::lround(255.0f * t));
    }
    const uint8_t top = std::max({m.screenFlash[0], m.screenFlash[1], m.screenFlash[2]});
    for (auto& v : m.screenFlash) v = static_cast<uint8_t>(std::lround(255.0f * v / std::max<uint8_t>(top, 1)));
  }
  return m;
}

// ------------------------------------------------------------------- tone

Error tone(ImageBuffer& image, const ToneParams& params, const CancellationToken* token, ToneReport* report) {
  ScopedTimer timer("light.tone");
  if (!isColour(image)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Tone needs an RGBA image");
  if (image.width() < 16 || image.height() < 16) return makeError(PP_ERROR_INVALID_ARGUMENT, "Image too small");
  for (float v : {params.exposure, params.shadows, params.highlights, params.night, params.detail, params.strength}) {
    if (!std::isfinite(v)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Non-finite tone parameter");
  }
  if (!std::isfinite(params.whiteBalance) || !std::isfinite(params.captureBias)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Non-finite tone parameter");
  if (report != nullptr) *report = ToneReport{};
  const float strength = std::clamp(params.strength, 0.0f, 1.0f);
  const float night = std::clamp(params.night, 0.0f, 1.0f);
  const float wbAmount = std::clamp(params.whiteBalance, 0.0f, 1.0f);
  const float detail = std::clamp(params.detail, 0.0f, 1.0f);
  const float exposure = std::clamp(params.exposure, -3.0f, 3.0f);
  const float captureBias = std::clamp(-params.captureBias, 0.0f, 3.0f);
  if (isCancelled(token)) return cancelledError();

  // 1. The scene on a working grid, as log2 luminance.
  auto g = gridOf(image, 640, token);
  if (!g) return g.error();
  const int32_t lw = g->w, lh = g->h;
  // Low-light colour: the light's colour from the scene's edges (grey-edge:
  // edges average to grey under a neutral light), ignoring lamps and the
  // darkest noise. Corrected part way, with a slight warm bias, so a
  // candle-lit room stays warm but a sodium-lit street isn't green.
  float wb[3] = {1, 1, 1};
  if (wbAmount > 0.0f) {
    double e[3] = {0, 0, 0};
    for (int32_t y = 1; y < lh - 1; ++y)
      for (int32_t x = 1; x < lw - 1; ++x) {
        const float yl = lum(g->c[0].row(y)[x], g->c[1].row(y)[x], g->c[2].row(y)[x]);
        if (yl < 0.004f || toGamma(yl) > 0.92f) continue;
        for (int c = 0; c < 3; ++c) {
          const Plane& p = g->c[static_cast<size_t>(c)];
          e[c] += std::fabs(p.row(y)[x + 1] - p.row(y)[x - 1]) + std::fabs(p.row(y + 1)[x] - p.row(y - 1)[x]);
        }
      }
    if (e[0] > 0 && e[1] > 0 && e[2] > 0) {
      const float warm[3] = {1.04f, 1.0f, 0.93f};
      float gsum = 0;
      for (int c = 0; c < 3; ++c) {
        wb[c] = std::pow(static_cast<float>(e[1] / e[c]) * warm[c], wbAmount);
        wb[c] = std::clamp(wb[c], 0.6f, 1.7f);
      }
      gsum = lum(wb[0], wb[1], wb[2]);
      for (float& v : wb) v /= gsum;  // brightness unchanged
      for (int c = 0; c < 3; ++c) {
        Plane& p = g->c[static_cast<size_t>(c)];
        for (size_t i = 0; i < p.size(); ++i) p.data()[i] *= wb[c];
      }
    }
  }
  auto L = Plane::allocate(lw, lh);
  if (!L) return L.error();
  std::vector<float> logs;
  logs.reserve(L->size());
  for (size_t i = 0; i < L->size(); ++i) {
    const float v = std::log2(lum(g->c[0].data()[i], g->c[1].data()[i], g->c[2].data()[i]) + kLogFloor);
    L->data()[i] = v;
    logs.push_back(v);
  }
  const float p2 = percentile(logs, 0.02f), p5 = percentile(logs, 0.05f), p95 = percentile(logs, 0.95f), p98 = percentile(logs, 0.98f);
  double ks = 0;
  int64_t kn = 0;
  for (float v : logs)
    if (v >= p5 && v <= p95) ks += v, ++kn;
  const float key = kn ? static_cast<float>(ks / static_cast<double>(kn)) : percentile(logs, 0.5f);
  // The lit part of the scene (its brightest 40%): at night, what the
  // picture is about — the buildings, the faces, the lamps — not the sky.
  const float p60 = percentile(logs, 0.6f);
  double ls = 0;
  int64_t ln = 0;
  for (float v : logs)
    if (v >= p60 && v <= p98) ls += v, ++ln;
  const float keyLit = ln ? static_cast<float>(ls / static_cast<double>(ln)) : key;
  const float range = p98 - p2;
  // 2. How much to compress: from the scene's own range (a flat scene is
  //    left alone; a sunset or a window behind a face is balanced).
  //    Highlights are compressed only when the brightest 1% sits well above
  //    the scene (a sky, a lamp, the sun: > 2.5 stops); skin shine is less,
  //    and stays as it is. Shadows are lifted only when the darkest 5% sit
  //    well below it (> 3 stops).
  const float p99 = percentile(logs, 0.99f);
  float shadows = params.shadows >= 0 ? std::clamp(params.shadows, 0.0f, 1.0f) : std::clamp((key - p5 - 3.0f) / 3.0f, 0.0f, 0.9f);
  float highlights = params.highlights >= 0 ? std::clamp(params.highlights, 0.0f, 1.0f) : std::clamp((p99 - key - 2.5f) / 2.5f, 0.0f, 1.0f);
  shadows *= 1.0f - 0.85f * night;  // night stays dark around its lights
  // Exposure. Day: the scene's key to mid-grey. Night: its lit part to a
  // clear, bright level, and the rest follows ("brighter than reality, but
  // still night"). The automatic lift is bounded.
  float target = key + exposure;
  if (params.autoExposure) {
    // The exposure the camera held back on purpose (to keep highlights)
    // comes back in full; any darkness left is the scene's own (a dim
    // room should still feel like one, and lifting a dark subject all the
    // way to mid-grey turns deep skin and dark clothes grey), so beyond
    // half a stop it is lifted only half way.
    const float want = std::log2(0.18f) - key;
    const float restore = std::clamp(want, 0.0f, captureBias);
    const float rest = want - restore;
    const float soft = rest > 0.5f ? 0.5f + 0.5f * (rest - 0.5f) : rest;
    const float day = std::clamp(restore + soft, -2.0f, 5.0f);
    const float lit = std::clamp(std::log2(0.30f) - keyLit, -2.0f, 5.0f);
    target = key + day + (lit - day) * night + exposure;
  }
  // 3. Base and detail: an edge-preserving smooth of log luminance (guided
  //    filter, ~3% of the frame), so whole regions move together and edges
  //    don't halo; detail rides on top.
  constexpr float kOff = 14.0f, kSpan = 16.0f;
  auto Ln = Plane::allocate(lw, lh), cA = Plane::allocate(lw, lh), cB = Plane::allocate(lw, lh);
  if (!Ln || !cA || !cB) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for tone");
  for (size_t i = 0; i < L->size(); ++i) Ln->data()[i] = (L->data()[i] + kOff) / kSpan;
  const int32_t r = std::max<int32_t>(2, std::min(lw, lh) / 30);
  const float eps = (0.7f / kSpan) * (0.7f / kSpan);
  if (Error e = guidedCoefficients(*Ln, *Ln, r, eps, *cA, *cB, token); e.status != PP_OK) return e;
  // Night: how much each region holds detail (the base vs the picture,
  // averaged): flat dark regions (a night sky, a dark wall) are noise only.
  auto texture = Plane::allocate(lw, lh);
  if (!texture) return texture.error();
  {
    auto dev = Plane::allocate(lw, lh);
    if (!dev) return dev.error();
    for (size_t i = 0; i < L->size(); ++i) {
      const float b = (cA->data()[i] * Ln->data()[i] + cB->data()[i]) * kSpan - kOff;
      dev->data()[i] = std::fabs(L->data()[i] - b);
    }
    if (Error e = gaussianBlur(*dev, *texture, 2.0f, token); e.status != PP_OK) return e;
  }
  // Colour noise in lifted shadows: the local chroma, to pull toward.
  std::array<Plane, 2> chroma;
  for (int c = 0; c < 2; ++c) {
    auto raw = Plane::allocate(lw, lh), soft = Plane::allocate(lw, lh);
    if (!raw || !soft) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for tone");
    for (size_t i = 0; i < raw->size(); ++i) {
      float yv, cb, cr;
      rgbToYcc(toGamma(g->c[0].data()[i]), toGamma(g->c[1].data()[i]), toGamma(g->c[2].data()[i]), yv, cb, cr);
      raw->data()[i] = (c == 0 ? cb : cr) / (yv + 0.05f);
    }
    if (Error e = gaussianBlur(*raw, *soft, 1.5f, token); e.status != PP_OK) return e;
    chroma[static_cast<size_t>(c)] = std::move(soft).value();
  }
  const float kS = 0.75f * shadows, kH = 0.9f * highlights;
  // A little more local contrast where the range was compressed (else a
  // balanced backlit scene looks flat).
  const float detailGain = 0.8f + 0.6f * detail + 0.1f * highlights;
  const float bp = 0.03f * night;  // night: true blacks
  const float sCurve = 0.45f * night;

  // 4. Per pixel.
  const int32_t W = image.width(), H = image.height();
  const Rgb o = offsets(image);
  const float inv = 1.0f / static_cast<float>(g->f);
  std::vector<double> rowKey(static_cast<size_t>(H), 0.0), rowClip(static_cast<size_t>(H), 0.0);
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* row = image.row(y);
      const float ly = (static_cast<float>(y) + 0.5f) * inv - 0.5f;
      double kAcc = 0, clip = 0;
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* q = row + x * 4;
        const float lx = (static_cast<float>(x) + 0.5f) * inv - 0.5f;
        const float R = q[o.r] / 255.0f, G = q[o.g] / 255.0f, B = q[o.b] / 255.0f;
        float lr = toLinear(R) * wb[0], lg = toLinear(G) * wb[1], lb = toLinear(B) * wb[2];
        const float Y = lum(lr, lg, lb);
        const float Lp = std::log2(Y + kLogFloor);
        const float base = (cA->sample(lx, ly) * ((Lp + kOff) / kSpan) + cB->sample(lx, ly)) * kSpan - kOff;
        const float d = Lp - base;
        float xr = base - key;
        if (xr < 0 && kS > 1e-3f) xr = -std::log1p(kS * -xr) / kS;
        if (xr > 0 && kH > 1e-3f) xr = std::log1p(kH * xr) / kH;
        float Lout = target + xr + d * detailGain;
        if (night > 0.0f) {
          // Darkness around the lights: flat dark regions sink toward black
          // and lose their noise (their "detail" is only noise).
          const float flat = 1.0f - smoothstep(0.12f, 0.45f, texture->sample(lx, ly));
          const float dark = smoothstep(1.5f, 4.0f, keyLit - base);
          const float k = night * flat * dark;
          Lout -= 0.9f * k + d * detailGain * 0.7f * k;
        }
        const float gain = std::exp2(std::clamp(Lout - Lp, -6.0f, 8.0f));
        lr *= gain, lg *= gain, lb *= gain;
        // Highlights roll off on the brightest channel (hue kept).
        const float mx = std::max({lr, lg, lb});
        if (mx > 0.8f) {
          const float to = 0.8f + 0.2f * std::tanh((mx - 0.8f) / 0.2f);
          const float k = to / mx;
          lr *= k, lg *= k, lb *= k;
        }
        float r2 = toGamma(lr), g2 = toGamma(lg), b2 = toGamma(lb);
        // Lifted shadows: colour noise pulled toward the local colour.
        const float lift = std::log2(std::max(gain, 1.0f));
        const float calm = std::clamp((lift - 1.0f) / 3.0f, 0.0f, 0.7f) * (1.0f - smoothstep(0.15f, 0.45f, toGamma(Y * gain)));
        if (calm > 0.0f) {
          float yv, cb, cr;
          rgbToYcc(r2, g2, b2, yv, cb, cr);
          const float k = yv + 0.05f;
          const float ncb = cb / k + (chroma[0].sample(lx, ly) - cb / k) * calm;
          const float ncr = cr / k + (chroma[1].sample(lx, ly) - cr / k) * calm;
          yccToRgb(yv, ncb * k, ncr * k, r2, g2, b2);
        }
        // Night: true blacks and a gentle S-curve around the lights.
        if (night > 0.0f) {
          auto curve = [&](float v) {
            float u = std::max(0.0f, (v - bp) / (1.0f - bp));
            u = std::clamp(u, 0.0f, 1.0f);
            return u + sCurve * u * (1.0f - u) * (2.0f * u - 1.0f);
          };
          r2 = curve(r2), g2 = curve(g2), b2 = curve(b2);
        }
        r2 = R + strength * (r2 - R), g2 = G + strength * (g2 - G), b2 = B + strength * (b2 - B);
        q[o.r] = toByte(r2), q[o.g] = toByte(g2), q[o.b] = toByte(b2);
        kAcc += std::log2(lum(toLinear(std::clamp(r2, 0.0f, 1.0f)), toLinear(std::clamp(g2, 0.0f, 1.0f)), toLinear(std::clamp(b2, 0.0f, 1.0f))) + kLogFloor);
        if (q[o.r] == 255 || q[o.g] == 255 || q[o.b] == 255) clip += 1.0;
      }
      rowKey[static_cast<size_t>(y)] = kAcc, rowClip[static_cast<size_t>(y)] = clip;
    }
  });
  if (!done) return cancelledError();
  if (report != nullptr) {
    double tk = 0, tc = 0;
    for (size_t i = 0; i < rowKey.size(); ++i) tk += rowKey[i], tc += rowClip[i];
    const double n = static_cast<double>(W) * H;
    report->keyBefore = std::exp2(key);
    report->keyAfter = static_cast<float>(std::exp2(tk / n));
    report->rangeBefore = range;
    report->shadows = shadows;
    report->highlights = highlights;
    report->clippedAfter = static_cast<float>(tc / n);
  }
  return ok();
}

// -------------------------------------------------------------- flashFuse

namespace {

// A lighting-independent picture of the scene's structure, for matching a
// flash frame to an ambient one: log luminance with its local mean removed
// and its local contrast normalised.
Result<ImageBuffer> structureOf(const ImageBuffer& img, const CancellationToken* token) {
  const int32_t W = img.width(), H = img.height();
  auto Lp = Plane::allocate(W, H);
  if (!Lp) return failure(Lp.error());
  const Rgb o = offsets(img);
  for (int32_t y = 0; y < H; ++y) {
    const uint8_t* row = img.row(y);
    for (int32_t x = 0; x < W; ++x) {
      const uint8_t* q = row + x * 4;
      Lp->row(y)[x] = std::log2(lum(toLinear(q[o.r] / 255.0f), toLinear(q[o.g] / 255.0f), toLinear(q[o.b] / 255.0f)) + 2e-3f);
    }
  }
  const float sigma = std::max(2.0f, 0.01f * static_cast<float>(std::min(W, H)));
  auto mean = Plane::allocate(W, H), sq = Plane::allocate(W, H), var = Plane::allocate(W, H);
  if (!mean || !sq || !var) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to align the flash"));
  if (Error e = gaussianBlur(*Lp, *mean, sigma, token); e.status != PP_OK) return failure(e);
  for (size_t i = 0; i < Lp->size(); ++i) {
    const float d = Lp->data()[i] - mean->data()[i];
    sq->data()[i] = d * d;
  }
  if (Error e = gaussianBlur(*sq, *var, sigma, token); e.status != PP_OK) return failure(e);
  auto out = ImageBuffer::allocate(W, H, img.format());
  if (!out) return failure(out.error());
  for (int32_t y = 0; y < H; ++y) {
    uint8_t* row = out->row(y);
    for (int32_t x = 0; x < W; ++x) {
      const size_t i = static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x);
      const float z = (Lp->data()[i] - mean->data()[i]) / std::sqrt(var->data()[i] + 0.04f);
      const auto v = static_cast<uint8_t>(std::clamp(128.0f + 40.0f * z, 0.0f, 255.0f));
      uint8_t* q = row + x * 4;
      q[0] = q[1] = q[2] = v;
      q[3] = 255;
    }
  }
  return out;
}

}  // namespace

Error flashFuse(const ImageBuffer& ambient, const ImageBuffer& flash, ImageBuffer& out, const FuseParams& params,
                const CancellationToken* token, FuseReport* report) {
  ScopedTimer timer("light.flashFuse");
  if (!isColour(ambient) || !isColour(flash) || !isColour(out)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Flash fusion needs RGBA images");
  if (ambient.width() != flash.width() || ambient.height() != flash.height() || out.width() != ambient.width() ||
      out.height() != ambient.height()) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "The flash and ambient photos must be the same size");
  }
  if (ambient.width() < 32 || ambient.height() < 32) return makeError(PP_ERROR_INVALID_ARGUMENT, "Image too small");
  if (!std::isfinite(params.fill) || !std::isfinite(params.strength)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Non-finite fusion parameter");
  if (report != nullptr) *report = FuseReport{};
  const float fill = std::clamp(params.fill, 0.0f, 1.0f), strength = std::clamp(params.strength, 0.0f, 1.0f);
  const int32_t W = ambient.width(), H = ambient.height();

  // 1. Match the flash frame to the ambient one (the hand moves between them).
  const ImageBuffer* F = &flash;
  std::optional<ImageBuffer> warped;
  {
    auto sa = structureOf(ambient, token), sf = structureOf(flash, token);
    if (!sa) return sa.error();
    if (!sf) return sf.error();
    auto mo = video::estimateMotion(*sa, *sf, token);
    if (mo && mo->confidence > 0.3f) {
      const auto& s = mo->m;
      const double cx = W / 2.0, cy = H / 2.0;
      const video::Affine m{s.a, -s.b, s.tx + cx - s.a * cx + s.b * cy, s.b, s.a, s.ty + cy - s.b * cx - s.a * cy};
      auto wb = ImageBuffer::allocate(W, H, flash.format());
      if (!wb) return wb.error();
      if (Error e = video::warp(flash, m, *wb, token); e.status != PP_OK) return e;
      warped = std::move(wb).value();
      F = &*warped;
      if (report != nullptr) report->aligned = true;
    }
  }
  if (isCancelled(token)) return cancelledError();

  // 2. Both on a working grid, linear.
  auto ga = gridOf(ambient, 768, token), gf = gridOf(*F, 768, token);
  if (!ga) return ga.error();
  if (!gf) return gf.error();
  const int32_t lw = ga->w, lh = ga->h;
  const size_t n = static_cast<size_t>(lw) * static_cast<size_t>(lh);
  // The ambient light inside the flash frame (the flash frame's exposure of
  // the room alone): where the flash doesn't reach, F ≈ k·A. k is a low
  // percentile of F/A over pixels with light in both.
  std::vector<float> ratios;
  ratios.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const float ay = lum(ga->c[0].data()[i], ga->c[1].data()[i], ga->c[2].data()[i]);
    const float fy = lum(gf->c[0].data()[i], gf->c[1].data()[i], gf->c[2].data()[i]);
    if (ay > 0.002f && fy > 0.002f && fy < 0.9f) ratios.push_back(fy / ay);
  }
  const float kA = ratios.size() > 50 ? percentile(ratios, 0.08f) : 1.0f;
  const float flashGain = ratios.size() > 50 ? percentile(ratios, 0.5f) : 1.0f;
  // 3. Where the flash adds light (its own share of the flash frame), and
  //    the flash light's colour relative to the room's (to recolour it).
  auto rel = Plane::allocate(lw, lh), Gf = Plane::allocate(lw, lh);
  if (!rel || !Gf) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for flash fusion");
  double room[3] = {0, 0, 0}, lit[3] = {0, 0, 0};
  for (size_t i = 0; i < n; ++i) {
    const float fy = lum(gf->c[0].data()[i], gf->c[1].data()[i], gf->c[2].data()[i]);
    const float ay = lum(ga->c[0].data()[i], ga->c[1].data()[i], ga->c[2].data()[i]);
    const float own = std::max(0.0f, fy - kA * ay);
    const float r = own / (fy + 1e-3f);
    rel->data()[i] = r;
    Gf->data()[i] = (std::log2(fy + 1e-3f) + 10.0f) / 10.0f;
    if (r > 0.5f && fy < 0.85f && ay > 0.003f) {
      for (int c = 0; c < 3; ++c) {
        room[c] += ga->c[static_cast<size_t>(c)].data()[i];
        lit[c] += std::max(0.0f, gf->c[static_cast<size_t>(c)].data()[i] - kA * ga->c[static_cast<size_t>(c)].data()[i]);
      }
    }
  }
  float recolour[3] = {1, 1, 1};
  {
    const double ry = lum(static_cast<float>(room[0]), static_cast<float>(room[1]), static_cast<float>(room[2]));
    const double fy = lum(static_cast<float>(lit[0]), static_cast<float>(lit[1]), static_cast<float>(lit[2]));
    if (ry > 1e-6 && fy > 1e-6) {
      for (int c = 0; c < 3; ++c) {
        const double w = (room[c] / ry) / std::max(lit[c] / fy, 1e-6);
        recolour[c] = static_cast<float>(std::clamp(w, 0.4, 2.5));
      }
    }
  }
  // 4. Joint filtering: each ambient channel smoothed along the flash
  //    frame's clean edges (the flash sees the structure the dark ambient
  //    frame buries in noise); and the flash frame's own large scale, for
  //    its detail (F / its base).
  const int32_t r = std::max<int32_t>(2, std::min(lw, lh) / 150);
  std::array<Plane, 3> jA, jB;
  for (int c = 0; c < 3; ++c) {
    auto a = Plane::allocate(lw, lh), b = Plane::allocate(lw, lh);
    if (!a || !b) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for flash fusion");
    // eps in the guide's units (log2 / 10): edges of ~0.2 stop are kept.
    if (Error e = guidedCoefficients(*Gf, ga->c[static_cast<size_t>(c)], r, 0.0004f, *a, *b, token); e.status != PP_OK) return e;
    jA[static_cast<size_t>(c)] = std::move(a).value();
    jB[static_cast<size_t>(c)] = std::move(b).value();
  }
  auto fA = Plane::allocate(lw, lh), fB = Plane::allocate(lw, lh);
  if (!fA || !fB) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for flash fusion");
  // The flash frame's base: texture under ~1 stop is detail (transferred), larger edges stay in the base.
  if (Error e = guidedCoefficients(*Gf, *Gf, r, 0.01f, *fA, *fB, token); e.status != PP_OK) return e;
  // 5. Where the flash can't be trusted: its shadows and the places it
  //    doesn't reach (rel small), and its hot spots (near white). Smoothed.
  // Flash shadows also show as the flash/ambient ratio dropping well below
  // its neighbourhood's (the room's own lamps can light a flash shadow, so
  // the flash's share alone misses them; Petschnigg et al. 2004).
  auto ratio = Plane::allocate(lw, lh), ratioS = Plane::allocate(lw, lh);
  if (!ratio || !ratioS) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for flash fusion");
  for (size_t i = 0; i < n; ++i) {
    const float fy = lum(gf->c[0].data()[i], gf->c[1].data()[i], gf->c[2].data()[i]);
    const float ay = lum(ga->c[0].data()[i], ga->c[1].data()[i], ga->c[2].data()[i]);
    ratio->data()[i] = std::log2((fy + 2e-3f) / (ay + 2e-3f));
  }
  if (Error e = gaussianBlur(*ratio, *ratioS, std::max(3.0f, 0.03f * static_cast<float>(std::min(lw, lh))), token); e.status != PP_OK) return e;
  auto useAmb = Plane::allocate(lw, lh);
  if (!useAmb) return useAmb.error();
  double shadowN = 0, hotN = 0;
  for (size_t i = 0; i < n; ++i) {
    const float fy = toGamma(lum(gf->c[0].data()[i], gf->c[1].data()[i], gf->c[2].data()[i]));
    const float drop = ratioS->data()[i] - ratio->data()[i];  // stops below the neighbourhood
    const float sh = std::max(1.0f - smoothstep(0.12f, 0.4f, rel->data()[i]), smoothstep(0.6f, 1.4f, drop));
    const float hot = smoothstep(0.9f, 0.98f, fy);
    useAmb->data()[i] = std::max(sh, hot);
    shadowN += sh > 0.5f ? 1.0 : 0.0;
    hotN += hot > 0.5f ? 1.0 : 0.0;
  }
  auto useAmbS = Plane::allocate(lw, lh);
  if (!useAmbS) return useAmbS.error();
  if (Error e = gaussianBlur(*useAmb, *useAmbS, 1.5f, token); e.status != PP_OK) return e;

  // 6. Per pixel.
  const Rgb oa = offsets(ambient), of = offsets(*F), oo = offsets(out);
  const float inv = 1.0f / static_cast<float>(ga->f);
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      const uint8_t* ra = ambient.row(y);
      const uint8_t* rf = F->row(y);
      uint8_t* ro = out.row(y);
      const float ly = (static_cast<float>(y) + 0.5f) * inv - 0.5f;
      for (int32_t x = 0; x < W; ++x) {
        const uint8_t* qa = ra + x * 4;
        const uint8_t* qf = rf + x * 4;
        uint8_t* qo = ro + x * 4;
        const float lx = (static_cast<float>(x) + 0.5f) * inv - 0.5f;
        const float A[3] = {toLinear(qa[oa.r] / 255.0f), toLinear(qa[oa.g] / 255.0f), toLinear(qa[oa.b] / 255.0f)};
        const float Fl[3] = {toLinear(qf[of.r] / 255.0f), toLinear(qf[of.g] / 255.0f), toLinear(qf[of.b] / 255.0f)};
        const float fy = lum(Fl[0], Fl[1], Fl[2]);
        const float gfv = (std::log2(fy + 1e-3f) + 10.0f) / 10.0f;
        const float fBase = std::exp2((fA->sample(lx, ly) * gfv + fB->sample(lx, ly)) * 10.0f - 10.0f) - 1e-3f;
        // The flash frame's detail, trusted only where the flash lit something
        // (in its own shadows the "detail" is darkness, not texture).
        const float reliable = smoothstep(0.004f, 0.03f, fBase);
        const float detailRatio = 1.0f + (std::clamp((fy + 0.01f) / (std::max(fBase, 0.0f) + 0.01f), 0.4f, 2.5f) - 1.0f) * reliable;
        const float m = useAmbS->sample(lx, ly);
        float res[3];
        for (int c = 0; c < 3; ++c) {
          const float smooth = std::max(0.0f, jA[static_cast<size_t>(c)].sample(lx, ly) * gfv + jB[static_cast<size_t>(c)].sample(lx, ly));
          const float fused = smooth * detailRatio;
          float v = (1.0f - m) * fused + m * A[c];
          // Fill: some of the flash's own light, in the room's colour, at
          // most about a stop over the room's own light there (a strong
          // flash would otherwise flatten the scene again).
          const float own = std::max(0.0f, Fl[c] - kA * A[c]) / std::max(kA, 1e-3f);
          v += fill * (1.0f - m) * std::min(own * recolour[c] * 0.5f, 2.0f * smooth + 0.01f);
          res[c] = A[c] + strength * (v - A[c]);
        }
        const float mx = std::max({res[0], res[1], res[2]});
        if (mx > 0.85f) {
          const float to = 0.85f + 0.15f * std::tanh((mx - 0.85f) / 0.15f);
          for (float& v : res) v *= to / mx;
        }
        qo[oo.r] = toByte(toGamma(res[0]));
        qo[oo.g] = toByte(toGamma(res[1]));
        qo[oo.b] = toByte(toGamma(res[2]));
        qo[3] = 255;
      }
    }
  });
  if (!done) return cancelledError();
  if (report != nullptr) {
    report->flashGain = flashGain;
    report->shadowShare = static_cast<float>(shadowN / static_cast<double>(n));
    report->hotShare = static_cast<float>(hotN / static_cast<double>(n));
  }
  return ok();
}

}  // namespace photoplus::light
