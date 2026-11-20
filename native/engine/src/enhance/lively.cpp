// Lively: the camera's default finish. Phone cameras on many Android devices
// save flat, grey-ish photos: skin looks ashy, blacks look washed out and the
// light looks dull. Flagship phones render the same scene warm, rich and
// bright. This finish closes that gap in one pass, and adapts to the photo:
//
//  * colour of the light: grey things made neutral (skin keeps its warmth),
//    then a little warmth when the scene is cool;
//  * tone: deeper blacks, a dark photo lifted toward a natural mid-tone, a
//    gentle S-curve for depth and a soft shoulder that keeps highlights;
//  * clarity: mid-size contrast (shapes, not grain);
//  * skin: rich, even colour. Grey skin is brought toward the colour healthy
//    skin has for its brightness (every skin tone keeps its own hue), and the
//    grey shine of a wet or oily highlight takes the colour around it;
//  * other colours: vibrance (muted colours gain more than vivid ones);
//  * neutral shadows (black clothes, dark corners) settle deeper;
//  * smooth and even: every colour gain is decided from a smooth,
//    edge-aware map (never from one noisy pixel), colour noise is evened
//    out, and the skin's fine texture is softened while edges stay sharp.
//
// Every step measures first and does little when the photo already has it:
// a well-exposed, colourful photo changes only slightly. Large-scale
// measurements run on a small grid (at most 1024 px), so full-resolution
// photos need no full-size working planes.
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "../portrait/common.hpp"
#include "photoplus/enhance.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::enhance {

namespace {
using portrait::detail::offsets;
using portrait::detail::rgbToYcc;
using portrait::detail::toByte;
using portrait::detail::yccToRgb;

constexpr int32_t kGridEdge = 1024;
constexpr int32_t kFineEdge = 1600;
// Healthy skin carries about this much colour for its brightness (chroma /
// luma, measured on flagship-phone portraits across skin tones).
constexpr float kSkinRatio = 0.22f;

/** How skin-like a colour is (0..1): hues from yellow-orange to red, with some colour, not black or blown. */
inline float skinWeight(float y, float cb, float cr) {
  const float c = std::hypot(cb, cr);
  const float hue = std::atan2(cr, cb) * 57.29578f;
  return std::clamp(1.0f - std::fabs(hue - 125.0f) / 35.0f, 0.0f, 1.0f) * std::clamp((c - 0.008f) / 0.02f, 0.0f, 1.0f) *
         std::clamp((y - 0.04f) / 0.08f, 0.0f, 1.0f) * std::clamp((0.97f - y) / 0.1f, 0.0f, 1.0f);
}

float quantile(std::vector<float>& v, float q) {
  if (v.empty()) return 0.0f;
  const auto k = static_cast<size_t>(std::clamp(q, 0.0f, 1.0f) * static_cast<float>(v.size() - 1));
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
  return v[k];
}

struct Look {
  float shiftB = 0, shiftR = 0, invGrey = 1;  // white balance (a gain on the light)
  bool cooling = false;
  float warmB = 0, warmR = 0;  // added warmth
  std::array<float, 1025> curve{};
  float clarity = 0;
  float skinBoost = 1;  // most a skin pixel's colour may grow
  float skinCb = 0, skinCr = 0;  // the person's skin colour, per unit of brightness (after the boost)
  float evenSkin = 0;            // how far skin is drawn toward that one colour
  float vibrance = 0;
  float toe = 0;
  float shine = 0;
};

/** Luma tone curve: black point, gain, lift, S-curve and highlight shoulder. */
float toneAt(float y, float black, float gain, float lift, float contrast) {
  float v = std::clamp((y - black) * gain, 0.0f, 1.0f);
  if (lift > 0.0f) v = v * (1.0f + lift) / (1.0f + lift * v);
  v += contrast * (v - 0.5f) * (1.0f - std::fabs(2.0f * v - 1.0f));
  if (v > 0.85f) v = 0.85f + (v - 0.85f) / (1.0f + (v - 0.85f) / 0.2f);
  return std::clamp(v, 0.0f, 1.0f);
}

inline float curveAt(const Look& L, float y) {
  const float f = std::clamp(y, 0.0f, 1.0f) * 1024.0f;
  const auto i = std::min<size_t>(static_cast<size_t>(f), 1023);
  const float t = f - static_cast<float>(i);
  return L.curve[i] + (L.curve[i + 1] - L.curve[i]) * t;
}

/** Colour follows brightness, but a lifted pixel gains colour more slowly (no orange glare). */
inline float chromaFor(float lumaRatio) {
  const float k = std::clamp(lumaRatio, 0.5f, 2.0f);
  return k > 1.0f ? 1.0f + 0.6f * (k - 1.0f) : k;
}

inline void whiteBalance(const Look& L, float y, float& cb, float& cr, float skin) {
  float k = std::min(y * L.invGrey, 1.6f);
  if (L.cooling) k *= 1.0f - 0.67f * skin;
  const float warm = std::min(y * 2.0f, 1.0f);
  cb += L.shiftB * k + L.warmB * warm;
  cr += L.shiftR * k + L.warmR * warm;
}
}  // namespace

Error lively(ImageBuffer& image, float strength, const CancellationToken* token, LivelyReport* report) {
  ScopedTimer timer("enh.lively");
  const float s = std::isfinite(strength) ? std::clamp(strength, 0.0f, 1.0f) : 0.0f;
  if (report) *report = LivelyReport{};
  if (s <= 0.0f) return Error{PP_OK, {}};
  const int32_t W = image.width(), H = image.height();
  if (W <= 0 || H <= 0) return Error{PP_OK, {}};
  const auto o = offsets(image);

  // ---- a small grid: box-averaged luma and chroma
  const int32_t f = std::max<int32_t>(1, (std::max(W, H) + kGridEdge - 1) / kGridEdge);
  const int32_t gw = (W + f - 1) / f, gh = (H + f - 1) / f;
  auto gy = Plane::allocate(gw, gh);
  if (!gy) return gy.error();
  auto gcb = Plane::allocate(gw, gh);
  if (!gcb) return gcb.error();
  auto gcr = Plane::allocate(gw, gh);
  if (!gcr) return gcr.error();
  {
    const bool done = parallelFor(gh, token, [&](int32_t begin, int32_t end) {
      for (int32_t gyi = begin; gyi < end; ++gyi) {
        for (int32_t gx = 0; gx < gw; ++gx) {
          float sr = 0, sg = 0, sb = 0;
          int n = 0;
          for (int32_t y = gyi * f; y < std::min(H, (gyi + 1) * f); ++y) {
            const uint8_t* row = image.row(y);
            for (int32_t x = gx * f; x < std::min(W, (gx + 1) * f); ++x) {
              const uint8_t* px = row + x * 4;
              sr += px[o.r];
              sg += px[o.g];
              sb += px[o.b];
              ++n;
            }
          }
          const float k = 1.0f / (255.0f * static_cast<float>(std::max(n, 1)));
          float yv, cb, cr;
          rgbToYcc(sr * k, sg * k, sb * k, yv, cb, cr);
          gy->row(gyi)[gx] = yv;
          gcb->row(gyi)[gx] = cb;
          gcr->row(gyi)[gx] = cr;
        }
      }
    });
    if (!done) return cancelledError();
  }
  const size_t n = gy->size();
  float* Y = gy->data();
  float* CB = gcb->data();
  float* CR = gcr->data();

  Look L;
  // ---- white balance from grey pixels (as Clarity's), a little firmer
  {
    double sb = 0, sr = 0, sy = 0;
    size_t grey = 0, seen = 0;
    for (size_t i = 0; i < n; ++i) {
      if (Y[i] < 0.15f || Y[i] > 0.95f) continue;
      ++seen;
      if (std::hypot(CB[i], CR[i]) > 0.06f * (0.3f + Y[i])) continue;
      sb += CB[i];
      sr += CR[i];
      sy += Y[i];
      ++grey;
    }
    if (grey > 256 && grey * 50 > seen) {
      const auto g = static_cast<double>(grey);
      L.shiftB = s * std::clamp(-static_cast<float>(sb / g) * 0.7f, -0.04f, 0.04f);
      L.shiftR = s * std::clamp(-static_cast<float>(sr / g) * 0.7f, -0.04f, 0.04f);
      L.invGrey = 1.0f / std::max(static_cast<float>(sy / g), 0.1f);
      L.cooling = L.shiftB > 0.0f || L.shiftR < 0.0f;
    }
  }
  // ---- the scene's warmth and colourfulness (after white balance)
  double warmth = 0, chroma = 0;
  size_t mid = 0;
  for (size_t i = 0; i < n; ++i) {
    float cb = CB[i], cr = CR[i];
    whiteBalance(L, Y[i], cb, cr, skinWeight(Y[i], cb, cr));
    CB[i] = cb;
    CR[i] = cr;
    chroma += std::hypot(cb, cr);
    if (Y[i] > 0.1f && Y[i] < 0.9f) {
      warmth += cr - cb;
      ++mid;
    }
  }
  const float meanChroma = static_cast<float>(chroma / static_cast<double>(std::max<size_t>(n, 1)));
  const float sceneWarmth = mid ? static_cast<float>(warmth / static_cast<double>(mid)) : 0.0f;
  // Warmth only where the scene is not warm already (sunsets, tungsten stay as they are).
  const float addWarm = s * std::clamp((0.035f - sceneWarmth) / 0.035f, 0.0f, 1.0f);
  L.warmB = -0.006f * addWarm;
  L.warmR = 0.005f * addWarm;
  // A colourful photo gets little more colour.
  const float colourRoom = std::clamp((0.085f - meanChroma) / 0.04f, 0.25f, 1.0f);
  L.vibrance = 0.45f * s * colourRoom;

  // ---- tone curve
  std::vector<float> lum(Y, Y + n);
  const float lo = quantile(lum, 0.005f);
  const float hi = quantile(lum, 0.996f);
  const float median = quantile(lum, 0.5f);
  const float p90 = quantile(lum, 0.9f);
  const float black = std::min(lo, 0.06f) * s;
  const float gain = 1.0f + s * (std::clamp(0.985f / std::max(hi - black, 0.2f), 1.0f, 1.5f) - 1.0f);
  const float m = std::clamp((median - black) * gain, 0.05f, 0.95f);
  constexpr float kTarget = 0.42f;
  // A photo with bright, well-exposed parts and a dark median is low-key on
  // purpose (a black shirt, a dim room lit by lamps): it is lifted less.
  const float headroom = std::clamp((0.8f - (p90 - black) * gain) / 0.3f, 0.35f, 1.0f);
  const float lift =
      m < kTarget ? s * headroom * std::clamp((kTarget - m) / (m * (1.0f - kTarget)), 0.0f, 1.2f) : 0.0f;
  const float contrast = 0.22f * s;
  for (size_t i = 0; i <= 1024; ++i) L.curve[i] = toneAt(static_cast<float>(i) / 1024.0f, black, gain, lift, contrast);
  L.clarity = 0.3f * s;
  L.toe = 0.3f * s;
  L.shine = s;

  // ---- on the grid: toned luma, skin, colour; then the large-scale planes
  auto gw8 = Plane::allocate(gw, gh);  // skin weight
  if (!gw8) return gw8.error();
  auto gc = Plane::allocate(gw, gh);  // skin colour (chroma × weight)
  if (!gc) return gc.error();
  std::vector<float> skinRatios, skinHues;
  for (size_t i = 0; i < n; ++i) {
    const float v = curveAt(L, Y[i]);
    const float kL = chromaFor(v / std::max(Y[i], 1e-4f));
    const float cb = CB[i] * kL, cr = CR[i] * kL;
    const float w = skinWeight(v, cb, cr);
    const float c = std::hypot(cb, cr);
    Y[i] = v;
    gw8->data()[i] = w;
    gc->data()[i] = c * w;
    if (w > 0.5f) {
      skinRatios.push_back(c / std::max(v, 0.05f));
      skinHues.push_back(std::atan2(cr, cb));
    }
  }
  if (skinRatios.size() > 500 / static_cast<size_t>(f * f) + 50) {
    const float ratio = quantile(skinRatios, 0.5f);
    L.skinBoost = 1.0f + s * (std::clamp(kSkinRatio / std::max(ratio, 1e-3f), 1.0f, 1.6f) - 1.0f);
    // One even skin colour: the person's own median hue at the boosted
    // ratio. Patches that are redder, greyer or more purple than the rest of
    // the skin move toward it, so the colour lies evenly over the face.
    const float hue = quantile(skinHues, 0.5f);
    const float target = std::min(std::max(ratio, kSkinRatio), ratio * L.skinBoost);
    L.skinCb = target * std::cos(hue);
    L.skinCr = target * std::sin(hue);
    L.evenSkin = 0.55f * s;
  }
  const float scale = static_cast<float>(std::max(gw, gh)) / 1100.0f;
  auto base = Plane::allocate(gw, gh);  // clarity base
  if (!base) return base.error();
  if (Error e = gaussianBlur(*gy, *base, std::max(4.0f, static_cast<float>(std::max(gw, gh)) / 80.0f), token); e.status != PP_OK) return e;
  auto near = Plane::allocate(gw, gh);  // local brightness around skin
  if (!near) return near.error();
  const float shineSigma = std::max(2.0f, 14.0f * scale);
  if (Error e = gaussianBlur(*gy, *near, shineSigma, token); e.status != PP_OK) return e;
  auto nearC = Plane::allocate(gw, gh);
  if (!nearC) return nearC.error();
  if (Error e = gaussianBlur(*gc, *nearC, shineSigma, token); e.status != PP_OK) return e;
  if (Error e = gaussianBlur(*gw8, *gc, shineSigma, token); e.status != PP_OK) return e;  // gc now: nearby skin weight
  for (size_t i = 0; i < n; ++i) nearC->data()[i] /= std::max(gc->data()[i], 1e-3f);

  // ---- a finer grid (at most 1600 px): the smooth, edge-aware maps every
  // pixel's change is decided from. Deciding colour gains from each pixel's
  // own (noisy) colour boosts noise into blotches; deciding them from these
  // maps gives even, uniform colour, and the skin surface can be smoothed.
  const int32_t f2 = std::max<int32_t>(1, (std::max(W, H) + kFineEdge - 1) / kFineEdge);
  const int32_t fw2 = (W + f2 - 1) / f2, fh2 = (H + f2 - 1) / f2;
  auto fv = Plane::allocate(fw2, fh2);   // toned luma
  if (!fv) return fv.error();
  auto fcb = Plane::allocate(fw2, fh2);  // colour after white balance (before tone)
  if (!fcb) return fcb.error();
  auto fcr = Plane::allocate(fw2, fh2);
  if (!fcr) return fcr.error();
  auto fsk = Plane::allocate(fw2, fh2);  // skin weight
  if (!fsk) return fsk.error();
  auto tmp = Plane::allocate(fw2, fh2);
  if (!tmp) return tmp.error();
  {
    const bool ok = parallelFor(fh2, token, [&](int32_t begin, int32_t end) {
      for (int32_t gyi = begin; gyi < end; ++gyi) {
        for (int32_t gx = 0; gx < fw2; ++gx) {
          float sr = 0, sg = 0, sb = 0;
          int cnt = 0;
          for (int32_t y = gyi * f2; y < std::min(H, (gyi + 1) * f2); ++y) {
            const uint8_t* row = image.row(y);
            for (int32_t x = gx * f2; x < std::min(W, (gx + 1) * f2); ++x) {
              const uint8_t* px = row + x * 4;
              sr += px[o.r];
              sg += px[o.g];
              sb += px[o.b];
              ++cnt;
            }
          }
          const float k = 1.0f / (255.0f * static_cast<float>(std::max(cnt, 1)));
          float yv, cb, cr;
          rgbToYcc(sr * k, sg * k, sb * k, yv, cb, cr);
          whiteBalance(L, yv, cb, cr, skinWeight(yv, cb, cr));
          const float v = curveAt(L, yv);
          const float kL = chromaFor(v / std::max(yv, 1e-4f));
          fv->row(gyi)[gx] = v;
          fcb->row(gyi)[gx] = cb;
          fcr->row(gyi)[gx] = cr;
          fsk->row(gyi)[gx] = skinWeight(v, cb * kL, cr * kL);
        }
      }
    });
    if (!ok) return cancelledError();
  }
  // Pores and grain sit below ~0.2 % of the photo's size; colour noise is wider.
  const float fineSigma = std::max(1.0f, 3.0f * static_cast<float>(std::max(fw2, fh2)) / 1600.0f);
  const auto blurInPlace = [&](Result<Plane>& p, float sigma) -> Error {
    if (Error e = gaussianBlur(*p, *tmp, sigma, token); e.status != PP_OK) return e;
    std::swap(*p, *tmp);
    return Error{PP_OK, {}};
  };
  if (Error e = blurInPlace(fv, fineSigma); e.status != PP_OK) return e;
  if (Error e = blurInPlace(fcb, 4.0f * fineSigma); e.status != PP_OK) return e;
  if (Error e = blurInPlace(fcr, 4.0f * fineSigma); e.status != PP_OK) return e;
  if (Error e = blurInPlace(fsk, 2.0f * fineSigma); e.status != PP_OK) return e;

  // ---- every pixel
  const float inv = 1.0f / static_cast<float>(f);
  const float inv2 = 1.0f / static_cast<float>(f2);
  const float smoothSkin = 0.75f * s;   // share of the skin's fine texture smoothed away
  const float smoothAll = 0.25f * s;   // grain everywhere (only small differences)
  const float smoothColour = 0.9f * s;
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end && !isCancelled(token); ++y) {
      uint8_t* row = image.row(y);
      const float sy = (static_cast<float>(y) + 0.5f) * inv - 0.5f;
      const float sy2 = (static_cast<float>(y) + 0.5f) * inv2 - 0.5f;
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* px = row + x * 4;
        const float sx = (static_cast<float>(x) + 0.5f) * inv - 0.5f;
        const float sx2 = (static_cast<float>(x) + 0.5f) * inv2 - 0.5f;
        float yv, cb, cr;
        rgbToYcc(px[o.r] / 255.0f, px[o.g] / 255.0f, px[o.b] / 255.0f, yv, cb, cr);
        const float skin = std::clamp(fsk->sample(sx2, sy2), 0.0f, 1.0f);
        whiteBalance(L, yv, cb, cr, skin);
        const float toned = curveAt(L, yv);
        float v = toned;
        // Smooth surface: fine texture eased toward its surroundings. The
        // range weight keeps edges (eyes, lips, hair, a beard's outline).
        const float vS = fv->sample(sx2, sy2);
        const float dd = v - vS;
        const float edge = std::exp(-(dd * dd) / (0.06f * 0.06f));
        const float grain = std::exp(-(dd * dd) / (0.02f * 0.02f));
        v -= (smoothSkin * skin * edge + smoothAll * (1.0f - skin) * grain) * dd;
        // Clarity on shapes only (the smooth map against the wide base): no grain boost.
        const float dm = vS - base->sample(sx, sy);
        v = std::clamp(v + L.clarity * dm / (1.0f + 6.0f * std::fabs(dm)), 0.0f, 1.0f);
        // Even colour: chroma noise and blotches eased toward the smooth colour.
        const float cbS = fcb->sample(sx2, sy2), crS = fcr->sample(sx2, sy2);
        const float dc = std::hypot(cb - cbS, cr - crS);
        const float a = smoothColour * std::exp(-(dc * dc) / (0.03f * 0.03f)) * (0.65f + 0.35f * skin);
        cb += (cbS - cb) * a;
        cr += (crS - cr) * a;
        const float kL = chromaFor(toned / std::max(yv, 1e-4f));
        cb *= kL;
        cr *= kL;
        // Gains from the smooth colour, so neighbours get the same gain.
        const float cS = std::max(std::hypot(cbS, crS) * kL, 1e-3f);
        float k = 1.0f + L.vibrance * std::clamp(1.0f - cS / 0.3f, 0.0f, 1.0f);
        if (skin > 0.0f) {
          // Shine: skin brighter than its surroundings, grey from the
          // light's reflection; a little dimmer, coloured like the skin around it.
          const float bv = near->sample(sx, sy);
          const float shine = std::clamp((vS - bv) / 0.05f, 0.0f, 1.0f) * skin * L.shine;
          v -= 0.5f * shine * std::max(v - bv, 0.0f);
          float ks = std::clamp(kSkinRatio * vS / cS, 1.0f, L.skinBoost);
          const float kShine = std::clamp(L.skinBoost * nearC->sample(sx, sy) * vS / std::max(bv, 0.05f) / cS, 1.0f, 2.5f);
          ks += (std::max(ks, kShine) - ks) * shine;
          k = skin * ks + (1.0f - skin) * k;
        }
        cb *= k;
        cr *= k;
        if (L.evenSkin > 0.0f && skin > 0.0f) {
          // Skin is never strongly saturated: a red hat, orange clothes or
          // wood (skin-like hues) keep their own colour.
          const float target = std::hypot(L.skinCb, L.skinCr);
          const float ratio = cS / std::max(vS, 0.05f);
          const float plausible = std::clamp(1.0f - (ratio - 1.4f * target) / std::max(target, 1e-3f), 0.0f, 1.0f);
          const float u = L.evenSkin * skin * plausible;
          const float lv = std::clamp(v, 0.0f, 0.85f);  // highlights carry no more colour than bright skin
          cb += (L.skinCb * lv - cb) * u;
          cr += (L.skinCr * lv - cr) * u;
        }
        // Neutral shadows (not skin) settle deeper: black looks black.
        if (v < 0.4f && L.toe > 0.0f) {
          const float t = L.toe * (1.0f - skin) * std::clamp(1.0f - cS * k / 0.08f, 0.0f, 1.0f);
          const float v2 = std::max(0.0f, v - t * v * (0.4f - v) / 0.4f);
          const float kk = std::clamp(v2 / std::max(v, 1e-4f), 0.5f, 1.0f);
          cb *= kk;
          cr *= kk;
          v = v2;
        }
        float r, g, b;
        yccToRgb(v, cb, cr, r, g, b);
        px[o.r] = toByte(r);
        px[o.g] = toByte(g);
        px[o.b] = toByte(b);
      }
    }
  });
  if (!done) return cancelledError();
  if (report) {
    report->skinBoost = L.skinBoost;
    report->lift = lift;
    report->warmth = addWarm;
    report->colourRoom = colourRoom;
  }
  return Error{PP_OK, {}};
}

}  // namespace photoplus::enhance
