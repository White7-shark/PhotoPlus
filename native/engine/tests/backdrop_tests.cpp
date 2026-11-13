// Backdrops (background designs). What a person would notice:
//  - the person is untouched inside (face interior), the background changes;
//  - every design gives a different picture;
//  - black stages are black, white studios are white, with no halo of the
//    old background around the head;
//  - Colour Pop keeps the person's colour and removes the world's;
//  - strength 0 changes nothing; strength blends;
//  - painted backgrounds do not depend on the old one (same seed, same paint);
//  - hair the mask missed is kept, a line in the wall behind the head is not.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "face_fixture.hpp"
#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

using namespace fixture;

pp_image* subjectMask(const Face& f) {
  pp_image* m = nullptr;
  pp_image_create(f.w, f.h, PP_FORMAT_GRAY8, &m);
  auto* p = static_cast<uint8_t*>(pp_image_pixels(m));
  pp_image_info info{};
  pp_image_get_info(m, &info);
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) p[static_cast<size_t>(y) * info.stride + x] = f.region[static_cast<size_t>(y) * f.w + x] != 9 ? 255 : 0;
  return m;
}

pp_image* run(const Face& f, pp_image* mask, int design, float strength = 1.0f, pp_status expect = PP_OK) {
  pp_image* img = f.image();
  pp_backdrop_params p{};
  p.design = design;
  p.strength = strength;
  p.seed = 7;
  EXPECT_EQ(pp_backdrop(img, mask, nullptr, &p, nullptr, nullptr), expect);
  return img;
}

// Mean of each channel (0..255) over pixels at least `margin` design units
// from the subject (deep background) or over a region id.
struct Mean {
  double r = 0, g = 0, b = 0;
};
Mean meanBackground(pp_image* img, const Face& f) {
  const auto* p = static_cast<const uint8_t*>(pp_image_pixels(img));
  pp_image_info info{};
  pp_image_get_info(img, &info);
  Mean m;
  size_t n = 0;
  for (int y = 0; y < f.h / 3; ++y)  // above the head
    for (int x = 0; x < f.w; ++x) {
      if (f.region[static_cast<size_t>(y) * f.w + x] != 9) continue;
      const uint8_t* q = p + static_cast<size_t>(y) * info.stride + x * 4;
      m.r += q[0], m.g += q[1], m.b += q[2], ++n;
    }
  const double d = n ? static_cast<double>(n) : 1.0;
  return {m.r / d, m.g / d, m.b / d};
}

// The largest channel value in a thin ring just outside the head: a halo of
// the old (bright) background would show here on a black stage.
double ringMax(pp_image* img, const Face& f, bool brightest) {
  const auto* p = static_cast<const uint8_t*>(pp_image_pixels(img));
  pp_image_info info{};
  pp_image_get_info(img, &info);
  std::vector<double> v;
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) {
      const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(f.k), fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(f.k);
      const float e = ell(fx, fy, f.cx, f.cy, f.rx, f.ry);
      if (e < 1.2f || e > 1.5f || fy > f.cy) continue;  // around the top of the head
      const uint8_t* q = p + static_cast<size_t>(y) * info.stride + x * 4;
      v.push_back((q[0] + q[1] + q[2]) / 3.0);
    }
  std::sort(v.begin(), v.end());
  return brightest ? v[v.size() * 98 / 100] : v[v.size() * 2 / 100];
}

}  // namespace

TEST(backdrop_changes_the_background_and_leaves_the_person_alone) {
  const Face f(kTones[2], {.shine = false});
  pp_image* mask = subjectMask(f);
  pp_image* before = f.image();
  // Designs that keep the person's own light (no stage fall-off, no mono, no glow).
  for (int d : {PP_BACKDROP_SOFT_STUDIO, PP_BACKDROP_VELOCITY, PP_BACKDROP_SWIRL, PP_BACKDROP_COLOR_POP}) {
    pp_image* after = run(f, mask, d);
    const double face = diffOver(before, after, f, 8), bg = diffOver(before, after, f, 9);
    std::printf("    design %d: background changed %.1f levels, cheeks %.2f\n", d, bg, face);
    EXPECT_TRUE(face < 6.0);
    EXPECT_TRUE(bg > 2.0);
    pp_image_destroy(after);
  }
  pp_image_destroy(before);
  pp_image_destroy(mask);
}

TEST(backdrop_every_design_is_its_own_picture) {
  const Face f(kTones[3], {});
  pp_image* mask = subjectMask(f);
  std::vector<pp_image*> out;
  for (int d = 0; d < PP_BACKDROP_COUNT; ++d) out.push_back(run(f, mask, d));
  double minDiff = 1e9;
  int a = 0, b = 0;
  for (int i = 0; i < PP_BACKDROP_COUNT; ++i)
    for (int j = i + 1; j < PP_BACKDROP_COUNT; ++j) {
      const double d = diffOver(out[static_cast<size_t>(i)], out[static_cast<size_t>(j)], f, 0);
      if (d < minDiff) minDiff = d, a = i, b = j;
    }
  std::printf("    %d designs; the closest two (%d, %d) differ by %.1f levels\n", PP_BACKDROP_COUNT, a, b, minDiff);
  EXPECT_TRUE(PP_BACKDROP_COUNT >= 14);
  EXPECT_TRUE(minDiff > 3.0);
  for (pp_image* i : out) pp_image_destroy(i);
  pp_image_destroy(mask);
}

TEST(backdrop_stage_is_black_and_porcelain_white_without_a_halo) {
  // A bright wall behind the person: any of it left at the edge would glow on black.
  const Face f(kTones[1], {.whiteWall = true});
  pp_image* mask = subjectMask(f);
  pp_image* stage = run(f, mask, PP_BACKDROP_STAGE);
  const Mean sb = meanBackground(stage, f);
  const double halo = ringMax(stage, f, true);
  std::printf("    stage: background %.1f, brightest ring pixel %.1f\n", sb.r, halo);
  EXPECT_TRUE(sb.r < 22.0 && sb.g < 22.0 && sb.b < 22.0);
  EXPECT_TRUE(halo < 60.0);
  pp_image_destroy(stage);
  // A dark wall, a white studio: no dark fringe.
  const Face g(kTones[4], {});
  pp_image* mask2 = subjectMask(g);
  pp_image* white = run(g, mask2, PP_BACKDROP_PORCELAIN);
  const Mean wb = meanBackground(white, g);
  const double fringe = ringMax(white, g, false);
  std::printf("    porcelain: background %.1f, darkest ring pixel %.1f\n", wb.g, fringe);
  EXPECT_TRUE(wb.r > 200.0 && wb.g > 200.0 && wb.b > 200.0);
  EXPECT_TRUE(fringe > 150.0);
  pp_image_destroy(white);
  pp_image_destroy(mask);
  pp_image_destroy(mask2);
}

TEST(backdrop_noir_is_monochrome_and_colour_pop_keeps_the_person) {
  const Face f(kTones[2], {.shine = false});
  pp_image* mask = subjectMask(f);
  pp_image* noir = run(f, mask, PP_BACKDROP_NOIR);
  const Px n = meanOf(noir, f, 8);
  EXPECT_TRUE(std::hypot(n.cb, n.cr) < 0.006f);
  pp_image* before = f.image();
  pp_image* pop = run(f, mask, PP_BACKDROP_COLOR_POP);
  const Px c0 = meanOf(before, f, 8), c1 = meanOf(pop, f, 8);
  const Mean bg = meanBackground(pop, f);
  std::printf("    colour pop: cheek chroma %.3f -> %.3f; background r-g-b %.1f %.1f %.1f\n", std::hypot(c0.cb, c0.cr),
              std::hypot(c1.cb, c1.cr), bg.r, bg.g, bg.b);
  EXPECT_TRUE(std::hypot(c1.cb, c1.cr) > 0.9f * std::hypot(c0.cb, c0.cr));
  EXPECT_TRUE(std::fabs(bg.r - bg.g) < 2.0 && std::fabs(bg.g - bg.b) < 2.0);
  for (pp_image* i : {noir, before, pop}) pp_image_destroy(i);
  pp_image_destroy(mask);
}

TEST(backdrop_strength_blends_and_zero_changes_nothing) {
  const Face f(kTones[3], {});
  pp_image* mask = subjectMask(f);
  pp_image* before = f.image();
  pp_image* none = run(f, mask, PP_BACKDROP_AURORA, 0.0f);
  EXPECT_NEAR(diffOver(before, none, f, 0), 0.0, 1e-9);
  pp_image* half = run(f, mask, PP_BACKDROP_AURORA, 0.5f);
  pp_image* full = run(f, mask, PP_BACKDROP_AURORA, 1.0f);
  const double dh = diffOver(before, half, f, 9), df = diffOver(before, full, f, 9);
  std::printf("    background change: half %.1f, full %.1f\n", dh, df);
  EXPECT_TRUE(dh > 0.35 * df && dh < 0.65 * df);
  for (pp_image* i : {before, none, half, full}) pp_image_destroy(i);
  pp_image_destroy(mask);
}

TEST(backdrop_painted_designs_do_not_depend_on_the_old_background) {
  const Face a(kTones[2], {}), b(kTones[2], {.whiteWall = true});
  pp_image* mask = subjectMask(a);
  for (int d : {PP_BACKDROP_CANVAS, PP_BACKDROP_NEON, PP_BACKDROP_AURORA}) {
    pp_image* x = run(a, mask, d);
    pp_image* y = run(b, mask, d);
    const Mean mx = meanBackground(x, a), my = meanBackground(y, b);
    EXPECT_NEAR(mx.r, my.r, 2.5);
    EXPECT_NEAR(mx.g, my.g, 2.5);
    EXPECT_NEAR(mx.b, my.b, 2.5);
    pp_image_destroy(x);
    pp_image_destroy(y);
  }
  pp_image_destroy(mask);
}

TEST(backdrop_signature_softens_the_background_and_keeps_the_face_sharp) {
  const Face f(kTones[2], {});
  pp_image* mask = subjectMask(f);
  pp_image* before = f.image();
  pp_image* after = run(f, mask, PP_BACKDROP_SIGNATURE);
  const double t0 = fineTexture(before, f, 9), t1 = fineTexture(after, f, 9);
  const double s0 = fineTexture(before, f, 8), s1 = fineTexture(after, f, 8);
  std::printf("    background texture x%.2f, cheek texture x%.2f\n", t1 / t0, s1 / s0);
  EXPECT_TRUE(t1 < 0.5 * t0);
  EXPECT_TRUE(s1 > 0.85 * s0);
  pp_image_destroy(before);
  pp_image_destroy(after);
  pp_image_destroy(mask);
}

TEST(backdrop_rejects_bad_input_and_can_be_cancelled) {
  const Face f(kTones[2], {});
  pp_image* mask = subjectMask(f);
  pp_image* img = f.image();
  pp_backdrop_params p{};
  p.design = PP_BACKDROP_STAGE;
  p.strength = 1.0f;
  EXPECT_EQ(pp_backdrop(nullptr, mask, nullptr, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_backdrop(img, nullptr, nullptr, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_backdrop(img, mask, nullptr, nullptr, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_backdrop_params bad = p;
  bad.design = PP_BACKDROP_COUNT;
  EXPECT_EQ(pp_backdrop(img, mask, nullptr, &bad, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  bad.design = -1;
  EXPECT_EQ(pp_backdrop(img, mask, nullptr, &bad, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  bad = p;
  bad.strength = std::nanf("");
  EXPECT_EQ(pp_backdrop(img, mask, nullptr, &bad, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_cancel_token* token = pp_cancel_token_create();
  pp_cancel_token_cancel(token);
  EXPECT_EQ(pp_backdrop(img, mask, nullptr, &p, token, nullptr), PP_ERROR_CANCELLED);
  pp_cancel_token_destroy(token);
  float cov[2] = {0, 0};
  EXPECT_EQ(pp_backdrop(img, mask, nullptr, &p, nullptr, cov), PP_OK);
  EXPECT_TRUE(cov[0] > 0.15f && cov[0] < 0.6f);
  pp_image_destroy(img);
  pp_image_destroy(mask);
}

// Segmenters stop short of soft or curly hair and can notch the crown; a
// repainted background must not cut the head along that line. A seam in the
// wall just above the head is not hair and must not be kept.
struct HairScene {
  int W = 360, H = 480;
  float cx = 180, cy = 260, rx = 95, ry = 125;  // head; the hair cap is its top part
  int seamTop = 0, seamBottom = 0;
  bool inHead(float x, float y, float grow) const {
    const float dx = (x - cx) / (rx + grow), dy = (y - cy) / (ry + grow);
    return dx * dx + dy * dy <= 1.0f;
  }
  // Hair: a mean level with curly detail (busy at fine scale). Wall: flat, with a seam.
  void build(float hairLevel, const float wall[3], const float seamCol[3], pp_image** img, pp_image** mask) {
    seamTop = static_cast<int>(cy - ry) - 2, seamBottom = seamTop + 7;
    pp_image_create(W, H, PP_FORMAT_RGBA8, img);
    pp_image_create(W, H, PP_FORMAT_GRAY8, mask);
    pp_image_info ii{}, mi{};
    pp_image_get_info(*img, &ii);
    pp_image_get_info(*mask, &mi);
    auto* px = static_cast<uint8_t*>(pp_image_pixels(*img));
    auto* mk = static_cast<uint8_t*>(pp_image_pixels(*mask));
    uint32_t h = 12345u;
    auto rnd = [&]() { h = h * 1664525u + 1013904223u; return static_cast<float>((h >> 8) & 0xFFFF) / 65535.0f; };
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        uint8_t* q = px + static_cast<size_t>(y) * ii.stride + x * 4;
        const float fx = static_cast<float>(x) + 0.5f, fy = static_cast<float>(y) + 0.5f;
        float c[3] = {wall[0], wall[1], wall[2]};
        if (y >= seamTop && y < seamBottom) c[0] = seamCol[0], c[1] = seamCol[1], c[2] = seamCol[2];
        const bool head = inHead(fx, fy, 0), hair = head && fy < cy - 0.35f * ry;
        if (head && !hair) c[0] = 150, c[1] = 104, c[2] = 80;
        if (hair) {
          const float v = hairLevel * (0.55f + 0.9f * rnd());
          c[0] = v, c[1] = v * 0.93f, c[2] = v * 0.8f;
        }
        for (int k = 0; k < 3; ++k) q[k] = static_cast<uint8_t>(std::clamp(c[k], 0.0f, 255.0f));
        q[3] = 255;
        // The mask: the head shrunk by 9 px (the soft outer hair missed), a
        // notch at the crown, and the body below.
        bool m = inHead(fx, fy, -9) || (fy > cy + 60 && std::fabs(fx - cx) < 150);
        if (std::fabs(fx - cx) < 14 && fy < cy - ry + 30) m = false;
        mk[static_cast<size_t>(y) * mi.stride + x] = m ? 255 : 0;
      }
  }
  // Mean output level over: the hair ring the mask missed, the crown notch,
  // and the seam beside the crown (within the hair search, but not hair).
  void measure(pp_image* img, double& ring, double& notch, double& seam) const {
    const auto* px = static_cast<const uint8_t*>(pp_image_pixels(img));
    pp_image_info ii{};
    pp_image_get_info(img, &ii);
    ring = notch = seam = 0;
    int nr = 0, nn = 0, ns = 0;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const uint8_t* q = px + static_cast<size_t>(y) * ii.stride + x * 4;
        const float fx = static_cast<float>(x) + 0.5f, fy = static_cast<float>(y) + 0.5f;
        const double l = (q[0] + q[1] + q[2]) / 3.0;
        if (inHead(fx, fy, -1) && !inHead(fx, fy, -8) && fy < cy - 0.45f * ry && std::fabs(fx - cx) > 20) ring += l, ++nr;
        if (std::fabs(fx - cx) < 10 && fy > cy - ry + 3 && fy < cy - ry + 26) notch += l, ++nn;
        if (y >= seamTop + 1 && y < seamBottom - 1 && !inHead(fx, fy, 6) && inHead(fx, fy, 16)) seam += l, ++ns;
      }
    EXPECT_TRUE(nr > 200 && nn > 100 && ns > 100);
    ring /= std::max(nr, 1), notch /= std::max(nn, 1), seam /= std::max(ns, 1);
  }
};

TEST(backdrop_keeps_hair_the_mask_missed_but_not_a_seam_in_the_wall) {
  pp_backdrop_params p{};
  p.strength = 1.0f;
  p.seed = 7;
  {
    // Dark curly hair, a pale wall with a brown seam, a white studio.
    HairScene s;
    const float wall[3] = {205, 192, 172}, seam[3] = {120, 98, 80};
    pp_image *img = nullptr, *mask = nullptr;
    s.build(40, wall, seam, &img, &mask);
    p.design = PP_BACKDROP_PORCELAIN;
    EXPECT_EQ(pp_backdrop(img, mask, nullptr, &p, nullptr, nullptr), PP_OK);
    double ring, notch, seamL;
    s.measure(img, ring, notch, seamL);
    // Hair ~40, the white backdrop ~240 (Porcelain's light wrap lifts the hair's very edge).
    std::printf("    dark hair on white: ring %.0f, crown notch %.0f, seam %.0f\n", ring, notch, seamL);
    EXPECT_TRUE(ring < 130);
    EXPECT_TRUE(notch < 130);
    EXPECT_TRUE(seamL > 200);
    pp_image_destroy(img);
    pp_image_destroy(mask);
  }
  {
    // Fair hair on a mid wall with a dark seam, a black stage.
    HairScene s;
    const float wall[3] = {110, 112, 118}, seam[3] = {60, 60, 64};
    pp_image *img = nullptr, *mask = nullptr;
    s.build(200, wall, seam, &img, &mask);
    p.design = PP_BACKDROP_STAGE;
    EXPECT_EQ(pp_backdrop(img, mask, nullptr, &p, nullptr, nullptr), PP_OK);
    double ring, notch, seamL;
    s.measure(img, ring, notch, seamL);
    // Hair ~190, the stage ~10.
    std::printf("    fair hair on black: ring %.0f, crown notch %.0f, seam %.0f\n", ring, notch, seamL);
    EXPECT_TRUE(ring > 120);
    EXPECT_TRUE(notch > 120);
    EXPECT_TRUE(seamL < 40);
    pp_image_destroy(img);
    pp_image_destroy(mask);
  }
}

// A patch of the background the segmenter took for the person, not connected
// to anyone, is not cut out and pasted onto the new scene.
TEST(backdrop_drops_a_stray_patch_of_background_taken_for_the_person) {
  const Face f(kTones[2], {});
  pp_image* mask = subjectMask(f);
  auto* mk = static_cast<uint8_t*>(pp_image_pixels(mask));
  pp_image_info mi{};
  pp_image_get_info(mask, &mi);
  // A stray patch in the top-left corner of the wall.
  for (int y = 6; y < 40; ++y)
    for (int x = 6; x < 50; ++x) mk[static_cast<size_t>(y) * mi.stride + x] = 255;
  pp_image* img = run(f, mask, PP_BACKDROP_PORCELAIN);
  const auto* px = static_cast<const uint8_t*>(pp_image_pixels(img));
  pp_image_info ii{};
  pp_image_get_info(img, &ii);
  double patch = 0;
  int n = 0;
  for (int y = 10; y < 36; ++y)
    for (int x = 10; x < 46; ++x, ++n) {
      const uint8_t* q = px + static_cast<size_t>(y) * ii.stride + x * 4;
      patch += (q[0] + q[1] + q[2]) / 3.0;
    }
  patch /= n;
  std::printf("    stray patch on the white studio: %.0f (white ~240, the old wall ~100)\n", patch);
  EXPECT_TRUE(patch > 215);
  // The person is still there.
  pp_image* before = f.image();
  const double cheek = diffOver(before, img, f, 8);
  EXPECT_TRUE(cheek < 12);
  pp_image_destroy(before);
  pp_image_destroy(img);
  pp_image_destroy(mask);
}
