// Skin Tone: see skintone.hpp. Layout of one run:
//   1. the person's skin measured on the face (beauty::measure);
//   2. on a working grid (~768 px): a skin weight from colour similarity,
//      brightness and texture (hair out), the face / subject region and the
//      protected features; then colour guided-filter coefficients so the weight follows
//      the photo's own edges when evaluated per pixel at full resolution;
//   3. per pixel: a partial brightness move in linear light (highlights
//      rolled off), then the chroma moved toward the shade relative to the
//      person's own skin, kept in gamut by easing chroma; optional sheen.
#include "photoplus/skintone.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "../portrait/common.hpp"
#include "photoplus/beauty.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::skintone {

using namespace portrait::detail;

namespace {

// Values pushed above max(original, knee) approach 1 smoothly: added light
// never clips, and what was already bright is not pushed further.
inline float rollOff(float v, float original, float knee) {
  const float hi = std::max(original, knee);
  if (v <= hi || hi >= 1.0f) return std::min(v, std::max(original, 1.0f));
  return hi + (1.0f - hi) * std::tanh((v - hi) / (1.0f - hi));
}

Result<Plane> resized(const ImageBuffer* m, int32_t w, int32_t h, const CancellationToken* token) {
  auto p = maskPlane(*m);
  if (!p) return failure(p.error());
  return resizeBilinear(*p, w, h, token);
}

}  // namespace

Error apply(ImageBuffer& image, const ImageBuffer* subject, const ImageBuffer* faces, const ImageBuffer* protect,
            const Params& params, const CancellationToken* token, Report* report) {
  ScopedTimer timer("skintone.apply");
  if (!isColour(image)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Skin Tone needs an RGBA image");
  if (image.width() < 16 || image.height() < 16) return makeError(PP_ERROR_INVALID_ARGUMENT, "Image too small");
  for (float v : {params.r, params.g, params.b, params.glow, params.strength}) {
    if (!std::isfinite(v)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Non-finite skin tone parameter");
  }
  if (report != nullptr) *report = Report{};
  if (isCancelled(token)) return cancelledError();
  const float strength = std::clamp(params.strength, 0.0f, 1.0f);
  const float glow = std::clamp(params.glow, 0.0f, 1.0f);
  if (faces == nullptr || faces->empty()) return ok();

  // 1. The person's own skin.
  const beauty::Scene sc = beauty::measure(image, subject, faces, protect, {});
  if (!sc.hasSkin) return ok();
  if (report != nullptr) report->hasSkin = true;
  if (strength <= 0.0f) return ok();
  const float sk = 1.0f / (sc.skinY + 0.05f);
  const float sNcb = sc.skinCb * sk, sNcr = sc.skinCr * sk;
  float ty, tcb, tcr;
  rgbToYcc(std::clamp(params.r, 0.0f, 1.0f), std::clamp(params.g, 0.0f, 1.0f), std::clamp(params.b, 0.0f, 1.0f), ty, tcb, tcr);
  const float tk = 1.0f / (ty + 0.05f);
  // Brightness: half the way in stops, eased off toward ~0.7 stop either
  // way (a soft limit, so deeper and fairer shades keep their order).
  const float wantedLog = 0.5f * std::log(std::max(toLinear(ty), 1e-4f) / std::max(toLinear(sc.skinY), 1e-3f));
  constexpr float kMaxLog = 0.5f;  // x1.65 / x0.61 in linear light
  const float logGain = kMaxLog * std::tanh(wantedLog / kMaxLog);
  const float gain = std::exp(logGain);
  // The undertone (hue) always goes all the way to the shade's. Its
  // richness (saturation) goes only as far as the brightness could: a pale
  // shade's low saturation on a face that stays deep would read as ashen
  // grey, a deep shade's richness on a face that stays fair as orange.
  // (At least half way, so shades of a similar hue stay apart.)
  const float reached = std::fabs(wantedLog) > 1e-3f ? 0.5f + 0.5f * std::clamp(logGain / wantedLog, 0.0f, 1.0f) : 1.0f;
  const float sMag = std::hypot(sNcb, sNcr), tMag = std::hypot(tcb * tk, tcr * tk);
  const float mag = sMag + (tMag - sMag) * reached;
  const float tAng = std::atan2(tcr * tk, tcb * tk);
  const float dNcb = mag * std::cos(tAng) - sNcb, dNcr = mag * std::sin(tAng) - sNcr;
  const float skinLin = toLinear(sc.skinY);
  const float skinYOut = toGamma(std::min(skinLin * gain, 1.0f));  // the skin's brightness after the move

  // 2. The skin weight on a working grid.
  const int32_t W = image.width(), H = image.height();
  const Rgb o = offsets(image);
  const int32_t f = std::max<int32_t>(1, (std::max(W, H) + 767) / 768);
  const int32_t lw = (W + f - 1) / f, lh = (H + f - 1) / f;
  std::array<Plane, 3> gam;
  for (auto& p : gam) {
    auto q = Plane::allocate(lw, lh);
    if (!q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for Skin Tone");
    p = std::move(q).value();
  }
  {
    const bool done = parallelFor(lh, token, [&](int32_t begin, int32_t end) {
      for (int32_t ly = begin; ly < end; ++ly)
        for (int32_t lx = 0; lx < lw; ++lx) {
          float s[3] = {0, 0, 0};
          int32_t n = 0;
          for (int32_t y = ly * f; y < std::min(H, (ly + 1) * f); ++y) {
            const uint8_t* row = image.row(y);
            for (int32_t x = lx * f; x < std::min(W, (lx + 1) * f); ++x, ++n) {
              const uint8_t* q = row + x * 4;
              s[0] += q[o.r], s[1] += q[o.g], s[2] += q[o.b];
            }
          }
          const float inv = n ? 1.0f / (255.0f * static_cast<float>(n)) : 0.0f;
          for (int c = 0; c < 3; ++c) gam[static_cast<size_t>(c)].row(ly)[lx] = s[c] * inv;
        }
    });
    if (!done) return cancelledError();
  }
  auto faceP = resized(faces, lw, lh, token);
  if (!faceP) return faceP.error();
  std::optional<Plane> subjP, protP;
  if (subject != nullptr && !subject->empty()) {
    auto p = resized(subject, lw, lh, token);
    if (!p) return p.error();
    subjP = std::move(p).value();
  }
  if (protect != nullptr && !protect->empty()) {
    auto p = resized(protect, lw, lh, token);
    if (!p) return p.error();
    protP = std::move(p).value();
  }
  auto weight = Plane::allocate(lw, lh);
  if (!weight) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for Skin Tone");
  // How far from the person's skin colour still counts as skin. Capped: in
  // mixed light or a group photo the measured spread is wide, and a wider
  // window would take in clothes and hair of other colours.
  const float sigma = std::clamp(2.5f * sc.skinSpread, 0.07f, 0.12f);
  // Texture. Fair or blonde hair can share the skin's colour and brightness,
  // but it is strands: busy at fine scale where skin is smooth. Each pixel's
  // fine detail is compared with that of the person's own face skin.
  auto luma = Plane::allocate(lw, lh), soft = Plane::allocate(lw, lh), tex = Plane::allocate(lw, lh);
  if (!luma || !soft || !tex) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for Skin Tone");
  for (size_t i = 0; i < luma->size(); ++i) {
    luma->data()[i] = 0.299f * gam[0].data()[i] + 0.587f * gam[1].data()[i] + 0.114f * gam[2].data()[i];
  }
  if (Error e = gaussianBlur(*luma, *soft, 1.0f, token); e.status != PP_OK) return e;
  for (size_t i = 0; i < luma->size(); ++i) soft->data()[i] = std::fabs(luma->data()[i] - soft->data()[i]);
  if (Error e = gaussianBlur(*soft, *tex, std::max(1.5f, 0.004f * static_cast<float>(std::min(lw, lh))), token); e.status != PP_OK) {
    return e;
  }
  // Without face rows on this grid (a tiny face) there is no hair zone (faceMid above the frame).
  float texSkin = 0.0f, faceMid = -1.0f, faceH = 1.0f;
  {
    std::vector<float> t;
    int32_t top = lh, bottom = -1;
    for (int32_t y = 0; y < lh; ++y)
      for (int32_t x = 0; x < lw; ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(lw) + static_cast<size_t>(x);
        if (faceP->data()[i] < 0.6f) continue;
        top = std::min(top, y), bottom = std::max(bottom, y);
        if (protP && protP->data()[i] > 0.3f) continue;
        float yv, cb, cr;
        rgbToYcc(gam[0].data()[i], gam[1].data()[i], gam[2].data()[i], yv, cb, cr);
        const float k = 1.0f / (yv + 0.05f);
        if (std::hypot(cb * k - sNcb, cr * k - sNcr) < sigma && yv > 0.6f * sc.skinY) t.push_back(tex->data()[i]);
      }
    if (t.size() >= 16) {
      std::nth_element(t.begin(), t.begin() + static_cast<long>(t.size() / 2), t.end());
      texSkin = t[t.size() / 2];
    }
    if (bottom >= top) faceMid = 0.5f * static_cast<float>(top + bottom), faceH = static_cast<float>(bottom - top + 1);
  }
  texSkin = std::max(texSkin, 0.004f);
  {
    const bool done = parallelFor(lh, token, [&](int32_t begin, int32_t end) {
      for (int32_t y = begin; y < end; ++y)
        for (int32_t x = 0; x < lw; ++x) {
          const size_t i = static_cast<size_t>(y) * static_cast<size_t>(lw) + static_cast<size_t>(x);
          float yv, cb, cr;
          rgbToYcc(gam[0].data()[i], gam[1].data()[i], gam[2].data()[i], yv, cb, cr);
          const float k = 1.0f / (yv + 0.05f);
          const float dc = std::hypot(cb * k - sNcb, cr * k - sNcr) / sigma;
          const float face = faceP->data()[i];
          // In the face, skin is the rule (what is not skin is protected or
          // dark); on the body, it must look like the person's skin.
          const float body = dc / 0.85f;  // a little stricter off the face
          const float chroma = std::max(std::exp(-0.5f * body * body), 0.55f * face * std::exp(-0.125f * dc * dc));
          // Hair, brows and beards are much darker than the skin; the very
          // brightest pixels (speculars) are left alone.
          const float ratio = yv / std::max(sc.skinY, 0.02f);
          const float bright = smoothstep(0.35f, 0.62f, ratio) * (1.0f - smoothstep(0.96f, 1.0f, yv));
          const float region = subjP ? std::max(subjP->data()[i], face) : face;
          const float keep = protP ? 1.0f - protP->data()[i] : 1.0f;
          // Around and above the face (outside it), where hair grows, the
          // test is stricter; hands and arms below are smooth either way.
          const float hairZone = (1.0f - face) * (1.0f - smoothstep(faceMid, faceMid + 0.2f * faceH, static_cast<float>(y)));
          const float busy = tex->data()[i] / texSkin;
          const float smooth = 1.0f - smoothstep(1.9f - 0.6f * hairZone, 3.2f - 1.0f * hairZone, busy);
          weight->data()[i] = chroma * bright * smooth * region * keep;
        }
    });
    if (!done) return cancelledError();
  }
  // The weight, aligned to the photo's own edges.
  std::array<Plane, 3> cA;
  for (auto& p : cA) {
    auto q = Plane::allocate(lw, lh);
    if (!q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for Skin Tone");
    p = std::move(q).value();
  }
  auto cB = Plane::allocate(lw, lh);
  if (!cB) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for Skin Tone");
  const int32_t gr = std::max<int32_t>(2, std::min(lw, lh) / 160);
  if (Error e = guidedCoefficientsColor({&gam[0], &gam[1], &gam[2]}, *weight, gr, 2e-3f, cA, *cB, token); e.status != PP_OK) return e;

  // 3. Per pixel.
  const float inv = 1.0f / static_cast<float>(f);
  std::vector<double> rowCov(static_cast<size_t>(H), 0.0);
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* row = image.row(y);
      const float ly = (static_cast<float>(y) + 0.5f) * inv - 0.5f;
      double cov = 0;
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* q = row + x * 4;
        const float lx = (static_cast<float>(x) + 0.5f) * inv - 0.5f;
        const float R = q[o.r] / 255.0f, G = q[o.g] / 255.0f, B = q[o.b] / 255.0f;
        const float wv = std::clamp(cA[0].sample(lx, ly) * R + cA[1].sample(lx, ly) * G + cA[2].sample(lx, ly) * B + cB->sample(lx, ly), 0.0f, 1.0f);
        if (wv > 0.5f) cov += 1.0;
        const float s = wv * strength;
        if (s <= 0.002f) continue;
        // Brightness first, in linear light (hue kept)…
        const float m = std::exp(logGain * s);
        const float R1 = rollOff(toGamma(std::min(toLinear(R) * m, 1.0f)), R, 0.9f);
        const float G1 = rollOff(toGamma(std::min(toLinear(G) * m, 1.0f)), G, 0.9f);
        const float B1 = rollOff(toGamma(std::min(toLinear(B) * m, 1.0f)), B, 0.9f);
        // …then the undertone at the new brightness: the person's own
        // variation, a little calmer, around the shade's chroma.
        float yv, cb, cr;
        rgbToYcc(R1, G1, B1, yv, cb, cr);
        const float k = yv + 0.05f;
        float ncb = cb / k, ncr = cr / k;
        // Shine stays nearly white: the undertone eases off on highlights
        // well above the skin's own brightness (else the skin looks metallic).
        const float shine = smoothstep(1.3f, 2.4f, yv / std::max(skinYOut, 0.02f));
        const float u = s * (1.0f - 0.65f * shine);
        ncb = sNcb + (ncb - sNcb) * (1.0f - 0.2f * u) + dNcb * u;
        ncr = sNcr + (ncr - sNcr) * (1.0f - 0.2f * u) + dNcr * u;
        float r2, g2, b2;
        yccToRgb(yv, ncb * k, ncr * k, r2, g2, b2);
        // Kept inside what the screen can show by easing the chroma (never
        // by clipping, which would flatten the skin's texture).
        float fit = 1.0f;
        for (float c : {r2, g2, b2}) {
          if (c > 1.0f) fit = std::min(fit, (1.0f - yv) / std::max(c - yv, 1e-5f));
          if (c < 0.0f) fit = std::min(fit, yv / std::max(yv - c, 1e-5f));
        }
        fit = std::clamp(fit, 0.0f, 1.0f);
        r2 = yv + (r2 - yv) * fit, g2 = yv + (g2 - yv) * fit, b2 = yv + (b2 - yv) * fit;
        float lr = toLinear(r2), lg = toLinear(g2), lb = toLinear(b2);
        if (glow > 0.0f) {
          // A luminous sheen on the skin's own highlights.
          const float ll = 0.2126f * lr + 0.7152f * lg + 0.0722f * lb;
          const float lift = 0.12f * glow * s * smoothstep(skinLin * gain * 0.9f, skinLin * gain * 2.4f + 0.02f, ll);
          lr += lift * (1.0f - lr), lg += lift * (1.0f - lg), lb += lift * (0.96f - lb);
        }
        q[o.r] = toByte(rollOff(toGamma(lr), R, 0.94f));
        q[o.g] = toByte(rollOff(toGamma(lg), G, 0.94f));
        q[o.b] = toByte(rollOff(toGamma(lb), B, 0.94f));
      }
      rowCov[static_cast<size_t>(y)] = cov;
    }
  });
  if (!done) return cancelledError();
  if (report != nullptr) {
    double t = 0;
    for (double v : rowCov) t += v;
    report->skinCoverage = static_cast<float>(t / (static_cast<double>(W) * H));
  }
  return ok();
}

}  // namespace photoplus::skintone
