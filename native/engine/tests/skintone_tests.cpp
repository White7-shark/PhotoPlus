// Skin Tone. What a person would notice:
//  - the skin of the face and the neck changes by the shade (a change of the
//    skin's own colour), for every starting tone, the face and body alike;
//  - it belongs to the photo: under a coloured light, the result is the
//    shade under that light (the scene's light stays on the skin);
//  - nothing else changes: background, eyes, lips and teeth keep their colour;
//  - texture is kept, and nothing newly clips;
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

// A shade as gains on the skin's own colour, linear light.
pp_skin_tone_params shade(float r, float g, float b, float glow = 0.0f, float strength = 1.0f) {
  pp_skin_tone_params p{};
  p.gain_r = r, p.gain_g = g, p.gain_b = b, p.glow = glow, p.strength = strength, p.evenness = 0.5f;
  return p;
}

pp_image* run(const Face& f, const Masks& m, const pp_skin_tone_params& p, int32_t* hasSkin = nullptr) {
  pp_image* img = f.image();
  EXPECT_EQ(pp_skin_tone(img, m.subject, m.faces, m.protect, &p, nullptr, hasSkin, nullptr), PP_OK);
  return img;
}

inline float linOf(uint8_t c) {
  const float v = c / 255.0f;
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
// Mean linear RGB over pixels picked by `pick`.
struct Lin {
  double r = 0, g = 0, b = 0;
};
Lin linOver(pp_image* img, const Face& f, bool (*pick)(const Face&, int, int)) {
  const auto* p = static_cast<const uint8_t*>(pp_image_pixels(img));
  pp_image_info info{};
  pp_image_get_info(img, &info);
  Lin s;
  size_t n = 0;
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) {
      if (!pick(f, x, y)) continue;
      const uint8_t* q = p + static_cast<size_t>(y) * info.stride + x * 4;
      s.r += linOf(q[0]), s.g += linOf(q[1]), s.b += linOf(q[2]);
      ++n;
    }
  const double d = n ? static_cast<double>(n) : 1.0;
  return {s.r / d, s.g / d, s.b / d};
}
bool isCheek(const Face& f, int x, int y) { return regionAt(f, x, y) == 8; }
// The colour change achieved over a region, as gains; and its mean log
// distance from the gains asked for.
Lin gainsOf(const Lin& a, const Lin& b) { return {b.r / a.r, b.g / a.g, b.b / a.b}; }
double gainError(const Lin& got, const pp_skin_tone_params& p) {
  return (std::fabs(std::log(got.r / p.gain_r)) + std::fabs(std::log(got.g / p.gain_g)) + std::fabs(std::log(got.b / p.gain_b))) / 3.0;
}

// Shades like the catalogue's: lighter and cleaner, golden, deeper and warmer.
const pp_skin_tone_params kFair = shade(1.45f, 1.43f, 1.66f);         // Ivory
const pp_skin_tone_params kGolden = shade(1.35f, 1.36f, 1.10f);       // Golden Beige
const pp_skin_tone_params kDeep = shade(0.82f, 0.68f, 0.62f);         // Espresso
const pp_skin_tone_params kRose = shade(1.54f, 1.32f, 1.58f, 0.15f);  // Porcelain Rose

}  // namespace

TEST(skintone_changes_face_and_neck_by_the_shade_for_every_tone) {
  for (size_t t = 0; t < kTones.size(); ++t) {
    const Face f(kTones[t], {});
    const Masks m(f);
    pp_image* before = f.image();
    const Lin c0 = linOver(before, f, isCheek), n0 = linOver(before, f, isNeck);
    for (const auto& p : {kFair, kGolden, kDeep}) {
      int32_t hasSkin = 0;
      pp_image* after = run(f, m, p, &hasSkin);
      const Lin c = gainsOf(c0, linOver(after, f, isCheek)), n = gainsOf(n0, linOver(after, f, isNeck));
      const double ec = gainError(c, p), en = gainError(n, p);
      std::printf("    tone %zu, shade (%.2f %.2f %.2f): cheek gains %.2f %.2f %.2f (off %.3f), neck %.2f %.2f %.2f (off %.3f)\n", t,
                  p.gain_r, p.gain_g, p.gain_b, c.r, c.g, c.b, ec, n.r, n.g, n.b, en);
      EXPECT_EQ(hasSkin, 1);
      // Close to the shade on the face and on the body. The fairest skin
      // has no headroom to be lifted further (it would lose its texture
      // toward white): it takes the shade's colour balance, not its lift.
      if (t + 1 < kTones.size() || p.gain_g < 1.0f) {
        EXPECT_TRUE(ec < 0.05);
        EXPECT_TRUE(en < 0.06);
      } else {
        EXPECT_TRUE(std::fabs(std::log((c.r / c.g) / (p.gain_r / p.gain_g))) < 0.04);
        EXPECT_TRUE(std::fabs(std::log((c.b / c.g) / (p.gain_b / p.gain_g))) < 0.04);
        EXPECT_TRUE(c.g < 1.1);
      }
      pp_image_destroy(after);
    }
    pp_image_destroy(before);
  }
}

// The shade belongs to the photo: the same face lit by a warm lamp and by a
// cool window takes the shade under that light, not a fixed colour.
TEST(skintone_keeps_the_scene_light_on_the_skin) {
  const Face f(kTones[2], {});
  const Masks m(f);
  const float lights[2][3] = {{1.0f, 0.82f, 0.60f}, {0.80f, 0.92f, 1.0f}};
  pp_image* plain = f.image();
  pp_image* plainOut = run(f, m, kGolden);
  const Lin s0 = linOver(plain, f, isCheek), s1 = linOver(plainOut, f, isCheek);
  for (const auto& L : lights) {
    // The photo under a coloured light (the whole frame: skin, wall, everything).
    Face lit = f;
    for (auto& c : lit.px) {
      auto relight = [](float v, float k) {
        const float l = (v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f)) * k;
        return l <= 0.0031308f ? 12.92f * l : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
      };
      c = {relight(c.r, L[0]), relight(c.g, L[1]), relight(c.b, L[2])};
    }
    const Masks ml(lit);
    pp_image* in = lit.image();
    pp_image* out = run(lit, ml, kGolden);
    const Lin g = gainsOf(linOver(in, lit, isCheek), linOver(out, lit, isCheek)), g0 = gainsOf(s0, s1);
    const double d = (std::fabs(std::log(g.r / g0.r)) + std::fabs(std::log(g.g / g0.g)) + std::fabs(std::log(g.b / g0.b))) / 3.0;
    std::printf("    light (%.2f %.2f %.2f): skin gains %.2f %.2f %.2f vs %.2f %.2f %.2f in plain light (off %.3f); wall %.2f levels\n", L[0], L[1],
                L[2], g.r, g.g, g.b, g0.r, g0.g, g0.b, d, diffOver(in, out, lit, 9));
    EXPECT_TRUE(d < 0.06);
    EXPECT_TRUE(diffOver(in, out, lit, 9) < 0.3);
    pp_image_destroy(in);
    pp_image_destroy(out);
  }
  pp_image_destroy(plain);
  pp_image_destroy(plainOut);
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
    EXPECT_TRUE(cheek > 4.0);
    EXPECT_TRUE(bg < 0.3);
    EXPECT_TRUE(eyes < 2.0 && iris < 2.0 && teeth < 2.0);
    EXPECT_TRUE(lips < 3.5);
    pp_image_destroy(after);
  }
  pp_image_destroy(before);
}

TEST(skintone_keeps_texture_and_never_clips) {
  // (The deepest fixture tone's pores sit within a level or two of 8-bit
  // rounding, so texture is measured on a mid and the fairest tone.)
  for (size_t t : {size_t{2}, size_t{5}}) {
    const Face f(kTones[t], {});
    const Masks m(f);
    pp_image* before = f.image();
    for (const auto& p : {kRose, kDeep, shade(0.66f, 0.64f, 0.74f, 0.6f)}) {
      pp_image* after = run(f, m, p);
      // Texture relative to the skin's brightness (what the eye sees: the
      // same pores on a darker or lighter skin).
      const double tex = (fineTexture(after, f, 8) / meanOf(after, f, 8).y) / (fineTexture(before, f, 8) / meanOf(before, f, 8).y);
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
      EXPECT_TRUE(tex > 0.8 && tex < 1.2);
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
  pp_image* none = run(f, m, shade(0.82f, 0.68f, 0.62f, 0.0f, 0.0f));
  EXPECT_NEAR(diffOver(before, none, f, 0), 0.0, 1e-9);
  pp_image* half = run(f, m, shade(0.82f, 0.68f, 0.62f, 0.0f, 0.5f));
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
  bad.gain_g = std::nanf("");
  EXPECT_EQ(pp_skin_tone(img, m.subject, m.faces, m.protect, &bad, nullptr, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  bad = kDeep;
  bad.strength = std::numeric_limits<float>::infinity();
  EXPECT_EQ(pp_skin_tone(img, m.subject, m.faces, m.protect, &bad, nullptr, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_cancel_token* token = pp_cancel_token_create();
  pp_cancel_token_cancel(token);
  EXPECT_EQ(pp_skin_tone(img, m.subject, m.faces, m.protect, &kDeep, token, nullptr, nullptr), PP_ERROR_CANCELLED);
  pp_cancel_token_destroy(token);
  // Out-of-range values are clamped, not refused; works without subject or protect.
  pp_skin_tone_params loud = shade(9.0f, -1.0f, 0.5f, 3.0f, 9.0f);
  loud.evenness = 4.0f;
  int32_t hasSkin = 0;
  float cov = 0;
  EXPECT_EQ(pp_skin_tone(img, nullptr, m.faces, nullptr, &loud, nullptr, &hasSkin, &cov), PP_OK);
  EXPECT_EQ(hasSkin, 1);
  EXPECT_TRUE(cov > 0.02f && cov < 0.5f);
  pp_image_destroy(img);
}
