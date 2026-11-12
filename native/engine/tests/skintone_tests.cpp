// Skin Tone. What a person would notice:
//  - the skin of the face and the neck takes the chosen shade's undertone,
//    for every starting tone, deeper or fairer (never ashen or orange);
//  - nothing else changes: background, eyes, lips and teeth keep their colour;
//  - the face stays the person's: brightness moves only part of the way,
//    texture is kept, and nothing newly clips;
//  - strength 0 changes nothing and strength blends;
//  - with no face to measure, nothing changes.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "face_fixture.hpp"
#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

using namespace fixture;

// Masks as the app draws them: the face oval from landmarks (the neck is
// body, not face), the subject, and the protected features.
pp_image* maskWhere(const Face& f, bool (*pick)(const Face&, int, int)) {
  pp_image* m = nullptr;
  pp_image_create(f.w, f.h, PP_FORMAT_GRAY8, &m);
  auto* p = static_cast<uint8_t*>(pp_image_pixels(m));
  pp_image_info info{};
  pp_image_get_info(m, &info);
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) p[static_cast<size_t>(y) * info.stride + x] = pick(f, x, y) ? 255 : 0;
  return m;
}
bool inOval(const Face& f, int x, int y) {
  return ell((static_cast<float>(x) + 0.5f) / f.k, (static_cast<float>(y) + 0.5f) / f.k, f.cx, f.cy, f.rx, f.ry) <= 1.0f;
}
uint8_t regionAt(const Face& f, int x, int y) { return f.region[static_cast<size_t>(y) * f.w + x]; }
bool isSubject(const Face& f, int x, int y) { return regionAt(f, x, y) != 9; }
bool isProtect(const Face& f, int x, int y) {
  const uint8_t r = regionAt(f, x, y);
  return r == 2 || r == 3 || r == 4 || r == 5;
}
// The neck (region 0 outside the face oval): body skin.
bool isNeck(const Face& f, int x, int y) { return regionAt(f, x, y) == 0 && !inOval(f, x, y); }

struct Masks {
  pp_image *subject, *faces, *protect;
  explicit Masks(const Face& f) : subject(maskWhere(f, isSubject)), faces(maskWhere(f, inOval)), protect(maskWhere(f, isProtect)) {}
  ~Masks() {
    pp_image_destroy(subject);
    pp_image_destroy(faces);
    pp_image_destroy(protect);
  }
  Masks(const Masks&) = delete;
  Masks& operator=(const Masks&) = delete;
};

pp_skin_tone_params shade(float r, float g, float b, float glow = 0.0f, float strength = 1.0f) {
  pp_skin_tone_params p{};
  p.shade_r = r, p.shade_g = g, p.shade_b = b, p.glow = glow, p.strength = strength;
  return p;
}

pp_image* run(const Face& f, const Masks& m, const pp_skin_tone_params& p, int32_t* hasSkin = nullptr) {
  pp_image* img = f.image();
  EXPECT_EQ(pp_skin_tone(img, m.subject, m.faces, m.protect, &p, nullptr, hasSkin, nullptr), PP_OK);
  return img;
}

// Mean normalised chroma (cb, cr divided by brightness, as skin colour is
// compared) and mean linear luminance over pixels picked by `pick`.
struct Skin {
  double ncb = 0, ncr = 0, lin = 0;
};
Skin skinOf(pp_image* img, const Face& f, bool (*pick)(const Face&, int, int)) {
  const auto* p = static_cast<const uint8_t*>(pp_image_pixels(img));
  pp_image_info info{};
  pp_image_get_info(img, &info);
  Skin s;
  size_t n = 0;
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) {
      if (!pick(f, x, y)) continue;
      const Px v = ycc(p + static_cast<size_t>(y) * info.stride + x * 4);
      s.ncb += v.cb / (v.y + 0.05f), s.ncr += v.cr / (v.y + 0.05f);
      const uint8_t* q = p + static_cast<size_t>(y) * info.stride + x * 4;
      auto lin = [](uint8_t c) { const float v = c / 255.0f; return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
      s.lin += 0.2126f * lin(q[0]) + 0.7152f * lin(q[1]) + 0.0722f * lin(q[2]);
      ++n;
    }
  const double d = n ? static_cast<double>(n) : 1.0;
  return {s.ncb / d, s.ncr / d, s.lin / d};
}
Skin shadeSkin(const pp_skin_tone_params& p) {
  const float y = 0.299f * p.shade_r + 0.587f * p.shade_g + 0.114f * p.shade_b;
  const float cb = -0.168736f * p.shade_r - 0.331264f * p.shade_g + 0.5f * p.shade_b;
  const float cr = 0.5f * p.shade_r - 0.418688f * p.shade_g - 0.081312f * p.shade_b;
  auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
  return {cb / (y + 0.05f), cr / (y + 0.05f), 0.2126f * lin(p.shade_r) + 0.7152f * lin(p.shade_g) + 0.0722f * lin(p.shade_b)};
}
bool isCheek(const Face& f, int x, int y) { return regionAt(f, x, y) == 8; }
// How far the undertone (the hue of the chroma) of `b` went from `a`'s to
// the shade `t`'s (1 = all the way).
double progress(const Skin& a, const Skin& b, const Skin& t) {
  const double ha = std::atan2(a.ncr, a.ncb), hb = std::atan2(b.ncr, b.ncb), ht = std::atan2(t.ncr, t.ncb);
  return std::fabs(ht - ha) > 1e-3 ? (hb - ha) / (ht - ha) : 1.0;
}
double richness(const Skin& v) { return std::hypot(v.ncb, v.ncr); }

const pp_skin_tone_params kFair = shade(0.93f, 0.83f, 0.71f);    // Ivory
const pp_skin_tone_params kDeep = shade(0.30f, 0.20f, 0.15f);    // Espresso
const pp_skin_tone_params kOlive = shade(0.71f, 0.61f, 0.41f);   // Olive
const pp_skin_tone_params kRose = shade(0.95f, 0.84f, 0.79f, 0.15f);  // Porcelain Rose

}  // namespace

TEST(skintone_moves_face_and_neck_toward_the_shade_for_every_tone) {
  for (size_t t = 0; t < kTones.size(); ++t) {
    const Face f(kTones[t], {});
    const Masks m(f);
    pp_image* before = f.image();
    const Skin c0 = skinOf(before, f, isCheek), n0 = skinOf(before, f, isNeck);
    for (const auto& p : {kFair, kDeep, kOlive}) {
      int32_t hasSkin = 0;
      pp_image* after = run(f, m, p, &hasSkin);
      const Skin c1 = skinOf(after, f, isCheek), n1 = skinOf(after, f, isNeck);
      const Skin target = shadeSkin(p);
      const double pc = progress(c0, c1, target), pn = progress(n0, n1, target);
      std::printf("    tone %zu → shade (%.2f %.2f %.2f): undertone %.0f%% of the way (neck %.0f%%), richness %.2f→%.2f (shade %.2f), brightness x%.2f\n",
                  t, p.shade_r, p.shade_g, p.shade_b, 100 * pc, 100 * pn, richness(c0), richness(c1), richness(target), c1.lin / c0.lin);
      EXPECT_EQ(hasSkin, 1);
      EXPECT_TRUE(pc > 0.75 && pc < 1.2);
      EXPECT_TRUE(pn > 0.6 && pn < 1.2);
      // Richness moves toward the shade's, never past it.
      const double r0 = richness(c0), r1 = richness(c1), rt = richness(target);
      EXPECT_TRUE((r1 - r0) * (rt - r0) >= -1e-3 && std::fabs(r1 - r0) <= std::fabs(rt - r0) + 0.02);
      // Brightness follows the shade, only part of the way and within ~0.7 stop.
      const double gain = c1.lin / c0.lin, wanted = target.lin / c0.lin;
      // (The cap is x0.62..x1.6 in linear light; the new undertone moves true luminance a little.)
      EXPECT_TRUE(gain > 0.58 && gain < 1.72);
      if (wanted > 1.3) EXPECT_TRUE(gain > 1.05);
      if (wanted < 0.75) EXPECT_TRUE(gain < 0.95);
      pp_image_destroy(after);
    }
    pp_image_destroy(before);
  }
}

TEST(skintone_leaves_everything_but_skin_alone) {
  const Face f(kTones[2], {});
  const Masks m(f);
  pp_image* before = f.image();
  for (const auto& p : {kFair, kDeep, kRose}) {
    pp_image* after = run(f, m, p);
    const double bg = diffOver(before, after, f, 9), eyes = diffOver(before, after, f, 2), iris = diffOver(before, after, f, 3);
    const double teeth = diffOver(before, after, f, 4), lips = diffOver(before, after, f, 5), cheek = diffOver(before, after, f, 8);
    std::printf("    change (levels): cheek %.1f; background %.2f, eye whites %.1f, iris %.1f, teeth %.1f, lips %.1f\n", cheek, bg, eyes,
                iris, teeth, lips);
    EXPECT_TRUE(cheek > 6.0);
    EXPECT_TRUE(bg < 0.3);
    EXPECT_TRUE(eyes < 2.0 && iris < 2.0 && teeth < 2.0);
    EXPECT_TRUE(lips < 3.5);
    pp_image_destroy(after);
  }
  pp_image_destroy(before);
}

TEST(skintone_keeps_texture_and_never_clips) {
  for (size_t t : {size_t{0}, size_t{5}}) {
    const Face f(kTones[t], {});
    const Masks m(f);
    pp_image* before = f.image();
    for (const auto& p : {kRose, kDeep, shade(0.23f, 0.15f, 0.11f, 0.6f)}) {
      pp_image* after = run(f, m, p);
      const double tex = fineTexture(after, f, 8) / fineTexture(before, f, 8);
      const auto* a = static_cast<const uint8_t*>(pp_image_pixels(after));
      const auto* b = static_cast<const uint8_t*>(pp_image_pixels(before));
      pp_image_info info{};
      pp_image_get_info(after, &info);
      size_t newly = 0;
      for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x) {
          const size_t o = static_cast<size_t>(y) * info.stride + x * 4;
          const bool wasClipped = b[o] == 255 || b[o + 1] == 255 || b[o + 2] == 255;
          const bool isClipped = a[o] == 255 || a[o + 1] == 255 || a[o + 2] == 255;
          if (isClipped && !wasClipped) ++newly;
        }
      std::printf("    tone %zu: cheek texture x%.2f, newly clipped %zu\n", t, tex, newly);
      // Brightened skin shows its own texture a little more, darkened a little less.
      EXPECT_TRUE(tex > 0.75 && tex < 1.45);
      EXPECT_EQ(newly, size_t{0});
      pp_image_destroy(after);
    }
    pp_image_destroy(before);
  }
}

TEST(skintone_strength_blends_and_zero_changes_nothing) {
  const Face f(kTones[1], {});
  const Masks m(f);
  pp_image* before = f.image();
  pp_image* none = run(f, m, shade(0.3f, 0.2f, 0.15f, 0.0f, 0.0f));
  EXPECT_NEAR(diffOver(before, none, f, 0), 0.0, 1e-9);
  pp_image* half = run(f, m, shade(0.3f, 0.2f, 0.15f, 0.0f, 0.5f));
  pp_image* full = run(f, m, kDeep);
  const double dh = diffOver(before, half, f, 8), df = diffOver(before, full, f, 8);
  std::printf("    cheek change: half %.1f, full %.1f\n", dh, df);
  EXPECT_TRUE(dh > 0.35 * df && dh < 0.65 * df);
  for (pp_image* i : {before, none, half, full}) pp_image_destroy(i);
}

TEST(skintone_without_a_face_changes_nothing) {
  const Face f(kTones[3], {});
  const Masks m(f);
  pp_image* before = f.image();
  pp_image* img = f.image();
  int32_t hasSkin = 7;
  float cov = -1;
  // No face mask at all, then an empty one.
  EXPECT_EQ(pp_skin_tone(img, m.subject, nullptr, m.protect, &kDeep, nullptr, &hasSkin, &cov), PP_OK);
  EXPECT_EQ(hasSkin, 0);
  EXPECT_NEAR(diffOver(before, img, f, 0), 0.0, 1e-9);
  pp_image* empty = maskWhere(f, [](const Face&, int, int) { return false; });
  EXPECT_EQ(pp_skin_tone(img, m.subject, empty, m.protect, &kDeep, nullptr, &hasSkin, &cov), PP_OK);
  EXPECT_EQ(hasSkin, 0);
  EXPECT_NEAR(diffOver(before, img, f, 0), 0.0, 1e-9);
  pp_image_destroy(empty);
  // A face too small for the working grid: whatever happens, nothing breaks.
  pp_image* tiny = maskWhere(f, [](const Face& ff, int x, int y) {
    return std::abs(x - static_cast<int>(ff.cx * ff.k)) < 2 && std::abs(y - static_cast<int>((ff.cy + 20) * ff.k)) < 2;
  });
  EXPECT_EQ(pp_skin_tone(img, m.subject, tiny, m.protect, &kDeep, nullptr, &hasSkin, &cov), PP_OK);
  EXPECT_TRUE(std::isfinite(cov));
  pp_image_destroy(tiny);
  pp_image_destroy(img);
  pp_image_destroy(before);
}

TEST(skintone_rejects_bad_input_and_can_be_cancelled) {
  const Face f(kTones[2], {});
  const Masks m(f);
  pp_image* img = f.image();
  EXPECT_EQ(pp_skin_tone(nullptr, m.subject, m.faces, m.protect, &kDeep, nullptr, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_skin_tone(img, m.subject, m.faces, m.protect, nullptr, nullptr, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_skin_tone_params bad = kDeep;
  bad.shade_g = std::nanf("");
  EXPECT_EQ(pp_skin_tone(img, m.subject, m.faces, m.protect, &bad, nullptr, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  bad = kDeep;
  bad.strength = std::numeric_limits<float>::infinity();
  EXPECT_EQ(pp_skin_tone(img, m.subject, m.faces, m.protect, &bad, nullptr, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_cancel_token* token = pp_cancel_token_create();
  pp_cancel_token_cancel(token);
  EXPECT_EQ(pp_skin_tone(img, m.subject, m.faces, m.protect, &kDeep, token, nullptr, nullptr), PP_ERROR_CANCELLED);
  pp_cancel_token_destroy(token);
  // Out-of-range values are clamped, not refused; works without subject or protect.
  pp_skin_tone_params loud = shade(2.0f, -1.0f, 0.5f, 3.0f, 9.0f);
  int32_t hasSkin = 0;
  float cov = 0;
  EXPECT_EQ(pp_skin_tone(img, nullptr, m.faces, nullptr, &loud, nullptr, &hasSkin, &cov), PP_OK);
  EXPECT_EQ(hasSkin, 1);
  EXPECT_TRUE(cov > 0.02f && cov < 0.5f);
  pp_image_destroy(img);
}
