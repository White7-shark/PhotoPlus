// Phase 5 portrait tools: landmark-driven face adjustments and the advanced
// Spotlight / Glow controls. Each test is phrased as what a person would
// notice in their photo:
//  - every adjustment touches only its own feature and nothing else;
//  - nobody's skin tone moves (checked for six tones, deep to light);
//  - results look the same at preview and full resolution;
//  - lighting is lifted, never darkened, and hue holds;
//  - the background can change without changing the subject, and edges
//    carry no fringe of the old background.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "photoplus/pp_engine.h"
#include "face_fixture.hpp"
#include "test_harness.hpp"

namespace {

using namespace fixture;

pp_face_params none() {
  pp_face_params p{};
  pp_face_default_params(&p);
  return p;
}

pp_image* adjusted(const Face& f, const pp_face_params& p, pp_face_report* r = nullptr) {
  pp_image* img = f.image();
  pp_face_report rep{};
  EXPECT_EQ(pp_face_adjust(img, &f.lm, 1, &p, nullptr, r ? r : &rep), PP_OK);
  return img;
}

}  // namespace

TEST(face_neutral_settings_change_nothing) {
  const Face f(kTones[2], {});
  pp_image* ref = f.image();
  pp_face_report r{};
  pp_image* out = adjusted(f, none(), &r);
  EXPECT_EQ(diffOver(out, ref, f, 0), 0.0);
  EXPECT_EQ(r.faces, 1);
  pp_image_destroy(out);
  pp_image_destroy(ref);
}

TEST(face_eyes_brighter_whites_clearer_irises_nothing_else) {
  const Face f(kTones[1], {});
  pp_image* ref = f.image();
  pp_face_params p = none();
  p.eyes = 1.0f;
  pp_image* out = adjusted(f, p);
  const Px w0 = meanOf(ref, f, 2), w1 = meanOf(out, f, 2);
  std::printf("    eye whites: luma %.3f -> %.3f, redness %.3f -> %.3f\n", static_cast<double>(w0.y), static_cast<double>(w1.y),
              static_cast<double>(w0.cr), static_cast<double>(w1.cr));
  EXPECT_TRUE(w1.y > w0.y + 0.01f);
  EXPECT_TRUE(w1.cr < w0.cr * 0.8f);
  EXPECT_TRUE(fineTexture(out, f, 3) > fineTexture(ref, f, 3) * 1.15);  // iris detail
  EXPECT_TRUE(diffOver(out, ref, f, 8) < 0.2);                         // cheeks untouched
  EXPECT_TRUE(diffOver(out, ref, f, 9) < 0.05);                        // background untouched
  pp_image_destroy(out);
  pp_image_destroy(ref);
}

TEST(face_teeth_whiter_inside_the_open_mouth_only) {
  for (const Rgbf& tone : {kTones[0], kTones[5]}) {
    const Face f(tone, {});
    pp_image* ref = f.image();
    pp_face_params p = none();
    p.teeth = 1.0f;
    pp_image* out = adjusted(f, p);
    const Px t0 = meanOf(ref, f, 4), t1 = meanOf(out, f, 4);
    std::printf("    teeth: yellow (cb) %.3f -> %.3f, luma %.3f -> %.3f\n", static_cast<double>(t0.cb),
                static_cast<double>(t1.cb), static_cast<double>(t0.y), static_cast<double>(t1.y));
    EXPECT_TRUE(t1.cb > t0.cb + 0.02f);  // less yellow
    EXPECT_TRUE(t1.y > t0.y);
    EXPECT_TRUE(diffOver(out, ref, f, 5) < 0.6);  // lips keep their colour
    EXPECT_TRUE(diffOver(out, ref, f, 1) < 0.3);  // skin untouched
    pp_image_destroy(out);
    pp_image_destroy(ref);
  }
  // A closed mouth has no teeth to whiten.
  FaceOptions closed;
  closed.mouthOpen = false;
  const Face f(kTones[3], closed);
  pp_image* ref = f.image();
  pp_face_params p = none();
  p.teeth = 1.0f;
  pp_image* out = adjusted(f, p);
  EXPECT_EQ(diffOver(out, ref, f, 0), 0.0);
  pp_image_destroy(out);
  pp_image_destroy(ref);
}

TEST(face_lips_richer_same_hue) {
  const Face f(kTones[3], {});
  pp_image* ref = f.image();
  pp_face_params p = none();
  p.lips = 1.0f;
  pp_image* out = adjusted(f, p);
  const Px l0 = meanOf(ref, f, 5), l1 = meanOf(out, f, 5);
  const float s0 = std::hypot(l0.cb, l0.cr), s1 = std::hypot(l1.cb, l1.cr);
  std::printf("    lips: saturation %.3f -> %.3f, hue shift %.2f deg\n", static_cast<double>(s0), static_cast<double>(s1),
              static_cast<double>(std::fabs(hueDeg(l1) - hueDeg(l0))));
  EXPECT_TRUE(s1 > s0 * 1.12f);
  EXPECT_TRUE(std::fabs(hueDeg(l1) - hueDeg(l0)) < 2.0f);
  EXPECT_TRUE(diffOver(out, ref, f, 8) < 0.2);
  pp_image_destroy(out);
  pp_image_destroy(ref);
}

TEST(face_dark_circles_soften_toward_the_cheek_every_skin_tone) {
  for (const Rgbf& tone : kTones) {
    const Face f(tone, {});
    pp_image* ref = f.image();
    pp_face_params p = none();
    p.under_eye = 1.0f;
    pp_image* out = adjusted(f, p);
    const float gap0 = meanOf(ref, f, 8).y - meanOf(ref, f, 6).y;
    const float gap1 = meanOf(out, f, 8).y - meanOf(out, f, 6).y;
    const double tex0 = fineTexture(ref, f, 6), tex1 = fineTexture(out, f, 6);
    std::printf("    tone %.2f: cheek-minus-under-eye %.3f -> %.3f, texture kept %.0f%%\n", static_cast<double>(tone.r),
                static_cast<double>(gap0), static_cast<double>(gap1), 100.0 * tex1 / tex0);
    EXPECT_TRUE(gap1 < 0.5f * gap0);
    EXPECT_TRUE(tex1 > 0.85 * tex0);
    EXPECT_TRUE(diffOver(out, ref, f, 2) < 0.5);  // the eyes themselves are not touched
    pp_image_destroy(out);
    pp_image_destroy(ref);
  }
}

TEST(face_shine_tamed_skin_tone_kept) {
  for (const Rgbf& tone : {kTones[0], kTones[2], kTones[4]}) {
    const Face f(tone, {});
    pp_image* ref = f.image();
    pp_face_params p = none();
    p.shine = 1.0f;
    pp_image* out = adjusted(f, p);
    const Px s0 = meanOf(ref, f, 7), s1 = meanOf(out, f, 7), c0 = meanOf(ref, f, 8), c1 = meanOf(out, f, 8);
    std::printf("    tone %.2f: shine luma %.3f -> %.3f (cheek %.3f -> %.3f)\n", static_cast<double>(tone.r), static_cast<double>(s0.y),
                static_cast<double>(s1.y), static_cast<double>(c0.y), static_cast<double>(c1.y));
    EXPECT_TRUE(s0.y - s1.y > 0.25f * (s0.y - c0.y));  // a real share of the excess goes
    EXPECT_TRUE(std::fabs(c1.y - c0.y) < 0.004f);      // ordinary skin stays
    // The shine's colour moves toward the skin's own (never a new hue).
    EXPECT_TRUE(std::hypot(s1.cb, s1.cr) >= std::hypot(s0.cb, s0.cr));
    pp_image_destroy(out);
    pp_image_destroy(ref);
  }
}

TEST(face_light_opens_shadows_never_darkens) {
  FaceOptions o;
  o.sideShadow = true;
  o.shine = false;
  const Face f(kTones[2], o);
  pp_image* ref = f.image();
  pp_face_params p = none();
  p.face_light = 1.0f;
  pp_face_report r{};
  pp_image* out = adjusted(f, p, &r);
  // Left and right cheek brightness: the shadowed side comes up.
  auto side = [&](pp_image* img, bool left) {
    const auto* q = static_cast<const uint8_t*>(pp_image_pixels(img));
    pp_image_info info{};
    pp_image_get_info(img, &info);
    double s = 0;
    int n = 0;
    for (int y = 0; y < f.h; ++y)
      for (int x = 0; x < f.w; ++x) {
        if (f.region[static_cast<size_t>(y) * f.w + x] != 8) continue;
        if ((x < f.w / 2) != left) continue;
        s += ycc(q + static_cast<size_t>(y) * info.stride + x * 4).y;
        ++n;
      }
    return s / std::max(1, n);
  };
  const double ratio0 = side(ref, true) / side(ref, false), ratio1 = side(out, true) / side(out, false);
  std::printf("    shadowed/lit cheek %.2f -> %.2f, backlight gain %.2f\n", ratio0, ratio1, static_cast<double>(r.light_gain));
  EXPECT_TRUE(ratio1 > ratio0 + 0.05);
  EXPECT_NEAR(r.light_gain, 1.0, 1e-6);  // evenly exposed scene: no exposure lift
  // Nothing anywhere gets darker.
  const auto* a = static_cast<const uint8_t*>(pp_image_pixels(out));
  const auto* b = static_cast<const uint8_t*>(pp_image_pixels(ref));
  pp_image_info info{};
  pp_image_get_info(out, &info);
  int darker = 0;
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) {
      const size_t i = static_cast<size_t>(y) * info.stride + x * 4;
      if (ycc(a + i).y < ycc(b + i).y - 1.5f / 255.0f) ++darker;
    }
  EXPECT_EQ(darker, 0);
  pp_image_destroy(out);
  pp_image_destroy(ref);
}

TEST(face_light_lifts_a_backlit_face_whatever_the_skin_tone_but_not_a_well_lit_one) {
  for (const Rgbf& tone : {kTones[0], kTones[3], kTones[5]}) {
    FaceOptions o;
    o.backlit = true;
    o.shine = false;
    const Face f(tone, o);
    pp_image* ref = f.image();
    pp_face_params p = none();
    p.face_light = 1.0f;
    pp_face_report r{};
    pp_image* out = adjusted(f, p, &r);
    const Px c0 = meanOf(ref, f, 8), c1 = meanOf(out, f, 8);
    std::printf("    backlit tone %.2f: gain %.2f, cheek %.3f -> %.3f, hue shift %.2f deg\n", static_cast<double>(tone.r),
                static_cast<double>(r.light_gain), static_cast<double>(c0.y), static_cast<double>(c1.y),
                static_cast<double>(std::fabs(hueDeg(c1) - hueDeg(c0))));
    EXPECT_TRUE(r.light_gain > 1.2f);
    EXPECT_TRUE(c1.y > c0.y * 1.15f);
    EXPECT_TRUE(std::fabs(hueDeg(c1) - hueDeg(c0)) < 3.0f);
    pp_image_destroy(out);
    pp_image_destroy(ref);
  }
  // Deep skin lit from the front in front of a white wall (with a white
  // shirt): bright surroundings, but the face has its own highlights. This
  // is a well-exposed photo and must not be lightened.
  for (const Rgbf& tone : {kTones[0], kTones[1]}) {
    FaceOptions wall;
    wall.whiteWall = true;
    const Face f(tone, wall);
    pp_face_params p = none();
    p.face_light = 1.0f;
    pp_face_report r{};
    pp_image* out = adjusted(f, p, &r);
    std::printf("    white wall, tone %.2f: gain %.2f\n", static_cast<double>(tone.r), static_cast<double>(r.light_gain));
    EXPECT_NEAR(r.light_gain, 1.0, 1e-6);
    pp_image_destroy(out);
  }
  // Deep skin in an ordinary, evenly lit photo is not "corrected" lighter.
  FaceOptions even;
  even.shine = false;
  even.darkCircles = false;
  const Face f(kTones[0], even);
  pp_face_params p = none();
  p.face_light = 1.0f;
  pp_face_report r{};
  pp_image* out = adjusted(f, p, &r);
  EXPECT_NEAR(r.light_gain, 1.0, 1e-6);
  pp_image_destroy(out);
}

TEST(face_studio_light_from_the_chosen_side) {
  FaceOptions o;
  o.shine = false;
  const Face f(kTones[3], o);
  pp_image* ref = f.image();
  for (const float angle : {-1.0f, 1.0f}) {
    pp_face_params p = none();
    p.studio_light = 1.0f;
    p.light_angle = angle;
    pp_image* out = adjusted(f, p);
    // Brightness change on the picture-left vs picture-right half of the face.
    const auto* a = static_cast<const uint8_t*>(pp_image_pixels(out));
    const auto* b = static_cast<const uint8_t*>(pp_image_pixels(ref));
    pp_image_info info{};
    pp_image_get_info(out, &info);
    double l = 0, rr = 0;
    int nl = 0, nr = 0;
    for (int y = 0; y < f.h; ++y)
      for (int x = 0; x < f.w; ++x) {
        if (f.region[static_cast<size_t>(y) * f.w + x] != 1) continue;
        const size_t i = static_cast<size_t>(y) * info.stride + x * 4;
        const double d = ycc(a + i).y - ycc(b + i).y;
        if (x < f.w / 2) l += d, ++nl;
        else rr += d, ++nr;
      }
    l /= std::max(1, nl);
    rr /= std::max(1, nr);
    std::printf("    light from %s: left %+.3f, right %+.3f\n", angle < 0 ? "left " : "right", l, rr);
    EXPECT_TRUE(angle < 0 ? l > rr + 0.01 : rr > l + 0.01);
    EXPECT_TRUE(diffOver(out, ref, f, 9) < 1.0);  // the background barely changes
    pp_image_destroy(out);
  }
  pp_image_destroy(ref);
}

TEST(face_slim_narrows_the_jaw_gently_forehead_stays) {
  FaceOptions o;
  o.shine = false;
  const Face f(kTones[2], o, 2);
  pp_image* ref = f.image();
  pp_face_params p = none();
  p.slim = 1.0f;
  pp_image* out = adjusted(f, p);
  // Face width at a design-space height: skin vs the green background.
  auto width = [&](pp_image* img, float designY) {
    const auto* q = static_cast<const uint8_t*>(pp_image_pixels(img));
    pp_image_info info{};
    pp_image_get_info(img, &info);
    const int y = static_cast<int>(designY * static_cast<float>(f.k));
    int left = -1, right = -1;
    for (int x = 0; x < f.w; ++x) {
      const uint8_t* v = q + static_cast<size_t>(y) * info.stride + x * 4;
      const bool skin = v[0] > v[1] + 20;  // skin is redder than the green background
      if (skin && left < 0) left = x;
      if (skin) right = x;
    }
    return right - left;
  };
  const float jaw = f.cy + 0.75f * f.ry, brow = f.cy - 0.45f * f.ry;
  const int jaw0 = width(ref, jaw), jaw1 = width(out, jaw), brow0 = width(ref, brow), brow1 = width(out, brow);
  std::printf("    jaw width %d -> %d px, forehead %d -> %d px (face %d px wide)\n", jaw0, jaw1, brow0, brow1,
              static_cast<int>(2 * f.rx * static_cast<float>(f.k)));
  const float faceW = 2.0f * f.rx * static_cast<float>(f.k);
  EXPECT_TRUE(jaw0 - jaw1 >= static_cast<int>(0.02f * faceW));
  EXPECT_TRUE(jaw0 - jaw1 <= static_cast<int>(0.08f * faceW));  // gentle
  EXPECT_TRUE(std::abs(brow0 - brow1) <= 2);
  // Moved skin keeps its pores (resampling must not soften it).
  const double t0 = fineTexture(ref, f, 1), t1 = fineTexture(out, f, 1);
  std::printf("    face texture kept %.0f%%\n", 100.0 * t1 / t0);
  EXPECT_TRUE(t1 > 0.9 * t0);
  pp_image_destroy(out);
  pp_image_destroy(ref);
}

TEST(face_all_tools_keep_every_skin_tone_its_own_colour) {
  for (const Rgbf& tone : kTones) {
    const Face f(tone, {});
    pp_image* ref = f.image();
    pp_face_params p = none();
    p.eyes = p.teeth = p.lips = p.under_eye = p.shine = p.face_light = 1.0f;
    pp_image* out = adjusted(f, p);
    const Px c0 = meanOf(ref, f, 8), c1 = meanOf(out, f, 8), s0 = meanOf(ref, f, 1), s1 = meanOf(out, f, 1);
    const float dh = std::fabs(hueDeg(c1) - hueDeg(c0));
    std::printf("    tone %.2f: cheek hue shift %.2f deg, face luma %.3f -> %.3f\n", static_cast<double>(tone.r),
                static_cast<double>(dh), static_cast<double>(s0.y), static_cast<double>(s1.y));
    EXPECT_TRUE(dh < 2.0f);
    EXPECT_TRUE(std::fabs(c1.y - c0.y) < 0.03f);  // the person's own tone is kept
    pp_image_destroy(out);
    pp_image_destroy(ref);
  }
}

TEST(face_preview_and_full_size_look_the_same) {
  // The same face at 1x and 4x: the 4x result reduced to 1x matches the
  // 1x result (what is previewed is what is saved).
  pp_face_params p = none();
  p.eyes = p.teeth = p.lips = p.under_eye = p.shine = p.face_light = 0.8f;
  p.slim = 0.5f;
  const Face small(kTones[2], {}, 1), big(kTones[2], {}, 4);
  pp_image* a = adjusted(small, p);
  pp_image* b = adjusted(big, p);
  pp_image* bRef = big.image();
  pp_image* aRef = small.image();
  // Box-reduce the big result and the big original by 4.
  auto reduce = [&](pp_image* img) {
    pp_image* outp = nullptr;
    pp_image_create(small.w, small.h, PP_FORMAT_RGBA8, &outp);
    const auto* q = static_cast<const uint8_t*>(pp_image_pixels(img));
    auto* d = static_cast<uint8_t*>(pp_image_pixels(outp));
    pp_image_info si{}, di{};
    pp_image_get_info(img, &si);
    pp_image_get_info(outp, &di);
    for (int y = 0; y < small.h; ++y)
      for (int x = 0; x < small.w; ++x)
        for (int c = 0; c < 4; ++c) {
          int s = 0;
          for (int yy = 0; yy < 4; ++yy)
            for (int xx = 0; xx < 4; ++xx) s += q[static_cast<size_t>(y * 4 + yy) * si.stride + (x * 4 + xx) * 4 + c];
          d[static_cast<size_t>(y) * di.stride + x * 4 + c] = static_cast<uint8_t>((s + 8) / 16);
        }
    return outp;
  };
  pp_image* bSmall = reduce(b);
  pp_image* bRefSmall = reduce(bRef);
  // Compare the *changes* each resolution made (the scenes differ in pixel-level texture).
  const auto* p1 = static_cast<const uint8_t*>(pp_image_pixels(a));
  const auto* p0 = static_cast<const uint8_t*>(pp_image_pixels(aRef));
  const auto* q1 = static_cast<const uint8_t*>(pp_image_pixels(bSmall));
  const auto* q0 = static_cast<const uint8_t*>(pp_image_pixels(bRefSmall));
  pp_image_info info{};
  pp_image_get_info(a, &info);
  double diff = 0, change = 0;
  size_t n = 0;
  for (int y = 0; y < small.h; ++y)
    for (int x = 0; x < small.w; ++x) {
      if (small.region[static_cast<size_t>(y) * small.w + x] == 9) continue;
      const size_t i = static_cast<size_t>(y) * info.stride + x * 4;
      const float da = ycc(p1 + i).y - ycc(p0 + i).y, db = ycc(q1 + i).y - ycc(q0 + i).y;
      diff += std::fabs(da - db);
      change += std::fabs(da);
      ++n;
    }
  diff = 255.0 * diff / static_cast<double>(n);
  change = 255.0 * change / static_cast<double>(n);
  std::printf("    mean change %.2f levels; 1x vs 4x differ by %.2f levels\n", change, diff);
  EXPECT_TRUE(change > 1.0);
  EXPECT_TRUE(diff < 0.35 * change);
  for (pp_image* img : {a, b, bRef, aRef, bSmall, bRefSmall}) pp_image_destroy(img);
}

TEST(face_bad_input_rejected_tiny_faces_ignored) {
  const Face f(kTones[2], {});
  pp_image* img = f.image();
  pp_face_params p = none();
  p.eyes = 1.0f;
  pp_face bad = f.lm;
  bad.points[10] = std::nanf("");
  EXPECT_EQ(pp_face_adjust(img, &bad, 1, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_face_adjust(img, nullptr, 1, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  std::vector<pp_face> many(33, f.lm);
  EXPECT_EQ(pp_face_adjust(img, many.data(), many.size(), &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  p.light_angle = std::nanf("");
  EXPECT_EQ(pp_face_adjust(img, &f.lm, 1, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  // A face a few pixels across (a crowd in the distance) is left alone.
  pp_face tiny = f.lm;
  for (int i = 0; i < PP_FACE_POINTS; ++i) {
    tiny.points[i * 2] = 0.5f + (tiny.points[i * 2] - 0.5f) * 0.03f;
    tiny.points[i * 2 + 1] = 0.5f + (tiny.points[i * 2 + 1] - 0.5f) * 0.03f;
  }
  p = none();
  p.eyes = p.teeth = 1.0f;
  pp_face_report r{};
  ASSERT_OK(pp_face_adjust(img, &tiny, 1, &p, nullptr, &r));
  EXPECT_EQ(r.adjusted, 0);
  // Cancellation is honoured.
  pp_cancel_token* t = pp_cancel_token_create();
  pp_cancel_token_cancel(t);
  EXPECT_EQ(pp_face_adjust(img, &f.lm, 1, &p, t, nullptr), PP_ERROR_CANCELLED);
  pp_cancel_token_destroy(t);
  pp_image_destroy(img);
}

// ---- Advanced Spotlight / Glow controls ---------------------------------------------------

namespace {

// A subject (with a wavy, hair-like soft edge) on a background of another
// hue. `alpha` is the true coverage; the model mask given to the engine is
// a coarse box-average of it (as from a low-resolution segmentation model).
struct EdgeScene {
  int w = 320, h = 240;
  std::vector<Rgbf> px;
  std::vector<float> alpha;
  EdgeScene(Rgbf fg, Rgbf bgc) {
    px.resize(static_cast<size_t>(w) * h);
    alpha.resize(px.size());
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float dx = static_cast<float>(x) + 0.5f - 160.0f, dy = static_cast<float>(y) + 0.5f - 140.0f;
        const float th = std::atan2(dy, dx);
        const float rr = 78.0f + 4.0f * std::sin(14.0f * th) + 2.0f * std::sin(31.0f * th);
        const float a = std::clamp((rr - std::hypot(dx, dy)) / 2.5f + 0.5f, 0.0f, 1.0f);  // ~2.5 px soft edge
        const size_t i = static_cast<size_t>(y) * w + x;
        const float t = 0.03f * (hash01(x, y, 5) - 0.5f);
        alpha[i] = a;
        px[i] = {a * fg.r + (1 - a) * bgc.r + t, a * fg.g + (1 - a) * bgc.g + t, a * fg.b + (1 - a) * bgc.b + t};
      }
  }
  pp_image* image() const {
    pp_image* img = nullptr;
    pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
    auto* p = static_cast<uint8_t*>(pp_image_pixels(img));
    for (size_t i = 0; i < px.size(); ++i) {
      p[i * 4] = static_cast<uint8_t>(std::lround(std::clamp(px[i].r, 0.0f, 1.0f) * 255));
      p[i * 4 + 1] = static_cast<uint8_t>(std::lround(std::clamp(px[i].g, 0.0f, 1.0f) * 255));
      p[i * 4 + 2] = static_cast<uint8_t>(std::lround(std::clamp(px[i].b, 0.0f, 1.0f) * 255));
      p[i * 4 + 3] = 255;
    }
    return img;
  }
  pp_image* coarseMask(int f) const {
    pp_image* m = nullptr;
    pp_image_create(w / f, h / f, PP_FORMAT_GRAY8, &m);
    pp_image_info info{};
    pp_image_get_info(m, &info);
    auto* p = static_cast<uint8_t*>(pp_image_pixels(m));
    for (int y = 0; y < h / f; ++y)
      for (int x = 0; x < w / f; ++x) {
        float s = 0;
        for (int yy = 0; yy < f; ++yy)
          for (int xx = 0; xx < f; ++xx) s += alpha[static_cast<size_t>(y * f + yy) * w + x * f + xx];
        p[static_cast<size_t>(y) * info.stride + x] = static_cast<uint8_t>(std::lround(255.0f * s / static_cast<float>(f * f)));
      }
    return m;
  }
};

pp_portrait_params portraitNone() {
  pp_portrait_params p{};
  pp_portrait_default_params(&p);
  p.blur = p.retouch = p.brighten = p.pop = 0.0f;
  return p;
}

pp_image* portraitRun(const EdgeScene& s, const pp_portrait_params& p, pp_portrait_report* r = nullptr) {
  pp_image* img = s.image();
  pp_image* m = s.coarseMask(8);
  pp_portrait_report rep{};
  EXPECT_EQ(pp_portrait(img, m, nullptr, nullptr, nullptr, &p, nullptr, r ? r : &rep), PP_OK);
  pp_image_destroy(m);
  return img;
}

}  // namespace

TEST(portrait_background_light_and_colour_leave_the_subject_alone) {
  const EdgeScene s({0.72f, 0.48f, 0.36f}, {0.25f, 0.55f, 0.3f});
  pp_image* ref = s.image();
  for (const int mode : {0, 1, 2}) {
    pp_portrait_params p = portraitNone();
    if (mode == 0) p.bg_color = -1.0f;  // black-and-white background ("colour pop")
    if (mode == 1) p.bg_light = 1.0f;   // a stop brighter
    if (mode == 2) p.bg_light = -1.0f;  // a stop darker
    pp_image* out = portraitRun(s, p);
    const auto* a = static_cast<const uint8_t*>(pp_image_pixels(out));
    const auto* b = static_cast<const uint8_t*>(pp_image_pixels(ref));
    pp_image_info info{};
    pp_image_get_info(out, &info);
    double subj = 0, bgSat = 0, bgY0 = 0, bgY1 = 0;
    int ns = 0, nb = 0;
    for (int y = 0; y < s.h; ++y)
      for (int x = 0; x < s.w; ++x) {
        const float al = s.alpha[static_cast<size_t>(y) * s.w + x];
        const size_t i = static_cast<size_t>(y) * info.stride + x * 4;
        const float dx = static_cast<float>(x) - 160.0f, dy = static_cast<float>(y) - 140.0f;
        if (std::hypot(dx, dy) < 60.0f) {  // deep inside the subject
          for (int c = 0; c < 3; ++c) subj += std::abs(a[i + c] - b[i + c]);
          ns += 3;
        } else if (al == 0.0f && std::hypot(dx, dy) > 100.0f) {  // clear background
          const Px v = ycc(a + i);
          bgSat += std::hypot(v.cb, v.cr);
          bgY0 += ycc(b + i).y;
          bgY1 += v.y;
          ++nb;
        }
      }
    subj /= ns;
    bgSat /= nb;
    std::printf("    mode %d: subject changed %.2f levels, background saturation %.3f, luma %.3f -> %.3f\n", mode, subj, bgSat,
                bgY0 / nb, bgY1 / nb);
    EXPECT_TRUE(subj < 0.5);
    if (mode == 0) EXPECT_TRUE(bgSat < 0.01);
    if (mode == 1) EXPECT_TRUE(bgY1 > bgY0 * 1.2);
    if (mode == 2) EXPECT_TRUE(bgY1 < bgY0 * 0.85);
    pp_image_destroy(out);
  }
  pp_image_destroy(ref);
}

TEST(portrait_edge_refinement_removes_the_old_backgrounds_fringe) {
  // Skin-coloured subject on saturated green; the background is turned
  // black and white. Without decontamination, the subject's soft edge
  // keeps a green rim.
  const EdgeScene s({0.78f, 0.55f, 0.42f}, {0.15f, 0.75f, 0.2f});
  double fringe[2] = {0, 0};
  for (const int e : {0, 1}) {
    pp_portrait_params p = portraitNone();
    p.bg_color = -1.0f;
    p.edge = static_cast<float>(e);
    pp_portrait_report r{};
    pp_image* out = portraitRun(s, p, &r);
    const auto* a = static_cast<const uint8_t*>(pp_image_pixels(out));
    pp_image_info info{};
    pp_image_get_info(out, &info);
    double sum = 0;
    int n = 0;
    for (int y = 0; y < s.h; ++y)
      for (int x = 0; x < s.w; ++x) {
        const float al = s.alpha[static_cast<size_t>(y) * s.w + x];
        if (al < 0.3f || al > 0.95f) continue;  // the subject's side of the soft edge
        const uint8_t* q = a + static_cast<size_t>(y) * info.stride + x * 4;
        sum += std::max(0, q[1] - (q[0] + q[2]) / 2);  // green excess
        ++n;
      }
    fringe[e] = sum / std::max(1, n);
    std::printf("    edge %d: green rim %.1f levels (edge pixels cleaned: %.2f%% of frame)\n", e, fringe[e],
                100.0 * static_cast<double>(r.edge_cleaned));
    pp_image_destroy(out);
  }
  EXPECT_TRUE(fringe[1] < 0.6 * fringe[0]);
}

TEST(portrait_colour_matte_separates_hair_from_a_background_of_equal_brightness) {
  // Dark red subject, dark green background of the same luma: brightness
  // alone cannot find the edge; colour can.
  const EdgeScene s({0.42f, 0.16f, 0.14f}, {0.10f, 0.30f, 0.12f});
  int wrong[2] = {0, 0};
  for (const int e : {0, 1}) {
    pp_portrait_params p = portraitNone();
    p.bg_color = -1.0f;
    p.edge = static_cast<float>(e);
    pp_image* out = portraitRun(s, p);
    const auto* a = static_cast<const uint8_t*>(pp_image_pixels(out));
    pp_image_info info{};
    pp_image_get_info(out, &info);
    for (int y = 0; y < s.h; ++y)
      for (int x = 0; x < s.w; ++x) {
        const float al = s.alpha[static_cast<size_t>(y) * s.w + x];
        if (al > 0.0f && al < 1.0f) continue;  // judge only clear subject and clear background
        const float dx = static_cast<float>(x) - 160.0f, dy = static_cast<float>(y) - 140.0f;
        if (std::fabs(std::hypot(dx, dy) - 78.0f) > 14.0f) continue;  // within the edge band
        const Px v = ycc(a + static_cast<size_t>(y) * info.stride + x * 4);
        const float sat = std::hypot(v.cb, v.cr);
        if (al >= 1.0f && sat < 0.06f) ++wrong[e];  // subject turned grey
        if (al <= 0.0f && sat > 0.04f) ++wrong[e];  // background kept its colour
      }
    std::printf("    edge %d: %d misjudged pixels near the outline\n", e, wrong[e]);
    pp_image_destroy(out);
  }
  EXPECT_TRUE(wrong[1] < wrong[0] * 0.7);
}

TEST(glow_skin_controls_are_independent_and_presets_unchanged) {
  // The existing Glow presets drive all three controls together: setting
  // them explicitly to the same value gives the same photo.
  FaceOptions o;
  o.shine = false;
  o.darkCircles = false;
  const Face f(kTones[2], o, 3);
  pp_image* subjectMask = nullptr;
  pp_image* faceMask = nullptr;
  pp_image_create(f.w, f.h, PP_FORMAT_GRAY8, &subjectMask);
  pp_image_create(f.w, f.h, PP_FORMAT_GRAY8, &faceMask);
  {
    pp_image_info si{};
    pp_image_get_info(subjectMask, &si);
    auto* sm = static_cast<uint8_t*>(pp_image_pixels(subjectMask));
    auto* fm = static_cast<uint8_t*>(pp_image_pixels(faceMask));
    for (int y = 0; y < f.h; ++y)
      for (int x = 0; x < f.w; ++x) {
        const uint8_t reg = f.region[static_cast<size_t>(y) * f.w + x];
        sm[static_cast<size_t>(y) * si.stride + x] = reg == 9 ? 0 : 255;
        fm[static_cast<size_t>(y) * si.stride + x] = (reg == 1 || reg == 6 || reg == 7 || reg == 8) ? 255 : 0;
      }
  }
  auto run = [&](pp_portrait_params p) {
    pp_image* img = f.image();
    EXPECT_EQ(pp_portrait(img, subjectMask, faceMask, nullptr, nullptr, &p, nullptr, nullptr), PP_OK);
    return img;
  };
  pp_portrait_params preset = portraitNone();
  preset.retouch = 0.75f;
  pp_portrait_params explicitly = preset;
  explicitly.smooth = explicitly.blemish = explicitly.even = 0.75f;
  pp_image* a = run(preset);
  pp_image* b = run(explicitly);
  EXPECT_EQ(diffOver(a, b, f, 0), 0.0);
  // Smoothing alone softens texture; evening alone keeps it.
  pp_portrait_params smoothOnly = portraitNone();
  smoothOnly.smooth = 1.0f;
  smoothOnly.blemish = smoothOnly.even = 0.0f;
  pp_portrait_params evenOnly = portraitNone();
  evenOnly.even = 1.0f;
  evenOnly.smooth = evenOnly.blemish = 0.0f;
  pp_image* ref = f.image();
  pp_image* sm = run(smoothOnly);
  pp_image* ev = run(evenOnly);
  const double t0 = fineTexture(ref, f, 8), ts = fineTexture(sm, f, 8), te = fineTexture(ev, f, 8);
  std::printf("    cheek texture: original %.4f, smooth only %.4f, even only %.4f\n", t0, ts, te);
  EXPECT_TRUE(ts < 0.8 * t0);
  EXPECT_TRUE(te > 0.9 * t0);
  for (pp_image* img : {a, b, ref, sm, ev, subjectMask, faceMask}) pp_image_destroy(img);
}
