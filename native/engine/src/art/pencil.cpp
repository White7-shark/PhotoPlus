// Pencil art (art.hpp): the photo drawn in pencil the way each master drew.
//
// After Lu, Xu and Jia, "Combining Sketch and Tone for Pencil Drawing
// Production" (NPAR 2012), with each master's own hand:
//  1. Lines: the photo's edges become directional pencil strokes — each
//     edge pixel is given to the best of eight directions and drawn as a
//     short straight stroke along it, so lines overshoot a little and cross
//     at corners like a real hand's (longer and looser for a sketcher,
//     short and exact for an engraver).
//  2. Tone: the photo's values matched to the tones of a real pencil drawing
//     (a lot of bare paper, a band of mid-tones, a few deep darks), kept
//     close to the photo on faces.
//  3. Hatching: tone built from layers of strokes — the master's direction
//     first, then crossing layers in the darker values — in patches that
//     turn with the forms for the masters who drew that way; a blending
//     stump's smooth tone for the masters who blended (sfumato).
//  4. The sheet: paper, its tooth catching the graphite, toned paper with
//     white chalk in the lights, and the drawing fading out into bare paper
//     towards the edges as real drawings do.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "core.hpp"
#include "photoplus/art.hpp"
#include "photoplus/parallel.hpp"

namespace photoplus::art {

using namespace core;

namespace {

struct PencilStyle {
  float lineLen;     // contour stroke length, share of the short side
  float lineWeight;  // contour darkness
  float angle;       // main hatching direction, radians (y down: -0.8 is "/" a right hand's, 0.8 is "\" Leonardo's left hand)
  int layers;        // hatching layers 1..3 (more cross the first in the darks)
  float spacing;     // hatching period, share of the short side
  float strokeLen;   // hatching stroke length, share of the short side
  float follow;      // 0 one direction .. 1 hatching turns with the forms
  float smudge;      // 0 pure hatching .. 1 blended tone (a stump)
  float contrast;    // tone curve
  float dark;        // 0 light, airy .. 1 deep darks
  float bg;          // how much of the background is drawn (0 the person only .. 1 everything)
  float sketch;      // loose, overshooting marks
  float grain;       // the paper's tooth showing in the graphite
  float detail;      // fine lines on the features
  float vignette;    // the drawing fades into bare paper towards the edges
};

// The twenty masters, as they drew (engine order).
constexpr PencilStyle kStyles[kArtistCount] = {
    // Leonardo: left-handed parallel hatching "\" under a smoky sfumato blend.
    {1 / 38.f, 0.70f, 0.80f, 1, 1 / 230.f, 1 / 26.f, 0.15f, 0.55f, 0.10f, 0.45f, 0.35f, 0.25f, 0.45f, 0.9f, 0.75f},
    // Michelangelo: sculptural cross-contour hatching that wraps the forms, firm contours.
    {1 / 32.f, 0.95f, -0.70f, 2, 1 / 210.f, 1 / 22.f, 0.75f, 0.20f, 0.30f, 0.65f, 0.30f, 0.35f, 0.45f, 0.9f, 0.70f},
    // Raphael: clean, graceful contours and delicate soft modelling.
    {1 / 44.f, 0.75f, -0.85f, 1, 1 / 250.f, 1 / 30.f, 0.25f, 0.45f, 0.05f, 0.35f, 0.30f, 0.10f, 0.40f, 1.0f, 0.70f},
    // Dürer: an engraver's precision — fine dense hatching and cross-hatching that follow the form.
    {1 / 60.f, 0.90f, -0.60f, 3, 1 / 300.f, 1 / 18.f, 0.55f, 0.05f, 0.35f, 0.60f, 0.85f, 0.05f, 0.25f, 1.2f, 0.20f},
    // Titian: broad, soft charcoal-like shading, loose lines.
    {1 / 30.f, 0.60f, -0.75f, 1, 1 / 180.f, 1 / 20.f, 0.20f, 0.65f, 0.15f, 0.55f, 0.50f, 0.45f, 0.55f, 0.8f, 0.60f},
    // Caravaggio: dramatic darks, the figure cut out of shadow by one light.
    {1 / 36.f, 0.85f, -0.80f, 2, 1 / 210.f, 1 / 24.f, 0.20f, 0.50f, 0.55f, 0.95f, 0.85f, 0.20f, 0.45f, 0.9f, 0.30f},
    // Rembrandt: quick, loose, searching lines and zigzag hatching in strong darks.
    {1 / 24.f, 0.95f, -0.95f, 2, 1 / 170.f, 1 / 16.f, 0.20f, 0.25f, 0.40f, 0.75f, 0.45f, 0.85f, 0.45f, 0.8f, 0.80f},
    // Vermeer: soft tonal drawing, smooth blended light, few lines.
    {1 / 48.f, 0.45f, -0.80f, 1, 1 / 260.f, 1 / 30.f, 0.10f, 0.85f, 0.05f, 0.45f, 0.55f, 0.05f, 0.35f, 0.9f, 0.55f},
    // Velázquez: fluid, economical lines, soft shading.
    {1 / 30.f, 0.70f, -0.70f, 1, 1 / 200.f, 1 / 22.f, 0.25f, 0.55f, 0.15f, 0.50f, 0.45f, 0.50f, 0.45f, 0.8f, 0.70f},
    // Goya: dark, expressive, rough hatching.
    {1 / 26.f, 0.95f, -0.60f, 2, 1 / 180.f, 1 / 18.f, 0.30f, 0.30f, 0.45f, 0.85f, 0.60f, 0.70f, 0.55f, 0.8f, 0.55f},
    // Hokusai: clean calligraphic outlines, very little shading.
    {1 / 22.f, 1.20f, -0.80f, 1, 1 / 220.f, 1 / 26.f, 0.10f, 0.35f, 0.25f, 0.25f, 0.40f, 0.15f, 0.30f, 1.1f, 0.40f},
    // Turner: airy, atmospheric smudged tone, faint lines.
    {1 / 34.f, 0.40f, -0.70f, 1, 1 / 190.f, 1 / 20.f, 0.10f, 0.80f, 0.00f, 0.40f, 0.70f, 0.40f, 0.60f, 0.7f, 0.85f},
    // Monet: loose broken strokes in several directions, light key.
    {1 / 36.f, 0.55f, -0.65f, 2, 1 / 170.f, 1 / 34.f, 0.35f, 0.30f, 0.00f, 0.35f, 0.60f, 0.65f, 0.55f, 0.7f, 0.70f},
    // Degas: bold diagonal chalk-like hatching, strong drawing.
    {1 / 30.f, 0.90f, -0.95f, 2, 1 / 160.f, 1 / 20.f, 0.25f, 0.30f, 0.30f, 0.60f, 0.40f, 0.45f, 0.65f, 0.9f, 0.65f},
    // Renoir: soft, feathery, warm and light.
    {1 / 40.f, 0.50f, -0.75f, 1, 1 / 220.f, 1 / 28.f, 0.30f, 0.65f, 0.00f, 0.35f, 0.40f, 0.35f, 0.50f, 0.8f, 0.80f},
    // Cézanne: constructive patches of parallel diagonal strokes building planes.
    {1 / 34.f, 0.75f, -1.05f, 2, 1 / 170.f, 1 / 24.f, 0.00f, 0.15f, 0.25f, 0.55f, 0.65f, 0.35f, 0.45f, 0.8f, 0.55f},
    // Seurat: velvety conté tone on textured paper, almost no lines.
    {1 / 40.f, 0.30f, -0.80f, 1, 1 / 200.f, 1 / 24.f, 0.00f, 0.90f, 0.45f, 0.80f, 0.85f, 0.05f, 1.00f, 0.6f, 0.30f},
    // Van Gogh: rhythmic reed-pen dashes that swirl along the forms.
    {1 / 26.f, 1.00f, -0.70f, 2, 1 / 150.f, 1 / 34.f, 0.95f, 0.10f, 0.35f, 0.60f, 0.70f, 0.60f, 0.45f, 0.8f, 0.45f},
    // Gauguin: bold simplified contours, flat shading.
    {1 / 24.f, 1.10f, -0.80f, 1, 1 / 190.f, 1 / 22.f, 0.10f, 0.45f, 0.35f, 0.55f, 0.55f, 0.25f, 0.45f, 0.9f, 0.50f},
    // Klimt: fine, elegant flowing outlines, delicate minimal shading.
    {1 / 30.f, 0.80f, -0.80f, 1, 1 / 260.f, 1 / 30.f, 0.35f, 0.55f, 0.00f, 0.25f, 0.30f, 0.20f, 0.35f, 1.1f, 0.75f},
};

struct Medium {
  Rgb3 colour;
  float max;    // the darkest it gets (graphite shines, never reaches black)
  float grain;  // how much it skips over the paper's valleys
};
constexpr Medium kMedia[kPencilMediumCount] = {
    {{0.17f, 0.17f, 0.19f}, 0.90f, 1.0f},  // graphite
    {{0.06f, 0.055f, 0.05f}, 0.96f, 1.5f},  // charcoal
    {{0.56f, 0.22f, 0.13f}, 0.92f, 1.2f},  // red chalk (sanguine)
};

struct Sheet {
  Rgb3 colour;
  bool toned;  // mid-tone paper: white chalk in the lights
};
constexpr Sheet kSheets[kPencilPaperCount] = {
    {{0.975f, 0.970f, 0.955f}, false},  // white drawing paper
    {{0.955f, 0.920f, 0.840f}, false},  // cream
    {{0.700f, 0.705f, 0.700f}, true},   // grey toned
    {{0.790f, 0.690f, 0.540f}, true},   // tan toned
};

// The paper at one point: its colour and tooth (0 valley .. 1 peak), at a scale relative to the sheet.
// One function for the sheet and the drawing, so bare paper is the same pixel for pixel.
inline void paperAt(int x, int y, float G, const Sheet& s, uint32_t seed, float rgb[3], float& tooth) {
  const float fine = std::max(1.0f, G / 850.0f);  // the tooth's size
  const float fx = static_cast<float>(x), fy = static_cast<float>(y);
  tooth = clamp01(0.5f + 1.25f * (fbm(fx / fine, fy / fine, seed + 31, 3) - 0.5f));
  // Fibres and a faint cloud in the sheet.
  const float v = 1 + 0.018f * (tooth - 0.5f) + 0.012f * (fbm(fx / (G / 9), fy / (G / 9), seed + 37, 3) - 0.5f) +
                  0.006f * (valueNoise(fx / (fine * 9), fy / (fine * 1.6f), seed + 41) - 0.5f);
  for (int k = 0; k < 3; ++k) rgb[k] = clamp01(s.colour[static_cast<size_t>(k)] * v);
}

inline uint8_t q8(float v) { return static_cast<uint8_t>(std::lround(clamp01(v) * 255.0f)); }

void writePixel(uint8_t* px, bool bgra, const float rgb[3]) {
  px[bgra ? 2 : 0] = q8(rgb[0]);
  px[1] = q8(rgb[1]);
  px[bgra ? 0 : 2] = q8(rgb[2]);
}

// The mean of src along a straight line of 2*half+1 samples in direction theta,
// in O(1) per pixel (running sums along digital lines).
Img lineBox(const Img& src, float theta, int half) {
  const int w = src.w, h = src.h;
  Img out(w, h, 1, 0.0f);
  const float c = std::cos(theta), s = std::sin(theta);
  const bool alongX = std::fabs(c) >= std::fabs(s);
  const float slope = alongX ? s / c : c / s;  // |slope| <= 1
  const int n = alongX ? w : h, m = alongX ? h : w;
  const int win = std::max(1, static_cast<int>(std::lround(static_cast<float>(half) * (alongX ? std::fabs(c) : std::fabs(s)))));
  const int lo = std::min(0, -static_cast<int>(std::lround(slope * static_cast<float>(n - 1)))) - 1;
  const int hi = m + std::max(0, -static_cast<int>(std::lround(slope * static_cast<float>(n - 1)))) + 1;
  parallelFor(hi - lo, nullptr, [&](int32_t a, int32_t b) {
    std::vector<int> pos;
    std::vector<float> pre;
    for (int f = lo + a; f < lo + b; ++f) {
      pos.clear();
      pre.assign(1, 0.0f);
      for (int i = 0; i < n; ++i) {
        const int j = f + static_cast<int>(std::lround(slope * static_cast<float>(i)));
        if (j < 0 || j >= m) continue;
        pos.push_back(i);
        const float v = alongX ? src.at(i, j) : src.at(j, i);
        pre.push_back(pre.back() + v);
      }
      const int cnt = static_cast<int>(pos.size());
      for (int k = 0; k < cnt; ++k) {
        const int k0 = std::max(0, k - win), k1 = std::min(cnt - 1, k + win);
        const float mean = (pre[static_cast<size_t>(k1 + 1)] - pre[static_cast<size_t>(k0)]) / static_cast<float>(k1 - k0 + 1);
        const int i = pos[static_cast<size_t>(k)];
        const int j = f + static_cast<int>(std::lround(slope * static_cast<float>(i)));
        if (alongX) out.at(i, j) = mean;
        else out.at(j, i) = mean;
      }
    }
  });
  return out;
}

float percentile(const std::vector<float>& d, float q) {
  std::vector<float> v;
  v.reserve(d.size() / 4 + 1);
  for (size_t i = 0; i < d.size(); i += 4)
    if (d[i] > 1e-4f) v.push_back(d[i]);
  if (v.empty()) return 1.0f;
  const size_t k = std::min(v.size() - 1, static_cast<size_t>(q * static_cast<float>(v.size())));
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
  return std::max(1e-4f, v[k]);
}

// Pencil strokes along the edges (Lu et al.): 0 none .. 1 a full line.
Img strokeLines(const Img& luma, float sigma, int half, float floorLo, float floorHi, const CancellationToken* token) {
  const Img l = blur(luma, sigma, token);
  const int w = l.w, h = l.h;
  Img g(w, h, 1, 0.0f);
  for (int y = 1; y < h - 1; ++y)
    for (int x = 1; x < w - 1; ++x) {
      const float gx = 0.5f * (l.at(x + 1, y) - l.at(x - 1, y)), gy = 0.5f * (l.at(x, y + 1) - l.at(x, y - 1));
      g.at(x, y) = std::hypot(gx, gy);
    }
  const float p = percentile(g.d, 0.985f);
  for (float& v : g.d) v = smoothstep(floorLo, floorHi, v / p) * std::min(1.0f, v / p);
  constexpr int kDirs = 8;
  std::vector<uint8_t> best(g.d.size(), 0);
  std::vector<float> bestV(g.d.size(), -1.0f);
  for (int d = 0; d < kDirs; ++d) {
    const Img r = lineBox(g, kPi * static_cast<float>(d) / kDirs, half);
    for (size_t i = 0; i < g.d.size(); ++i)
      if (r.d[i] > bestV[i]) bestV[i] = r.d[i], best[i] = static_cast<uint8_t>(d);
    if (isCancelled(token)) return g;
  }
  Img s(w, h, 1, 0.0f);
  Img ci(w, h, 1, 0.0f);
  for (int d = 0; d < kDirs; ++d) {
    for (size_t i = 0; i < g.d.size(); ++i) ci.d[i] = best[i] == d ? g.d[i] : 0.0f;
    const Img r = lineBox(ci, kPi * static_cast<float>(d) / kDirs, half);
    for (size_t i = 0; i < s.d.size(); ++i) s.d[i] += r.d[i];
    if (isCancelled(token)) return s;
  }
  const float q = percentile(s.d, 0.99f);
  for (float& v : s.d) v = std::pow(clamp01(v / q), 1.25f);
  return s;
}

// Matches the values to a pencil drawing's tones (Lu et al.: bright paper,
// a band of mid-tones, a few darks), as a lookup from the photo's histogram.
Img pencilTone(const Img& luma, float dark) {
  std::array<double, 256> hist{};
  for (float v : luma.d) hist[static_cast<size_t>(std::clamp(static_cast<int>(v * 255.0f), 0, 255))] += 1.0;
  std::array<double, 256> cdf{};
  double acc = 0;
  for (size_t i = 0; i < 256; ++i) acc += hist[i], cdf[i] = acc / std::max(1.0, static_cast<double>(luma.d.size()));
  // The target: weights of bright, mid and dark layers; deeper styles have more dark.
  const double w1 = 0.56 - 0.26 * dark, w2 = 0.36, w3 = 0.08 + 0.26 * dark;
  const double sb = 9.0 / 255, ua = 105.0 / 255, ub = 225.0 / 255, md = (90.0 - 40.0 * dark) / 255, sd = 11.0 / 255 + 0.03 * dark;
  std::array<double, 256> tc{};
  acc = 0;
  for (size_t i = 0; i < 256; ++i) {
    const double v = static_cast<double>(i) / 255;
    const double p1 = std::exp(-(1 - v) / sb) / sb, p2 = (v >= ua && v <= ub) ? 1 / (ub - ua) : 0,
                 p3 = std::exp(-(v - md) * (v - md) / (2 * sd * sd)) / std::sqrt(2 * kPi * sd * sd);
    acc += w1 * p1 + w2 * p2 + w3 * p3;
    tc[i] = acc;
  }
  for (double& v : tc) v /= acc;
  std::array<float, 256> map{};
  size_t j = 0;
  for (size_t i = 0; i < 256; ++i) {
    while (j < 255 && tc[j] < cdf[i]) ++j;
    map[i] = static_cast<float>(j) / 255.0f;
  }
  Img out(luma.w, luma.h, 1);
  for (size_t i = 0; i < luma.d.size(); ++i) {
    const float f = std::clamp(luma.d[i], 0.0f, 1.0f) * 255.0f;
    const int a = std::min(254, static_cast<int>(f));
    out.d[i] = mix(map[static_cast<size_t>(a)], map[static_cast<size_t>(a + 1)], f - static_cast<float>(a));
  }
  return out;
}

// One family of hatching strokes: coverage 0..1 at (x, y), straight strokes at
// angle a with the given period, line width (0..1 of the period) and stroke length.
float hatch(float x, float y, float a, float period, float width, float stroke, float wobble, uint32_t seed) {
  if (width <= 0.001f) return 0;
  const float ca = std::cos(a), sa = std::sin(a);
  const float u = x * ca + y * sa;  // along the strokes
  const float v = (-x * sa + y * ca) / period;
  const float line = std::floor(v);
  const int li = static_cast<int>(line);
  // A hand, not a ruler: each line wavers and its strokes start at their own places.
  const float wob = wobble * (valueNoise(u / (stroke * 0.7f), line * 0.61f, seed) - 0.5f);
  const float off = hash01(li, 3, seed) * stroke;
  const float su = (u + off) / stroke;
  const float seg = std::floor(su), f = su - seg;
  const float jitter = 0.25f * (hash01(li, static_cast<int>(seg), seed + 5) - 0.5f);
  const float d = std::fabs(v - line - 0.5f - wob - jitter * 0.3f);  // 0 at the stroke's centre
  const float aa = 0.75f / period;
  const float half = 0.5f * width;
  float cover = 1 - smoothstep(half - aa, half + aa, d);
  // Each stroke presses in, then lifts (lighter, thinner tail).
  const float press = 0.45f + 0.55f * smoothstep(0.0f, 0.1f, f) * (1 - smoothstep(0.8f, 1.0f, f));
  cover *= press * (0.8f + 0.2f * hash01(li, static_cast<int>(seg), seed + 9));
  return cover;
}

struct Prepared {
  int w = 0, h = 0;
  float G = 1;
  Img photo;    // rgb
  Img subject;  // 1 channel or empty
  Img faces;    // face ovals, soft
  Img features; // eyes, brows, mouth
};

void faceMasks(Prepared& p, const std::vector<FaceBox>& boxes) {
  if (boxes.empty()) return;
  p.faces = Img(p.w, p.h, 1, 0.0f);
  p.features = Img(p.w, p.h, 1, 0.0f);
  const float W = static_cast<float>(p.w), H = static_cast<float>(p.h);
  for (const FaceBox& f : boxes) {
    if (!(f.w > 0 && f.h > 0)) continue;
    const float cx = (f.x + f.w * 0.5f) * W, cy = (f.y + f.h * 0.5f) * H, rx = f.w * W * 0.58f, ry = f.h * H * 0.65f;
    const bool eyes = f.eyes[0] > 0 || f.eyes[2] > 0;
    const bool mouth = f.mouth[2] > f.mouth[0];
    const int x0 = std::max(0, static_cast<int>(cx - rx * 1.2f)), x1 = std::min(p.w - 1, static_cast<int>(cx + rx * 1.2f));
    const int y0 = std::max(0, static_cast<int>(cy - ry * 1.2f)), y1 = std::min(p.h - 1, static_cast<int>(cy + ry * 1.2f));
    const float fw = f.w * W;
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        const float fx = static_cast<float>(x), fy = static_cast<float>(y);
        const float dx = (fx - cx) / rx, dy = (fy - cy) / ry;
        p.faces.at(x, y) = std::max(p.faces.at(x, y), 1 - smoothstep(0.75f, 1.0f, std::sqrt(dx * dx + dy * dy)));
        float feat = 0;
        if (eyes) {
          for (int e = 0; e < 2; ++e) {
            const float ex = f.eyes[static_cast<size_t>(e * 2)] * W, ey = f.eyes[static_cast<size_t>(e * 2 + 1)] * H;
            const float ddx = (fx - ex) / (fw * 0.2f), ddy = (fy - ey + fw * 0.04f) / (fw * 0.14f);  // the eye and its brow
            feat = std::max(feat, 1 - smoothstep(0.6f, 1.0f, std::sqrt(ddx * ddx + ddy * ddy)));
          }
        } else {
          const float ddy = (fy - (f.y + f.h * 0.4f) * H) / (fw * 0.14f), ddx = std::fabs(dx) < 0.8f ? 0.0f : 2.0f;
          feat = std::max(feat, 1 - smoothstep(0.6f, 1.0f, std::sqrt(ddx * ddx + ddy * ddy)));
        }
        if (mouth) {
          const float mx = (f.mouth[0] + f.mouth[2]) * 0.5f * W, my = (f.mouth[1] + f.mouth[3]) * 0.5f * H;
          const float mw = std::max((f.mouth[2] - f.mouth[0]) * W * 0.65f, fw * 0.16f), mh = std::max((f.mouth[3] - f.mouth[1]) * H * 0.8f, fw * 0.08f);
          const float ddx = (fx - mx) / mw, ddy = (fy - my) / mh;
          feat = std::max(feat, 1 - smoothstep(0.6f, 1.0f, std::sqrt(ddx * ddx + ddy * ddy)));
        }
        // The nose, between the eyes and the mouth.
        const float nx = (fx - cx) / (fw * 0.12f), ny = (fy - (cy + 0.05f * f.h * H)) / (fw * 0.2f);
        feat = std::max(feat, 0.7f * (1 - smoothstep(0.6f, 1.0f, std::sqrt(nx * nx + ny * ny))));
        p.features.at(x, y) = std::max(p.features.at(x, y), feat * p.faces.at(x, y));
      }
  }
}

Error prepare(const ImageBuffer& image, const PencilOptions& o, Prepared& p, const CancellationToken* token) {
  p.w = image.width(), p.h = image.height();
  p.G = static_cast<float>(std::min(p.w, p.h));
  const bool bgra = image.format() == PixelFormat::BGRA8;
  p.photo = Img(p.w, p.h, 3);
  for (int y = 0; y < p.h; ++y) {
    const uint8_t* row = image.row(y);
    for (int x = 0; x < p.w; ++x) {
      float* q = p.photo.px(x, y);
      q[0] = static_cast<float>(row[x * 4 + (bgra ? 2 : 0)]) / 255.0f;
      q[1] = static_cast<float>(row[x * 4 + 1]) / 255.0f;
      q[2] = static_cast<float>(row[x * 4 + (bgra ? 0 : 2)]) / 255.0f;
    }
  }
  if (o.subject != nullptr) {
    const ImageBuffer& m = *o.subject;
    Img mask(m.width(), m.height(), 1);
    for (int y = 0; y < m.height(); ++y)
      for (int x = 0; x < m.width(); ++x) mask.at(x, y) = static_cast<float>(m.row(y)[x]) / 255.0f;
    p.subject = blur(resize(mask, p.w, p.h), std::max(1.0f, p.G / 120.0f), token);
  }
  faceMasks(p, o.faces);
  if (!p.subject.d.empty() && !p.faces.d.empty())
    for (size_t i = 0; i < p.subject.d.size(); ++i) p.subject.d[i] = std::max(p.subject.d[i], p.faces.d[i]);
  return {PP_OK, {}};
}

Error check(const ImageBuffer& image, const PencilOptions& o) {
  if (image.empty() || (image.format() != PixelFormat::RGBA8 && image.format() != PixelFormat::BGRA8))
    return {PP_ERROR_INVALID_ARGUMENT, "Pencil art needs an RGBA or BGRA image"};
  if (image.width() < 16 || image.height() < 16) return {PP_ERROR_INVALID_ARGUMENT, "The photo is too small to draw"};
  const int a = static_cast<int>(o.artist), lv = static_cast<int>(o.level);
  if (a < 0 || a >= kArtistCount || lv < 0 || lv > 2) return {PP_ERROR_INVALID_ARGUMENT, "Unknown artist or level"};
  if (static_cast<int>(o.medium) < 0 || static_cast<int>(o.medium) >= kPencilMediumCount || static_cast<int>(o.paper) < 0 ||
      static_cast<int>(o.paper) >= kPencilPaperCount)
    return {PP_ERROR_INVALID_ARGUMENT, "Unknown medium or paper"};
  auto ok = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
  if (!ok(o.intensity, 0, 1) || !ok(o.lines, 0.3f, 2) || !ok(o.shading, 0.3f, 2))
    return {PP_ERROR_INVALID_ARGUMENT, "A pencil control is out of range"};
  if (o.subject != nullptr && (o.subject->empty() || o.subject->format() != PixelFormat::Gray8))
    return {PP_ERROR_INVALID_ARGUMENT, "The person mask must be a grey image"};
  return {PP_OK, {}};
}

void writeBack(ImageBuffer& image, const Img& rgb) {
  const bool bgra = image.format() == PixelFormat::BGRA8;
  for (int y = 0; y < image.height(); ++y) {
    uint8_t* row = image.row(y);
    for (int x = 0; x < image.width(); ++x) {
      const float* p = rgb.px(x, y);
      auto q = [](float v) { return static_cast<uint8_t>(std::lround(clamp01(v) * 255.0f)); };
      row[x * 4 + (bgra ? 2 : 0)] = q(p[0]);
      row[x * 4 + 1] = q(p[1]);
      row[x * 4 + (bgra ? 0 : 2)] = q(p[2]);
    }
  }
}

}  // namespace

Error pencilSheet(ImageBuffer& image, const PencilOptions& options) {
  if (Error e = check(image, options); e.status != PP_OK) return e;
  const Sheet& sheet = kSheets[static_cast<int>(options.paper)];
  const uint32_t seed = options.seed == 0 ? 1U : options.seed;
  const int w = image.width(), h = image.height();
  const float G = static_cast<float>(std::min(w, h));
  const bool bgra = image.format() == PixelFormat::BGRA8;
  parallelFor(h, nullptr, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y) {
      uint8_t* row = image.row(y);
      for (int x = 0; x < w; ++x) {
        float rgb[3], t;
        paperAt(x, y, G, sheet, seed, rgb, t);
        writePixel(row + x * 4, bgra, rgb);
      }
    }
  });
  return {PP_OK, {}};
}

Error pencil(ImageBuffer& image, const PencilOptions& options, const CancellationToken* token) {
  if (Error e = check(image, options); e.status != PP_OK) return e;
  Prepared p;
  if (Error e = prepare(image, options, p, token); e.status != PP_OK) return e;
  const uint32_t seed = options.seed == 0 ? 1U : options.seed;
  PencilStyle st = kStyles[static_cast<int>(options.artist)];
  const float k = options.intensity;
  // The finish: a study is quick and loose, a masterpiece has every layer.
  if (options.level == Level::Study) {
    st.layers = 1, st.smudge = std::min(1.0f, st.smudge + 0.1f), st.sketch = std::min(1.0f, st.sketch + 0.25f), st.detail *= 0.6f;
    st.spacing *= 1.3f, st.vignette = std::min(1.0f, st.vignette + 0.2f);
  } else if (options.level == Level::Masterpiece) {
    st.layers = std::min(3, st.layers + 1), st.detail *= 1.25f, st.spacing *= 0.85f;
  }
  // Natural keeps closer to the photo (smoother tone, lighter lines); full is the master's whole manner.
  st.smudge = mix(std::min(1.0f, st.smudge + 0.3f), st.smudge, k);
  st.lineWeight *= (0.75f + 0.45f * k) * options.lines;
  const float G = p.G;
  const int w = p.w, h = p.h;
  const Sheet& sheet = kSheets[static_cast<int>(options.paper)];
  const Medium& med = kMedia[static_cast<int>(options.medium)];

  // ---- values: autolevels, then a pencil drawing's tones, closer to the photo on faces
  Img L = lumaOf(p.photo);
  {
    std::vector<float> v(L.d.begin(), L.d.end());
    const size_t n = v.size();
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n / 100), v.end());
    const float lo = v[n / 100];
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n * 99 / 100), v.end());
    const float hi = std::max(lo + 0.1f, v[n * 99 / 100]);
    for (float& x : L.d) x = clamp01((x - lo) / (hi - lo));
  }
  Img Ls = blur(L, std::max(0.7f, G / 900.0f), token);  // pores and noise aren't drawn
  Img T = pencilTone(Ls, st.dark);
  const float keepPhoto = 0.6f - 0.3f * k;  // the photo's own order of values: a dark coat stays darker than a face
  // Each face's own range of values, brought into a readable band (a dark face never sinks into the darks,
  // a light one never bleaches out): how a portrait draughtsman keys a face.
  Img faceKey;
  if (!p.faces.d.empty()) {
    faceKey = Img(w, h, 1, -1.0f);
    for (const FaceBox& fb : options.faces) {
      if (!(fb.w > 0 && fb.h > 0)) continue;
      const float cx = (fb.x + fb.w * 0.5f) * static_cast<float>(w), cy = (fb.y + fb.h * 0.5f) * static_cast<float>(h);
      const float rx = fb.w * static_cast<float>(w) * 0.5f, ry = fb.h * static_cast<float>(h) * 0.55f;
      std::vector<float> vals;
      const int x0 = std::max(0, static_cast<int>(cx - rx * 1.25f)), x1 = std::min(w - 1, static_cast<int>(cx + rx * 1.25f));
      const int y0 = std::max(0, static_cast<int>(cy - ry * 1.25f)), y1 = std::min(h - 1, static_cast<int>(cy + ry * 1.25f));
      for (int y = y0; y <= y1; y += 2)
        for (int x = x0; x <= x1; x += 2) {
          const float dx = (static_cast<float>(x) - cx) / rx, dy = (static_cast<float>(y) - cy) / ry;
          if (dx * dx + dy * dy < 0.45f) vals.push_back(Ls.at(x, y));  // the skin, well inside the box
        }
      if (vals.size() < 16) continue;
      std::sort(vals.begin(), vals.end());
      const float lo = vals[vals.size() * 8 / 100], hi = vals[vals.size() * 92 / 100], median = vals[vals.size() / 2];
      const float span = std::max(0.3f, hi - lo);       // a face of one even tone keeps its tone
      const float target = mix(median, 0.55f, 0.55f);   // partly towards the middle: dark faces open up, light ones stay light
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          const float dx = (static_cast<float>(x) - cx) / (rx * 1.15f), dy = (static_cast<float>(y) - cy) / (ry * 1.15f);
          const float wgt = 1 - smoothstep(0.7f, 1.0f, std::sqrt(dx * dx + dy * dy));
          if (wgt <= 0) continue;
          const float keyed = clamp01(target + (Ls.at(x, y) - median) * std::min(1.0f, 0.6f / span));  // shifted, never steeper
          float& key = faceKey.at(x, y);
          key = key < 0 ? mix(Ls.at(x, y), keyed, wgt) : std::max(key, mix(Ls.at(x, y), keyed, wgt));
        }
    }
  }
  // Faces: the photo's own values, softly — a shine on the skin or a light lip is a gentle light, not bare paper.
  Img detailL = blur(Ls, std::max(2.0f, G / 90.0f), token);
  for (size_t i = 0; i < detailL.d.size(); ++i) detailL.d[i] = Ls.d[i] - detailL.d[i];
  Img ownL = Ls;
  if (!faceKey.d.empty())
    for (size_t i = 0; i < ownL.d.size(); ++i)
      if (faceKey.d[i] >= 0) ownL.d[i] = faceKey.d[i];
  Img faceTone = p.faces.d.empty() ? Img{} : blur(ownL, std::max(1.0f, G / 160.0f), token);
  for (size_t i = 0; i < T.d.size(); ++i) {
    const float f = p.faces.d.empty() ? 0.0f : p.faces.d[i];
    float c = mix(T.d[i], ownL.d[i], clamp01(keepPhoto + 0.4f * f));
    // The master's contrast, around the middle (less on faces).
    c = clamp01(0.5f + (c - 0.5f) * (1 + st.contrast * (1 - 0.6f * f)));
    if (f > 0) {
      const float local = faceTone.d[i];
      c = mix(c, local + (c - local) * 0.65f, f);  // the face's local contrast, compressed
      c -= f * 0.7f * std::max(0.0f, c - 0.8f);    // highlights keep a touch of graphite
    }
    // In the darks, the form's detail is drawn out, and nothing goes to a solid black.
    c += 1.1f * detailL.d[i] * smoothstep(0.55f, 0.1f, c);
    T.d[i] = clamp01(0.05f + 0.95f * c);
  }
  // Done with these (memory, at HD sizes).
  faceKey = Img(), ownL = Img(), detailL = Img(), faceTone = Img();
  if (isCancelled(token)) return {PP_ERROR_CANCELLED, "Cancelled"};

  // ---- lines: pencil strokes along the edges, finer ones in the features
  const int half = std::max(2, static_cast<int>(G * st.lineLen * (0.8f + 0.5f * st.sketch) * 0.5f));
  Img lines = strokeLines(Ls, std::max(0.8f, G / 650.0f), half, 0.12f + 0.08f * (1 - st.sketch), 0.45f, token);
  if (!p.faces.d.empty()) {
    // Inside a face (away from the features), only the larger forms are lined: no outlines around a shine.
    const Img soft = strokeLines(Ls, std::max(1.0f, G / 220.0f), half, 0.2f, 0.55f, token);
    for (size_t i = 0; i < lines.d.size(); ++i) {
      const float f = p.faces.d[i] * (1 - (p.features.d.empty() ? 0.0f : p.features.d[i]));
      lines.d[i] = mix(lines.d[i], soft.d[i] * 0.8f, f);
    }
  }
  if (!p.features.d.empty() && st.detail > 0) {
    const Img fine = strokeLines(L, std::max(0.6f, G / 1400.0f), std::max(2, static_cast<int>(G / 160.0f)), 0.1f, 0.4f, token);
    for (size_t i = 0; i < lines.d.size(); ++i) lines.d[i] = std::max(lines.d[i], fine.d[i] * p.features.d[i] * 0.75f * st.detail);
  }
  if (isCancelled(token)) return {PP_ERROR_CANCELLED, "Cancelled"};

  L = Img(), Ls = Img();  // the lines are drawn: the values are in T now
  // ---- where the drawing is: the person (and as much of the background as the master drew), fading at the edges
  Img where(w, h, 1, 1.0f);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
      const float m = p.subject.d.empty() ? 1.0f : p.subject.d[i];
      float v = m + (1 - m) * st.bg;
      const float fx = static_cast<float>(x) / static_cast<float>(w), fy = static_cast<float>(y) / static_cast<float>(h);
      const float edge = std::min(std::min(fx, 1 - fx), std::min(fy, 1 - fy));
      const float ragged = 0.07f * (fbm(static_cast<float>(x) / (G / 14), static_cast<float>(y) / (G / 14), seed + 51, 3) - 0.5f);
      const float fade = smoothstep(0.0f, 0.16f, edge + ragged);
      v *= mix(1.0f, fade, st.vignette);
      where.d[i] = clamp01(v);
    }

  // ---- shading: hatching layers that add strokes as the tone darkens
  Field field = st.follow > 0 ? orientation(p.photo, std::max(1.0f, G / 220.0f), G / 18.0f, token) : Field{};
  p.photo = Img();  // read: not needed again (memory, at HD sizes)
  const float period = std::max(3.0f, G * st.spacing);
  const float strokeLen = std::max(6.0f, G * st.strokeLen);
  const float wobble = 0.12f + 0.3f * st.sketch;
  constexpr int kAngles = 12;  // patches of straight hatching that turn with the forms
  Img shade(w, h, 1, 0.0f);
  parallelFor(h, token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
        const float D = clamp01((1 - T.d[i]) * options.shading) * where.d[i];
        if (D <= 0.01f) continue;
        float a = st.angle;
        if (st.follow > 0) {
          float tx = 0, ty = 0;
          field.at(static_cast<float>(x), static_cast<float>(y), tx, ty);
          const float c = st.follow * field.coherence[i];
          // Doubled angles: directions, not arrows.
          const float bx = std::cos(2 * st.angle) * (1 - c) + (tx * tx - ty * ty) * c, by = std::sin(2 * st.angle) * (1 - c) + 2 * tx * ty * c;
          a = 0.5f * std::atan2(by, bx);
          // Across the forms, as cross-contour hatching wraps them.
          a += 0.5f * kPi * st.follow * 0.6f;
        }
        // Two neighbouring quantised directions, blended: straight strokes in each patch.
        float qa = a / kPi * kAngles;
        qa -= std::floor(qa / kAngles) * kAngles;
        const int q0 = static_cast<int>(std::floor(qa)) % kAngles, q1 = (q0 + 1) % kAngles;
        const float fq = qa - std::floor(qa);
        float keep = 1;  // paper left untouched
        const float fx = static_cast<float>(x), fy = static_cast<float>(y);
        for (int layer = 0; layer < st.layers; ++layer) {
          const float start = layer == 0 ? 0.0f : layer == 1 ? 0.38f : 0.62f;
          const float span = layer == 0 ? 0.5f : 0.32f;
          const float wv = clamp01((D - start) / span);
          if (wv <= 0) break;
          const float off = layer == 0 ? 0.0f : layer == 1 ? 1.05f : -1.05f;
          const float width = 0.18f + 0.62f * wv;
          const uint32_t ls = seed + 61U + static_cast<uint32_t>(layer) * 17U;
          const float c0 = hatch(fx, fy, kPi * static_cast<float>(q0) / kAngles + off, period, width, strokeLen, wobble, ls + static_cast<uint32_t>(q0));
          const float c1 = fq > 0.02f ? hatch(fx, fy, kPi * static_cast<float>(q1) / kAngles + off, period, width, strokeLen, wobble, ls + static_cast<uint32_t>(q1)) : 0.0f;
          const float cov = mix(c0, c1, smoothstep(0.25f, 0.75f, fq));
          const float press = 0.55f + 0.45f * wv;
          keep *= 1 - press * cov;
        }
        shade.d[i] = 1 - keep;
      }
  });
  field = Field{};
  if (isCancelled(token)) return {PP_ERROR_CANCELLED, "Cancelled"};
  // The hatching's local mean matches the tone (so values stay true), and a stump blends where the master blended.
  {
    const float s = period * 1.6f;
    Img target(w, h, 1);
    for (size_t i = 0; i < target.d.size(); ++i) target.d[i] = clamp01((1 - T.d[i]) * options.shading) * where.d[i];
    const Img mt = blur(target, s, token), mh = blur(shade, s, token);
    const Img soft = blur(target, std::max(1.0f, G / 500.0f), token);
    for (size_t i = 0; i < shade.d.size(); ++i) {
      const float face = p.faces.d.empty() ? 0.0f : p.faces.d[i];
      const float hatchTone = clamp01(shade.d[i] * std::min(2.5f, mt.d[i] / std::max(0.02f, mh.d[i])));
      const float sm = std::max(st.smudge, 0.6f * face);  // faces are blended smooth, not scratched
      shade.d[i] = mix(hatchTone, soft.d[i], sm);
    }
  }

  // ---- the sheet: paper, graphite caught on its tooth, chalk lights on toned paper
  const float paperL = luma(sheet.colour.data());
  const bool bgra = image.format() == PixelFormat::BGRA8;
  parallelFor(h, token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y) {
      uint8_t* row = image.row(y);
      for (int x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
        float pp[3], t;
        paperAt(x, y, G, sheet, seed, pp, t);
        float s = shade.d[i];
        if (sheet.toned) s = clamp01((s - (1 - paperL) * 0.55f) / (1 - (1 - paperL) * 0.55f));  // the paper is the mid-tone
        const float ln = clamp01(lines.d[i] * st.lineWeight) * where.d[i];
        // Light pressure skips the valleys; hard pressure fills them.
        const float g = st.grain * med.grain;
        const float sd = s * (1 - g * 0.55f * (1 - t) * (1 - s));
        const float ld = ln * (1 - 0.35f * g * (1 - t) * (1 - ln));
        const float dep = std::min(med.max, 1 - (1 - sd) * (1 - ld));
        float q[3];
        for (int c = 0; c < 3; ++c) q[c] = mix(pp[c], med.colour[static_cast<size_t>(c)], dep);
        if (sheet.toned) {
          // White chalk in the lights, hatched across the graphite's direction.
          const float lit = smoothstep(0.7f, 0.95f, T.d[i]) * where.d[i] * (p.subject.d.empty() ? 1.0f : 0.4f + 0.6f * p.subject.d[i]);
          if (lit > 0.01f) {
            const float cv = hatch(static_cast<float>(x), static_cast<float>(y), st.angle + 0.9f, period * 1.2f, 0.3f + 0.4f * lit, strokeLen, wobble, seed + 91);
            const float a = clamp01(lit * (0.35f + 0.6f * cv)) * (0.6f + 0.4f * t);
            for (int c = 0; c < 3; ++c) q[c] = mix(q[c], 0.97f - 0.02f * static_cast<float>(c), a);
          }
        }
        writePixel(row + x * 4, bgra, q);
      }
    }
  });
  if (isCancelled(token)) return {PP_ERROR_CANCELLED, "Cancelled"};
  return {PP_OK, {}};
}

// ---- the ground a painting starts from ----------------------------------------

Error paintGround(ImageBuffer& image, const Options& options) {
  if (image.empty() || (image.format() != PixelFormat::RGBA8 && image.format() != PixelFormat::BGRA8))
    return {PP_ERROR_INVALID_ARGUMENT, "The ground needs an RGBA or BGRA image"};
  const int a = static_cast<int>(options.artist);
  if (a < 0 || a >= kArtistCount) return {PP_ERROR_INVALID_ARGUMENT, "Unknown artist"};
  // The priming each worked on: lead white, a brown imprimatura, plaster, paper.
  static constexpr uint32_t kGround[kArtistCount] = {
      0xD9CBAE, 0xE6D8BE, 0xEDE5D2, 0xF5F2EA, 0x8C6A4A, 0x5E4632, 0x6B5038, 0xE8E0CF, 0x9C8468, 0x6E5844,
      0xEDE3CC, 0xEFE6D0, 0xF1ECE0, 0xB4A78F, 0xEFE7D6, 0xEDE6D6, 0xF2EEE4, 0xEDE4CF, 0xE3D6BC, 0xE6DCC6,
  };
  const bool paperLike = options.artist == Artist::Durer || options.artist == Artist::Hokusai || options.artist == Artist::Degas;
  const int w = image.width(), h = image.height();
  const float G = static_cast<float>(std::min(w, h));
  const uint32_t seed = options.seed == 0 ? 1U : options.seed;
  const Rgb3 col = hex(kGround[a]);
  Img rgb(w, h, 3);
  Img height(w, h, 1, 0.0f);
  if (!paperLike) canvasWeave(height, std::max(1.6f, G / 300.0f), 0.6f, seed + 7);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      // A priming brushed on by hand: broad, faint unevenness.
      const float v = 1 + 0.05f * (fbm(static_cast<float>(x) / (G / 6), static_cast<float>(y) / (G / 6), seed + 3, 3) - 0.5f) +
                      (paperLike ? 0.02f * (hash01(x, y, seed) - 0.5f) : 0.0f);
      float* p = rgb.px(x, y);
      for (int k = 0; k < 3; ++k) p[k] = clamp01(col[static_cast<size_t>(k)] * v);
    }
  if (!paperLike) lightRelief(rgb, height, 0.25f, 0.0f, nullptr);
  writeBack(image, rgb);
  return {PP_OK, {}};
}

}  // namespace photoplus::art
