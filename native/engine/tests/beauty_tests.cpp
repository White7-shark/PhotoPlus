// The beauty engine (skin → face → lighting / background / colour). Each test
// is phrased as what a person would notice in their photo:
//  - a neutral recipe changes nothing;
//  - a warm or green look reaches the room, not the person's skin (six
//    tones, deep to light);
//  - background effects leave the subject alone; subject light leaves the
//    background alone;
//  - the key light follows the face's own shading; rim light sits on the
//    subject's edge;
//  - added light never clips highlights;
//  - make-up lands where it should (blush on the cheek apples, highlight on
//    the nose bridge, contour at the jaw) and keeps skin texture;
//  - different looks give visibly different photos.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "face_fixture.hpp"
#include "photoplus/beauty.hpp"
#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

using namespace fixture;

pp_image* maskOf(const Face& f, bool (*pick)(uint8_t)) {
  pp_image* m = nullptr;
  pp_image_create(f.w, f.h, PP_FORMAT_GRAY8, &m);
  auto* p = static_cast<uint8_t*>(pp_image_pixels(m));
  pp_image_info info{};
  pp_image_get_info(m, &info);
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) p[static_cast<size_t>(y) * info.stride + x] = pick(f.region[static_cast<size_t>(y) * f.w + x]) ? 255 : 0;
  return m;
}
bool isSubject(uint8_t r) { return r != 9; }
bool isFace(uint8_t r) { return r >= 1 && r <= 8; }
bool isProtect(uint8_t r) { return r == 2 || r == 3 || r == 4 || r == 5; }

struct Scene {
  Face face;
  pp_image* subject;
  pp_image* faces;
  pp_image* protect;
  explicit Scene(Face f) : face(std::move(f)) {
    subject = maskOf(face, isSubject);
    faces = maskOf(face, isFace);
    protect = maskOf(face, isProtect);
  }
  ~Scene() {
    pp_image_destroy(subject);
    pp_image_destroy(faces);
    pp_image_destroy(protect);
  }
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;
  // Runs the compositor on a fresh copy of the photo.
  pp_image* run(const pp_beauty_params& p, pp_beauty_report* report = nullptr, bool withSubject = true) const {
    pp_image* img = face.image();
    const pp_status st = pp_beauty(img, withSubject ? subject : nullptr, faces, protect, nullptr, &face.lm, 1, &p, nullptr, report);
    EXPECT_EQ(st, PP_OK);
    return img;
  }
};

pp_beauty_params neutral() {
  pp_beauty_params p{};
  pp_beauty_default_params(&p);
  return p;
}

// Mean Y/Cb/Cr over every pixel of the regions `pick` accepts.
Px meanWhere(pp_image* img, const Face& f, bool (*pick)(uint8_t)) {
  const auto* p = static_cast<const uint8_t*>(pp_image_pixels(img));
  pp_image_info info{};
  pp_image_get_info(img, &info);
  double y = 0, cb = 0, cr = 0;
  size_t n = 0;
  for (int yy = 0; yy < f.h; ++yy)
    for (int xx = 0; xx < f.w; ++xx)
      if (pick(f.region[static_cast<size_t>(yy) * f.w + xx])) {
        const Px v = ycc(p + static_cast<size_t>(yy) * info.stride + xx * 4);
        y += v.y, cb += v.cb, cr += v.cr, ++n;
      }
  const double d = n ? static_cast<double>(n) : 1.0;
  return {static_cast<float>(y / d), static_cast<float>(cb / d), static_cast<float>(cr / d)};
}
bool isBackground(uint8_t r) { return r == 9; }
bool isSkin(uint8_t r) { return r == 1 || r == 8; }

float hueShift(const Px& a, const Px& b) {
  float d = hueDeg(b) - hueDeg(a);
  while (d > 180) d -= 360;
  while (d < -180) d += 360;
  return std::fabs(d);
}
float chromaShift(const Px& a, const Px& b) { return std::hypot(b.cb - a.cb, b.cr - a.cr); }

void set3(float* d, float r, float g, float b) {
  d[0] = r;
  d[1] = g;
  d[2] = b;
}

pp_beauty_params golden() {
  pp_beauty_params p = neutral();
  p.grade.temperature = 0.8f;
  set3(p.grade.bg_tint, 0.95f, 0.72f, 0.35f);
  p.grade.bg_tint_amount = 0.6f;
  p.grade.skin_protect = 0.85f;
  return p;
}

}  // namespace

TEST(beauty_neutral_recipe_changes_nothing) {
  Scene s(Face(kTones[2], {}));
  pp_image* before = s.face.image();
  pp_beauty_report r{};
  pp_image* after = s.run(neutral(), &r);
  EXPECT_NEAR(diffOver(before, after, s.face, 0), 0.0, 1e-9);
  EXPECT_EQ(r.layers, 0);
  pp_image_destroy(before);
  pp_image_destroy(after);
}

TEST(beauty_warm_look_reaches_the_room_not_the_skin) {
  for (size_t t = 0; t < kTones.size(); ++t) {
    Scene s(Face(kTones[t], {.shine = false}));
    pp_image* before = s.face.image();
    pp_image* after = s.run(golden());
    const Px skin0 = meanWhere(before, s.face, isSkin), skin1 = meanWhere(after, s.face, isSkin);
    const Px bg0 = meanWhere(before, s.face, isBackground), bg1 = meanWhere(after, s.face, isBackground);
    const float bgMove = chromaShift(bg0, bg1), skinMove = chromaShift(skin0, skin1);
    std::printf("    tone %zu: background chroma moved %.4f, skin %.4f (hue %.1f deg)\n", t, bgMove, skinMove, hueShift(skin0, skin1));
    EXPECT_TRUE(bgMove > 0.03f);
    EXPECT_TRUE(skinMove < 0.3f * bgMove);
    EXPECT_TRUE(hueShift(skin0, skin1) < 4.0f);
    // Warm means golden: Cr up, Cb down in the room.
    EXPECT_TRUE(bg1.cr > bg0.cr && bg1.cb < bg0.cb);
    pp_image_destroy(before);
    pp_image_destroy(after);
  }
}

TEST(beauty_green_band_deepens_greens_but_not_skin) {
  Scene s(Face(kTones[1], {.shine = false}));  // the background is a muted green
  pp_beauty_params p = neutral();
  set3(p.grade.band_color, 0.2f, 0.6f, 0.4f);
  p.grade.band_width = 50.0f;
  p.grade.band_saturation = 0.8f;
  p.grade.band_hue_shift = 10.0f;
  pp_image* before = s.face.image();
  pp_image* after = s.run(p);
  const Px bg0 = meanWhere(before, s.face, isBackground), bg1 = meanWhere(after, s.face, isBackground);
  const Px sk0 = meanWhere(before, s.face, isSkin), sk1 = meanWhere(after, s.face, isSkin);
  const float sat0 = std::hypot(bg0.cb, bg0.cr), sat1 = std::hypot(bg1.cb, bg1.cr);
  std::printf("    background saturation %.4f -> %.4f; skin chroma moved %.4f\n", sat0, sat1, chromaShift(sk0, sk1));
  EXPECT_TRUE(sat1 > 1.4f * sat0);
  EXPECT_TRUE(chromaShift(sk0, sk1) < 0.004f);
  pp_image_destroy(before);
  pp_image_destroy(after);
}

TEST(beauty_background_effects_leave_the_subject_alone) {
  Scene s(Face(kTones[3], {}));
  pp_beauty_params p = neutral();
  p.grade.bg_exposure = -0.6f;
  p.grade.bg_saturation = 0.5f;
  set3(p.grade.bg_tint, 0.4f, 0.55f, 0.9f);
  p.grade.bg_tint_amount = 0.5f;
  p.grade.bg_vignette = 0.6f;
  pp_image* before = s.face.image();
  pp_image* after = s.run(p);
  const double face = diffOver(before, after, s.face, 1), bg = diffOver(before, after, s.face, 9);
  std::printf("    background changed %.2f levels, face interior %.2f\n", bg, face);
  EXPECT_TRUE(bg > 10.0);
  EXPECT_TRUE(face < 1.5);
  pp_image_destroy(before);
  pp_image_destroy(after);
}

TEST(beauty_subject_light_lifts_the_person_not_the_room) {
  Scene s(Face(kTones[1], {}));
  pp_beauty_params p = neutral();
  p.grade.subject_light = 0.8f;
  p.grade.shadow_lift = 0.5f;
  pp_image* before = s.face.image();
  pp_image* after = s.run(p);
  const Px f0 = meanWhere(before, s.face, isSkin), f1 = meanWhere(after, s.face, isSkin);
  const double bg = diffOver(before, after, s.face, 9);
  std::printf("    skin luma %.3f -> %.3f; background changed %.2f levels\n", f0.y, f1.y, bg);
  EXPECT_TRUE(f1.y > f0.y + 0.03f);
  EXPECT_TRUE(hueShift(f0, f1) < 3.0f);
  EXPECT_TRUE(bg < 3.0);
  pp_image_destroy(before);
  pp_image_destroy(after);
}

TEST(beauty_key_light_follows_the_faces_own_shading) {
  // sideShadow darkens the picture-left side: the light comes from the right.
  Scene s(Face(kTones[2], {.sideShadow = true}));
  pp_beauty_params p = neutral();
  p.face.studio_light = 0.6f;
  pp_beauty_report r{};
  pp_image* img = s.run(p, &r);
  std::printf("    light from %.2f (picture right is +)\n", r.light_angle);
  EXPECT_TRUE(r.light_angle > 0.3f);
  pp_image_destroy(img);
  // The same face lit evenly gets the classic key from the picture right.
  Scene even(Face(kTones[2], {}));
  pp_image* img2 = even.run(p, &r);
  EXPECT_NEAR(r.light_angle, 0.35, 1e-6);
  pp_image_destroy(img2);
  // With auto light off, the recipe's angle is used.
  p.auto_light = 0;
  p.face.light_angle = -0.7f;
  pp_image* img3 = s.run(p, &r);
  EXPECT_NEAR(r.light_angle, -0.7, 1e-6);
  pp_image_destroy(img3);
}

TEST(beauty_rim_light_sits_on_the_subjects_edge) {
  Scene s(Face(kTones[1], {.shine = false}));
  pp_beauty_params p = neutral();
  p.grade.rim = 1.0f;
  pp_image* before = s.face.image();
  pp_image* after = s.run(p);
  // Edge band: subject pixels within ~4% of the frame of the background.
  const auto* a = static_cast<const uint8_t*>(pp_image_pixels(before));
  const auto* b = static_cast<const uint8_t*>(pp_image_pixels(after));
  pp_image_info info{};
  pp_image_get_info(before, &info);
  const int band = s.face.w / 25;
  double edge = 0, inner = 0;
  size_t ne = 0, ni = 0;
  for (int y = band; y < s.face.h - band; ++y)
    for (int x = band; x < s.face.w - band; ++x) {
      const uint8_t r = s.face.region[static_cast<size_t>(y) * s.face.w + x];
      if (r == 9) continue;
      bool nearBg = false;
      for (int dy = -band; dy <= band && !nearBg; dy += band / 2)
        for (int dx = -band; dx <= band && !nearBg; dx += band / 2)
          nearBg = s.face.region[static_cast<size_t>(y + dy) * s.face.w + (x + dx)] == 9;
      const size_t o = static_cast<size_t>(y) * info.stride + x * 4;
      double d = 0;
      for (int c = 0; c < 3; ++c) d += std::abs(a[o + c] - b[o + c]);
      if (nearBg) edge += d, ++ne;
      else if (r == 1) inner += d, ++ni;
    }
  edge /= std::max<size_t>(ne, 1) * 3.0;
  inner /= std::max<size_t>(ni, 1) * 3.0;
  std::printf("    edge band %.2f levels, face interior %.2f\n", edge, inner);
  EXPECT_TRUE(edge > 3.0);
  EXPECT_TRUE(edge > 5.0 * inner);
  pp_image_destroy(before);
  pp_image_destroy(after);
}

TEST(beauty_added_light_never_clips_highlights) {
  Scene s(Face(kTones[5], {.whiteWall = true}));
  pp_beauty_params p = neutral();
  p.grade.bloom = 1.0f;
  p.grade.bg_glow = 1.0f;
  p.grade.bg_glow_size = 0.6f;
  p.grade.subject_light = 1.0f;
  p.grade.highlights = 1.0f;
  p.grade.skin_sheen = 1.0f;
  p.grade.soft_focus = 0.5f;
  pp_beauty_report r{};
  pp_image* img = s.run(p, &r);
  std::printf("    clipped %.4f -> %.4f\n", r.clipped_before, r.clipped_after);
  EXPECT_TRUE(r.clipped_after <= r.clipped_before + 0.002f);
  pp_image_destroy(img);
}

TEST(beauty_makeup_lands_where_it_should_and_keeps_texture) {
  Scene s(Face(kTones[2], {.shine = false, .darkCircles = false}));
  pp_image* before = s.face.image();
  // Blush: the cheeks (region 8) gain red; the forehead does not.
  pp_beauty_params p = neutral();
  p.face.blush = 1.0f;
  set3(p.face.blush_color, 0.86f, 0.36f, 0.45f);
  pp_image* blush = s.run(p);
  const Px c0 = meanOf(before, s.face, 8), c1 = meanOf(blush, s.face, 8);
  std::printf("    blush: cheek Cr %.4f -> %.4f; texture x%.2f\n", c0.cr, c1.cr,
              fineTexture(blush, s.face, 8) / fineTexture(before, s.face, 8));
  EXPECT_TRUE(c1.cr > c0.cr + 0.012f);
  EXPECT_TRUE(fineTexture(blush, s.face, 8) > 0.9 * fineTexture(before, s.face, 8));
  EXPECT_TRUE(diffOver(before, blush, s.face, 9) < 0.01);  // never the background
  // Highlight brightens the nose bridge more than the cheeks' outer edge.
  p = neutral();
  p.face.highlight = 1.0f;
  pp_image* hl = s.run(p);
  auto lumaAt = [&](pp_image* img, float fx, float fy) {
    const auto* q = static_cast<const uint8_t*>(pp_image_pixels(img));
    pp_image_info info{};
    pp_image_get_info(img, &info);
    double sum = 0;
    int n = 0;
    for (int dy = -3; dy <= 3; ++dy)
      for (int dx = -3; dx <= 3; ++dx, ++n) {
        const int x = static_cast<int>(fx * s.face.k) + dx, y = static_cast<int>(fy * s.face.k) + dy;
        sum += ycc(q + static_cast<size_t>(y) * info.stride + x * 4).y;
      }
    return sum / n;
  };
  const double noseGain = lumaAt(hl, 120, 120) - lumaAt(before, 120, 120);
  const double jawGain = lumaAt(hl, 75, 160) - lumaAt(before, 75, 160);
  std::printf("    highlight: nose bridge %+.4f, jaw side %+.4f\n", noseGain, jawGain);
  EXPECT_TRUE(noseGain > 0.012);
  EXPECT_TRUE(noseGain > 3.0 * std::max(0.0, jawGain));
  // Contour shades the jaw side, not the nose bridge.
  p = neutral();
  p.face.contour = 1.0f;
  pp_image* ct = s.run(p);
  const double jawShade = lumaAt(ct, 75, 160) - lumaAt(before, 75, 160);
  const double noseShade = lumaAt(ct, 120, 120) - lumaAt(before, 120, 120);
  std::printf("    contour: jaw side %+.4f, nose bridge %+.4f\n", jawShade, noseShade);
  EXPECT_TRUE(jawShade < -0.01);
  EXPECT_TRUE(std::fabs(noseShade) < 0.004);
  for (pp_image* i : {before, blush, hl, ct}) pp_image_destroy(i);
}

TEST(beauty_makeup_map_is_symmetric_and_peaks_on_its_features) {
  const photoplus::portrait::MakeupFrame f{-0.25f, 0.2f, 0.52f};
  const auto l = photoplus::portrait::makeupAt(-0.48f, 0.15f, f), r = photoplus::portrait::makeupAt(0.48f, 0.15f, f);
  EXPECT_NEAR(l.blush, r.blush, 1e-6);
  EXPECT_TRUE(l.blush > 0.8f);
  EXPECT_TRUE(photoplus::portrait::makeupAt(0.0f, -0.05f, f).blush < 0.05f);   // not on the nose
  EXPECT_TRUE(photoplus::portrait::makeupAt(0.0f, -0.025f, f).highlight > 0.8f); // nose bridge
  EXPECT_TRUE(photoplus::portrait::makeupAt(0.9f, 0.6f, f).contour > 0.5f);    // jaw side
  EXPECT_TRUE(photoplus::portrait::makeupAt(0.0f, -0.025f, f).contour < 0.05f);
}

TEST(beauty_full_look_keeps_every_skin_tone) {
  for (size_t t = 0; t < kTones.size(); ++t) {
    Scene s(Face(kTones[t], {}));
    pp_beauty_params p = golden();
    p.portrait.retouch = 0.6f;
    p.portrait.brighten = 0.3f;
    p.face.eyes = 0.3f;
    p.face.highlight = 0.4f;
    p.face.blush = 0.3f;
    set3(p.face.blush_color, 0.8f, 0.4f, 0.4f);
    p.grade.contrast = 0.3f;
    p.grade.vibrance = 0.4f;
    p.grade.bloom = 0.3f;
    pp_image* before = s.face.image();
    pp_image* after = s.run(p);
    const Px a = meanOf(before, s.face, 8), b = meanOf(after, s.face, 8);
    std::printf("    tone %zu: cheek hue moved %.1f deg, luma %.3f -> %.3f\n", t, hueShift(a, b), a.y, b.y);
    EXPECT_TRUE(hueShift(a, b) < 8.0f);  // blush is meant to add a little red
    EXPECT_TRUE(b.y > a.y - 0.02f);      // never darker overall
    pp_image_destroy(before);
    pp_image_destroy(after);
  }
}

TEST(beauty_different_looks_give_visibly_different_photos) {
  Scene s(Face(kTones[2], {}));
  std::vector<pp_beauty_params> looks;
  looks.push_back(golden());
  pp_beauty_params crystal = neutral();
  crystal.grade.temperature = -0.6f;
  crystal.grade.highlights = 0.6f;
  crystal.grade.bg_exposure = 0.3f;
  set3(crystal.grade.bg_tint, 0.6f, 0.75f, 0.95f);
  crystal.grade.bg_tint_amount = 0.5f;
  looks.push_back(crystal);
  pp_beauty_params midnight = neutral();
  midnight.grade.contrast = 0.7f;
  midnight.grade.blacks = -0.6f;
  midnight.grade.bg_exposure = -0.9f;
  midnight.grade.bg_vignette = 0.8f;
  looks.push_back(midnight);
  pp_beauty_params dream = neutral();
  dream.grade.soft_focus = 0.8f;
  dream.grade.bloom = 0.6f;
  dream.grade.bg_haze = 0.7f;
  set3(dream.grade.bg_glow_color, 1.0f, 0.85f, 0.92f);
  looks.push_back(dream);
  std::vector<pp_image*> out;
  for (const auto& p : looks) out.push_back(s.run(p));
  double minDiff = 1e9;
  for (size_t i = 0; i < out.size(); ++i)
    for (size_t j = i + 1; j < out.size(); ++j) minDiff = std::min(minDiff, diffOver(out[i], out[j], s.face, 0));
  std::printf("    smallest difference between two looks: %.1f levels\n", minDiff);
  EXPECT_TRUE(minDiff > 6.0);
  for (pp_image* i : out) pp_image_destroy(i);
}

TEST(beauty_rejects_bad_input_and_can_be_cancelled) {
  Scene s(Face(kTones[2], {}));
  pp_image* img = s.face.image();
  pp_beauty_params p = neutral();
  EXPECT_EQ(pp_beauty(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_beauty(img, nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_beauty(img, nullptr, nullptr, nullptr, nullptr, nullptr, 1, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_beauty_params bad = p;
  bad.grade.bloom = std::nanf("");
  EXPECT_EQ(pp_beauty(img, s.subject, nullptr, nullptr, nullptr, nullptr, 0, &bad, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_face far = s.face.lm;
  far.points[0] = 40.0f;
  EXPECT_EQ(pp_beauty(img, s.subject, nullptr, nullptr, nullptr, &far, 1, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  // Out-of-range values are clamped, not rejected.
  pp_beauty_params loud = p;
  loud.grade.contrast = 7.0f;
  loud.grade.bg_glow = -3.0f;
  EXPECT_EQ(pp_beauty(img, s.subject, nullptr, nullptr, nullptr, nullptr, 0, &loud, nullptr, nullptr), PP_OK);
  pp_cancel_token* token = pp_cancel_token_create();
  pp_cancel_token_cancel(token);
  pp_beauty_params busy = golden();
  busy.grade.bloom = 0.5f;
  EXPECT_EQ(pp_beauty(img, s.subject, nullptr, nullptr, nullptr, nullptr, 0, &busy, token, nullptr), PP_ERROR_CANCELLED);
  pp_cancel_token_destroy(token);
  pp_image_destroy(img);
}
