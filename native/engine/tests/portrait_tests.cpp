// Portrait ("Spotlight") and look ("Vibe Match") tests, written around what
// people posting to social media care about:
//  - the background is clearly, smoothly blurred; the subject stays crisp;
//  - no halo: the subject's colour never smears into the background;
//  - skin is smoothed and spots disappear, but texture is not plastic;
//  - skin keeps its own colour (checked for deep, medium and light tones):
//    brighter, never whitened or shifted;
//  - eyes, brows and lips stay sharp;
//  - tapping chooses which subject stands out;
//  - a reference photo's look transfers, and a neutral run changes nothing.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

struct Rgbf {
  float r, g, b;
};

uint64_t liveBytes() {
  pp_memory_stats s{};
  pp_memory_get_stats(&s);
  return s.current_bytes;
}

float hash01(int x, int y, unsigned seed) {
  auto h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
}

bool inEllipse(float x, float y, float cx, float cy, float rx, float ry, float grow = 0.0f) {
  const float dx = (x - cx) / (rx + grow), dy = (y - cy) / (ry + grow);
  return dx * dx + dy * dy <= 1.0f;
}

// A selfie-like scene: textured park background with bright highlights, a
// red shirt with fabric texture, a face with pores, dark and red spots,
// eyes, brows and lips. Ground-truth masks come with it.
struct Scene {
  int k = 1;  // scale: k = 3 gives a 1080x1440 photo with a large face
  int w = 360, h = 480;
  std::vector<Rgbf> px;
  std::vector<uint8_t> subject, face, protect, spotCentre;
  std::vector<std::array<int, 2>> spots;
  float fcx = 180, fcy = 170, frx = 62, fry = 80;

  explicit Scene(Rgbf skin, unsigned seed = 1, int scale = 1) : k(scale), w(360 * scale), h(480 * scale) {
    const auto n = static_cast<size_t>(w) * h;
    px.resize(n);
    subject.assign(n, 0);
    face.assign(n, 0);
    protect.assign(n, 0);
    spotCentre.assign(n, 0);
    // Cheeks and chin, clear of the eyes and lips (those are protected).
    spots = {{135, 188}, {150, 192}, {210, 186}, {225, 190}, {160, 232}, {172, 236}, {188, 232}, {200, 236}};
    std::array<std::array<int, 2>, 3> red = {{{140, 205}, {222, 204}, {182, 244}}};
    for (auto& sp : spots) sp = {sp[0] * k, sp[1] * k};
    for (auto& sp : red) sp = {sp[0] * k, sp[1] * k};
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * w + x;
        const float fx = static_cast<float>(x) / static_cast<float>(k), fy = static_cast<float>(y) / static_cast<float>(k);
        const int tx = x / k, ty = y / k;
        // Background: foliage-like texture and a few bright highlights.
        float t = 0.5f + 0.18f * std::sin(fx * 0.31f) * std::cos(fy * 0.27f) + 0.12f * (hash01(tx / 3, ty / 3, seed) - 0.5f) +
                  0.1f * std::sin((fx + fy) * 0.07f);
        Rgbf c{0.25f * t + 0.05f, 0.55f * t + 0.1f, 0.3f * t + 0.08f};
        if ((tx - 40) * (tx - 40) + (ty - 60) * (ty - 60) < 16 || (tx - 320) * (tx - 320) + (ty - 90) * (ty - 90) < 16) c = {1, 1, 0.95f};
        // Shirt.
        const bool shirt = fy > 300 && inEllipse(fx, fy, 180, 470, 150, 190);
        const bool neck = fx > 152 && fx < 208 && fy > 220 && fy < 320;
        const bool head = inEllipse(fx, fy, fcx, fcy, frx, fry);
        if (shirt) {
          const float stripe = 0.85f + 0.15f * ((tx / 2 + ty / 2) % 2);
          c = {0.78f * stripe, 0.12f * stripe, 0.14f * stripe};
          subject[i] = 255;
        }
        if (neck || head) {
          const float pore = 0.05f * (hash01(x, y, seed + 7) - 0.5f) + 0.03f * (hash01(x / 2, y / 2, seed + 9) - 0.5f);
          // Soft side light: the left cheek is in gentle shadow (shape).
          const float light = 0.85f + 0.15f * std::clamp((fx - (fcx - frx)) / (2.0f * frx), 0.0f, 1.0f);
          c = {skin.r * light * (1 + pore), skin.g * light * (1 + pore), skin.b * light * (1 + pore)};
          subject[i] = 255;
          if (head) face[i] = 255;
        }
        if (head) {
          // Eyes (sclera, iris, pupil), brows and lips.
          for (const float ex : {fcx - 24, fcx + 24}) {
            if (inEllipse(fx, fy, ex, fcy - 12, 13, 7)) {
              const float d = std::hypot(fx - ex, fy - (fcy - 12));
              c = d < 3 ? Rgbf{0.03f, 0.03f, 0.03f} : d < 6 ? Rgbf{0.25f, 0.15f, 0.08f} : Rgbf{0.93f, 0.93f, 0.9f};
            }
            if (inEllipse(fx, fy, ex, fcy - 30, 16, 3.5f)) c = {0.12f, 0.08f, 0.06f};
            if (inEllipse(fx, fy, ex, fcy - 12, 15, 9) || inEllipse(fx, fy, ex, fcy - 30, 18, 6)) protect[i] = 255;
          }
          if (inEllipse(fx, fy, fcx, fcy + 42, 22, 8)) c = {0.62f, 0.2f, 0.25f};
          if (inEllipse(fx, fy, fcx, fcy + 42, 25, 11)) protect[i] = 255;
        }
        px[i] = c;
      }
    // Blemishes: dark spots and red spots on the skin.
    for (const auto& s : spots) stamp(s[0], s[1], 3.0f * static_cast<float>(k), [](Rgbf& c) { c = {c.r * 0.72f, c.g * 0.68f, c.b * 0.66f}; });
    for (const auto& s : red) stamp(s[0], s[1], 3.5f * static_cast<float>(k), [](Rgbf& c) { c = {std::min(1.0f, c.r * 1.05f + 0.06f), c.g * 0.82f, c.b * 0.85f}; });
  }

  template <typename Fn>
  void stamp(int cx, int cy, float r, Fn fn) {
    for (int y = cy - 5 * k; y <= cy + 5 * k; ++y)
      for (int x = cx - 5 * k; x <= cx + 5 * k; ++x)
        if (std::hypot(static_cast<float>(x - cx), static_cast<float>(y - cy)) <= r) {
          fn(px[static_cast<size_t>(y) * w + x]);
          spotCentre[static_cast<size_t>(y) * w + x] = 1;
        }
  }

  pp_image* image() const {
    pp_image* img = nullptr;
    pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
    auto* p = static_cast<uint8_t*>(pp_image_pixels(img));
    for (size_t i = 0; i < px.size(); ++i) {
      p[i * 4 + 0] = static_cast<uint8_t>(std::lround(std::clamp(px[i].r, 0.0f, 1.0f) * 255));
      p[i * 4 + 1] = static_cast<uint8_t>(std::lround(std::clamp(px[i].g, 0.0f, 1.0f) * 255));
      p[i * 4 + 2] = static_cast<uint8_t>(std::lround(std::clamp(px[i].b, 0.0f, 1.0f) * 255));
      p[i * 4 + 3] = 255;
    }
    return img;
  }

  // Masks are delivered at reduced resolution, like a segmentation model's.
  pp_image* mask(const std::vector<uint8_t>& m, int factor) const {
    const int mw = w / factor, mh = h / factor;
    pp_image* img = nullptr;
    pp_image_create(mw, mh, PP_FORMAT_GRAY8, &img);
    pp_image_info info{};
    pp_image_get_info(img, &info);
    auto* p = static_cast<uint8_t*>(pp_image_pixels(img));
    for (int y = 0; y < mh; ++y)
      for (int x = 0; x < mw; ++x) {
        int s = 0;
        for (int yy = 0; yy < factor; ++yy)
          for (int xx = 0; xx < factor; ++xx) s += m[static_cast<size_t>(y * factor + yy) * w + x * factor + xx];
        p[static_cast<size_t>(y) * info.stride + x] = static_cast<uint8_t>(s / (factor * factor));
      }
    return img;
  }
};

struct Ycc {
  std::vector<float> y, cb, cr;
};

Ycc yccOf(pp_image* img) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  const auto* p = static_cast<const uint8_t*>(pp_image_pixels(img));
  Ycc o;
  const auto n = static_cast<size_t>(info.width) * info.height;
  o.y.resize(n);
  o.cb.resize(n);
  o.cr.resize(n);
  for (int y = 0; y < info.height; ++y)
    for (int x = 0; x < info.width; ++x) {
      const uint8_t* q = p + static_cast<size_t>(y) * info.stride + x * 4;
      const float r = q[0] / 255.0f, g = q[1] / 255.0f, b = q[2] / 255.0f;
      const size_t i = static_cast<size_t>(y) * info.width + x;
      o.y[i] = 0.299f * r + 0.587f * g + 0.114f * b;
      o.cb[i] = -0.168736f * r - 0.331264f * g + 0.5f * b;
      o.cr[i] = 0.5f * r - 0.418688f * g - 0.081312f * b;
    }
  return o;
}

std::vector<float> boxBlur(const std::vector<float>& v, int w, int h, int r) {
  std::vector<float> out(v.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float s = 0;
      int n = 0;
      for (int yy = std::max(0, y - r); yy <= std::min(h - 1, y + r); ++yy)
        for (int xx = std::max(0, x - r); xx <= std::min(w - 1, x + r); ++xx, ++n) s += v[static_cast<size_t>(yy) * w + xx];
      out[static_cast<size_t>(y) * w + x] = s / static_cast<float>(n);
    }
  return out;
}

// Mid-frequency texture (what reads as "sharp background" at viewing size),
// insensitive to pixel-level grain.
double texture(const std::vector<float>& y, const std::vector<uint8_t>& region, int w, int h) {
  const auto a = boxBlur(y, w, h, 1), b = boxBlur(y, w, h, 4);
  double s = 0;
  size_t n = 0;
  for (size_t i = 0; i < y.size(); ++i)
    if (region[i]) {
      s += std::fabs(a[i] - b[i]);
      ++n;
    }
  return n ? s / static_cast<double>(n) : 0.0;
}

// Mean absolute high-frequency detail over a region.
double detail(const std::vector<float>& y, const std::vector<uint8_t>& region, int w, int h) {
  const auto b = boxBlur(y, w, h, 1);
  double s = 0;
  size_t n = 0;
  for (size_t i = 0; i < y.size(); ++i)
    if (region[i]) {
      s += std::fabs(y[i] - b[i]);
      ++n;
    }
  return n ? s / static_cast<double>(n) : 0.0;
}

// Region = mask eroded by r pixels (so measurements avoid edges).
std::vector<uint8_t> erode(const std::vector<uint8_t>& m, int w, int h, int r) {
  std::vector<uint8_t> out(m.size(), 0);
  for (int y = r; y < h - r; ++y)
    for (int x = r; x < w - r; ++x) {
      bool all = true;
      for (int dy = -r; dy <= r && all; dy += r)
        for (int dx = -r; dx <= r && all; dx += r) all = m[static_cast<size_t>(y + dy) * w + x + dx] != 0;
      out[static_cast<size_t>(y) * w + x] = all ? 1 : 0;
    }
  return out;
}

std::vector<uint8_t> invert(const std::vector<uint8_t>& m) {
  std::vector<uint8_t> o(m.size());
  for (size_t i = 0; i < m.size(); ++i) o[i] = m[i] ? 0 : 1;
  return o;
}

std::vector<uint8_t> andNot(std::vector<uint8_t> a, const std::vector<uint8_t>& b) {
  for (size_t i = 0; i < a.size(); ++i)
    if (b[i]) a[i] = 0;
  return a;
}

struct Run {
  pp_image* out;
  pp_portrait_report report;
};

Run portrait(const Scene& s, const pp_portrait_params& p, bool withFaces = true) {
  pp_image* img = s.image();
  pp_image* subj = s.mask(s.subject, 2);
  pp_image* faces = withFaces ? s.mask(s.face, 2) : nullptr;
  pp_image* prot = withFaces ? s.mask(s.protect, 2) : nullptr;
  pp_portrait_report r{};
  const pp_status st = pp_portrait(img, subj, faces, prot, nullptr, &p, nullptr, &r);
  EXPECT_EQ(st, PP_OK);
  pp_image_destroy(subj);
  pp_image_destroy(faces);
  pp_image_destroy(prot);
  return {img, r};
}

pp_portrait_params defaults() {
  pp_portrait_params p{};
  pp_portrait_default_params(&p);
  return p;
}

const Rgbf kDeep{0.36f, 0.22f, 0.15f}, kMedium{0.71f, 0.50f, 0.37f}, kLight{0.94f, 0.78f, 0.69f};

}  // namespace

TEST(portrait_background_blurred_subject_crisp) {
  Scene s(kMedium);
  pp_image* before = s.image();
  const Ycc a = yccOf(before);
  Run r = portrait(s, defaults());
  const Ycc b = yccOf(r.out);
  const auto bg = erode(invert(s.subject), s.w, s.h, 20);
  const auto shirt = andNot(erode(s.subject, s.w, s.h, 6), s.face);
  const double bgBefore = texture(a.y, bg, s.w, s.h), bgAfter = texture(b.y, bg, s.w, s.h);
  const double shBefore = detail(a.y, shirt, s.w, s.h), shAfter = detail(b.y, shirt, s.w, s.h);
  std::printf("    background detail %.4f -> %.4f, subject detail %.4f -> %.4f\n", bgBefore, bgAfter, shBefore, shAfter);
  EXPECT_TRUE(bgAfter < 0.25 * bgBefore);  // clearly blurred
  EXPECT_TRUE(shAfter > 0.9 * shBefore);   // subject stays crisp...
  EXPECT_TRUE(shAfter < 1.6 * shBefore);   // ...without being over-sharpened
  EXPECT_TRUE(r.report.blur_radius > 4.0f);
  EXPECT_NEAR(r.report.subject_coverage, 0.37, 0.08);
  pp_image_destroy(before);
  pp_image_destroy(r.out);
}

TEST(portrait_no_halo_of_subject_colour) {
  Scene s(kMedium);
  Run r = portrait(s, defaults());
  const Ycc b = yccOf(r.out);
  // Band just outside the red shirt vs background further away: a naive blur
  // would drag red into the band.
  const auto outside = invert(s.subject);
  const auto far = erode(outside, s.w, s.h, 30);
  const auto band = andNot(andNot(erode(outside, s.w, s.h, 3), far), erode(outside, s.w, s.h, 12));
  double bandCr = 0, farCr = 0;
  size_t nb = 0, nf = 0;
  for (size_t i = 0; i < b.cr.size(); ++i) {
    const int y = static_cast<int>(i) / s.w;
    if (y < 330) continue;  // shirt edges only
    if (band[i]) {
      bandCr += b.cr[i];
      ++nb;
    } else if (far[i]) {
      farCr += b.cr[i];
      ++nf;
    }
  }
  bandCr /= static_cast<double>(std::max<size_t>(nb, 1));
  farCr /= static_cast<double>(std::max<size_t>(nf, 1));
  std::printf("    redness near subject %.4f vs background %.4f\n", bandCr, farCr);
  EXPECT_TRUE(nb > 200);
  EXPECT_TRUE(bandCr - farCr < 0.03);
  pp_image_destroy(r.out);
}

namespace {
double spotContrast(const Scene& s, const Ycc& c) {
  const int k = s.k;
  double sum = 0;
  for (const auto& sp : s.spots) {
    double in = 0, ring = 0;
    int ni = 0, nr = 0;
    for (int y = sp[1] - 9 * k; y <= sp[1] + 9 * k; ++y)
      for (int x = sp[0] - 9 * k; x <= sp[0] + 9 * k; ++x) {
        const float d = std::hypot(static_cast<float>(x - sp[0]), static_cast<float>(y - sp[1])) / static_cast<float>(k);
        const size_t i = static_cast<size_t>(y) * s.w + x;
        if (d <= 2) {
          in += c.y[i];
          ++ni;
        } else if (d >= 6 && d <= 9) {
          ring += c.y[i];
          ++nr;
        }
      }
    sum += ring / nr - in / ni;
  }
  return sum / static_cast<double>(s.spots.size());
}
}  // namespace

TEST(portrait_large_photo_retouches_on_a_small_budget) {
  Scene s(kDeep, 4, 3);  // 1080x1440, face ~375 px across
  pp_image* before = s.image();
  const Ycc a = yccOf(before);
  pp_image* img = s.image();
  pp_image* subj = s.mask(s.subject, 8);
  pp_image* faces = s.mask(s.face, 4);
  pp_image* prot = s.mask(s.protect, 4);
  const uint64_t imageBytes = static_cast<uint64_t>(s.w) * s.h * 4;
  pp_memory_reset_peak();
  const uint64_t base = liveBytes();
  const pp_portrait_params p = defaults();
  pp_portrait_report r{};
  EXPECT_EQ(pp_portrait(img, subj, faces, prot, nullptr, &p, nullptr, &r), PP_OK);
  pp_memory_stats st{};
  pp_memory_get_stats(&st);
  const Ycc b = yccOf(img);
  const double cBefore = spotContrast(s, a), cAfter = spotContrast(s, b);
  std::printf("    1080x1440: working memory %.1f MB (photo %.1f MB), spot contrast %.4f -> %.4f\n",
              static_cast<double>(st.peak_bytes - base) / 1e6, static_cast<double>(imageBytes) / 1e6, cBefore, cAfter);
  // ~30 MB whatever the photo size: retouching runs at a fixed face size.
  EXPECT_TRUE(st.peak_bytes - base < 40u * 1000 * 1000);
  EXPECT_TRUE(cAfter < 0.3 * cBefore);
  pp_image_destroy(subj);
  pp_image_destroy(faces);
  pp_image_destroy(prot);
  pp_image_destroy(img);
  pp_image_destroy(before);
}

TEST(portrait_spots_removed_texture_kept) {
  Scene s(kMedium);
  pp_image* before = s.image();
  const Ycc a = yccOf(before);
  Run r = portrait(s, defaults());
  const Ycc b = yccOf(r.out);
  const double cBefore = spotContrast(s, a), cAfter = spotContrast(s, b);
  // Pore texture on clean skin.
  std::vector<uint8_t> skin = andNot(andNot(erode(s.face, s.w, s.h, 8), s.protect), s.spotCentre);
  for (int y = 0; y < s.h; ++y)
    for (int x = 0; x < s.w; ++x)
      for (const auto& sp : s.spots)
        if (std::abs(x - sp[0]) < 9 && std::abs(y - sp[1]) < 9) skin[static_cast<size_t>(y) * s.w + x] = 0;
  const double tBefore = detail(a.y, skin, s.w, s.h), tAfter = detail(b.y, skin, s.w, s.h);
  std::printf("    spot contrast %.4f -> %.4f, skin texture %.4f -> %.4f, spots healed %d\n", cBefore, cAfter, tBefore,
              tAfter, r.report.spots_removed);
  EXPECT_TRUE(cAfter < 0.25 * cBefore);  // spots gone
  EXPECT_TRUE(tAfter < 0.6 * tBefore);   // visibly smoother
  EXPECT_TRUE(tAfter > 0.12 * tBefore);  // but still real skin, not plastic
  EXPECT_TRUE(r.report.spots_removed >= 8 && r.report.spots_removed <= 16);  // 11 real blemishes
  EXPECT_TRUE(r.report.skin_coverage > 0.02f);
  pp_image_destroy(before);
  pp_image_destroy(r.out);
}

TEST(portrait_keeps_every_skin_tone_its_own_colour) {
  for (const Rgbf tone : {kDeep, kMedium, kLight}) {
    Scene s(tone);
    pp_image* before = s.image();
    const Ycc a = yccOf(before);
    Run r = portrait(s, defaults());
    const Ycc b = yccOf(r.out);
    const auto skin = andNot(erode(s.face, s.w, s.h, 8), s.protect);
    double ya = 0, yb = 0, cba = 0, cra = 0, cbb = 0, crb = 0;
    size_t n = 0;
    for (size_t i = 0; i < skin.size(); ++i)
      if (skin[i]) {
        ya += a.y[i];
        yb += b.y[i];
        cba += a.cb[i];
        cra += a.cr[i];
        cbb += b.cb[i];
        crb += b.cr[i];
        ++n;
      }
    const double k = 1.0 / static_cast<double>(n);
    const double hueA = std::atan2(cra * k, cba * k), hueB = std::atan2(crb * k, cbb * k);
    const double chromaA = std::hypot(cba * k, cra * k), chromaB = std::hypot(cbb * k, crb * k);
    const double hueShiftDeg = std::fabs(hueB - hueA) * 180.0 / 3.14159265;
    const double satA = chromaA / (ya * k), satB = chromaB / (yb * k);  // colourfulness relative to light
    std::printf("    tone %.2f/%.2f/%.2f: luma %.3f -> %.3f, hue shift %.2f deg, chroma x%.3f\n", tone.r, tone.g, tone.b,
                ya * k, yb * k, hueShiftDeg, chromaB / chromaA);
    std::printf("      saturation x%.3f\n", satB / satA);
    EXPECT_TRUE(satB / satA > 0.94 && satB / satA < 1.08);           // never pale or ashy
    EXPECT_TRUE(hueShiftDeg < 3.0);                                  // same undertone
    EXPECT_TRUE(chromaB / chromaA > 0.9 && chromaB / chromaA < 1.12);  // not washed out or tinted
    EXPECT_TRUE(yb * k > ya * k);                                    // brighter...
    EXPECT_TRUE(yb * k - ya * k < 0.06);                             // ...never whitened
    pp_image_destroy(before);
    pp_image_destroy(r.out);
  }
}

TEST(portrait_keeps_the_shape_light_gives_a_face) {
  Scene s(kMedium);
  pp_image* before = s.image();
  const Ycc a = yccOf(before);
  Run r = portrait(s, defaults());
  const Ycc b = yccOf(r.out);
  // Mean light of the shadowed (left) vs lit (right) cheek.
  auto cheeks = [&](const Ycc& c) {
    double l = 0, rr = 0;
    int nl = 0, nr = 0;
    for (int y = 175; y < 200; ++y)
      for (int x = 130; x < 230; ++x) {
        const size_t i = static_cast<size_t>(y) * s.w + x;
        if (!s.face[i] || s.protect[i]) continue;
        if (x < 150) {
          l += c.y[i];
          ++nl;
        } else if (x > 210) {
          rr += c.y[i];
          ++nr;
        }
      }
    return (rr / nr) / (l / nl);
  };
  const double ra = cheeks(a), rb = cheeks(b);
  std::printf("    lit/shadow cheek ratio %.3f -> %.3f\n", ra, rb);
  EXPECT_TRUE(rb > 1.0 + 0.8 * (ra - 1.0));  // at least 80% of the modelling kept
  pp_image_destroy(before);
  pp_image_destroy(r.out);
}

TEST(portrait_eyes_brows_lips_stay_sharp) {
  Scene s(kMedium);
  pp_image* before = s.image();
  const Ycc a = yccOf(before);
  Run r = portrait(s, defaults());
  const Ycc b = yccOf(r.out);
  const auto features = erode(s.protect, s.w, s.h, 2);
  const double da = detail(a.y, features, s.w, s.h), db = detail(b.y, features, s.w, s.h);
  std::printf("    feature detail %.4f -> %.4f\n", da, db);
  EXPECT_TRUE(db > 0.85 * da);
  pp_image_destroy(before);
  pp_image_destroy(r.out);
}

// Deep and very deep skin, at a phone-like face size, with near-black
// nostrils that the feature mask does not cover (as with a real face model).
struct DeepFace {
  Scene s;
  std::vector<uint8_t> nostrils;
  explicit DeepFace(Rgbf tone) : s(tone, 5, 2) {
    nostrils.assign(s.px.size(), 0);
    for (int y = 0; y < s.h; ++y)
      for (int x = 0; x < s.w; ++x) {
        const float fx = static_cast<float>(x) / 2.0f, fy = static_cast<float>(y) / 2.0f;
        for (const float nx : {s.fcx - 9.0f, s.fcx + 9.0f})
          if (inEllipse(fx, fy, nx, s.fcy + 24.0f, 5.0f, 3.0f)) {
            s.px[static_cast<size_t>(y) * s.w + x] = {0.035f, 0.02f, 0.008f};
            nostrils[static_cast<size_t>(y) * s.w + x] = 1;
          }
      }
  }
};

pp_portrait_params glowFull() {
  pp_portrait_params p = defaults();
  p.retouch = 1.0f;
  p.pop = 0.3f;  // clarity pushes the darkest pixels hardest
  return p;
}

TEST(portrait_deep_skin_dark_features_get_no_false_colour) {
  for (const Rgbf tone : {kDeep, Rgbf{0.22f, 0.13f, 0.085f}}) {
    DeepFace f(tone);
    pp_image* before = f.s.image();
    const Ycc a = yccOf(before);
    Run r = portrait(f.s, glowFull());
    const Ycc b = yccOf(r.out);
    double worst = 0.0, ya = 0.0, yb = 0.0;
    size_t n = 0;
    for (size_t i = 0; i < f.nostrils.size(); ++i)
      if (f.nostrils[i]) {
        worst = std::max({worst, static_cast<double>(std::fabs(b.cb[i] - a.cb[i])), static_cast<double>(std::fabs(b.cr[i] - a.cr[i]))});
        ya += a.y[i];
        yb += b.y[i];
        ++n;
      }
    std::printf("    tone %.2f: nostril colour shift %.4f, luma %.3f -> %.3f\n", tone.r, worst, ya / n, yb / n);
    EXPECT_TRUE(worst < 0.015);          // no blue or green cast in near-black features
    EXPECT_TRUE(yb / n - ya / n < 0.04);  // and they are not "healed" into skin
    pp_image_destroy(before);
    pp_image_destroy(r.out);
  }
}

TEST(portrait_deep_skin_blemishes_healed_shape_kept) {
  for (const Rgbf tone : {kDeep, Rgbf{0.22f, 0.13f, 0.085f}}) {
    DeepFace f(tone);
    pp_image* before = f.s.image();
    const Ycc a = yccOf(before);
    Run r = portrait(f.s, defaults());
    const Ycc b = yccOf(r.out);
    const double cBefore = spotContrast(f.s, a), cAfter = spotContrast(f.s, b);
    // Lit/shadowed cheek ratio, relative to this tone (the shape of the face).
    auto cheeks = [&](const Ycc& c) {
      double l = 0, rr = 0;
      int nl = 0, nr = 0;
      for (int y = 350; y < 400; ++y)
        for (int x = 260; x < 460; ++x) {
          const size_t i = static_cast<size_t>(y) * f.s.w + x;
          if (!f.s.face[i] || f.s.protect[i] || f.s.spotCentre[i]) continue;
          if (x < 300) {
            l += c.y[i];
            ++nl;
          } else if (x > 420) {
            rr += c.y[i];
            ++nr;
          }
        }
      return (rr / nr) / (l / nl);
    };
    const double ra = cheeks(a), rb = cheeks(b);
    std::printf("    tone %.2f: spot contrast %.4f -> %.4f (%d healed), lit/shadow %.3f -> %.3f\n", tone.r, cBefore, cAfter,
                r.report.spots_removed, ra, rb);
    EXPECT_TRUE(cAfter < 0.3 * cBefore);       // blemishes on deep skin are found and cleared
    EXPECT_TRUE(r.report.spots_removed >= 8);  // ...each of them
    EXPECT_TRUE(r.report.spots_removed <= 16);  // nostrils are not counted as blemishes
    EXPECT_TRUE(rb > 1.0 + 0.8 * (ra - 1.0));   // shading not flattened
    pp_image_destroy(before);
    pp_image_destroy(r.out);
  }
}

TEST(portrait_neutral_settings_change_nothing) {
  Scene s(kLight);
  pp_image* before = s.image();
  pp_portrait_params p{0.0f, 0.0f, 0.0f, 0.0f};
  Run r = portrait(s, p);
  const auto* x = static_cast<const uint8_t*>(pp_image_pixels(before));
  const auto* y = static_cast<const uint8_t*>(pp_image_pixels(r.out));
  int maxDiff = 0;
  for (size_t i = 0; i < static_cast<size_t>(s.w) * s.h * 4; ++i) maxDiff = std::max(maxDiff, std::abs(x[i] - y[i]));
  EXPECT_TRUE(maxDiff <= 1);
  pp_image_destroy(before);
  pp_image_destroy(r.out);
}

TEST(portrait_without_faces_never_smooths_objects) {
  Scene s(kMedium);
  pp_image* before = s.image();
  const Ycc a = yccOf(before);
  Run r = portrait(s, defaults(), /*withFaces=*/false);
  const Ycc b = yccOf(r.out);
  const auto face = andNot(erode(s.face, s.w, s.h, 8), s.protect);
  EXPECT_TRUE(detail(b.y, face, s.w, s.h) > 0.9 * detail(a.y, face, s.w, s.h));
  EXPECT_EQ(r.report.spots_removed, 0);
  pp_image_destroy(before);
  pp_image_destroy(r.out);
}

TEST(portrait_is_deterministic_and_leak_free) {
  const uint64_t start = liveBytes();
  Scene s(kDeep);
  Run r1 = portrait(s, defaults());
  Run r2 = portrait(s, defaults());
  EXPECT_TRUE(std::memcmp(pp_image_pixels(r1.out), pp_image_pixels(r2.out), static_cast<size_t>(s.w) * s.h * 4) == 0);
  pp_image_destroy(r1.out);
  pp_image_destroy(r2.out);
  EXPECT_EQ(liveBytes(), start);
}

TEST(portrait_rejects_bad_input_and_honours_cancel) {
  Scene s(kMedium);
  pp_image* img = s.image();
  pp_image* subj = s.mask(s.subject, 2);
  const pp_portrait_params p = defaults();
  EXPECT_EQ(pp_portrait(img, nullptr, nullptr, nullptr, nullptr, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_portrait_params bad = p;
  bad.blur = NAN;
  EXPECT_EQ(pp_portrait(img, subj, nullptr, nullptr, nullptr, &bad, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_cancel_token* t = pp_cancel_token_create();
  pp_cancel_token_cancel(t);
  EXPECT_EQ(pp_portrait(img, subj, nullptr, nullptr, nullptr, &p, t, nullptr), PP_ERROR_CANCELLED);
  // ...also with skin retouching in play (never misreported as low memory).
  pp_image* faces = s.mask(s.face, 2);
  EXPECT_EQ(pp_portrait(img, subj, faces, nullptr, nullptr, &p, t, nullptr), PP_ERROR_CANCELLED);
  pp_image_destroy(faces);
  pp_cancel_token_destroy(t);
  float pts[2] = {0.5f, 0.5f};
  pp_image* out = nullptr;
  EXPECT_EQ(pp_subject_mask(img, nullptr, 1, nullptr, &out), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_subject_mask(img, pts, 65, nullptr, &out), PP_ERROR_INVALID_ARGUMENT);
  pp_image_destroy(subj);
  pp_image_destroy(img);
}

namespace {

// Two distinct objects on a textured background.
pp_image* twoObjects(int w, int h, std::vector<uint8_t>& disk, std::vector<uint8_t>& square) {
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
  auto* p = static_cast<uint8_t*>(pp_image_pixels(img));
  disk.assign(static_cast<size_t>(w) * h, 0);
  square.assign(static_cast<size_t>(w) * h, 0);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      float t = 0.45f + 0.1f * std::sin(x * 0.2f) * std::cos(y * 0.23f) + 0.08f * (hash01(x / 2, y / 2, 3) - 0.5f);
      float r = t * 0.8f, g = t * 0.85f, b = t * 0.7f;
      if (std::hypot(x - 0.3f * w, y - 0.5f * h) < 0.17f * w) {
        r = 0.85f + 0.05f * hash01(x, y, 5);
        g = 0.3f;
        b = 0.2f;
        disk[i] = 1;
      }
      if (std::fabs(x - 0.72f * w) < 0.12f * w && std::fabs(y - 0.55f * h) < 0.18f * h) {
        r = 0.15f;
        g = 0.3f + 0.05f * hash01(x, y, 6);
        b = 0.85f;
        square[i] = 1;
      }
      p[i * 4] = static_cast<uint8_t>(r * 255);
      p[i * 4 + 1] = static_cast<uint8_t>(g * 255);
      p[i * 4 + 2] = static_cast<uint8_t>(b * 255);
      p[i * 4 + 3] = 255;
    }
  return img;
}

// IoU of a (low-res) mask against a full-res truth.
double iou(pp_image* mask, const std::vector<uint8_t>& truth, int w, int h) {
  pp_image_info info{};
  pp_image_get_info(mask, &info);
  const auto* m = static_cast<const uint8_t*>(pp_image_pixels(mask));
  size_t inter = 0, uni = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int mx = std::min(info.width - 1, x * info.width / w), my = std::min(info.height - 1, y * info.height / h);
      const bool a = m[static_cast<size_t>(my) * info.stride + mx] > 127;
      const bool b = truth[static_cast<size_t>(y) * w + x] != 0;
      inter += (a && b) ? 1 : 0;
      uni += (a || b) ? 1 : 0;
    }
  return uni ? static_cast<double>(inter) / static_cast<double>(uni) : 0.0;
}

}  // namespace

TEST(tap_chooses_which_subject_stands_out) {
  const int w = 640, h = 480;
  std::vector<uint8_t> disk, square;
  pp_image* img = twoObjects(w, h, disk, square);
  float left[2] = {0.3f, 0.5f}, right[2] = {0.72f, 0.55f};
  pp_image* m1 = nullptr;
  pp_image* m2 = nullptr;
  EXPECT_EQ(pp_subject_mask(img, left, 1, nullptr, &m1), PP_OK);
  EXPECT_EQ(pp_subject_mask(img, right, 1, nullptr, &m2), PP_OK);
  const double a = iou(m1, disk, w, h), b = iou(m1, square, w, h);
  const double c = iou(m2, square, w, h), d = iou(m2, disk, w, h);
  std::printf("    tap left: disk IoU %.2f (square %.2f); tap right: square IoU %.2f (disk %.2f)\n", a, b, c, d);
  EXPECT_TRUE(a > 0.8 && b < 0.05);
  EXPECT_TRUE(c > 0.8 && d < 0.05);
  // Tapping both selects both.
  float both[4] = {0.3f, 0.5f, 0.72f, 0.55f};
  pp_image* m3 = nullptr;
  EXPECT_EQ(pp_subject_mask(img, both, 2, nullptr, &m3), PP_OK);
  std::vector<uint8_t> both2(disk.size());
  for (size_t i = 0; i < disk.size(); ++i) both2[i] = disk[i] | square[i];
  const double e = iou(m3, both2, w, h);
  std::printf("    tap both: IoU %.2f\n", e);
  EXPECT_TRUE(e > 0.8);
  pp_image_destroy(m1);
  pp_image_destroy(m2);
  pp_image_destroy(m3);
  pp_image_destroy(img);
}

TEST(tap_on_person_finds_the_person) {
  Scene s(kDeep);
  pp_image* img = s.image();
  float pts[4] = {0.5f, 0.35f, 0.5f, 0.8f};  // face and shirt
  pp_image* m = nullptr;
  EXPECT_EQ(pp_subject_mask(img, pts, 2, nullptr, &m), PP_OK);
  const double v = iou(m, s.subject, s.w, s.h);
  std::printf("    person IoU %.2f\n", v);
  EXPECT_TRUE(v > 0.75);
  pp_image_destroy(m);
  pp_image_destroy(img);
}

namespace {

// A "graded" look: lifted blacks, teal shadows, warm highlights, softer
// saturation and a vignette — a typical social-media preset.
pp_image* graded(const Scene& s) {
  pp_image* img = s.image();
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* p = static_cast<uint8_t*>(pp_image_pixels(img));
  for (int y = 0; y < s.h; ++y)
    for (int x = 0; x < s.w; ++x) {
      uint8_t* q = p + static_cast<size_t>(y) * info.stride + x * 4;
      float r = q[0] / 255.0f, g = q[1] / 255.0f, b = q[2] / 255.0f;
      const float l = 0.299f * r + 0.587f * g + 0.114f * b;
      r = l + 0.75f * (r - l);
      g = l + 0.75f * (g - l);
      b = l + 0.75f * (b - l);
      const float sh = std::clamp(1.0f - l * 2.0f, 0.0f, 1.0f), hi = std::clamp(l * 2.0f - 1.0f, 0.0f, 1.0f);
      r += 0.06f * hi - 0.03f * sh;
      b += 0.05f * sh - 0.04f * hi;
      g += 0.01f * sh;
      auto tone = [](float v) { return 0.08f + 0.9f * std::pow(std::clamp(v, 0.0f, 1.0f), 0.9f); };
      const float dx = (x - s.w * 0.5f) / (s.w * 0.5f), dy = (y - s.h * 0.5f) / (s.h * 0.5f);
      const float vig = 1.0f - 0.25f * std::clamp((dx * dx + dy * dy) / 2.0f, 0.0f, 1.0f);
      q[0] = static_cast<uint8_t>(std::lround(std::clamp(tone(r) * vig, 0.0f, 1.0f) * 255));
      q[1] = static_cast<uint8_t>(std::lround(std::clamp(tone(g) * vig, 0.0f, 1.0f) * 255));
      q[2] = static_cast<uint8_t>(std::lround(std::clamp(tone(b) * vig, 0.0f, 1.0f) * 255));
    }
  return img;
}

double meanAbs(pp_image* a, pp_image* b, int n) {
  const auto* x = static_cast<const uint8_t*>(pp_image_pixels(a));
  const auto* y = static_cast<const uint8_t*>(pp_image_pixels(b));
  double s = 0;
  for (int i = 0; i < n; ++i)
    for (int c = 0; c < 3; ++c) s += std::abs(x[i * 4 + c] - y[i * 4 + c]) / 255.0;
  return s / (3.0 * n);
}

}  // namespace

TEST(look_transfers_from_a_reference_photo) {
  Scene s(kMedium, 2);
  pp_image* ref = graded(s);
  pp_style look{};
  EXPECT_EQ(pp_style_analyze(ref, &look), PP_OK);
  pp_image* mine = s.image();
  pp_style own{};
  pp_style_analyze(mine, &own);
  EXPECT_TRUE(look.luma[0] > own.luma[0] + 0.04f);     // lifted blacks detected
  EXPECT_TRUE(look.chroma_std[1] < own.chroma_std[1]); // softer colour
  EXPECT_TRUE(look.zone_cb[0] > own.zone_cb[0]);       // cooler (teal) shadows
  EXPECT_TRUE(look.vignette < own.vignette);           // darker corners
  const int n = s.w * s.h;
  const double before = meanAbs(mine, ref, n);
  EXPECT_EQ(pp_style_apply(mine, &look, 1.0f, nullptr), PP_OK);
  const double after = meanAbs(mine, ref, n);
  std::printf("    distance to the reference look %.4f -> %.4f\n", before, after);
  EXPECT_TRUE(after < 0.45 * before);
  pp_image_destroy(ref);
  pp_image_destroy(mine);
}

TEST(look_of_itself_or_zero_strength_changes_nothing) {
  Scene s(kDeep, 3);
  pp_image* img = s.image();
  pp_image* orig = s.image();
  pp_style own{};
  EXPECT_EQ(pp_style_analyze(img, &own), PP_OK);
  EXPECT_EQ(pp_style_apply(img, &own, 1.0f, nullptr), PP_OK);
  const int n = s.w * s.h;
  EXPECT_TRUE(meanAbs(img, orig, n) < 0.004);
  pp_image* img2 = s.image();
  pp_style other{};
  pp_image* ref = graded(s);
  pp_style_analyze(ref, &other);
  EXPECT_EQ(pp_style_apply(img2, &other, 0.0f, nullptr), PP_OK);
  EXPECT_TRUE(meanAbs(img2, orig, n) == 0.0);
  pp_style broken = other;
  broken.luma[3] = NAN;
  EXPECT_EQ(pp_style_apply(img2, &broken, 1.0f, nullptr), PP_ERROR_INVALID_ARGUMENT);
  for (float* field : {&broken.zone_cb[1], &broken.zone_cr[0], &broken.chroma_std[1], &broken.mean_luma}) {
    broken = other;
    *field = NAN;
    EXPECT_EQ(pp_style_apply(img2, &broken, 1.0f, nullptr), PP_ERROR_INVALID_ARGUMENT);
  }
  pp_image_destroy(img);
  pp_image_destroy(img2);
  pp_image_destroy(orig);
  pp_image_destroy(ref);
}

// ---------------------------------------------------------------------------
// Depth: a person standing on textured ground in front of a wall. Like a real
// lens, the ground at their feet stays sharp, the ground further back blurs
// progressively, the wall blurs most, and ground nearer than them blurs too.
namespace {

struct Scene3D {
  int w = 360, h = 480, horizon = 180, feet = 400;
  std::vector<uint8_t> rgba, person, depth;
  float groundDepth(int y) const { return 0.2f + 0.8f * static_cast<float>(y - horizon) / static_cast<float>(h - horizon); }

  explicit Scene3D(bool closeUp = false) {
    const auto n = static_cast<size_t>(w) * h;
    rgba.assign(n * 4, 255);
    person.assign(n, 0);
    depth.assign(n, 0);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * w + x;
        float r, g, b, dv;
        if (y < horizon) {  // wall
          const float t = 0.45f + 0.25f * (((x / 6) + (y / 6)) % 2) + 0.05f * (hash01(x, y, 11) - 0.5f);
          r = t * 0.8f, g = t * 0.75f, b = t * 0.9f, dv = 0.1f;
        } else {  // ground with texture that stays the same size in pixels
          // Fine and coarse pattern, so both light and heavy blur are measurable.
          const float t = 0.3f + 0.15f * (((x / 4) + (y / 4)) % 2) + 0.2f * (((x / 12) + (y / 12)) % 2) +
                          0.05f * (hash01(x, y, 12) - 0.5f);
          r = t * 0.6f, g = t * 0.9f, b = t * 0.5f, dv = groundDepth(y);
        }
        const bool body = closeUp ? (inEllipse(x, y, 180, 330, 120, 200) || y > 400)
                                  : (inEllipse(x, y, 180, 150, 28, 34) || (std::abs(x - 180) < 40 && y > 180 && y <= feet));
        if (body) {
          const float t = 0.55f + 0.2f * ((x / 2 + y / 2) % 2);
          r = t * 0.85f, g = t * 0.2f, b = t * 0.25f;
          dv = closeUp ? 0.9f : groundDepth(feet);
          person[i] = 255;
        }
        rgba[i * 4] = static_cast<uint8_t>(r * 255);
        rgba[i * 4 + 1] = static_cast<uint8_t>(g * 255);
        rgba[i * 4 + 2] = static_cast<uint8_t>(b * 255);
        depth[i] = static_cast<uint8_t>(std::lround(std::clamp(dv, 0.0f, 1.0f) * 255));
      }
  }

  pp_image* image() const {
    pp_image* img = nullptr;
    pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
    std::memcpy(pp_image_pixels(img), rgba.data(), rgba.size());
    return img;
  }
  pp_image* gray(const std::vector<uint8_t>& v, int f) const {
    pp_image* img = nullptr;
    pp_image_create(w / f, h / f, PP_FORMAT_GRAY8, &img);
    pp_image_info info{};
    pp_image_get_info(img, &info);
    auto* p = static_cast<uint8_t*>(pp_image_pixels(img));
    for (int y = 0; y < h / f; ++y)
      for (int x = 0; x < w / f; ++x) p[static_cast<size_t>(y) * info.stride + x] = v[static_cast<size_t>(y * f) * w + x * f];
    return img;
  }
};

// Texture kept in a horizontal band of rows, away from the person.
double bandTexture(const Ycc& a, const Ycc& b, const Scene3D& s, int y0, int y1) {
  std::vector<uint8_t> region(static_cast<size_t>(s.w) * s.h, 0);
  for (int y = y0; y < y1; ++y)
    for (int x = 8; x < s.w - 8; ++x)
      if (std::abs(x - 180) > 70) region[static_cast<size_t>(y) * s.w + x] = 1;
  // Band-pass at two scales: fine detail and the coarser pattern.
  auto measure = [&](const Ycc& c) {
    const auto b1 = boxBlur(c.y, s.w, s.h, 1), b4 = boxBlur(c.y, s.w, s.h, 4), b12 = boxBlur(c.y, s.w, s.h, 12);
    double sum = 0;
    for (size_t i = 0; i < region.size(); ++i)
      if (region[i]) sum += std::fabs(b1[i] - b4[i]) + std::fabs(b4[i] - b12[i]);
    return sum;
  };
  return measure(b) / measure(a);
}

pp_portrait_params blurOnly() {
  pp_portrait_params p = defaults();
  p.retouch = 0.0f;
  p.brighten = 0.0f;
  p.pop = 0.0f;
  return p;
}

}  // namespace

TEST(depth_blur_grows_with_distance_like_a_lens) {
  Scene3D s;
  pp_image* before = s.image();
  pp_image* img = s.image();
  pp_image* subj = s.gray(s.person, 2);
  pp_image* dep = s.gray(s.depth, 4);  // depth models output low resolution
  const pp_portrait_params p = blurOnly();
  pp_portrait_report r{};
  EXPECT_EQ(pp_portrait(img, subj, nullptr, nullptr, dep, &p, nullptr, &r), PP_OK);
  const Ycc a = yccOf(before), b = yccOf(img);
  const double atFeet = bandTexture(a, b, s, 385, 405);
  const double midGround = bandTexture(a, b, s, 290, 320);
  const double farGround = bandTexture(a, b, s, 185, 215);
  const double wall = bandTexture(a, b, s, 40, 150);
  const double nearGround = bandTexture(a, b, s, 450, 475);
  std::printf("    texture kept: wall %.2f, far ground %.2f, mid %.2f, at the feet %.2f, in front %.2f (focus %.2f)\n", wall,
              farGround, midGround, atFeet, nearGround, r.focus_depth);
  EXPECT_TRUE(atFeet > 0.75);                 // where they stand stays sharp
  EXPECT_TRUE(midGround < atFeet - 0.15);     // softer further back...
  EXPECT_TRUE(farGround < midGround);         // ...and softer still
  EXPECT_TRUE(wall < 0.3);                    // the far background melts
  EXPECT_TRUE(nearGround < atFeet - 0.1);     // nearer than them blurs too
  EXPECT_TRUE(r.depth_estimated == 0);
  EXPECT_NEAR(r.focus_depth, s.groundDepth(s.feet), 0.05);
  // The person stays crisp.
  std::vector<uint8_t> body(s.person.size());
  for (size_t i = 0; i < body.size(); ++i) body[i] = s.person[i] ? 1 : 0;
  const auto core = erode(body, s.w, s.h, 5);
  EXPECT_TRUE(detail(b.y, core, s.w, s.h) > 0.9 * detail(a.y, core, s.w, s.h));
  pp_image_destroy(dep);
  pp_image_destroy(subj);
  pp_image_destroy(img);
  pp_image_destroy(before);
}

TEST(depth_stronger_blur_means_shallower_focus) {
  Scene3D s;
  pp_image* before = s.image();
  const Ycc a = yccOf(before);
  double mid[2];
  for (int i = 0; i < 2; ++i) {
    pp_image* img = s.image();
    pp_image* subj = s.gray(s.person, 2);
    pp_image* dep = s.gray(s.depth, 4);
    pp_portrait_params p = blurOnly();
    p.blur = i == 0 ? 0.35f : 0.9f;
    EXPECT_EQ(pp_portrait(img, subj, nullptr, nullptr, dep, &p, nullptr, nullptr), PP_OK);
    mid[i] = bandTexture(a, yccOf(img), s, 300, 330);
    pp_image_destroy(dep);
    pp_image_destroy(subj);
    pp_image_destroy(img);
  }
  std::printf("    mid-ground texture kept: light %.2f, strong %.2f\n", mid[0], mid[1]);
  EXPECT_TRUE(mid[1] < mid[0] - 0.1);
  pp_image_destroy(before);
}

TEST(without_depth_the_ground_plane_is_estimated) {
  Scene3D s;
  pp_image* before = s.image();
  pp_image* img = s.image();
  pp_image* subj = s.gray(s.person, 2);
  const pp_portrait_params p = blurOnly();
  pp_portrait_report r{};
  EXPECT_EQ(pp_portrait(img, subj, nullptr, nullptr, nullptr, &p, nullptr, &r), PP_OK);
  const Ycc a = yccOf(before), b = yccOf(img);
  const double atFeet = bandTexture(a, b, s, 385, 405), farGround = bandTexture(a, b, s, 185, 215);
  const double wall = bandTexture(a, b, s, 40, 150);
  std::printf("    estimated depth: at the feet %.2f, far ground %.2f, wall %.2f\n", atFeet, farGround, wall);
  EXPECT_TRUE(r.depth_estimated == 1);
  EXPECT_TRUE(atFeet > 0.7);
  EXPECT_TRUE(farGround < atFeet - 0.25);
  EXPECT_TRUE(wall < 0.35);
  pp_image_destroy(subj);
  pp_image_destroy(img);
  pp_image_destroy(before);
}

TEST(close_up_without_depth_blurs_the_whole_background) {
  Scene3D s(/*closeUp=*/true);
  pp_image* before = s.image();
  pp_image* img = s.image();
  pp_image* subj = s.gray(s.person, 2);
  const pp_portrait_params p = blurOnly();
  EXPECT_EQ(pp_portrait(img, subj, nullptr, nullptr, nullptr, &p, nullptr, nullptr), PP_OK);
  const Ycc a = yccOf(before), b = yccOf(img);
  // Beside the shoulders (bottom of the frame) is background too: no sharp patch.
  std::vector<uint8_t> side(static_cast<size_t>(s.w) * s.h, 0);
  for (int y = 300; y < 400; ++y)
    for (int x = 4; x < 40; ++x) side[static_cast<size_t>(y) * s.w + x] = s.person[static_cast<size_t>(y) * s.w + x] ? 0 : 1;
  const double kept = texture(b.y, side, s.w, s.h) / texture(a.y, side, s.w, s.h);
  std::printf("    beside the shoulders: texture kept %.2f\n", kept);
  EXPECT_TRUE(kept < 0.5);
  pp_image_destroy(subj);
  pp_image_destroy(img);
  pp_image_destroy(before);
}
