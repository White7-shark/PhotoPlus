// The beauty engine's grade (lighting, background, colour) and compositor.
//
// The grade is one per-pixel function (`shade`) of:
//   - the pixel (gamma sRGB) and its position,
//   - the subject alpha a (refined to the photo's edges),
//   - one low-resolution blurred copy (linear RGB + blurred alpha, sigma =
//     1.5% of the short side), used for soft focus, bloom, local shadow
//     recovery and the rim light's edge band and direction,
//   - the scene (the person's own skin colour, the subject's extent, the key
//     light's side), and the Grade parameters.
// The live camera shader (android assets beauty/composite.frag) evaluates
// the same function on the GPU, so the preview matches the saved photo's
// look. Keep the two in step.
#include "photoplus/beauty.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "../portrait/common.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::beauty {

using namespace portrait::detail;

namespace {

constexpr float kLowSigma = 0.015f;  // low-pass sigma, fraction of the short side
constexpr float kPi = 3.14159265f;

struct V3 {
  float r, g, b;
};
inline V3 operator+(V3 a, V3 b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline V3 operator-(V3 a, V3 b) { return {a.r - b.r, a.g - b.g, a.b - b.b}; }
inline V3 operator*(V3 a, V3 b) { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
inline V3 operator*(V3 a, float k) { return {a.r * k, a.g * k, a.b * k}; }
inline V3 mix(V3 a, V3 b, float t) { return a + (b - a) * t; }
inline float lum(V3 c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
// Exposure gain with a highlight shoulder (gains > 1 never clip).
inline float gainOf(float v, float g) { return g > 1.0f ? v * g / (1.0f + v * (g - 1.0f)) : v * g; }
inline V3 gainOf(V3 c, float g) { return {gainOf(c.r, g), gainOf(c.g, g), gainOf(c.b, g)}; }
inline V3 linearOf(const Color& c) { return {toLinear(c[0]), toLinear(c[1]), toLinear(c[2])}; }
// A light's colour with unit luminance (so it tints without changing exposure).
inline V3 unitLight(const Color& c) {
  V3 l = linearOf(c);
  const float y = std::max(lum(l), 1e-3f);
  return l * (1.0f / y);
}
inline float chromaAngle(const Color& c) {
  float y, cb, cr;
  rgbToYcc(c[0], c[1], c[2], y, cb, cr);
  return std::atan2(cr, cb);
}
inline std::array<float, 2> chromaDir(const Color& c) {
  float y, cb, cr;
  rgbToYcc(c[0], c[1], c[2], y, cb, cr);
  const float n = std::hypot(cb, cr);
  return n > 1e-4f ? std::array<float, 2>{cb / n, cr / n} : std::array<float, 2>{0.0f, 0.0f};
}

// Everything per-photo the per-pixel function needs (GPU: uniforms).
struct Uniforms {
  Grade g;
  Scene scene;
  float aspect = 1.0f;  // width / height
  V3 wb{1, 1, 1}, keyColor{1, 1, 1}, rimLin{1, 1, 1}, tintUnit{1, 1, 1}, glowLin{1, 1, 1}, bgWarm{1, 1, 1};
  std::array<float, 2> sh{0, 0}, hl{0, 0};
  float bandAngle = 0, bandWidth = 0, bandShift = 0;
  std::array<float, 2> rimDir{0, -1};
  float skinNCb = 0, skinNCr = 0, skinSigma = 0.12f;
};

Uniforms uniformsFor(const Grade& g, const Scene& scene, int32_t W, int32_t H) {
  Uniforms u;
  u.g = g;
  u.scene = scene;
  u.aspect = static_cast<float>(W) / static_cast<float>(H);
  V3 wb{1.0f + 0.18f * g.temperature, 1.0f - 0.12f * g.tint, 1.0f - 0.22f * g.temperature};
  u.wb = wb * (1.0f / std::max(lum(wb), 1e-3f));
  V3 key = g.lightWarmth >= 0 ? V3{1.1f, 1.0f, 0.82f} : V3{0.9f, 1.0f, 1.14f};
  u.keyColor = key * (1.0f / lum(key));
  u.rimLin = unitLight(g.rimColor);
  V3 bw = g.bgWarmth >= 0 ? V3{1.16f, 1.0f, 0.78f} : V3{0.86f, 1.0f, 1.2f};
  u.bgWarm = bw * (1.0f / lum(bw));
  u.tintUnit = unitLight(g.bgTint);
  u.glowLin = linearOf(g.bgGlowColor);
  const auto shd = chromaDir(g.shadowColor), hld = chromaDir(g.highlightColor);
  u.sh = {g.shadowAmount * 0.06f * shd[0], g.shadowAmount * 0.06f * shd[1]};
  u.hl = {g.highlightAmount * 0.06f * hld[0], g.highlightAmount * 0.06f * hld[1]};
  u.bandAngle = chromaAngle(g.bandColor);
  u.bandWidth = g.bandWidth * kPi / 180.0f;
  u.bandShift = g.bandHueShift * kPi / 180.0f;
  // Rim light comes from behind, opposite the key, a little from above.
  const float rx = -scene.lightAngle, ry = -0.6f;
  const float rn = std::hypot(rx, ry);
  u.rimDir = {rx / rn, ry / rn};
  const float yk = scene.skinY + 0.05f;
  u.skinNCb = scene.skinCb / yk;
  u.skinNCr = scene.skinCr / yk;
  u.skinSigma = std::max(0.1f, 2.5f * scene.skinSpread);
  return u;
}

// A value pushed above max(original, knee) approaches `ceiling` smoothly;
// values that were already above the ceiling are never raised further.
inline float rollOff(float v, float original, float knee, float ceiling) {
  if (original >= ceiling) return std::min(v, original);
  const float hi = std::max(original, knee);
  if (v <= hi) return v;
  return hi + (ceiling - hi) * std::tanh((v - hi) / (ceiling - hi));
}

// Inputs at one pixel besides its colour.
struct Px {
  float x, y;   // 0..1
  float a;      // subject alpha
  V3 low;       // blurred linear RGB
  float lowA;   // blurred alpha
  float gx, gy; // gradient of the blurred alpha (per unit of x, y)
};

inline float skinWeight(float r, float g, float b, float a, const Uniforms& u) {
  if (!u.scene.hasSkin) return 0.0f;
  float y, cb, cr;
  rgbToYcc(r, g, b, y, cb, cr);
  const float k = 1.0f / (y + 0.05f);
  const float dc = std::hypot(cb * k - u.skinNCb, cr * k - u.skinNCr) / u.skinSigma;
  return a * std::exp(-0.5f * dc * dc) * smoothstep(0.04f, 0.12f, y) * (1.0f - smoothstep(0.96f, 1.0f, y));
}

// The grade at one pixel: gamma RGB in, gamma RGB out. Mirrored in
// beauty/composite.frag (shadeGrade).
inline V3 shade(V3 rgb, const Px& p, const Uniforms& u, float* skinOut) {
  const Grade& g = u.g;
  const float s = skinWeight(rgb.r, rgb.g, rgb.b, p.a, u);
  if (skinOut != nullptr) *skinOut = s;
  const V3 orig{toLinear(rgb.r), toLinear(rgb.g), toLinear(rgb.b)};
  V3 lin = orig;
  const float a = p.a, wbg = 1.0f - a;
  // ---- D: lighting --------------------------------------------------------
  if (g.softFocus > 0.0f) {
    lin = mix(lin, p.low, 0.35f * g.softFocus);
    lin = lin + p.low * (V3{1, 1, 1} - lin) * (0.3f * g.softFocus);
  }
  const float lowY = lum(p.low);
  if (g.bloom > 0.0f) {
    const float h = std::max(lowY - 0.45f, 0.0f) / 0.55f;
    lin = lin + p.low * (V3{1, 1, 1} - lin) * (g.bloom * 0.6f * h * h);
  }
  if (g.shadowLift > 0.0f && a > 0.0f) {
    // Shadows on the person open up (the room has its own light control).
    const float dark = 1.0f - smoothstep(0.02f, 0.22f, lowY);
    const float pxDark = 1.0f - smoothstep(0.03f, 0.35f, lum(lin));
    lin = lin * (1.0f + g.shadowLift * 1.5f * dark * pxDark * a);
  }
  if (g.subjectLight != 0.0f && a > 0.0f) {
    const float side = std::clamp((p.x - u.scene.subjectX) / (0.5f * u.scene.subjectW + 1e-3f), -1.5f, 1.5f) * u.scene.lightAngle;
    const float gs = std::exp2(g.subjectLight * 0.5f * (1.0f + 0.3f * side));
    lin = gainOf(lin, mixf(1.0f, gs, a));
  }
  if (g.lightWarmth != 0.0f && a > 0.0f) {
    // The key light's colour warms the scene's light, not the skin's own tone.
    const float k = a * 0.6f * std::fabs(g.lightWarmth) * (1.0f - 0.7f * s * g.skinProtect);
    lin = lin * mix(V3{1, 1, 1}, u.keyColor, k);
  }
  if (g.rim > 0.0f && a > 0.0f) {
    const float band = a * smoothstep(0.15f, 0.7f, 1.0f - p.lowA);
    float dirW = 0.35f;
    const float gl = std::hypot(p.gx, p.gy);
    if (gl > 1e-3f) dirW = std::clamp(0.35f + 0.65f * (-p.gx * u.rimDir[0] - p.gy * u.rimDir[1]) / gl, 0.0f, 1.0f);
    // On skin the rim is softer and its colour neutral (a coloured edge light
    // reads as a cast on the face, deep skin most of all).
    const float sp = s * g.skinProtect;
    lin = lin + mix(u.rimLin, V3{1, 1, 1}, sp) * (g.rim * 0.35f * band * dirW * (1.0f - 0.5f * sp));
  }
  // ---- C: background ------------------------------------------------------
  if (wbg > 0.0f) {
    if (g.bgExposure != 0.0f) lin = mix(lin, gainOf(lin, std::exp2(g.bgExposure)), wbg);
    if (g.bgSaturation != 0.0f) {
      const float Y = lum(lin);
      const float k = 1.0f + g.bgSaturation * (g.bgSaturation > 0.0f ? 0.6f : 1.0f);
      V3 c = V3{Y, Y, Y} + (lin - V3{Y, Y, Y}) * k;
      c = {std::max(c.r, 0.0f), std::max(c.g, 0.0f), std::max(c.b, 0.0f)};
      lin = mix(lin, c, wbg);
    }
    if (g.bgTintAmount > 0.0f) lin = mix(lin, u.tintUnit * lum(lin), g.bgTintAmount * 0.7f * wbg);
    if (g.bgWarmth != 0.0f) lin = lin * mix(V3{1, 1, 1}, u.bgWarm, wbg * std::fabs(g.bgWarmth));
  }
  const float diag = std::sqrt(u.aspect * u.aspect + 1.0f);
  if (g.bgGlow > 0.0f) {
    const float dx = (p.x - g.bgGlowX) * u.aspect, dy = p.y - g.bgGlowY;
    const float d = std::sqrt(dx * dx + dy * dy) / (std::max(g.bgGlowSize, 0.02f) * diag);
    lin = lin + u.glowLin * (g.bgGlow * 0.55f * std::exp(-0.5f * d * d) * (wbg + 0.2f * a));
  }
  if (g.bgHaze > 0.0f) lin = lin + (u.glowLin * 0.7f - lin) * (g.bgHaze * 0.22f * wbg);
  if (g.bgVignette > 0.0f) {
    const float dx = (p.x - 0.5f) * u.aspect, dy = p.y - 0.5f;
    const float vd = std::sqrt(dx * dx + dy * dy) / (0.5f * diag);
    lin = lin * (1.0f - g.bgVignette * 0.6f * smoothstep(0.45f, 1.05f, vd) * (0.25f + 0.75f * wbg));
  }
  // ---- E: white balance, kept off skin -------------------------------------
  lin = mix(lin * u.wb, lin, s * g.skinProtect);
  // Added light rolls off below white instead of clipping (unchanged
  // pixels stay put).
  auto roll = [](float v, float o) { return rollOff(v, o, 0.8f, 0.99f); };
  lin = {roll(lin.r, orig.r), roll(lin.g, orig.g), roll(lin.b, orig.b)};
  // ---- E: tone and colour (gamma YCbCr) ------------------------------------
  float y, cb, cr;
  rgbToYcc(toGamma(lin.r), toGamma(lin.g), toGamma(lin.b), y, cb, cr);
  const float y0 = y;
  if (g.contrast != 0.0f) {
    const float d = y - 0.5f;
    y += 0.45f * g.contrast * d * (1.0f - 4.0f * d * d);
  }
  if (g.blacks > 0.0f) {
    y = g.blacks * 0.07f + y * (1.0f - g.blacks * 0.07f);
  } else if (g.blacks < 0.0f) {
    const float k = -g.blacks * 0.05f;
    y = std::max(0.0f, (y - k) / (1.0f - k));
  }
  if (g.highlights > 0.0f) y += g.highlights * 0.18f * smoothstep(0.45f, 1.0f, y) * (1.0f - y);
  else if (g.highlights < 0.0f) y -= -g.highlights * 0.25f * smoothstep(0.6f, 1.0f, y) * (y - 0.6f);
  if (g.skinLuminance > 0.0f) {
    const float k = g.skinLuminance * 0.3f * s;
    y = y * (1.0f + k) / (1.0f + k * y);
  } else if (g.skinLuminance < 0.0f) {
    y *= 1.0f + 0.15f * g.skinLuminance * s;
  }
  if (g.skinSheen > 0.0f) y += g.skinSheen * 0.35f * s * std::max(0.0f, y - u.scene.skinY) * (1.0f - y);
  y = std::clamp(y, 0.0f, 1.0f);
  const float k = std::clamp((y + 0.02f) / (y0 + 0.02f), 0.0f, 1.6f);
  cb *= k;
  cr *= k;
  const float sat = std::hypot(cb, cr);
  float sf = (1.0f + g.saturation * 0.5f) * (1.0f + g.vibrance * 0.7f * (1.0f - smoothstep(0.0f, 0.22f, sat)));
  sf = mixf(sf, 1.0f + (sf - 1.0f) * 0.4f, s);  // skin keeps most of its own colour
  cb *= sf;
  cr *= sf;
  const float prot = 1.0f - s * g.skinProtect;
  if (u.bandWidth > 0.0f) {
    float dh = std::atan2(cr, cb) - u.bandAngle;
    dh = std::remainder(dh, 2.0f * kPi);
    const float w = (1.0f - smoothstep(0.5f * u.bandWidth, u.bandWidth, std::fabs(dh))) * smoothstep(0.02f, 0.06f, sat) * prot;
    if (w > 0.0f) {
      const float ang = u.bandShift * w, c = std::cos(ang), sn = std::sin(ang), m = 1.0f + g.bandSaturation * w;
      const float ncb = (cb * c - cr * sn) * m, ncr = (cb * sn + cr * c) * m;
      cb = ncb;
      cr = ncr;
    }
  }
  const float wsh = 1.0f - smoothstep(0.08f, 0.5f, y), whl = smoothstep(0.5f, 0.95f, y);
  cb += (u.sh[0] * wsh + u.hl[0] * whl) * prot;
  cr += (u.sh[1] * wsh + u.hl[1] * whl) * prot;
  cb += s * (-0.014f * g.skinWarmth + 0.002f * g.skinRosy);
  cr += s * (0.008f * g.skinWarmth + 0.018f * g.skinRosy);
  V3 out;
  yccToRgb(y, cb, cr, out.r, out.g, out.b);
  // Nothing new reaches 255: brightened channels approach 0.995 at most.
  return {std::max(rollOff(out.r, rgb.r, 0.97f, 0.995f), 0.0f), std::max(rollOff(out.g, rgb.g, 0.97f, 0.995f), 0.0f),
          std::max(rollOff(out.b, rgb.b, 0.97f, 0.995f), 0.0f)};
}

float clipped(const ImageBuffer& img) {
  const Rgb o = offsets(img);
  int64_t n = 0;
  for (int32_t y = 0; y < img.height(); y += 2) {
    const uint8_t* row = img.row(y);
    for (int32_t x = 0; x < img.width(); x += 2) {
      const uint8_t* q = row + x * 4;
      if (q[o.r] == 255 || q[o.g] == 255 || q[o.b] == 255) ++n;
    }
  }
  const int64_t total = static_cast<int64_t>((img.height() + 1) / 2) * ((img.width() + 1) / 2);
  return total ? static_cast<float>(static_cast<double>(n) / static_cast<double>(total)) : 0.0f;
}

}  // namespace

bool active(const Grade& g) {
  for (float v : {g.temperature, g.tint, g.contrast, g.blacks, g.highlights, g.saturation, g.vibrance, g.shadowAmount,
                  g.highlightAmount, g.skinWarmth, g.skinRosy, g.skinLuminance, g.subjectLight, g.lightWarmth,
                  g.shadowLift, g.rim, g.bloom, g.softFocus, g.skinSheen, g.bgExposure, g.bgSaturation, g.bgTintAmount,
                  g.bgGlow, g.bgVignette, g.bgHaze, g.bgWarmth}) {
    if (v != 0.0f) return true;
  }
  return g.bandWidth > 0.0f && (g.bandHueShift != 0.0f || g.bandSaturation != 0.0f);
}

Scene measure(const ImageBuffer& image, const ImageBuffer* subject, const ImageBuffer* faces, const ImageBuffer* protect,
              const std::vector<portrait::FaceLandmarks>& landmarks) {
  Scene sc;
  if (!isColour(image) || image.width() < 2 || image.height() < 2) return sc;
  const int32_t W = image.width(), H = image.height();
  const Rgb o = offsets(image);
  auto maskAt = [&](const ImageBuffer* m, int32_t x, int32_t y) -> float {
    if (m == nullptr || m->empty()) return -1.0f;
    const int32_t mx = std::clamp(static_cast<int32_t>((static_cast<int64_t>(x) * m->width()) / W), 0, m->width() - 1);
    const int32_t my = std::clamp(static_cast<int32_t>((static_cast<int64_t>(y) * m->height()) / H), 0, m->height() - 1);
    return static_cast<float>(m->row(my)[mx * bytesPerPixel(m->format())]) / 255.0f;
  };
  const int32_t step = std::max<int32_t>(1, std::max(W, H) / 400);
  std::vector<float> ys, ncb, ncr;
  double sx = 0, sxx = 0, sw = 0;
  for (int32_t y = 0; y < H; y += step) {
    const uint8_t* row = image.row(y);
    for (int32_t x = 0; x < W; x += step) {
      const float a = maskAt(subject, x, y);
      if (a > 0.5f) {
        const double fx = (static_cast<double>(x) + 0.5) / W;
        sx += fx;
        sxx += fx * fx;
        sw += 1.0;
      }
      const float f = maskAt(faces, x, y);
      if (f < 0.6f || maskAt(protect, x, y) > 0.3f) continue;
      const uint8_t* q = row + x * 4;
      float yv, cb, cr;
      rgbToYcc(q[o.r] / 255.0f, q[o.g] / 255.0f, q[o.b] / 255.0f, yv, cb, cr);
      if (yv < 0.05f || yv > 0.97f) continue;
      ys.push_back(yv);
      ncb.push_back(cb / (yv + 0.05f));
      ncr.push_back(cr / (yv + 0.05f));
    }
  }
  if (sw > 0) {
    const double m = sx / sw, var = std::max(0.0, sxx / sw - m * m);
    sc.subjectX = static_cast<float>(m);
    sc.subjectW = std::clamp(static_cast<float>(4.0 * std::sqrt(var)), 0.1f, 1.0f);  // ±2 sigma
  }
  if (ys.size() >= 30) {
    auto med = [](std::vector<float> v) {
      std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
      return v[v.size() / 2];
    };
    sc.hasSkin = true;
    sc.skinY = med(ys);
    const float mcb = med(ncb), mcr = med(ncr);
    std::vector<float> dev(ncb.size());
    for (size_t i = 0; i < ncb.size(); ++i) dev[i] = std::hypot(ncb[i] - mcb, ncr[i] - mcr);
    sc.skinSpread = 1.4826f * med(dev);
    std::vector<float> ydev(ys.size());
    for (size_t i = 0; i < ys.size(); ++i) ydev[i] = std::fabs(ys[i] - sc.skinY);
    sc.skinYSpread = std::max(0.01f, 1.4826f * med(ydev));
    sc.skinCb = mcb * (sc.skinY + 0.05f);
    sc.skinCr = mcr * (sc.skinY + 0.05f);
  }
  if (!landmarks.empty()) {
    // The largest face decides the light.
    size_t best = 0;
    float bestW = -1.0f;
    for (size_t i = 0; i < landmarks.size(); ++i) {
      float lo = 1e9f, hi = -1e9f;
      for (int k = 0; k < 36; ++k) lo = std::min(lo, landmarks[i].points[2 * k]), hi = std::max(hi, landmarks[i].points[2 * k]);
      if (hi - lo > bestW) bestW = hi - lo, best = i;
    }
    sc.lightAngle = portrait::estimateLightAngle(image, landmarks[best]);
  }
  return sc;
}

Error grade(ImageBuffer& image, const ImageBuffer* subject, const ImageBuffer* faces, const Scene& scene, const Grade& gIn,
            const CancellationToken* token, GradeReport* report) {
  ScopedTimer timer("beauty.grade");
  if (!isColour(image)) return makeError(PP_ERROR_INVALID_ARGUMENT, "The beauty grade needs an RGBA image");
  if (image.width() < 16 || image.height() < 16) return makeError(PP_ERROR_INVALID_ARGUMENT, "Image too small");
  Grade g = gIn;
  auto clampTo = [](float& v, float lo, float hi) {
    if (!std::isfinite(v)) return false;
    v = std::clamp(v, lo, hi);
    return true;
  };
  bool okv = true;
  for (float* v : {&g.temperature, &g.tint, &g.contrast, &g.blacks, &g.highlights, &g.saturation, &g.vibrance,
                   &g.bandSaturation, &g.skinWarmth, &g.skinLuminance, &g.subjectLight, &g.lightWarmth, &g.bgExposure,
                   &g.bgSaturation, &g.bgWarmth}) okv = okv && clampTo(*v, -1.0f, 1.0f);
  for (float* v : {&g.shadowAmount, &g.highlightAmount, &g.skinProtect, &g.skinRosy, &g.shadowLift, &g.rim, &g.bloom,
                   &g.softFocus, &g.skinSheen, &g.bgTintAmount, &g.bgGlow, &g.bgVignette, &g.bgHaze, &g.bgGlowX,
                   &g.bgGlowY}) okv = okv && clampTo(*v, 0.0f, 1.0f);
  okv = okv && clampTo(g.bandWidth, 0.0f, 90.0f) && clampTo(g.bandHueShift, -60.0f, 60.0f) && clampTo(g.bgGlowSize, 0.02f, 1.5f);
  for (Color* c : {&g.shadowColor, &g.highlightColor, &g.bandColor, &g.rimColor, &g.bgTint, &g.bgGlowColor})
    for (float& v : *c) okv = okv && clampTo(v, 0.0f, 1.0f);
  if (!okv) return makeError(PP_ERROR_INVALID_ARGUMENT, "Non-finite beauty parameter");
  const int32_t W = image.width(), H = image.height();
  if (report != nullptr) {
    *report = GradeReport{};
    report->clippedBefore = clipped(image);
  }
  if (!active(g)) {
    if (report != nullptr) report->clippedAfter = report->clippedBefore;
    return ok();
  }
  const Uniforms u = uniformsFor(g, scene, W, H);
  const Rgb o = offsets(image);
  const float shortSide = static_cast<float>(std::min(W, H));

  // Subject alpha at a working resolution (~1024 px), aligned to the photo's
  // edges, then sampled bilinearly per pixel.
  std::optional<Plane> alpha;
  if (subject != nullptr && !subject->empty()) {
    auto coarse = maskPlane(*subject);
    if (!coarse) return coarse.error();
    if (faces != nullptr && !faces->empty()) {
      // Faces are always subject (landmarks are more reliable than
      // segmentation on a lit cheek against a pale wall).
      auto fm = maskPlane(*faces);
      if (!fm) return fm.error();
      auto fr = resizeBilinear(*fm, coarse->width(), coarse->height(), token);
      if (!fr) return fr.error();
      for (size_t i = 0; i < coarse->size(); ++i) coarse->data()[i] = std::max(coarse->data()[i], fr->data()[i]);
    }
    const int32_t f = std::max<int32_t>(1, (std::max(W, H) + 1023) / 1024);
    auto Y = lumaOf(image, token);
    if (!Y) return Y.error();
    auto guide = boxDown(*Y, f, token);
    if (!guide) return guide.error();
    auto up = resizeBilinear(*coarse, guide->width(), guide->height(), token);
    if (!up) return up.error();
    const auto r = std::max<int32_t>(2, static_cast<int32_t>(std::min(guide->width(), guide->height()) / 160));
    auto refined = portrait::refineMask(*up, *guide, r, 1e-3f, token);
    if (!refined) return refined.error();
    alpha = std::move(refined).value();
  }

  // Low-resolution blurred copy (linear RGB + alpha).
  const int32_t lf = std::max<int32_t>(1, static_cast<int32_t>(std::lround(shortSide / 256.0f)));
  const int32_t lw = std::max(1, (W + lf - 1) / lf), lh = std::max(1, (H + lf - 1) / lf);
  std::array<Plane, 4> low;
  for (auto& pl : low) {
    auto q = Plane::allocate(lw, lh);
    if (!q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the beauty grade");
    pl = std::move(q).value();
  }
  {
    std::array<float, 256> lut{};
    for (int i = 0; i < 256; ++i) lut[static_cast<size_t>(i)] = toLinear(static_cast<float>(i) / 255.0f);
    const float ax = alpha ? static_cast<float>(alpha->width()) / static_cast<float>(W) : 0.0f;
    const float ay = alpha ? static_cast<float>(alpha->height()) / static_cast<float>(H) : 0.0f;
    const bool done = parallelFor(lh, token, [&](int32_t begin, int32_t end) {
      for (int32_t ly = begin; ly < end; ++ly) {
        for (int32_t lx = 0; lx < lw; ++lx) {
          float sr = 0, sg = 0, sb = 0, sa = 0;
          int32_t n = 0;
          for (int32_t y = ly * lf; y < std::min(H, (ly + 1) * lf); ++y) {
            const uint8_t* row = image.row(y);
            for (int32_t x = lx * lf; x < std::min(W, (lx + 1) * lf); ++x, ++n) {
              const uint8_t* q = row + x * 4;
              sr += lut[q[o.r]];
              sg += lut[q[o.g]];
              sb += lut[q[o.b]];
              sa += alpha ? alpha->sample((static_cast<float>(x) + 0.5f) * ax - 0.5f, (static_cast<float>(y) + 0.5f) * ay - 0.5f) : 1.0f;
            }
          }
          const float inv = n ? 1.0f / static_cast<float>(n) : 0.0f;
          low[0].row(ly)[lx] = sr * inv;
          low[1].row(ly)[lx] = sg * inv;
          low[2].row(ly)[lx] = sb * inv;
          low[3].row(ly)[lx] = std::clamp(sa * inv, 0.0f, 1.0f);
        }
      }
    });
    if (!done) return cancelledError();
    const float sigma = kLowSigma * shortSide / static_cast<float>(lf);
    for (auto& pl : low) {
      auto b = Plane::allocate(lw, lh);
      if (!b) return b.error();
      if (Error e = gaussianBlur(pl, *b, sigma, token); e.status != PP_OK) return e;
      pl = std::move(b).value();
    }
  }

  // One pass.
  const float invLf = 1.0f / static_cast<float>(lf);
  const float ax = alpha ? static_cast<float>(alpha->width()) / static_cast<float>(W) : 0.0f;
  const float ay = alpha ? static_cast<float>(alpha->height()) / static_cast<float>(H) : 0.0f;
  std::vector<double> rowA(static_cast<size_t>(H), 0.0), rowS(static_cast<size_t>(H), 0.0);
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* row = image.row(y);
      const float fy = (static_cast<float>(y) + 0.5f) * invLf - 0.5f;
      double sa = 0, ss = 0;
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* q = row + x * 4;
        const float fx = (static_cast<float>(x) + 0.5f) * invLf - 0.5f;
        Px p;
        p.x = (static_cast<float>(x) + 0.5f) / static_cast<float>(W);
        p.y = (static_cast<float>(y) + 0.5f) / static_cast<float>(H);
        p.a = alpha ? std::clamp(alpha->sample((static_cast<float>(x) + 0.5f) * ax - 0.5f, (static_cast<float>(y) + 0.5f) * ay - 0.5f), 0.0f, 1.0f)
                    : 1.0f;
        p.low = {low[0].sample(fx, fy), low[1].sample(fx, fy), low[2].sample(fx, fy)};
        p.lowA = low[3].sample(fx, fy);
        // Gradient per unit of x and y (one low-res cell each way).
        p.gx = (low[3].sample(fx + 1.0f, fy) - low[3].sample(fx - 1.0f, fy)) * 0.5f * static_cast<float>(lw);
        p.gy = (low[3].sample(fx, fy + 1.0f) - low[3].sample(fx, fy - 1.0f)) * 0.5f * static_cast<float>(lh);
        float s = 0.0f;
        const V3 c = shade({q[o.r] / 255.0f, q[o.g] / 255.0f, q[o.b] / 255.0f}, p, u, &s);
        q[o.r] = toByte(c.r);
        q[o.g] = toByte(c.g);
        q[o.b] = toByte(c.b);
        sa += p.a;
        ss += s;
      }
      rowA[static_cast<size_t>(y)] = sa;
      rowS[static_cast<size_t>(y)] = ss;
    }
  });
  if (!done) return cancelledError();
  if (report != nullptr) {
    double ta = 0, ts = 0;
    for (size_t i = 0; i < rowA.size(); ++i) ta += rowA[i], ts += rowS[i];
    const double n = static_cast<double>(W) * H;
    report->subjectCoverage = alpha ? static_cast<float>(ta / n) : 1.0f;
    report->skinCoverage = static_cast<float>(ts / n);
    report->clippedAfter = clipped(image);
  }
  return ok();
}

Recipe::Recipe() {
  portrait.blur = 0;
  portrait.retouch = 0;
  portrait.brighten = 0;
  portrait.pop = 0;
}

Error apply(ImageBuffer& image, const ImageBuffer* subject, const ImageBuffer* faces, const ImageBuffer* protect,
            const ImageBuffer* depth, const std::vector<portrait::FaceLandmarks>& landmarks, const Recipe& recipe,
            const CancellationToken* token, Report* report) {
  ScopedTimer timer("beauty.apply");
  if (!isColour(image)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Beauty needs an RGBA image");
  if (image.width() < 16 || image.height() < 16) return makeError(PP_ERROR_INVALID_ARGUMENT, "Image too small");
  Report rep;
  // The key light's side is judged on the photo as taken.
  float lightAngle = recipe.face.lightAngle;
  if (recipe.autoLight && !landmarks.empty()) lightAngle = measure(image, nullptr, nullptr, nullptr, landmarks).lightAngle;
  rep.lightAngle = lightAngle;

  // A: skin (and Portrait mode's blur, background and edges).
  const portrait::Params& pp = recipe.portrait;
  const bool hasSubject = subject != nullptr && !subject->empty();
  const bool portraitWork = pp.skinWork() || pp.pop > 0.0f ||
                            (hasSubject && (pp.blur > 0.0f || pp.bgLight != 0.0f || pp.bgColor != 0.0f || pp.edge > 0.0f));
  if (portraitWork) {
    const bool skinOnly = !hasSubject;
    std::optional<ImageBuffer> whole;
    if (skinOnly) {
      auto m = ImageBuffer::allocate(2, 2, PixelFormat::Gray8);
      if (!m) return m.error();
      for (int32_t y = 0; y < 2; ++y) std::fill_n(m->row(y), 2, uint8_t{255});
      whole = std::move(m).value();
    }
    portrait::Params p = pp;
    if (skinOnly) p.blur = 0, p.bgLight = 0, p.bgColor = 0, p.edge = 0;
    if (Error e = portrait::apply(image, skinOnly ? *whole : *subject, faces, protect, depth, p, token, &rep.portrait);
        e.status != PP_OK) {
      return e;
    }
    rep.layers |= 1;
  }
  // B: face.
  if (recipe.face.any() && !landmarks.empty()) {
    portrait::FaceParams fp = recipe.face;
    fp.lightAngle = lightAngle;
    if (Error e = portrait::adjustFaces(image, landmarks, fp, token, &rep.face); e.status != PP_OK) return e;
    rep.layers |= 2;
  }
  // C, D, E.
  if (active(recipe.grade)) {
    Scene sc = measure(image, hasSubject ? subject : nullptr, faces, protect, {});
    sc.lightAngle = lightAngle;
    if (Error e = grade(image, hasSubject ? subject : nullptr, faces, sc, recipe.grade, token, &rep.grade); e.status != PP_OK) return e;
    rep.layers |= 4;
  }
  if (report != nullptr) *report = rep;
  return ok();
}

}  // namespace photoplus::beauty
