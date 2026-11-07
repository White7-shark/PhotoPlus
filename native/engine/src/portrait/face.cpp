// Face-precise portrait adjustments, driven by facial landmarks (Phase 5).
//
// Masks for the eyes, mouth, lips, under-eye areas and skin are drawn from
// the landmarks at a face-sized working resolution (so a 4000-pixel selfie
// costs no more than a small face), feathered, and applied per pixel at full
// resolution. Every change is relative to the person's own measurements:
// the eye whites to the eye's own brightness, dark circles to the person's
// own cheek, shine to their own skin, teeth to the mouth they sit in, so
// deep and light skin are treated alike and no one's tone is shifted.
//
//   eyes       whites lifted and less red, irises given local clarity
//   teeth      only bright, unsaturated pixels inside the open mouth
//   lips       colour and definition, hue unchanged
//   under-eye  low-frequency luma and colour pulled toward the cheek's own
//   shine      highlights brighter than the skin's own spread are tamed and
//              their colour restored (texture is kept: the change is smooth)
//   slim       a small, smooth inward shift of the jaw and lower cheeks
//   face light shadows on the face opened toward its own median; a face
//              darker than its backlit surroundings lifted (in linear light)
//   studio     a soft key light shaped by the face (an ellipsoid model),
//              from the chosen side
//   blush      colour on the cheek apples, scaled to the person's own tone
//   contour    soft shading under the cheekbones, at the temples and jaw
//   highlight  soft light on the nose bridge, forehead, cheekbone tops, chin
//
// Blush, contour and highlight are analytic functions of the face frame
// (u across, v down, in face half-widths), which the live camera shader
// evaluates the same way.
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "common.hpp"
#include "photoplus/face.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::portrait {

using namespace detail;

namespace {

using Pt = std::array<float, 2>;

struct Rect {
  int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  int32_t w() const { return x1 - x0; }
  int32_t h() const { return y1 - y0; }
};

std::vector<Pt> range(const std::vector<Pt>& p, int begin, int count) {
  return {p.begin() + begin, p.begin() + begin + count};
}

// Joins two contours into one closed outline whatever their direction.
std::vector<Pt> joined(std::vector<Pt> a, std::vector<Pt> b) {
  auto d2 = [](const Pt& u, const Pt& v) { return (u[0] - v[0]) * (u[0] - v[0]) + (u[1] - v[1]) * (u[1] - v[1]); };
  if (d2(a.back(), b.back()) < d2(a.back(), b.front())) std::reverse(b.begin(), b.end());
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

Pt centre(const std::vector<Pt>& p) {
  Pt c{0, 0};
  for (const auto& q : p) c[0] += q[0], c[1] += q[1];
  const auto n = static_cast<float>(std::max<size_t>(1, p.size()));
  return {c[0] / n, c[1] / n};
}

float area(const std::vector<Pt>& p) {
  double s = 0;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) s += static_cast<double>(p[j][0]) * p[i][1] - static_cast<double>(p[i][0]) * p[j][1];
  return static_cast<float>(std::fabs(s) * 0.5);
}

std::vector<Pt> ellipse(Pt c, Pt u, float ru, float rv) {
  std::vector<Pt> out;
  const Pt v{-u[1], u[0]};
  for (int i = 0; i < 28; ++i) {
    const float t = 6.2831853f * static_cast<float>(i) / 28.0f;
    const float a = ru * std::cos(t), b = rv * std::sin(t);
    out.push_back({c[0] + a * u[0] + b * v[0], c[1] + a * u[1] + b * v[1]});
  }
  return out;
}

// Anti-aliased polygon fill (4 sub-scanlines, exact horizontal coverage),
// max-combined into `m`. Points are in grid cells (cell j spans [j, j+1)).
void fillPolygon(Plane& m, const std::vector<Pt>& poly) {
  if (poly.size() < 3) return;
  float ymin = poly[0][1], ymax = poly[0][1];
  for (const auto& p : poly) ymin = std::min(ymin, p[1]), ymax = std::max(ymax, p[1]);
  const int32_t r0 = std::max(0, static_cast<int32_t>(std::floor(ymin)));
  const int32_t r1 = std::min(m.height() - 1, static_cast<int32_t>(std::ceil(ymax)));
  std::vector<float> acc(static_cast<size_t>(m.width()));
  std::vector<float> xs;
  for (int32_t y = r0; y <= r1; ++y) {
    std::fill(acc.begin(), acc.end(), 0.0f);
    for (int s = 0; s < 4; ++s) {
      const float yy = static_cast<float>(y) + (static_cast<float>(s) + 0.5f) * 0.25f;
      xs.clear();
      for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Pt& a = poly[j];
        const Pt& b = poly[i];
        if ((a[1] <= yy) != (b[1] <= yy)) xs.push_back(a[0] + (yy - a[1]) / (b[1] - a[1]) * (b[0] - a[0]));
      }
      std::sort(xs.begin(), xs.end());
      for (size_t k = 0; k + 1 < xs.size(); k += 2) {
        const float xa = std::max(0.0f, xs[k]), xb = std::min(static_cast<float>(m.width()), xs[k + 1]);
        if (xb <= xa) continue;
        for (auto x = static_cast<int32_t>(xa); x < static_cast<int32_t>(std::ceil(xb)) && x < m.width(); ++x) {
          const float cover = std::min(xb, static_cast<float>(x + 1)) - std::max(xa, static_cast<float>(x));
          if (cover > 0.0f) acc[static_cast<size_t>(x)] += 0.25f * cover;
        }
      }
    }
    float* row = m.row(y);
    for (int32_t x = 0; x < m.width(); ++x) row[x] = std::max(row[x], std::min(1.0f, acc[static_cast<size_t>(x)]));
  }
}

float medianOf(std::vector<float>& v) {
  if (v.empty()) return 0.0f;
  auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
  std::nth_element(v.begin(), mid, v.end());
  return *mid;
}

float quantile(std::vector<float> v, float q) {
  if (v.empty()) return 0.0f;
  auto k = v.begin() + static_cast<std::ptrdiff_t>(std::clamp(q, 0.0f, 1.0f) * static_cast<float>(v.size() - 1));
  std::nth_element(v.begin(), k, v.end());
  return *k;
}

Result<Plane> blurred(const Plane& src, float sigma, const CancellationToken* token) {
  auto out = Plane::allocate(src.width(), src.height());
  if (!out) return failure(out.error());
  if (sigma < 0.3f) {
    std::copy(src.data(), src.data() + src.size(), out->data());
    return out;
  }
  if (Error e = gaussianBlur(src, *out, sigma, token); e.status != PP_OK) return failure(e);
  return out;
}

// Highlights roll off instead of clipping when a gain lifts them.
inline float applyGain(float v, float g) { return g > 1.0f ? v * g / (1.0f + v * (g - 1.0f)) : v * g; }

struct Planes {
  Plane y, cb, cr;
};

inline float bump(float u, float v, float cu, float cv, float ru, float rv) {
  const float du = (u - cu) / ru, dv = (v - cv) / rv;
  return std::exp(-0.5f * (du * du + dv * dv));
}

}  // namespace

// Make-up weights at face-frame (u, v): u across (−1 left edge … 1 right
// edge of the face), v down (−1 hairline … 1 chin). Mirrored by the live
// camera shader (beauty_composite.frag, makeupAt), so keep them in step.
MakeupWeights makeupAt(float u, float v, const MakeupFrame& f) {
  MakeupWeights m;
  const float au = std::fabs(u);
  // Cheek apples: below the eyes, beside the nose, a little outward.
  const float appleV = f.eyeV + 0.55f * (f.noseV - f.eyeV) + 0.08f;
  m.blush = bump(au, v, 0.48f, appleV, 0.2f, 0.15f);
  // Contour: hollows under the cheekbones (diagonal toward the mouth
  // corner), the temples and the sides of the jaw.
  const float hollowV = f.noseV + 0.08f;
  m.contour = std::min(1.0f, bump(au, v + 0.35f * (au - 0.7f), 0.7f, hollowV, 0.2f, 0.09f) +
                                 0.7f * bump(au, v, 0.82f, f.eyeV - 0.42f, 0.14f, 0.22f) +
                                 0.8f * smoothstep(0.62f, 0.92f, au) * smoothstep(f.eyeV + 0.1f, f.noseV + 0.1f, v));
  // Highlight: nose bridge, centre forehead, cheekbone tops, chin.
  m.highlight = std::min(1.0f, bump(u, v, 0.0f, 0.5f * (f.eyeV + f.noseV), 0.07f, 0.28f) +
                                   0.8f * bump(u, v, 0.0f, f.eyeV - 0.5f, 0.24f, 0.15f) +
                                   0.75f * bump(au, v, 0.46f, f.eyeV + 0.24f, 0.17f, 0.07f) +
                                   0.6f * bump(u, v, 0.0f, 0.86f, 0.12f, 0.07f));
  return m;
}

namespace {

Error adjustOne(ImageBuffer& image, const FaceLandmarks& lm, const FaceParams& p, const CancellationToken* token, FaceReport* report) {
  const int32_t W = image.width(), H = image.height();
  std::vector<Pt> pts(kFacePoints);
  for (int i = 0; i < kFacePoints; ++i) {
    pts[static_cast<size_t>(i)] = {lm.points[static_cast<size_t>(2 * i)] * static_cast<float>(W),
                                   lm.points[static_cast<size_t>(2 * i + 1)] * static_cast<float>(H)};
  }
  const auto oval = range(pts, kOval, 36);
  const auto eyeA = range(pts, kLeftEye, 16), eyeB = range(pts, kRightEye, 16);
  Pt ca = centre(eyeA), cb = centre(eyeB);
  if (ca[0] > cb[0]) std::swap(ca, cb);
  // Face frame: `right` along the eye line (picture left to right), `down`
  // toward the chin, so tilted heads are handled.
  float ex = cb[0] - ca[0], ey = cb[1] - ca[1];
  const float eyeDist = std::hypot(ex, ey);
  float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
  for (const auto& q : oval) minX = std::min(minX, q[0]), maxX = std::max(maxX, q[0]), minY = std::min(minY, q[1]), maxY = std::max(maxY, q[1]);
  const float faceW = maxX - minX, faceH = maxY - minY;
  const float S = std::max(faceW, faceH);
  if (eyeDist < 1e-3f || faceW < 24.0f || !std::isfinite(S)) return ok();
  ex /= eyeDist;
  ey /= eyeDist;
  const Pt right{ex, ey}, down{-ey, ex};
  const Pt fc = centre(oval);
  auto along = [&](const Pt& q, const Pt& axis) { return (q[0] - fc[0]) * axis[0] + (q[1] - fc[1]) * axis[1]; };
  float halfW = 1.0f, halfH = 1.0f;
  for (const auto& q : oval) halfW = std::max(halfW, std::fabs(along(q, right))), halfH = std::max(halfH, std::fabs(along(q, down)));

  // Working rectangle: the face plus room for the light and the slim warp.
  const float margin = 0.55f * S;
  Rect r;
  r.x0 = std::clamp(static_cast<int32_t>(std::floor(minX - margin)), 0, W);
  r.y0 = std::clamp(static_cast<int32_t>(std::floor(minY - margin)), 0, H);
  r.x1 = std::clamp(static_cast<int32_t>(std::ceil(maxX + margin)), 0, W);
  r.y1 = std::clamp(static_cast<int32_t>(std::ceil(maxY + margin)), 0, H);
  if (r.w() < 8 || r.h() < 8) return ok();
  const int32_t g = std::max<int32_t>(1, static_cast<int32_t>(std::lround(S / 200.0f)));
  const int32_t gw = (r.w() + g - 1) / g, gh = (r.h() + g - 1) / g;
  const float Sg = S / static_cast<float>(g);  // face size in grid cells
  const float featureK = lm.detailed ? 1.0f : 0.6f;  // approximate outlines: gentler
  if (report != nullptr) report->adjusted++;

  auto toGrid = [&](std::vector<Pt> poly) {
    for (auto& q : poly) {
      q[0] = (q[0] - static_cast<float>(r.x0)) / static_cast<float>(g);
      q[1] = (q[1] - static_cast<float>(r.y0)) / static_cast<float>(g);
    }
    return poly;
  };
  auto plane = [&]() -> Result<Plane> {
    auto q = Plane::allocate(gw, gh);
    if (q) std::fill(q->data(), q->data() + q->size(), 0.0f);
    return q;
  };
  auto mask = [&](const std::vector<std::vector<Pt>>& polys, float sigma) -> Result<Plane> {
    auto m = plane();
    if (!m) return m;
    for (const auto& poly : polys) fillPolygon(*m, toGrid(poly));
    return blurred(*m, sigma, token);
  };

  // ---- Masks ---------------------------------------------------------------
  const auto upperLip = joined(range(pts, kUpperLipTop, 11), range(pts, kUpperLipBottom, 9));
  const auto lowerLip = joined(range(pts, kLowerLipTop, 9), range(pts, kLowerLipBottom, 9));
  const auto mouthIn = joined(range(pts, kUpperLipBottom, 9), range(pts, kLowerLipTop, 9));
  const auto browA = joined(range(pts, kLeftBrowTop, 5), range(pts, kLeftBrowBottom, 5));
  const auto browB = joined(range(pts, kRightBrowTop, 5), range(pts, kRightBrowBottom, 5));
  const float eyeWa = std::max(4.0f, [&] {
    float lo = 1e9f, hi = -1e9f;
    for (const auto& q : eyeA) lo = std::min(lo, along(q, right)), hi = std::max(hi, along(q, right));
    return hi - lo;
  }());
  const float eyeWb = std::max(4.0f, [&] {
    float lo = 1e9f, hi = -1e9f;
    for (const auto& q : eyeB) lo = std::min(lo, along(q, right)), hi = std::max(hi, along(q, right));
    return hi - lo;
  }());
  auto below = [&](const Pt& c, float k) { return Pt{c[0] + down[0] * k, c[1] + down[1] * k}; };
  const Pt eyeCa = centre(eyeA), eyeCb = centre(eyeB);
  const std::vector<Pt> underA = ellipse(below(eyeCa, 0.42f * eyeWa), right, 0.55f * eyeWa, 0.24f * eyeWa);
  const std::vector<Pt> underB = ellipse(below(eyeCb, 0.42f * eyeWb), right, 0.55f * eyeWb, 0.24f * eyeWb);
  const std::vector<Pt> cheekA = ellipse(below(eyeCa, 1.05f * eyeWa), right, 0.45f * eyeWa, 0.28f * eyeWa);
  const std::vector<Pt> cheekB = ellipse(below(eyeCb, 1.05f * eyeWb), right, 0.45f * eyeWb, 0.28f * eyeWb);

  auto ovalM = mask({oval}, 0.012f * Sg);
  auto ovalSoft = mask({oval}, 0.06f * Sg);
  auto eyeM = mask({eyeA, eyeB}, 0.006f * Sg);
  auto mouthM = mask({mouthIn}, 0.004f * Sg);
  auto lipsM = mask({upperLip, lowerLip}, 0.006f * Sg);
  auto underM = mask({underA, underB}, 0.02f * Sg);
  auto cheekM = mask({cheekA, cheekB}, 0.0f);
  // Features never treated as skin: eyes, brows, lips, nostrils (dilated).
  auto featRaw = mask({eyeA, eyeB, browA, browB, upperLip, lowerLip, mouthIn}, 0.025f * Sg);
  for (auto* q : {&ovalM, &ovalSoft, &eyeM, &mouthM, &lipsM, &underM, &cheekM, &featRaw}) {
    if (!*q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to adjust the face");
  }
  Plane& feat = *featRaw;
  for (size_t i = 0; i < feat.size(); ++i) feat.data()[i] = std::min(1.0f, feat.data()[i] * 2.5f);
  // Under-eye areas sit right against the lower lid: only the eye opening
  // itself (barely dilated) is kept out of them.
  {
    auto lids = mask({eyeA, eyeB}, 0.01f * Sg);
    if (!lids) return lids.error();
    for (size_t i = 0; i < underM->size(); ++i) {
      underM->data()[i] = std::min(1.0f, underM->data()[i] * 1.4f) * (1.0f - std::min(1.0f, lids->data()[i] * 1.6f));
    }
  }
  if (isCancelled(token)) return cancelledError();

  // ---- Face-sized luma/chroma grid ----------------------------------------
  Planes G{};
  {
    auto y = plane(), cbp = plane(), crp = plane();
    if (!y || !cbp || !crp) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to adjust the face");
    G = {std::move(y).value(), std::move(cbp).value(), std::move(crp).value()};
  }
  const Rgb o = offsets(image);
  for (int32_t gy = 0; gy < gh; ++gy)
    for (int32_t gx = 0; gx < gw; ++gx) {
      float sy = 0, scb = 0, scr = 0;
      int32_t n = 0;
      for (int32_t yy = r.y0 + gy * g; yy < std::min(r.y1, r.y0 + (gy + 1) * g); ++yy) {
        const uint8_t* row = image.row(yy);
        for (int32_t xx = r.x0 + gx * g; xx < std::min(r.x1, r.x0 + (gx + 1) * g); ++xx, ++n) {
          const uint8_t* q = row + xx * 4;
          float yv, cbv, crv;
          rgbToYcc(q[o.r] / 255.0f, q[o.g] / 255.0f, q[o.b] / 255.0f, yv, cbv, crv);
          sy += yv, scb += cbv, scr += crv;
        }
      }
      const float inv = n ? 1.0f / static_cast<float>(n) : 0.0f;
      G.y.row(gy)[gx] = sy * inv;
      G.cb.row(gy)[gx] = scb * inv;
      G.cr.row(gy)[gx] = scr * inv;
    }

  // The person's own skin, eyes, mouth and cheeks.
  std::vector<float> skY, skCb, skCr, eyY, moY, chY, chCb, chCr;
  for (size_t i = 0; i < G.y.size(); ++i) {
    const float yv = G.y.data()[i];
    if (ovalM->data()[i] > 0.8f && feat.data()[i] < 0.1f && underM->data()[i] < 0.2f && yv > 0.04f && yv < 0.98f) {
      skY.push_back(yv);
      skCb.push_back(G.cb.data()[i]);
      skCr.push_back(G.cr.data()[i]);
    }
    if (eyeM->data()[i] > 0.6f) eyY.push_back(yv);
    if (mouthM->data()[i] > 0.6f) moY.push_back(yv);
    if (cheekM->data()[i] > 0.5f && feat.data()[i] < 0.1f && ovalM->data()[i] > 0.5f) {
      chY.push_back(yv);
      chCb.push_back(G.cb.data()[i]);
      chCr.push_back(G.cr.data()[i]);
    }
  }
  if (skY.size() < 30) return ok();  // too little visible skin to judge
  const float skinY = medianOf(skY), skinCb = medianOf(skCb), skinCr = medianOf(skCr);
  std::vector<float> dev;
  dev.reserve(skY.size());
  for (float v : skY) dev.push_back(std::fabs(v - skinY));
  const float skinMad = std::max(0.01f, 1.4826f * medianOf(dev));
  // Eyes and mouth hold two populations (whites vs iris, teeth vs shadow):
  // split them at the midpoint of their own range, not at a median that
  // the larger population would own.
  const float eyeLo = quantile(eyY, 0.1f), eyeHi = quantile(eyY, 0.9f);
  const float eyeMid = 0.5f * (eyeLo + eyeHi), eyeRange = std::max(0.05f, eyeHi - eyeLo);
  const float moLo = quantile(moY, 0.1f), moHi = quantile(moY, 0.9f);
  const float mouthMid = 0.5f * (moLo + moHi), mouthRange = std::max(0.05f, moHi - moLo);
  const bool cheekOk = chY.size() >= 8;
  const float cheekY = cheekOk ? medianOf(chY) : skinY;
  const float cheekCb = cheekOk ? medianOf(chCb) : skinCb, cheekCr = cheekOk ? medianOf(chCr) : skinCr;
  const float mouthArea = area(mouthIn), faceArea = std::max(1.0f, area(oval));
  const bool mouthOpen = mouthArea > 0.004f * faceArea && moY.size() >= 4 && moHi > 0.2f;

  auto Yb = blurred(G.y, 0.025f * Sg, token), Cbb = blurred(G.cb, 0.025f * Sg, token), Crb = blurred(G.cr, 0.025f * Sg, token);
  auto Yl = blurred(G.y, 0.09f * Sg, token);
  // Fine scales: under-eye shading (a small area) and iris detail.
  auto Yu = blurred(G.y, 0.012f * Sg, token), Cbu = blurred(G.cb, 0.012f * Sg, token), Cru = blurred(G.cr, 0.012f * Sg, token);
  auto Ye = blurred(G.y, 0.006f * Sg, token);
  for (auto* q : {&Yu, &Cbu, &Cru, &Ye}) {
    if (!*q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to adjust the face");
  }
  auto dY = plane(), dCb = plane(), dCr = plane(), gain = plane();
  for (auto* q : {&Yb, &Cbb, &Crb, &Yl, &dY, &dCb, &dCr, &gain}) {
    if (!*q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to adjust the face");
  }
  std::fill(gain->data(), gain->data() + gain->size(), 1.0f);

  // ---- Low-frequency corrections on the grid --------------------------------
  // Backlight: judged on the light, never on the skin. Two signs together:
  //  - the background right around the head (beside and above it, never
  //    the clothes below) is bright, like a window or sky;
  //  - the face has no bright highlights. A face lit from the front shows
  //    highlights (forehead, nose, cheekbones) whatever its tone, so a deep
  //    skin tone in front of a white wall is not "corrected" lighter.
  // At most one stop.
  float lightGain = 1.0f;
  if (p.faceLight > 0.0f) {
    std::vector<float> ring;
    for (int32_t gy = 0; gy < gh; ++gy)
      for (int32_t gx = 0; gx < gw; ++gx) {
        const Pt q{static_cast<float>(r.x0) + (static_cast<float>(gx) + 0.5f) * static_cast<float>(g),
                   static_cast<float>(r.y0) + (static_cast<float>(gy) + 0.5f) * static_cast<float>(g)};
        const float u = along(q, right) / halfW, v = along(q, down) / halfH;
        const float d = std::sqrt(u * u + v * v);
        if (d > 1.2f && d < 1.9f && v < 0.3f) ring.push_back(G.y.row(gy)[gx]);
      }
    const float ringHi = quantile(ring, 0.75f);
    const float faceTop = quantile(skY, 0.98f);
    const float ringL = toLinear(ringHi), faceTopL = toLinear(faceTop);
    if (ring.size() >= 20 && ringHi >= 0.75f && faceTop < 0.45f && ringL > 3.0f * faceTopL) {
      lightGain = std::pow(std::clamp(toLinear(0.55f) / std::max(faceTopL, 1e-3f), 1.0f, 2.0f), p.faceLight);
    }
  }
  if (report != nullptr && report->lightGain == 1.0f) report->lightGain = lightGain;
  // Studio light: the face as an ellipsoid, lit from the chosen side.
  const float la = std::clamp(p.lightAngle, -1.0f, 1.0f);
  float Lx = 0.85f * la, Ly = -0.35f, Lz = 0.6f;
  {
    const float n = std::sqrt(Lx * Lx + Ly * Ly + Lz * Lz);
    Lx /= n, Ly /= n, Lz /= n;
  }
  double shadeSum = 0, shadeW = 0;
  std::vector<float> shade;
  if (p.studio > 0.0f) {
    shade.resize(G.y.size());
    for (int32_t gy = 0; gy < gh; ++gy)
      for (int32_t gx = 0; gx < gw; ++gx) {
        const Pt q{static_cast<float>(r.x0) + (static_cast<float>(gx) + 0.5f) * static_cast<float>(g),
                   static_cast<float>(r.y0) + (static_cast<float>(gy) + 0.5f) * static_cast<float>(g)};
        const float u = along(q, right) / (1.15f * halfW), v = along(q, down) / (1.15f * halfH);
        const float z = std::sqrt(std::max(0.0f, 1.0f - u * u - v * v));
        const float s = std::max(0.0f, u * Lx + v * Ly + z * Lz);
        const size_t i = static_cast<size_t>(gy) * static_cast<size_t>(gw) + static_cast<size_t>(gx);
        shade[i] = s;
        const float w = ovalM->data()[i];
        shadeSum += static_cast<double>(s) * w;
        shadeW += w;
      }
  }
  const float shadeMean = shadeW > 0 ? static_cast<float>(shadeSum / shadeW) : 0.0f;
  // Make-up: the face frame (eye line, nose and mouth in face units).
  const bool makeup = p.blush > 0.0f || p.contour > 0.0f || p.highlight > 0.0f;
  MakeupFrame frame;
  frame.eyeV = along(centre({eyeCa, eyeCb}), down) / halfH;
  frame.noseV = along(centre(range(pts, kNoseBottom, 3)), down) / halfH;
  frame.mouthV = along(centre(mouthIn), down) / halfH;
  std::array<float, 2> blushDir{0.0f, 0.0f};
  {
    float by, bcb, bcr;
    rgbToYcc(p.blushColor[0], p.blushColor[1], p.blushColor[2], by, bcb, bcr);
    const float n = std::hypot(bcb, bcr);
    if (n > 1e-4f) blushDir = {bcb / n, bcr / n};
  }

  for (size_t i = 0; i < G.y.size(); ++i) {
    const float base = Yb->data()[i];
    float dy = 0.0f, dcb = 0.0f, dcr = 0.0f;
    const float skinW = ovalM->data()[i] * (1.0f - feat.data()[i]);
    // Under-eye: toward the cheek's own light and colour (low frequencies only).
    if (p.underEye > 0.0f) {
      const float w = underM->data()[i] * p.underEye * featureK;
      if (w > 0.0f) {
        const float ub = Yu->data()[i];
        dy += w * 0.9f * std::max(0.0f, cheekY - ub);
        const float rel = std::clamp((ub + dy + 0.02f) / (cheekY + 0.02f), 0.0f, 1.3f);
        dcb += w * 0.6f * (cheekCb * rel - Cbu->data()[i]);
        dcr += w * 0.6f * (cheekCr * rel - Cru->data()[i]);
      }
    }
    // Shine: highlights beyond the skin's own spread, and their lost colour.
    if (p.shine > 0.0f && skinW > 0.0f) {
      const float over = std::max(0.0f, base - (skinY + 1.6f * skinMad));
      if (over > 0.0f) {
        const float cut = p.shine * 0.75f * over * skinW;
        dy -= cut;
        const float rel = std::clamp((base - cut + 0.02f) / (skinY + 0.02f), 0.0f, 1.5f);
        const float k = p.shine * skinW * std::min(1.0f, over / 0.05f) * 0.6f;
        dcb += k * (skinCb * rel - Cbb->data()[i]);
        dcr += k * (skinCr * rel - Crb->data()[i]);
      }
    }
    // Face light: open shadows toward the face's own median (never darken).
    if (p.faceLight > 0.0f) {
      const float w = ovalSoft->data()[i] * (1.0f - 0.6f * feat.data()[i]);
      dy += p.faceLight * 0.65f * w * std::max(0.0f, skinY - Yl->data()[i]);
    }
    float gn = 1.0f;
    if (makeup) {
      const auto gx = static_cast<int32_t>(i % static_cast<size_t>(gw)), gy = static_cast<int32_t>(i / static_cast<size_t>(gw));
      const Pt q{static_cast<float>(r.x0) + (static_cast<float>(gx) + 0.5f) * static_cast<float>(g),
                 static_cast<float>(r.y0) + (static_cast<float>(gy) + 0.5f) * static_cast<float>(g)};
      const MakeupWeights m = makeupAt(along(q, right) / halfW, along(q, down) / halfH, frame);
      const float onSkin = ovalSoft->data()[i] * (1.0f - feat.data()[i]);
      if (p.blush > 0.0f && m.blush > 0.0f) {
        // Chroma toward the blush colour, sized to the person's own tone
        // (deeper skin carries a deeper, richer blush); a touch darker.
        const float k = p.blush * m.blush * onSkin;
        dcb += k * 0.055f * blushDir[0] * (0.6f + 0.6f * skinY);
        dcr += k * 0.055f * blushDir[1] * (0.6f + 0.6f * skinY);
        dy -= k * 0.02f * skinY;
      }
      if (p.contour > 0.0f) gn *= 1.0f - p.contour * 0.2f * m.contour * ovalSoft->data()[i];
      if (p.highlight > 0.0f) gn *= 1.0f + p.highlight * 0.28f * m.highlight * onSkin;
    }
    dY->data()[i] = dy;
    dCb->data()[i] = dcb;
    dCr->data()[i] = dcr;
    if (!shade.empty()) gn *= 1.0f + p.studio * ovalSoft->data()[i] * (0.55f * (shade[i] - shadeMean) + 0.05f);
    gain->data()[i] = gn;
  }
  // The backlight lift covers the head and a soft halo (like a reflector),
  // never a face-shaped patch.
  if (lightGain > 1.001f) {
    auto halo = mask({oval}, 0.16f * Sg);
    if (!halo) return halo.error();
    for (size_t i = 0; i < gain->size(); ++i) {
      const float w = std::min(1.0f, halo->data()[i] * 1.3f);
      gain->data()[i] *= 1.0f + (lightGain - 1.0f) * w;
    }
  }

  // ---- Slim: smooth inward shift of the jaw and lower cheeks ----------------
  struct Push {
    Pt at, v;
  };
  std::vector<Push> pushes;
  const float eyeLine = along(centre({eyeCa, eyeCb}), down);
  if (p.slim > 0.0f) {
    for (const auto& q : oval) {
      const float dv = along(q, down) - eyeLine;
      if (dv <= 0.1f * halfH) continue;  // forehead and temples stay
      const float du = along(q, right);
      const float lower = smoothstep(0.1f * halfH, 0.9f * halfH, dv);
      const float side = std::clamp(std::fabs(du) / halfW, 0.0f, 1.0f);  // the chin itself does not move sideways
      const float m = p.slim * 0.035f * faceW * lower * side;
      const float s = du > 0 ? -1.0f : 1.0f;
      pushes.push_back({q, {s * right[0] * m, s * right[1] * m}});
    }
  }
  std::optional<Plane> dispX, dispY;
  Rect warpRect;
  if (!pushes.empty()) {
    auto dx = plane(), dyp = plane();
    if (!dx || !dyp) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to adjust the face");
    const float sig = 0.13f * faceW, inv2 = 1.0f / (2.0f * sig * sig);
    float maxD = 0.0f;
    float wx0 = 1e9f, wy0 = 1e9f, wx1 = -1e9f, wy1 = -1e9f;
    for (int32_t gy = 0; gy < gh; ++gy)
      for (int32_t gx = 0; gx < gw; ++gx) {
        const Pt q{static_cast<float>(r.x0) + (static_cast<float>(gx) + 0.5f) * static_cast<float>(g),
                   static_cast<float>(r.y0) + (static_cast<float>(gy) + 0.5f) * static_cast<float>(g)};
        float sx = 0, sy = 0, sk = 0;
        for (const auto& pu : pushes) {
          const float d2 = (q[0] - pu.at[0]) * (q[0] - pu.at[0]) + (q[1] - pu.at[1]) * (q[1] - pu.at[1]);
          const float k = std::exp(-d2 * inv2);
          sx += k * pu.v[0], sy += k * pu.v[1], sk += k;
        }
        const float norm = 1.0f / std::max(1.0f, sk);
        const float vx = sx * norm, vy = sy * norm;
        dx->row(gy)[gx] = vx;
        dyp->row(gy)[gx] = vy;
        const float mag = std::hypot(vx, vy);
        maxD = std::max(maxD, mag);
        if (mag > 0.05f) wx0 = std::min(wx0, q[0]), wy0 = std::min(wy0, q[1]), wx1 = std::max(wx1, q[0]), wy1 = std::max(wy1, q[1]);
      }
    if (maxD > 0.05f) {
      const auto pad = static_cast<float>(g) + maxD + 2.0f;
      warpRect.x0 = std::clamp(static_cast<int32_t>(wx0 - pad), r.x0, r.x1);
      warpRect.y0 = std::clamp(static_cast<int32_t>(wy0 - pad), r.y0, r.y1);
      warpRect.x1 = std::clamp(static_cast<int32_t>(wx1 + pad), r.x0, r.x1);
      warpRect.y1 = std::clamp(static_cast<int32_t>(wy1 + pad), r.y0, r.y1);
      dispX = std::move(dx).value();
      dispY = std::move(dyp).value();
    }
  }
  // Source pixels for the warp (only the affected box is copied).
  std::optional<ImageBuffer> src;
  if (dispX && warpRect.w() > 0 && warpRect.h() > 0) {
    auto copy = ImageBuffer::allocate(warpRect.w(), warpRect.h(), image.format());
    if (!copy) return copy.error();
    for (int32_t y = 0; y < warpRect.h(); ++y) {
      std::copy_n(image.row(warpRect.y0 + y) + static_cast<size_t>(warpRect.x0) * 4, static_cast<size_t>(warpRect.w()) * 4,
                  copy->row(y));
    }
    src = std::move(copy).value();
  }
  if (isCancelled(token)) return cancelledError();

  // ---- Per-pixel pass ---------------------------------------------------------
  const float invG = 1.0f / static_cast<float>(g);
  const float eyesK = p.eyes * featureK, teethK = mouthOpen ? p.teeth * featureK : 0.0f, lipsK = p.lips * featureK;
  const bool done = parallelFor(r.h(), token, [&](int32_t begin, int32_t end) {
    for (int32_t ry = begin; ry < end; ++ry) {
      const int32_t y = r.y0 + ry;
      uint8_t* row = image.row(y);
      const float fy = (static_cast<float>(ry) + 0.5f) * invG - 0.5f;
      for (int32_t x = r.x0; x < r.x1; ++x) {
        const float fx = (static_cast<float>(x - r.x0) + 0.5f) * invG - 0.5f;
        uint8_t* px = row + x * 4;
        float R = px[o.r] / 255.0f, Gc = px[o.g] / 255.0f, B = px[o.b] / 255.0f;
        if (src && x >= warpRect.x0 && x < warpRect.x1 && y >= warpRect.y0 && y < warpRect.y1) {
          // out(q) = in(q - d(q)). Catmull-Rom (bicubic) sampling keeps skin
          // texture crisp where pixels move by a fraction of a pixel (bilinear
          // would visibly soften pores); pixels that do not move are copied.
          const float ddx = dispX->sample(fx, fy), ddy = dispY->sample(fx, fy);
          if (std::fabs(ddx) + std::fabs(ddy) > 0.01f) {
            const float sx = static_cast<float>(x - warpRect.x0) - ddx;
            const float sy = static_cast<float>(y - warpRect.y0) - ddy;
            const auto ix = static_cast<int32_t>(std::floor(sx)), iy = static_cast<int32_t>(std::floor(sy));
            const float tx = sx - static_cast<float>(ix), ty = sy - static_cast<float>(iy);
            auto cr = [](float t, float w[4]) {
              const float t2 = t * t, t3 = t2 * t;
              w[0] = 0.5f * (-t3 + 2 * t2 - t);
              w[1] = 0.5f * (3 * t3 - 5 * t2 + 2);
              w[2] = 0.5f * (-3 * t3 + 4 * t2 + t);
              w[3] = 0.5f * (t3 - t2);
            };
            float wx[4], wy[4];
            cr(tx, wx);
            cr(ty, wy);
            float acc[3] = {0, 0, 0};
            for (int j = 0; j < 4; ++j) {
              const uint8_t* srow = src->row(std::clamp(iy - 1 + j, 0, warpRect.h() - 1));
              for (int i = 0; i < 4; ++i) {
                const uint8_t* q = srow + std::clamp(ix - 1 + i, 0, warpRect.w() - 1) * 4;
                const float w = wx[i] * wy[j];
                acc[0] += w * q[o.r];
                acc[1] += w * q[o.g];
                acc[2] += w * q[o.b];
              }
            }
            R = std::clamp(acc[0] / 255.0f, 0.0f, 1.0f);
            Gc = std::clamp(acc[1] / 255.0f, 0.0f, 1.0f);
            B = std::clamp(acc[2] / 255.0f, 0.0f, 1.0f);
          } else {
            const uint8_t* q = src->row(y - warpRect.y0) + (x - warpRect.x0) * 4;
            R = q[o.r] / 255.0f, Gc = q[o.g] / 255.0f, B = q[o.b] / 255.0f;
          }
        }
        float yv, cbv, crv;
        rgbToYcc(R, Gc, B, yv, cbv, crv);
        const float y0v = yv;
        const float base = Yb->sample(fx, fy);
        // Eyes: whites a little brighter and less red; irises clearer.
        if (eyesK > 0.0f) {
          const float w = eyeM->sample(fx, fy) * eyesK;
          if (w > 0.0f) {
            const float sat = std::sqrt(cbv * cbv + crv * crv);
            const float white = smoothstep(eyeMid - 0.05f * eyeRange, eyeMid + 0.2f * eyeRange, yv) *
                                (1.0f - smoothstep(0.06f, 0.13f, sat));
            yv += w * white * 0.1f * (1.0f - yv);
            crv -= w * white * 0.6f * std::max(0.0f, crv);
            // Iris clarity: fine local detail (rings, crypts), bounded.
            const float detailV = std::clamp(yv - Ye->sample(fx, fy), -0.1f, 0.1f);
            yv += w * (1.0f - white) * 0.5f * detailV;
          }
        }
        // Teeth: bright, unsaturated pixels inside the open mouth only (lips
        // and gums are red, the mouth's shadow is dark: both are left alone).
        if (teethK > 0.0f) {
          const float w = mouthM->sample(fx, fy) * teethK;
          if (w > 0.0f) {
            const float tooth = smoothstep(mouthMid - 0.05f * mouthRange, mouthMid + 0.2f * mouthRange, yv) *
                                (1.0f - smoothstep(0.05f, 0.1f, crv)) * smoothstep(0.2f, 0.35f, yv);
            const float k = w * tooth;
            cbv += k * 0.7f * (0.0f - cbv);
            crv += k * 0.4f * (0.0f - crv);
            yv += k * 0.08f * (1.0f - yv);
          }
        }
        // Lips: richer colour and a little definition; hue unchanged.
        if (lipsK > 0.0f) {
          const float w = lipsM->sample(fx, fy) * lipsK * (1.0f - mouthM->sample(fx, fy));
          if (w > 0.0f) {
            const float sat = std::sqrt(cbv * cbv + crv * crv);
            const float k = 1.0f + 0.5f * w * (1.0f - smoothstep(0.2f, 0.34f, sat));
            cbv *= k;
            crv *= k;
            yv += w * 0.3f * (yv - base);
          }
        }
        // Low-frequency corrections; colour follows the light (same saturation).
        yv = std::clamp(yv + dY->sample(fx, fy), 0.0f, 1.0f);
        const float ratio = std::clamp((yv + 0.02f) / (y0v + 0.02f), 0.5f, 1.6f);
        cbv = cbv * ratio + dCb->sample(fx, fy);
        crv = crv * ratio + dCr->sample(fx, fy);
        yccToRgb(yv, cbv, crv, R, Gc, B);
        const float gn = gain->sample(fx, fy);
        if (std::fabs(gn - 1.0f) > 1e-4f) {
          R = toGamma(applyGain(toLinear(R), gn));
          Gc = toGamma(applyGain(toLinear(Gc), gn));
          B = toGamma(applyGain(toLinear(B), gn));
        }
        px[o.r] = toByte(R);
        px[o.g] = toByte(Gc);
        px[o.b] = toByte(B);
      }
    }
  });
  if (!done) return cancelledError();
  return ok();
}

}  // namespace

bool FaceParams::any() const {
  return eyes > 0 || teeth > 0 || lips > 0 || underEye > 0 || shine > 0 || slim > 0 || faceLight > 0 || studio > 0 ||
         blush > 0 || contour > 0 || highlight > 0;
}

float estimateLightAngle(const ImageBuffer& image, const FaceLandmarks& lm) {
  if (!isColour(image) || image.width() < 16 || image.height() < 16) return 0.35f;
  const auto W = static_cast<float>(image.width()), H = static_cast<float>(image.height());
  std::vector<Pt> pts(kFacePoints);
  for (int i = 0; i < kFacePoints; ++i) {
    pts[static_cast<size_t>(i)] = {lm.points[static_cast<size_t>(2 * i)] * W, lm.points[static_cast<size_t>(2 * i + 1)] * H};
  }
  Pt a = centre(range(pts, kLeftEye, 16)), b = centre(range(pts, kRightEye, 16));
  if (a[0] > b[0]) std::swap(a, b);
  const float eyeDist = std::hypot(b[0] - a[0], b[1] - a[1]);
  if (!(eyeDist > 8.0f)) return 0.35f;
  const Pt right{(b[0] - a[0]) / eyeDist, (b[1] - a[1]) / eyeDist}, down{-right[1], right[0]};
  const Rgb o = offsets(image);
  // Mean luma of each cheek (below the eye, beside the nose), in linear light.
  auto cheek = [&](const Pt& eye) {
    const Pt c{eye[0] + down[0] * 0.75f * eyeDist, eye[1] + down[1] * 0.75f * eyeDist};
    const float rad = 0.22f * eyeDist;
    double sum = 0;
    int n = 0;
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) {
        const int x = static_cast<int>(c[0] + rad * static_cast<float>(dx) / 4.0f);
        const int y = static_cast<int>(c[1] + rad * static_cast<float>(dy) / 4.0f);
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) continue;
        const uint8_t* q = image.row(y) + x * 4;
        sum += toLinear((0.299f * q[o.r] + 0.587f * q[o.g] + 0.114f * q[o.b]) / 255.0f);
        ++n;
      }
    return n > 20 ? static_cast<float>(sum / n) : -1.0f;
  };
  const float l = cheek(a), r = cheek(b);
  if (l < 0.0f || r < 0.0f) return 0.35f;
  const float contrast = (r - l) / (r + l + 0.02f);
  if (std::fabs(contrast) < 0.06f) return 0.35f;
  return std::clamp(3.0f * contrast, -1.0f, 1.0f);
}

Error adjustFaces(ImageBuffer& image, const std::vector<FaceLandmarks>& faces, const FaceParams& params,
                  const CancellationToken* token, FaceReport* report) {
  ScopedTimer timer("portrait.faces");
  if (!isColour(image)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Face adjustments need an RGBA image");
  FaceParams p = params;
  for (float* v : {&p.eyes, &p.teeth, &p.lips, &p.underEye, &p.shine, &p.slim, &p.faceLight, &p.studio, &p.blush,
                   &p.contour, &p.highlight, &p.blushColor[0], &p.blushColor[1], &p.blushColor[2]}) {
    if (!std::isfinite(*v)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Non-finite face parameter");
    *v = std::clamp(*v, 0.0f, 1.0f);
  }
  if (!std::isfinite(p.lightAngle)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Non-finite face parameter");
  for (const auto& f : faces)
    for (float v : f.points)
      if (!std::isfinite(v) || std::fabs(v) > 4.0f) return makeError(PP_ERROR_INVALID_ARGUMENT, "Face landmarks must be finite, near the image");
  if (report != nullptr) *report = FaceReport{static_cast<int32_t>(faces.size()), 0, 1.0f};
  if (!p.any() || faces.empty()) return ok();
  for (const auto& f : faces) {
    if (isCancelled(token)) return cancelledError();
    if (Error e = adjustOne(image, f, p, token, report); e.status != PP_OK) return e;
  }
  return ok();
}

}  // namespace photoplus::portrait
