// Product photography (Phase 12). What a person would notice:
//  - a product shot on white paper (brighter in the middle, as real paper
//    is) comes out whole, without its own shadow and without a fringe of
//    the paper's colour;
//  - a white label on the product stays; a real gap (a mug's handle) the
//    object mask calls background stays open; specks of dust go; a set of
//    two products stays a set;
//  - on a busy backdrop the object mask decides;
//  - keep and remove strokes do what they say;
//  - the catalogue shot puts the product where asked, at the size asked,
//    standing on its base, mirrored if asked;
//  - shadows sit under the product, a cast shadow goes where the light
//    sends it, a reflection fades out below; a transparent frame keeps its
//    shadow as soft black over nothing;
//  - backdrops are smooth (no banding); the old paper's colour cast is
//    taken out and a grey paper brought up to white;
//  - bad input is refused.
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

constexpr int kW = 400, kH = 320;

pp_image* rgba(int w, int h) {
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
  return img;
}
pp_image* gray(int w, int h) {
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_GRAY8, &img);
  return img;
}
int strideOf(pp_image* img) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  return info.stride;
}
uint8_t* at(pp_image* img, int x, int y, int bpp = 4) {
  return static_cast<uint8_t*>(pp_image_pixels(img)) + static_cast<size_t>(y) * strideOf(img) + x * bpp;
}
float alphaAt(pp_image* img, int x, int y) { return at(img, x, y)[3] / 255.0f; }

// A product (a rounded box, red, with a white label) on white paper that is
// brighter in the middle and a little blue, with the product's shadow to
// its right. Optional: a hole through it (a handle) and a second product.
struct Shot {
  pp_image* image = nullptr;
  std::vector<float> truth;
  int x0 = 140, y0 = 80, x1 = 260, y1 = 260;  // the product
};

float boxAlpha(float x, float y, float x0, float y0, float x1, float y1) {
  const float d = std::min({x - x0, x1 - x, y - y0, y1 - y});
  return std::clamp(0.5f + 0.5f * d, 0.0f, 1.0f);  // a 2 px soft edge, as a lens gives
}

Shot makeShot(bool hole = false, bool second = false, bool speck = false, bool busy = false) {
  Shot s;
  s.image = rgba(kW, kH);
  s.truth.assign(static_cast<size_t>(kW) * kH, 0.0f);
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const float fx = static_cast<float>(x), fy = static_cast<float>(y);
      float a = boxAlpha(fx + 0.5f, fy + 0.5f, static_cast<float>(s.x0), static_cast<float>(s.y0), static_cast<float>(s.x1), static_cast<float>(s.y1));
      if (hole) a = std::min(a, 1.0f - boxAlpha(fx + 0.5f, fy + 0.5f, 175, 130, 225, 190));
      if (second) a = std::max(a, boxAlpha(fx + 0.5f, fy + 0.5f, 300, 180, 350, 260));
      const float sp = speck ? boxAlpha(fx + 0.5f, fy + 0.5f, 60, 60, 63, 63) : 0.0f;
      s.truth[static_cast<size_t>(y) * kW + x] = a;
      // The paper: light fall-off from the middle, a blue cast.
      const float r2 = ((fx - 200) * (fx - 200) + (fy - 160) * (fy - 160)) / (220.0f * 220.0f);
      float bg = 238.0f - 30.0f * r2;
      float bgR = bg - 8, bgG = bg - 3, bgB = bg + 6;
      if (busy) {
        const float t = ((x / 20 + y / 20) % 2) ? 1.0f : 0.0f;
        bgR = 60 + 150 * t, bgG = 120 + 40 * t, bgB = 40 + 160 * (1 - t);
      }
      // The product's shadow on the paper, to its right and below.
      const float shadow = boxAlpha(fx + 0.5f, fy + 0.5f, 262, 200, 300, 262) * 0.45f;
      bgR *= 1 - shadow, bgG *= 1 - shadow, bgB *= 1 - shadow;
      bgR = bgR * (1 - sp) + 60 * sp, bgG = bgG * (1 - sp) + 60 * sp, bgB = bgB * (1 - sp) + 60 * sp;
      // The product: red with a white label (a small white area inside).
      const bool label = fx >= 185 && fx < 215 && fy >= 215 && fy < 235;
      const float fR = label ? 236.0f : 200.0f, fG = label ? 236.0f : 40.0f, fB = label ? 240.0f : 45.0f;
      uint8_t* p = at(s.image, x, y);
      p[0] = static_cast<uint8_t>(std::lround(a * fR + (1 - a) * bgR));
      p[1] = static_cast<uint8_t>(std::lround(a * fG + (1 - a) * bgG));
      p[2] = static_cast<uint8_t>(std::lround(a * fB + (1 - a) * bgB));
      p[3] = 255;
    }
  return s;
}

pp_image* maskOf(const Shot& s, bool holeIsBackground) {
  // What an object model gives: a third of the size, blurry.
  const int w = kW / 3, h = kH / 3;
  pp_image* m = gray(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float v = 0;
      for (int dy = 0; dy < 3; ++dy)
        for (int dx = 0; dx < 3; ++dx) {
          const int X = x * 3 + dx, Y = y * 3 + dy;
          float t = s.truth[static_cast<size_t>(Y) * kW + X];
          if (!holeIsBackground && X >= 175 && X < 225 && Y >= 130 && Y < 190) t = 1.0f;
          v += t;
        }
      *at(m, x, y, 1) = static_cast<uint8_t>(std::lround(v / 9.0f * 255.0f));
    }
  return m;
}

float iou(pp_image* out, const std::vector<float>& truth) {
  double inter = 0, uni = 0;
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const bool a = alphaAt(out, x, y) > 0.5f, b = truth[static_cast<size_t>(y) * kW + x] > 0.5f;
      inter += a && b;
      uni += a || b;
    }
  return static_cast<float>(inter / std::max(1.0, uni));
}

pp_product_matte_params defaults() { return {0.5f, 0.2f, 0.0f, 1}; }

pp_product_render_params catalogue(int w = 600, int h = 600) {
  pp_product_render_params p{};
  p.backdrop = PP_PRODUCT_SOLID;
  p.wall[0] = p.wall[1] = p.wall[2] = 255;
  p.floor[0] = p.floor[1] = p.floor[2] = 255;
  p.horizon = 0.65f, p.softness = 0.5f, p.light = 0.0f;
  p.contact = 0.0f, p.soft = 0.0f, p.cast = 0.0f, p.cast_angle = 0.0f, p.cast_length = 0.5f, p.cast_softness = 0.5f, p.reflection = 0.0f;
  p.auto_white = 0, p.auto_exposure = 0;
  p.reference[0] = p.reference[1] = p.reference[2] = -1;
  p.width = w, p.height = h, p.fill = 0.5f, p.x = 0.5f, p.y = 0.5f;
  p.seed = 3;
  return p;
}

// A cut-out of a plain red box (alpha exact) at 200 x 300.
pp_image* boxCutout() {
  pp_image* c = rgba(200, 300);
  for (int y = 0; y < 300; ++y)
    for (int x = 0; x < 200; ++x) {
      uint8_t* p = at(c, x, y);
      const float a = boxAlpha(x + 0.5f, y + 0.5f, 50, 50, 150, 250);
      p[0] = 200, p[1] = 40, p[2] = 45, p[3] = static_cast<uint8_t>(std::lround(a * 255));
      if (x < 100 && a > 0) p[2] = 200;  // the left half blue: shows a mirror
    }
  return c;
}

float lum(pp_image* img, int x, int y) {
  const uint8_t* p = at(img, x, y);
  return (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.0f;
}

}  // namespace

TEST(product_cutout_takes_the_product_and_leaves_its_shadow_and_the_paper) {
  Shot s = makeShot();
  pp_image* out = rgba(kW, kH);
  pp_product_matte_params p = defaults();
  pp_product_matte_report r{};
  EXPECT_EQ(pp_product_cutout(s.image, nullptr, nullptr, 0, &p, out, nullptr, &r), PP_OK);
  EXPECT_TRUE(r.plain == 1);
  EXPECT_EQ(r.objects, 1);
  EXPECT_TRUE(iou(out, s.truth) > 0.97f);
  // The shadow to the right is not the product.
  double shadow = 0;
  for (int y = 205; y < 258; ++y)
    for (int x = 268; x < 296; ++x) shadow += alphaAt(out, x, y);
  EXPECT_TRUE(shadow / (53.0 * 28.0) < 0.05);
  // The white label inside the product stays.
  EXPECT_TRUE(alphaAt(out, 200, 225) > 0.95f);
  // The paper's colour (near white, a little blue) is reported.
  EXPECT_TRUE(r.backdrop[2] > r.backdrop[0] && r.backdrop[0] > 200.0f);
  EXPECT_NEAR(r.left, 140.0f / kW, 0.02f);
  EXPECT_NEAR(r.bottom, 260.0f / kH, 0.02f);
  // Edge colours are the product's, not pink from the paper.
  int edge = 0, pinkish = 0;
  for (int y = 100; y < 240; ++y)
    for (int x = 135; x < 146; ++x) {
      const uint8_t* q = at(out, x, y);
      if (q[3] > 20 && q[3] < 235) {
        ++edge;
        if (q[1] > 90) ++pinkish;
      }
    }
  EXPECT_TRUE(edge > 0 && pinkish * 5 < edge);
  pp_image_destroy(out);
  pp_image_destroy(s.image);
}

TEST(product_cutout_keeps_real_gaps_open_and_drops_specks_but_keeps_sets) {
  // A handle-like gap, shown as background by the model: it stays open.
  Shot s = makeShot(true, true, true);
  pp_image* hint = maskOf(s, true);
  pp_image* out = rgba(kW, kH);
  pp_product_matte_params p = defaults();
  pp_product_matte_report r{};
  EXPECT_EQ(pp_product_cutout(s.image, hint, nullptr, 0, &p, out, nullptr, &r), PP_OK);
  EXPECT_TRUE(alphaAt(out, 200, 160) < 0.1f);  // the gap
  EXPECT_TRUE(alphaAt(out, 325, 220) > 0.9f);  // the second product
  EXPECT_EQ(r.objects, 2);
  EXPECT_TRUE(iou(out, s.truth) > 0.95f);
  // Without a model the speck of dust (3 px) is too small to be a product.
  pp_image* out2 = rgba(kW, kH);
  EXPECT_EQ(pp_product_cutout(s.image, nullptr, nullptr, 0, &p, out2, nullptr, &r), PP_OK);
  EXPECT_TRUE(alphaAt(out2, 61, 61) < 0.1f);
  EXPECT_EQ(r.objects, 2);
  pp_image_destroy(out2);
  pp_image_destroy(out);
  pp_image_destroy(hint);
  pp_image_destroy(s.image);
}

TEST(product_cutout_follows_the_object_mask_on_a_busy_backdrop) {
  Shot s = makeShot(false, false, false, true);
  pp_image* hint = maskOf(s, true);
  pp_image* out = rgba(kW, kH);
  pp_product_matte_params p = defaults();
  pp_product_matte_report r{};
  EXPECT_EQ(pp_product_cutout(s.image, hint, nullptr, 0, &p, out, nullptr, &r), PP_OK);
  EXPECT_TRUE(r.plain == 0);
  EXPECT_TRUE(iou(out, s.truth) > 0.93f);
  pp_image_destroy(out);
  pp_image_destroy(hint);
  pp_image_destroy(s.image);
}

TEST(product_cutout_strokes_keep_and_remove) {
  Shot s = makeShot();
  pp_image* out = rgba(kW, kH);
  pp_product_matte_params p = defaults();
  const float keepPts[] = {60.0f / kW, 60.0f / kH};
  const float removePts[] = {200.0f / kW, 120.0f / kH};
  const pp_studio_stroke strokes[] = {{PP_STUDIO_KEEP, 0.03f, 1, keepPts}, {PP_STUDIO_REMOVE, 0.03f, 1, removePts}};
  EXPECT_EQ(pp_product_cutout(s.image, nullptr, strokes, 2, &p, out, nullptr, nullptr), PP_OK);
  EXPECT_TRUE(alphaAt(out, 60, 60) > 0.8f);
  EXPECT_TRUE(alphaAt(out, 200, 120) < 0.2f);
  pp_image_destroy(out);
  pp_image_destroy(s.image);
}

TEST(product_render_places_the_product_at_the_size_and_place_asked) {
  pp_image* c = boxCutout();
  pp_product_render_params p = catalogue(800, 600);
  p.backdrop = PP_PRODUCT_TRANSPARENT;
  p.fill = 0.6f, p.x = 0.4f, p.y = 0.85f, p.ground = 1;
  pp_image* out = nullptr;
  pp_product_render_report r{};
  EXPECT_EQ(pp_product_render(c, nullptr, &p, nullptr, &out, &r), PP_OK);
  pp_image_info info{};
  pp_image_get_info(out, &info);
  EXPECT_EQ(info.width, 800);
  EXPECT_EQ(info.height, 600);
  // The box is 100 x 200: its height fills 60% of 600 = 360 px.
  EXPECT_NEAR(r.bottom, 0.85f, 0.005f);
  EXPECT_NEAR((r.bottom - r.top) * 600.0f, 360.0f, 2.0f);
  EXPECT_NEAR((r.left + r.right) * 0.5f, 0.4f, 0.005f);
  int top = -1, bottom = -1;
  for (int y = 0; y < 600; ++y)
    if (alphaAt(out, 320, y) > 0.5f) bottom = y, top = top < 0 ? y : top;
  EXPECT_NEAR(static_cast<float>(bottom + 1), 0.85f * 600.0f, 2.0f);
  EXPECT_NEAR(static_cast<float>(bottom - top + 1), 360.0f, 3.0f);
  EXPECT_TRUE(alphaAt(out, 20, 20) < 0.01f);  // nothing behind
  // Mirrored: blue on the right.
  pp_image* flipped = nullptr;
  p.flip = 1;
  EXPECT_EQ(pp_product_render(c, nullptr, &p, nullptr, &flipped, nullptr), PP_OK);
  EXPECT_TRUE(at(out, 290, 300)[2] > 150 && at(flipped, 290, 300)[2] < 100);
  pp_image_destroy(flipped);
  pp_image_destroy(out);
  pp_image_destroy(c);
}

TEST(product_render_shadows_ground_the_product_where_the_light_sends_them) {
  pp_image* c = boxCutout();
  pp_product_render_params p = catalogue(600, 600);
  p.fill = 0.5f, p.y = 0.8f, p.ground = 1;
  pp_image* plain = nullptr;
  EXPECT_EQ(pp_product_render(c, nullptr, &p, nullptr, &plain, nullptr), PP_OK);
  p.contact = 0.8f, p.soft = 0.5f;
  pp_image* grounded = nullptr;
  EXPECT_EQ(pp_product_render(c, nullptr, &p, nullptr, &grounded, nullptr), PP_OK);
  // Base at y = 480; the box spans x 225..375.
  EXPECT_TRUE(lum(plain, 300, 483) > 0.99f);
  EXPECT_TRUE(lum(grounded, 300, 483) < 0.85f);       // just under the base: shade
  EXPECT_TRUE(lum(grounded, 300, 590) > 0.97f);       // far below: none
  EXPECT_TRUE(lum(grounded, 300, 100) > 0.99f);       // above the product: none
  // A cast shadow behind, leaning right: shade up and to the right of the box.
  p.contact = 0.0f, p.soft = 0.0f, p.cast = 1.0f, p.cast_angle = 40.0f, p.cast_length = 0.6f;
  pp_image* cast = nullptr;
  EXPECT_EQ(pp_product_render(c, nullptr, &p, nullptr, &cast, nullptr), PP_OK);
  EXPECT_TRUE(lum(cast, 420, 420) < 0.9f);
  EXPECT_TRUE(lum(cast, 180, 420) > 0.98f);
  // A reflection: the product's colour below the base, fading.
  p.cast = 0.0f, p.reflection = 1.0f;
  pp_image* refl = nullptr;
  EXPECT_EQ(pp_product_render(c, nullptr, &p, nullptr, &refl, nullptr), PP_OK);
  const uint8_t* near = at(refl, 340, 486);
  const uint8_t* far = at(refl, 340, 560);
  EXPECT_TRUE(near[1] < 200 && far[1] > near[1]);
  // Transparent: the shadow is kept as soft black.
  p.reflection = 0.0f, p.contact = 0.8f, p.backdrop = PP_PRODUCT_TRANSPARENT;
  pp_image* clear = nullptr;
  EXPECT_EQ(pp_product_render(c, nullptr, &p, nullptr, &clear, nullptr), PP_OK);
  EXPECT_TRUE(alphaAt(clear, 300, 483) > 0.1f && at(clear, 300, 483)[0] < 30);
  EXPECT_TRUE(alphaAt(clear, 300, 590) < 0.02f);
  for (auto* img : {plain, grounded, cast, refl, clear}) pp_image_destroy(img);
  pp_image_destroy(c);
}

TEST(product_render_backdrops_are_smooth_and_colour_is_corrected) {
  pp_image* c = boxCutout();
  pp_product_render_params p = catalogue(400, 800);
  p.backdrop = PP_PRODUCT_SWEEP, p.light = 0.6f;
  p.wall[0] = 70, p.wall[1] = 90, p.wall[2] = 120;
  p.floor[0] = 200, p.floor[1] = 210, p.floor[2] = 225;
  p.x = 0.5f, p.y = 0.5f, p.fill = 0.2f;
  pp_image* out = nullptr;
  EXPECT_EQ(pp_product_render(c, nullptr, &p, nullptr, &out, nullptr), PP_OK);
  // Down the left edge (no product): the largest step between rows is tiny.
  float worst = 0;
  for (int y = 1; y < 800; ++y) {
    float a = 0, b = 0;
    for (int x = 5; x < 45; ++x) a += lum(out, x, y), b += lum(out, x, y - 1);
    worst = std::max(worst, std::fabs(a - b) / 40.0f * 255.0f);
  }
  EXPECT_TRUE(worst < 1.5f);
  EXPECT_TRUE(lum(out, 20, 20) < lum(out, 20, 780));  // the wall darker, the floor lighter
  // The old paper was blue: auto white warms the product back.
  p.backdrop = PP_PRODUCT_SOLID, p.auto_white = 1;
  p.reference[0] = 200, p.reference[1] = 210, p.reference[2] = 240;
  pp_image* fixed = nullptr;
  pp_product_render_report r{};
  EXPECT_EQ(pp_product_render(c, nullptr, &p, nullptr, &fixed, &r), PP_OK);
  EXPECT_TRUE(r.gain[0] > 1.0f && r.gain[2] < 1.0f);
  // Grey paper (as shot a stop under) is brought up toward white.
  p.auto_white = 0, p.auto_exposure = 1;
  p.reference[0] = p.reference[1] = p.reference[2] = 150;
  pp_image* bright = nullptr;
  EXPECT_EQ(pp_product_render(c, nullptr, &p, nullptr, &bright, &r), PP_OK);
  EXPECT_TRUE(r.gain[1] > 1.5f);
  EXPECT_TRUE(lum(bright, 220, 400) > lum(fixed, 220, 400));
  for (auto* img : {out, fixed, bright}) pp_image_destroy(img);
  pp_image_destroy(c);
}

TEST(product_rejects_bad_input) {
  pp_image* c = boxCutout();
  pp_image* empty = rgba(50, 50);  // all transparent
  for (int y = 0; y < 50; ++y) std::fill(at(empty, 0, y), at(empty, 0, y) + 200, uint8_t{0});
  pp_product_render_params p = catalogue();
  pp_image* out = nullptr;
  EXPECT_TRUE(pp_product_render(empty, nullptr, &p, nullptr, &out, nullptr) != PP_OK);
  EXPECT_TRUE(out == nullptr);
  p.width = 9;
  EXPECT_TRUE(pp_product_render(c, nullptr, &p, nullptr, &out, nullptr) != PP_OK);
  p = catalogue();
  p.backdrop = 9;
  EXPECT_TRUE(pp_product_render(c, nullptr, &p, nullptr, &out, nullptr) != PP_OK);
  p = catalogue();
  p.backdrop = PP_PRODUCT_SCENE;  // no scene photo
  EXPECT_TRUE(pp_product_render(c, nullptr, &p, nullptr, &out, nullptr) != PP_OK);
  p = catalogue();
  p.fill = NAN;
  EXPECT_TRUE(pp_product_render(c, nullptr, &p, nullptr, &out, nullptr) != PP_OK);
  pp_image* small = rgba(10, 10);
  pp_product_matte_params mp = defaults();
  EXPECT_TRUE(pp_product_cutout(c, nullptr, nullptr, 0, &mp, small, nullptr, nullptr) != PP_OK);
  pp_image_destroy(small);
  pp_image_destroy(empty);
  pp_image_destroy(c);
}
