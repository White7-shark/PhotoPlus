// Lips: natural pink, or lipstick. The lips found from the face's outline (upper and lower lip
// contours; the open mouth and teeth left out) and repainted in a chosen pink.
//
// The lips keep their own light: shading, creases and gloss are carried over
// as a ratio to the lips' mean brightness, so the colour sits on the lips
// rather than over them. Dark lips are lifted toward the pink's brightness
// (pink shows on deep skin, as in the looks people share), the edge is
// feathered into the skin, and bright, colourless pixels inside the mouth
// (teeth) are left alone.
//
// Natural pink adapts to the person: lips that are already pink (as most
// fair people's are) get almost nothing, a fair face only a faint touch, and
// brown or greyish lips (common on deep skin) the full soft pink, so it looks
// like their own lips, never like make-up. Lipstick is the chosen colour at
// the chosen strength.
#include <algorithm>
#include <cmath>
#include <vector>

#include "../portrait/common.hpp"
#include "photoplus/face.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/profiler.hpp"
#include "photoplus/studio.hpp"

namespace photoplus::studio {

namespace {
using portrait::detail::offsets;
using portrait::detail::rgbToYcc;
using portrait::detail::toByte;
using portrait::detail::yccToRgb;
using P = std::array<float, 2>;

std::vector<P> range(const std::vector<P>& p, int begin, int count) {
  return {p.begin() + begin, p.begin() + begin + count};
}

float d2(const P& a, const P& b) { return (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]); }

/** One outline from two contours (the second reversed when that closes the loop). */
std::vector<P> joined(std::vector<P> a, std::vector<P> b) {
  if (d2(a.back(), b.back()) < d2(a.back(), b.front())) std::reverse(b.begin(), b.end());
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

/** Fills a polygon (even-odd, 4 sub-rows per row) into `m`, with `value` (max-combined, or min when value is 0). */
void fill(Plane& m, const std::vector<P>& poly, float ox, float oy, float value) {
  if (poly.size() < 3) return;
  std::vector<float> xs;
  for (int32_t y = 0; y < m.height(); ++y) {
    float* row = m.row(y);
    for (int s = 0; s < 4; ++s) {
      const float yy = oy + static_cast<float>(y) + (static_cast<float>(s) + 0.5f) * 0.25f;
      xs.clear();
      for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const P& a = poly[j];
        const P& b = poly[i];
        if ((a[1] <= yy) != (b[1] <= yy)) xs.push_back(a[0] + (yy - a[1]) / (b[1] - a[1]) * (b[0] - a[0]));
      }
      std::sort(xs.begin(), xs.end());
      for (size_t k = 0; k + 1 < xs.size(); k += 2) {
        const int32_t x0 = std::max(0, static_cast<int32_t>(std::lround(xs[k] - ox)));
        const int32_t x1 = std::min(m.width(), static_cast<int32_t>(std::lround(xs[k + 1] - ox)));
        for (int32_t x = x0; x < x1; ++x) {
          if (value > 0.0f) row[x] = std::min(1.0f, row[x] + 0.25f * value);
          else row[x] = std::max(0.0f, row[x] - 0.25f);
        }
      }
    }
  }
}
}  // namespace

Error colourLips(ImageBuffer& image, const std::vector<std::array<float, kLipLandmarkFloats>>& faces,
                 const std::array<uint8_t, 3>& rgb, float strength, bool natural, const CancellationToken* token,
                 LipsReport* report) {
  ScopedTimer timer("studio.lips");
  if (report) *report = LipsReport{};
  const float st = std::isfinite(strength) ? std::clamp(strength, 0.0f, 1.0f) : 0.0f;
  if (st <= 0.0f || faces.empty()) return Error{PP_OK, {}};
  const int32_t W = image.width(), H = image.height();
  const auto o = offsets(image);
  float Yp, cbp, crp;
  rgbToYcc(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, Yp, cbp, crp);
  Yp = std::max(Yp, 0.05f);

  for (const auto& f : faces) {
    if (isCancelled(token)) return cancelledError();
    std::vector<P> pts(portrait::kFacePoints);
    bool valid = true;
    for (int i = 0; i < portrait::kFacePoints; ++i) {
      const float x = f[static_cast<size_t>(2 * i)], y = f[static_cast<size_t>(2 * i + 1)];
      if (!std::isfinite(x) || !std::isfinite(y)) valid = false;
      pts[static_cast<size_t>(i)] = {x * static_cast<float>(W), y * static_cast<float>(H)};
    }
    if (!valid) continue;
    const auto upper = joined(range(pts, portrait::kUpperLipTop, 11), range(pts, portrait::kUpperLipBottom, 9));
    const auto lower = joined(range(pts, portrait::kLowerLipTop, 9), range(pts, portrait::kLowerLipBottom, 9));
    const auto mouth = joined(range(pts, portrait::kUpperLipBottom, 9), range(pts, portrait::kLowerLipTop, 9));
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const auto* poly : {&upper, &lower})
      for (const auto& p : *poly) x0 = std::min(x0, p[0]), y0 = std::min(y0, p[1]), x1 = std::max(x1, p[0]), y1 = std::max(y1, p[1]);
    const float width = std::max(x1 - x0, y1 - y0);
    if (!(width >= 6.0f)) continue;  // too small (or no lips) to colour well
    const float margin = 0.15f * width + 2.0f;
    const auto cx0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(x0 - margin)));
    const auto cy0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(y0 - margin)));
    const auto cx1 = std::min<int32_t>(W, static_cast<int32_t>(std::ceil(x1 + margin)));
    const auto cy1 = std::min<int32_t>(H, static_cast<int32_t>(std::ceil(y1 + margin)));
    if (cx1 - cx0 < 4 || cy1 - cy0 < 4) continue;
    auto raw = Plane::allocate(cx1 - cx0, cy1 - cy0);
    if (!raw) return raw.error();
    std::fill(raw->data(), raw->data() + raw->size(), 0.0f);
    const auto ox = static_cast<float>(cx0), oy = static_cast<float>(cy0);
    fill(*raw, upper, ox, oy, 1.0f);
    fill(*raw, lower, ox, oy, 1.0f);
    fill(*raw, mouth, ox, oy, 0.0f);
    auto m = Plane::allocate(raw->width(), raw->height());
    if (!m) return m.error();
    if (Error e = gaussianBlur(*raw, *m, std::max(0.8f, 0.02f * width), token); e.status != PP_OK) return e;

    // The lips' own brightness and colour (teeth and blown highlights left
    // out), and the skin around them (the margin outside the lips).
    double sum = 0, wsum = 0, sumCb = 0, sumCr = 0;
    std::vector<float> around;
    for (int32_t y = 0; y < m->height(); ++y)
      for (int32_t x = 0; x < m->width(); ++x) {
        const float w = m->row(y)[x];
        const uint8_t* px = image.row(cy0 + y) + (cx0 + x) * 4;
        float Y, cb, cr;
        rgbToYcc(px[o.r] / 255.0f, px[o.g] / 255.0f, px[o.b] / 255.0f, Y, cb, cr);
        if (w < 0.02f) {
          around.push_back(Y);
          continue;
        }
        if (w < 0.6f || Y > 0.92f) continue;
        sum += Y * w;
        sumCb += cb * w;
        sumCr += cr * w;
        wsum += w;
      }
    if (wsum < 4.0) continue;
    const float Lm = std::max(static_cast<float>(sum / wsum), 0.03f);
    float use = st;
    if (natural) {
      // Measured on the lips and the skin around them:
      //  * fair: the skin's brightness (deep skin ~0.4-0.5, fair 0.6-0.8);
      //  * rosy: lips already pink or red (hue toward red, not brown), when
      //    they carry enough colour for the hue to mean something;
      //  * coloured: strongly coloured lips (lipstick) need no pink.
      const auto lcb = static_cast<float>(sumCb / wsum), lcr = static_cast<float>(sumCr / wsum);
      const float hue = std::atan2(lcr, lcb) * 57.29578f;
      const float chromaL = std::hypot(lcb, lcr) / Lm;
      float skinLuma = Lm;
      if (around.size() > 16) {
        auto mid = around.begin() + static_cast<std::ptrdiff_t>(around.size() / 2);
        std::nth_element(around.begin(), mid, around.end());
        skinLuma = *mid;
      }
      const float fair = std::clamp((skinLuma - 0.48f) / 0.2f, 0.0f, 1.0f);
      const float rosy = std::clamp((114.0f - hue) / 10.0f, 0.0f, 1.0f) * std::clamp((chromaL - 0.1f) / 0.15f, 0.0f, 1.0f);
      const float coloured = std::clamp((chromaL - 0.42f) / 0.15f, 0.0f, 1.0f);
      use = st * std::max(0.06f, (1.0f - 0.85f * fair) * (1.0f - 0.6f * rosy) * (1.0f - 0.7f * coloured));
    }
    if (use <= 0.005f) continue;
    // Dark lips are lifted most of the way to the pink (it must show on deep skin).
    const float target = Lm + (Yp - Lm) * 0.8f;

    for (int32_t y = 0; y < m->height(); ++y) {
      uint8_t* row = image.row(cy0 + y);
      for (int32_t x = 0; x < m->width(); ++x) {
        float w = m->row(y)[x] * use;
        if (w <= 0.002f) continue;
        uint8_t* px = row + (cx0 + x) * 4;
        float Y, cb, cr;
        rgbToYcc(px[o.r] / 255.0f, px[o.g] / 255.0f, px[o.b] / 255.0f, Y, cb, cr);
        const float shade = Y / Lm;
        // Teeth: much brighter than the lips and colourless.
        if (shade > 1.8f && std::hypot(cb, cr) < 0.05f) w *= 0.15f;
        // Creases and gloss carried over; a darker part (the upper lip, in
        // shade) is evened a little, so the whole mouth reads as one pink.
        const float s2 = 1.0f + (shade - 1.0f) * (shade < 1.0f ? 0.6f : 0.85f);
        const float Yn = std::clamp(target * s2, 0.0f, 1.0f);
        // Colour falls off more slowly than brightness: a shaded part stays pink, not grey.
        float k = std::clamp(std::sqrt(Yn / Yp), 0.55f, 1.25f);
        if (shade > 1.25f) k *= std::clamp(1.0f - (shade - 1.25f) / 0.8f, 0.4f, 1.0f);  // gloss stays light
        const float yo = Y + (Yn - Y) * w;
        const float cbo = cb + (cbp * k - cb) * w;
        const float cro = cr + (crp * k - cr) * w;
        float r, g, b;
        yccToRgb(yo, cbo, cro, r, g, b);
        px[o.r] = toByte(r);
        px[o.g] = toByte(g);
        px[o.b] = toByte(b);
      }
    }
    if (report) {
      report->lips += 1;
      report->lipLuma = Lm;
      report->applied = use;
    }
  }
  return Error{PP_OK, {}};
}

}  // namespace photoplus::studio
