// The photoshoot studio (cut-out and compose). What a person would notice:
//  - the cut-out follows the photo's own edge, not the blocky, slightly
//    misplaced person mask, and leaves no veil of the old room;
//  - edge pixels lose the old background's colour (no pink fringe);
//  - "keep", "remove" and "fix hair" strokes do what they say;
//  - fine strands in a hair zone are kept when hair detail is on;
//  - "shrink" shrinks and "grow" grows the edge; "softer" softens it;
//  - the report says where the person is and when the photo cuts them off;
//  - compose puts the person where asked, at the size asked, mirrored if
//    asked; the rest of the scene is untouched when no effect is on;
//  - colour match warms a person in a warm scene and stays within bounds;
//  - a person standing in the scene shades the floor at their feet, and a
//    portrait cut at the chest casts nothing; shadows stay below the horizon;
//  - the background blur softens the scene but not the person;
//  - bad input is refused.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

constexpr int kW = 360, kH = 300;

struct Px {
  uint8_t r, g, b, a;
};

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

// The scene: a person (an ellipse, brown) on a background that is pink on
// the left and green on the right, with a soft (2 px) edge. `truth` is the
// exact alpha. Thin dark strands reach out of the top of the head.
struct Scene {
  pp_image* image = nullptr;
  std::vector<float> truth;  // person alpha
  std::vector<float> strand; // 1 on the strands
  float cx = 180, cy = 175, rx = 70, ry = 105;
};

float ellipseAlpha(const Scene& s, float x, float y) {
  const float d = std::hypot((x - s.cx) / s.rx, (y - s.cy) / s.ry);
  // Distance in pixels to the boundary, roughly.
  const float px = (1.0f - d) * std::min(s.rx, s.ry);
  return std::clamp(0.5f + px / 2.0f, 0.0f, 1.0f);
}

Scene makeScene(bool strands = false) {
  Scene s;
  s.image = rgba(kW, kH);
  s.truth.assign(static_cast<size_t>(kW) * kH, 0.0f);
  s.strand.assign(static_cast<size_t>(kW) * kH, 0.0f);
  if (strands) {
    // Five strands rising from the crown, one pixel wide, 25 px long.
    for (int k = 0; k < 5; ++k) {
      const int x0 = 150 + k * 15;
      for (int t = 0; t < 25; ++t) {
        const int x = x0 + (t % 6 < 3 ? 0 : 1), y = static_cast<int>(s.cy - s.ry) + 4 - t;
        if (y >= 0) s.strand[static_cast<size_t>(y) * kW + x] = 1.0f;
      }
    }
  }
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const size_t i = static_cast<size_t>(y) * kW + x;
      float a = ellipseAlpha(s, static_cast<float>(x), static_cast<float>(y));
      a = std::max(a, s.strand[i]);
      s.truth[i] = a;
      const float bgR = x < kW / 2 ? 235.0f : 40.0f, bgG = x < kW / 2 ? 180.0f : 150.0f, bgB = x < kW / 2 ? 195.0f : 70.0f;
      // A little texture so the photo is not perfectly flat.
      const float n = static_cast<float>(((x * 7 + y * 13) % 5) - 2);
      const float fR = 110.0f + n, fG = 72.0f + n, fB = 50.0f + n;
      uint8_t* p = at(s.image, x, y);
      p[0] = static_cast<uint8_t>(std::lround(a * fR + (1 - a) * bgR));
      p[1] = static_cast<uint8_t>(std::lround(a * fG + (1 - a) * bgG));
      p[2] = static_cast<uint8_t>(std::lround(a * fB + (1 - a) * bgB));
      p[3] = 255;
    }
  return s;
}

// The person mask a model would give: a third of the resolution, blurry,
// shifted 3 px right, a little small, with a faint haze everywhere.
constexpr int kMW = 120, kMH = 100;
pp_image* coarseMask(const Scene& s, float haze = 0.06f) {
  const int w = kMW, h = kMH;
  pp_image* m = gray(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float fx = (static_cast<float>(x) + 0.5f) * kW / w - 3.0f, fy = (static_cast<float>(y) + 0.5f) * kH / h;
      const float d = std::hypot((fx - s.cx) / (s.rx - 2), (fy - s.cy) / (s.ry - 2));
      const float v = std::clamp((1.0f - d) * 6.0f + 0.5f, 0.0f, 1.0f);
      *at(m, x, y, 1) = static_cast<uint8_t>(std::lround(255.0f * std::max(v, haze)));
    }
  return m;
}

// A parts map: hair over the top of the head.
pp_image* hairZones(const Scene& s) {
  const int w = 64, h = 54;
  pp_image* z = gray(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float fx = (static_cast<float>(x) + 0.5f) * kW / w, fy = (static_cast<float>(y) + 0.5f) * kH / h;
      const float d = std::hypot((fx - s.cx) / s.rx, (fy - s.cy) / s.ry);
      uint8_t v = d < 1.0f ? 4 : 0;                               // clothes
      if (d < 1.1f && fy < s.cy - s.ry * 0.6f) v = 1;             // hair
      *at(z, x, y, 1) = v;
    }
  return z;
}

pp_studio_cutout_params defaults() { return pp_studio_cutout_params{0.0f, 0.0f, 0.5f, 1}; }

pp_image* cut(const Scene& s, pp_image* mask, const pp_image* zones, const pp_studio_cutout_params& p,
              const std::vector<pp_studio_stroke>& strokes = {}, pp_studio_cutout_report* rep = nullptr) {
  pp_image* out = rgba(kW, kH);
  EXPECT_EQ(pp_studio_cutout(s.image, mask, zones, strokes.data(), static_cast<int32_t>(strokes.size()), &p, out, nullptr, rep), PP_OK);
  return out;
}

float alphaAt(pp_image* img, int x, int y) { return at(img, x, y)[3] / 255.0f; }

// Mean |alpha - truth| over pixels within `band` px of the true edge, and
// the mean alpha far from the person.
struct Err {
  double band = 0, far = 0;
};
Err errors(pp_image* out, const Scene& s, bool fromCoarse = false, pp_image* coarse = nullptr) {
  Err e;
  int nb = 0, nf = 0;
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const size_t i = static_cast<size_t>(y) * kW + x;
      if (s.strand[i] > 0) continue;
      float a;
      if (fromCoarse) {
        const float fx = (static_cast<float>(x) + 0.5f) * kMW / kW - 0.5f, fy = (static_cast<float>(y) + 0.5f) * kMH / kH - 0.5f;
        const int ix = std::clamp(static_cast<int>(std::lround(fx)), 0, kMW - 1), iy = std::clamp(static_cast<int>(std::lround(fy)), 0, kMH - 1);
        a = *at(coarse, ix, iy, 1) / 255.0f;
      } else {
        a = alphaAt(out, x, y);
      }
      const float d = std::hypot((x - s.cx) / s.rx, (y - s.cy) / s.ry);
      const float px = std::fabs(1.0f - d) * std::min(s.rx, s.ry);
      if (px < 8) e.band += std::fabs(a - s.truth[i]), ++nb;
      if (d > 1.4f) e.far += a, ++nf;
    }
  e.band /= std::max(nb, 1);
  e.far /= std::max(nf, 1);
  return e;
}

}  // namespace

TEST(studio_cutout_follows_the_photo_not_the_coarse_mask) {
  Scene s = makeScene();
  pp_image* mask = coarseMask(s);
  pp_studio_cutout_report rep{};
  pp_image* out = cut(s, mask, nullptr, defaults(), {}, &rep);
  const Err coarse = errors(nullptr, s, true, mask), mine = errors(out, s);
  std::printf("    edge error: coarse %.3f → cut-out %.3f; far alpha %.4f → %.4f\n", coarse.band, mine.band, coarse.far, mine.far);
  EXPECT_TRUE(mine.band < 0.5 * coarse.band);
  EXPECT_TRUE(mine.band < 0.08);
  EXPECT_TRUE(mine.far < 0.002);  // no veil of the old room
  // Inside the person, the photo's own colours stand.
  const uint8_t* c = at(out, 180, 175);
  const uint8_t* o = at(s.image, 180, 175);
  EXPECT_TRUE(c[3] == 255 && c[0] == o[0] && c[1] == o[1] && c[2] == o[2]);
  // Report: the box, not cut off.
  EXPECT_TRUE(std::fabs(rep.left - 110.0f / kW) < 0.03f && std::fabs(rep.right - 250.0f / kW) < 0.03f);
  EXPECT_TRUE(std::fabs(rep.top - 70.0f / kH) < 0.03f && std::fabs(rep.bottom - 280.0f / kH) < 0.03f);
  EXPECT_TRUE(!rep.cut_bottom && !rep.cut_top && !rep.cut_left && !rep.cut_right);
  EXPECT_TRUE(rep.coverage > 0.15f && rep.coverage < 0.25f);
  pp_image_destroy(out);
  pp_image_destroy(mask);
  pp_image_destroy(s.image);
}

TEST(studio_cutout_frees_edge_colours_of_the_old_background) {
  Scene s = makeScene();
  pp_image* mask = coarseMask(s);
  pp_studio_cutout_params p = defaults();
  pp_image* clean = cut(s, mask, nullptr, p);
  p.clean_colours = 0;
  pp_image* raw = cut(s, mask, nullptr, p);
  // On the pink side, partial pixels: how far their colour is from the person's.
  double dClean = 0, dRaw = 0;
  int n = 0;
  for (int y = 100; y < 250; ++y)
    for (int x = 100; x < 180; ++x) {
      const float a = alphaAt(clean, x, y);
      if (a < 0.2f || a > 0.8f) continue;
      auto dist = [](const uint8_t* q) { return std::sqrt(std::pow(q[0] - 110.0, 2) + std::pow(q[1] - 72.0, 2) + std::pow(q[2] - 50.0, 2)); };
      dClean += dist(at(clean, x, y)), dRaw += dist(at(raw, x, y)), ++n;
    }
  dClean /= std::max(n, 1), dRaw /= std::max(n, 1);
  std::printf("    edge colour distance from the person: raw %.1f, cleaned %.1f (%d px)\n", dRaw, dClean, n);
  EXPECT_TRUE(n > 50);
  EXPECT_TRUE(dClean < 0.35 * dRaw);
  pp_image_destroy(clean);
  pp_image_destroy(raw);
  pp_image_destroy(mask);
  pp_image_destroy(s.image);
}

TEST(studio_strokes_keep_remove_and_fix_hair) {
  Scene s = makeScene(true);
  pp_image* mask = coarseMask(s);
  // Remove: a band across the person's middle. Keep: a patch of background.
  const float rm[] = {130.0f / kW, 180.0f / kH, 230.0f / kW, 180.0f / kH};
  const float kp[] = {30.0f / kW, 40.0f / kH};
  std::vector<pp_studio_stroke> strokes = {{PP_STUDIO_REMOVE, 0.03f, 2, rm}, {PP_STUDIO_KEEP, 0.04f, 1, kp}};
  pp_image* out = cut(s, mask, nullptr, defaults(), strokes);
  EXPECT_TRUE(alphaAt(out, 180, 180) < 0.05f);
  EXPECT_TRUE(alphaAt(out, 30, 40) > 0.95f);
  EXPECT_TRUE(alphaAt(out, 180, 130) > 0.95f);  // untouched elsewhere
  pp_image_destroy(out);
  // Fix hair: strands above the crown, where the mask has nothing.
  auto strandAlpha = [&](pp_image* img) {
    double a = 0;
    int n = 0;
    for (int y = 0; y < kH; ++y)
      for (int x = 0; x < kW; ++x)
        if (s.strand[static_cast<size_t>(y) * kW + x] > 0 && y < static_cast<int>(s.cy - s.ry) - 6) a += alphaAt(img, x, y), ++n;
    return a / std::max(n, 1);
  };
  pp_image* plain = cut(s, mask, nullptr, defaults());
  const float hp[] = {150.0f / kW, 55.0f / kH, 215.0f / kW, 55.0f / kH};
  pp_image* fixed = cut(s, mask, nullptr, defaults(), {{PP_STUDIO_HAIR, 0.06f, 2, hp}});
  std::printf("    strands: plain %.2f, with \"fix hair\" %.2f\n", strandAlpha(plain), strandAlpha(fixed));
  EXPECT_TRUE(strandAlpha(fixed) > strandAlpha(plain) + 0.25);
  // …and the background between the strands stays background.
  EXPECT_TRUE(alphaAt(fixed, 157, 55) < 0.2f);
  pp_image_destroy(plain);
  pp_image_destroy(fixed);
  pp_image_destroy(mask);
  pp_image_destroy(s.image);
}

TEST(studio_hair_zone_keeps_strands) {
  Scene s = makeScene(true);
  pp_image* mask = coarseMask(s);
  pp_image* zones = hairZones(s);
  pp_studio_cutout_params p = defaults();
  p.hair_detail = 0.0f;
  pp_image* none = cut(s, mask, zones, p);
  p.hair_detail = 1.0f;
  pp_image* full = cut(s, mask, zones, p);
  double a0 = 0, a1 = 0;
  int n = 0;
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const size_t i = static_cast<size_t>(y) * kW + x;
      // The strands just beyond the crown (within the hair band).
      if (s.strand[i] > 0 && y < static_cast<int>(s.cy - s.ry) - 1 && y > static_cast<int>(s.cy - s.ry) - 10) a0 += alphaAt(none, x, y), a1 += alphaAt(full, x, y), ++n;
    }
  a0 /= std::max(n, 1), a1 /= std::max(n, 1);
  std::printf("    strands near the crown: hair detail 0 → %.2f, 1 → %.2f\n", a0, a1);
  EXPECT_TRUE(a1 > a0 + 0.1);
  EXPECT_TRUE(errors(full, s).far < 0.002);
  pp_image_destroy(none);
  pp_image_destroy(full);
  pp_image_destroy(zones);
  pp_image_destroy(mask);
  pp_image_destroy(s.image);
}

TEST(studio_report_finds_the_face_of_the_person_kept) {
  Scene s = makeScene();
  pp_image* mask = coarseMask(s);
  // Parts map: clothes over the person, face skin in a box near the top of
  // the head, and another face (someone else) outside the person.
  const int w = 120, h = 100;
  pp_image* zones = gray(w, h);
  const float fl = 160, ft = 95, fr = 200, fb = 140;  // the face, in photo pixels
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float fx = (static_cast<float>(x) + 0.5f) * kW / w, fy = (static_cast<float>(y) + 0.5f) * kH / h;
      const float d = std::hypot((fx - s.cx) / s.rx, (fy - s.cy) / s.ry);
      uint8_t v = d < 1.0f ? 4 : 0;
      if (fx >= fl && fx < fr && fy >= ft && fy < fb) v = 3;
      if (fx >= 10 && fx < 50 && fy >= 20 && fy < 70) v = 3;  // not in the cut-out
      *at(zones, x, y, 1) = v;
    }
  pp_studio_cutout_report rep{};
  pp_image* o = cut(s, mask, zones, defaults(), {}, &rep);
  std::printf("    face %d: %.3f %.3f %.3f %.3f\n", rep.face, rep.face_left, rep.face_top, rep.face_right, rep.face_bottom);
  EXPECT_EQ(rep.face, 1);
  EXPECT_TRUE(std::fabs(rep.face_left - fl / kW) < 0.02f && std::fabs(rep.face_right - fr / kW) < 0.02f);
  EXPECT_TRUE(std::fabs(rep.face_top - ft / kH) < 0.02f && std::fabs(rep.face_bottom - fb / kH) < 0.02f);
  // Without a parts map there is no face to measure.
  pp_studio_cutout_report none{};
  pp_image* o2 = cut(s, mask, nullptr, defaults(), {}, &none);
  EXPECT_EQ(none.face, 0);
  pp_image_destroy(o);
  pp_image_destroy(o2);
  pp_image_destroy(zones);
  pp_image_destroy(mask);
  pp_image_destroy(s.image);
}

TEST(studio_edge_shift_and_softness) {
  Scene s = makeScene();
  pp_image* mask = coarseMask(s);
  auto run = [&](float shift, float soft, pp_studio_cutout_report& r) {
    pp_studio_cutout_params p = defaults();
    p.shift = shift, p.softness = soft;
    pp_image* o = cut(s, mask, nullptr, p, {}, &r);
    pp_image_destroy(o);
  };
  pp_studio_cutout_report base{}, shrink{}, grow{}, soft{};
  run(0, 0, base), run(1, 0, shrink), run(-1, 0, grow), run(0, 1, soft);
  std::printf("    coverage: %.4f, shrink %.4f, grow %.4f; edge share %.4f, softer %.4f\n", base.coverage, shrink.coverage,
              grow.coverage, base.edge, soft.edge);
  EXPECT_TRUE(shrink.coverage < base.coverage - 0.002f);
  EXPECT_TRUE(grow.coverage > base.coverage + 0.002f);
  EXPECT_TRUE(soft.edge > base.edge * 1.3f);
  pp_image_destroy(mask);
  pp_image_destroy(s.image);
}

TEST(studio_report_says_when_the_photo_cuts_the_person_off) {
  Scene s = makeScene();
  s.cy = 260;  // the person runs off the bottom
  pp_image_destroy(s.image);
  Scene t = makeScene();
  pp_image_destroy(t.image);
  // Rebuild with the person low in the frame.
  Scene low;
  low.cy = 260;
  low.image = rgba(kW, kH);
  low.truth.assign(static_cast<size_t>(kW) * kH, 0.0f);
  low.strand.assign(static_cast<size_t>(kW) * kH, 0.0f);
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const float a = ellipseAlpha(low, static_cast<float>(x), static_cast<float>(y));
      uint8_t* p = at(low.image, x, y);
      p[0] = static_cast<uint8_t>(a * 110 + (1 - a) * 235), p[1] = static_cast<uint8_t>(a * 72 + (1 - a) * 180);
      p[2] = static_cast<uint8_t>(a * 50 + (1 - a) * 195), p[3] = 255;
    }
  pp_image* mask = coarseMask(low);
  pp_studio_cutout_report rep{};
  pp_image* out = cut(low, mask, nullptr, defaults(), {}, &rep);
  EXPECT_TRUE(rep.cut_bottom && !rep.cut_top);
  EXPECT_TRUE(rep.bottom > 0.99f);
  pp_image_destroy(out);
  pp_image_destroy(mask);
  pp_image_destroy(low.image);
}

namespace {

// A cut-out of a flat "person": a red rectangle 40 x 120 with feet (its
// bottom) inside the image, on a transparent canvas 60 x 140.
pp_image* flatPerson(bool cutAtBottom = false, uint8_t r = 200, uint8_t g = 40, uint8_t b = 40) {
  pp_image* c = rgba(60, 140);
  for (int y = 0; y < 140; ++y)
    for (int x = 0; x < 60; ++x) {
      uint8_t* p = at(c, x, y);
      const bool in = x >= 10 && x < 50 && y >= 10 && (cutAtBottom ? true : y < 130);
      p[0] = r, p[1] = g, p[2] = b, p[3] = in ? 255 : 0;
    }
  return c;
}

pp_image* scene(uint8_t r, uint8_t g, uint8_t b, bool stripes = false) {
  pp_image* bg = rgba(400, 300);
  for (int y = 0; y < 300; ++y)
    for (int x = 0; x < 400; ++x) {
      uint8_t* p = at(bg, x, y);
      const bool s = stripes && ((x / 3) % 2 == 0);
      p[0] = s ? 255 - r : r, p[1] = s ? 255 - g : g, p[2] = s ? 255 - b : b, p[3] = 255;
    }
  return bg;
}

pp_studio_look plainLook() { return pp_studio_look{0, 0, 0, 0, 0, 0.45f, 0, 0, 11u, 0}; }

double meanLuma(pp_image* img, int x0, int y0, int x1, int y1) {
  double s = 0;
  int n = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      const uint8_t* p = at(img, x, y);
      s += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2], ++n;
    }
  return s / std::max(n, 1);
}

}  // namespace

TEST(studio_compose_places_the_person_where_asked) {
  pp_image* cutImg = flatPerson();
  pp_image* bg = scene(90, 120, 160);
  pp_image* ref = scene(90, 120, 160);
  // Feet at (0.3, 0.9), 60% of the frame tall.
  pp_studio_placement pl{0.3f, 0.9f, 0.6f, 0, 0, 0};
  pp_studio_look lk = plainLook();
  pp_studio_compose_report rep{};
  ASSERT_OK(pp_studio_compose(bg, cutImg, &pl, &lk, nullptr, &rep));
  std::printf("    box %.3f %.3f %.3f %.3f\n", rep.left, rep.top, rep.right, rep.bottom);
  EXPECT_TRUE(std::fabs(rep.bottom - 0.9f) < 0.01f && std::fabs(rep.top - 0.3f) < 0.01f);
  // 40 x 120 at 180 px tall → 60 px wide, centred on x = 120.
  EXPECT_TRUE(std::fabs(rep.left * 400 - 90) < 2 && std::fabs(rep.right * 400 - 150) < 2);
  EXPECT_EQ(rep.grounded, 1);
  const uint8_t* mid = at(bg, 120, 180);
  EXPECT_TRUE(mid[0] == 200 && mid[1] == 40 && mid[2] == 40);
  // Everything outside the person's box is untouched.
  bool same = true;
  for (int y = 0; y < 300 && same; ++y)
    for (int x = 0; x < 400; ++x) {
      if (x >= 86 && x < 154 && y >= 86 && y < 274) continue;
      if (std::memcmp(at(bg, x, y), at(ref, x, y), 3) != 0) {
        same = false;
        break;
      }
    }
  EXPECT_TRUE(same);
  // Mirror: an asymmetric cut-out lands mirrored.
  pp_image* half = rgba(60, 140);
  for (int y = 0; y < 140; ++y)
    for (int x = 0; x < 60; ++x) {
      uint8_t* p = at(half, x, y);
      p[0] = x < 30 ? 255 : 0, p[1] = x < 30 ? 0 : 255, p[2] = 0, p[3] = (x >= 10 && x < 50 && y >= 10 && y < 130) ? 255 : 0;
    }
  pp_image* bg2 = scene(90, 120, 160);
  pl.flip = 1;
  ASSERT_OK(pp_studio_compose(bg2, half, &pl, &lk, nullptr, nullptr));
  EXPECT_TRUE(at(bg2, 100, 180)[1] > 200 && at(bg2, 140, 180)[0] > 200);  // green now on the left, red on the right
  pp_image_destroy(bg2);
  pp_image_destroy(half);
  pp_image_destroy(cutImg);
  pp_image_destroy(bg);
  pp_image_destroy(ref);
}

TEST(studio_compose_colour_match_moves_toward_the_scene_within_bounds) {
  pp_image* person = flatPerson(false, 150, 150, 150);
  auto run = [&](uint8_t r, uint8_t g, uint8_t b, float match, pp_studio_compose_report& rep) {
    pp_image* bg = scene(r, g, b);
    pp_studio_placement pl{0.5f, 0.9f, 0.6f, 0, 0, 0};
    pp_studio_look lk = plainLook();
    lk.color_match = match;
    EXPECT_EQ(pp_studio_compose(bg, person, &pl, &lk, nullptr, &rep), PP_OK);
    const uint8_t* p = at(bg, 200, 180);
    const double warm = static_cast<double>(p[0]) - p[2];
    const double luma = meanLuma(bg, 190, 150, 210, 210);
    pp_image_destroy(bg);
    return std::pair<double, double>{warm, luma};
  };
  pp_studio_compose_report r0{}, r1{}, r2{};
  const auto off = run(230, 150, 80, 0.0f, r0);
  const auto warm = run(230, 150, 80, 1.0f, r1);
  const auto dark = run(25, 25, 30, 1.0f, r2);
  std::printf("    warmth off %.0f, in a warm scene %.0f; luma off %.0f, in a dark scene %.0f (gain %.2f)\n", off.first, warm.first,
              off.second, dark.second, r2.exposure);
  EXPECT_TRUE(warm.first > off.first + 8);
  EXPECT_TRUE(r2.exposure < 1.0f && r2.exposure >= 0.7f);
  EXPECT_TRUE(dark.second < off.second && dark.second > 0.6 * off.second);
  EXPECT_TRUE(r1.balance[0] > r1.balance[2]);
  pp_image_destroy(person);
}

TEST(studio_compose_shadows_at_the_feet_only_on_the_ground) {
  pp_image* standing = flatPerson(false);
  pp_image* portrait = flatPerson(true);
  pp_studio_placement pl{0.5f, 0.85f, 0.5f, 0, 0, 0};
  auto floorUnderFeet = [&](pp_image* person, float shadow, float horizon, pp_studio_compose_report* rep) {
    pp_image* bg = scene(200, 200, 200);
    pp_studio_look lk = plainLook();
    lk.shadow = shadow;
    lk.horizon = horizon;
    EXPECT_EQ(pp_studio_compose(bg, person, &pl, &lk, nullptr, rep), PP_OK);
    const double under = meanLuma(bg, 185, 256, 215, 262);  // just below the feet (y = 255)
    const double above = meanLuma(bg, 120, 40, 160, 60);    // the sky
    pp_image_destroy(bg);
    return std::pair<double, double>{under, above};
  };
  pp_studio_compose_report rep{};
  const auto none = floorUnderFeet(standing, 0.0f, 0.45f, nullptr);
  const auto with = floorUnderFeet(standing, 1.0f, 0.45f, &rep);
  std::printf("    floor under the feet: no shadow %.0f, shadow %.0f\n", none.first, with.first);
  EXPECT_EQ(rep.grounded, 1);
  EXPECT_TRUE(with.first < none.first - 20);
  EXPECT_TRUE(std::fabs(with.second - none.second) < 0.5);
  // A portrait cut off at the bottom casts nothing.
  pp_studio_compose_report rp{};
  const auto cutOff = floorUnderFeet(portrait, 1.0f, 0.45f, &rp);
  EXPECT_EQ(rp.grounded, 0);
  (void)cutOff;
  // Feet above the horizon (a sky): no shadow.
  const auto sky = floorUnderFeet(standing, 1.0f, 0.95f, nullptr);
  EXPECT_TRUE(std::fabs(sky.first - none.first) < 1.0);
  pp_image_destroy(standing);
  pp_image_destroy(portrait);
}

TEST(studio_compose_background_blur_softens_the_scene_not_the_person) {
  pp_image* person = flatPerson();
  auto detail = [&](float blur) {
    pp_image* bg = scene(40, 40, 40, true);
    pp_studio_placement pl{0.5f, 0.9f, 0.6f, 0, 0, 0};
    pp_studio_look lk = plainLook();
    lk.bg_blur = blur;
    EXPECT_EQ(pp_studio_compose(bg, person, &pl, &lk, nullptr, nullptr), PP_OK);
    double d = 0;
    for (int x = 20; x < 120; ++x) d += std::abs(at(bg, x + 1, 40)[0] - at(bg, x, 40)[0]);
    const uint8_t* p = at(bg, 200, 180);
    const bool personSharp = p[0] == 200 && p[1] == 40;
    pp_image_destroy(bg);
    return std::pair<double, bool>{d, personSharp};
  };
  const auto sharp = detail(0.0f), soft = detail(1.0f);
  std::printf("    background detail: sharp %.0f, blurred %.0f\n", sharp.first, soft.first);
  EXPECT_TRUE(soft.first < 0.2 * sharp.first);
  EXPECT_TRUE(soft.second && sharp.second);
  pp_image_destroy(person);
}

TEST(studio_compose_spotlight_lights_the_person_and_dims_the_scene) {
  pp_image* person = flatPerson();
  auto run = [&](float spot, double& corner, double& beside, double& body) {
    pp_image* bg = scene(120, 120, 120);
    pp_studio_placement pl{0.5f, 0.9f, 0.6f, 0, 0, 0};
    pp_studio_look lk = plainLook();
    lk.spotlight = spot;
    EXPECT_EQ(pp_studio_compose(bg, person, &pl, &lk, nullptr, nullptr), PP_OK);
    corner = meanLuma(bg, 0, 0, 30, 30), beside = meanLuma(bg, 150, 140, 165, 160), body = meanLuma(bg, 190, 150, 210, 210);
    pp_image_destroy(bg);
  };
  double c0, b0, p0, c1, b1, p1;
  run(0.0f, c0, b0, p0);
  run(1.0f, c1, b1, p1);
  std::printf("    corner %.0f → %.0f, beside the person %.0f → %.0f, person %.0f → %.0f\n", c0, c1, b0, b1, p0, p1);
  EXPECT_TRUE(c1 < 0.75 * c0);          // the scene falls off away from the person
  EXPECT_TRUE(b1 > c1 + 15.0);          // a pool of light around them
  EXPECT_TRUE(p1 > p0 + 3.0);           // the person lifted
  pp_image_destroy(person);
}

TEST(studio_compose_fixed_box_keeps_the_person_steady) {
  // Frame 2: the same person with an arm raised above the head.
  pp_image* a = flatPerson();
  pp_image* b = flatPerson();
  for (int y = 0; y < 10; ++y)
    for (int x = 44; x < 50; ++x) at(b, x, y)[3] = 255;
  auto compose = [&](pp_image* cutImg, bool fixed) {
    pp_image* bg = scene(90, 120, 160);
    pp_studio_placement pl{0.5f, 0.9f, 0.6f, 0, 0, 0, fixed ? 1 : 0, 10.0f / 60, 10.0f / 140, 50.0f / 60, 130.0f / 140};
    pp_studio_look lk = plainLook();
    EXPECT_EQ(pp_studio_compose(bg, cutImg, &pl, &lk, nullptr, nullptr), PP_OK);
    return bg;
  };
  auto bodyDiff = [&](pp_image* x, pp_image* y) {
    double d = 0;
    for (int yy = 120; yy < 300; ++yy)
      for (int xx = 0; xx < 400; ++xx) d += std::abs(at(x, xx, yy)[0] - at(y, xx, yy)[0]);
    return d;
  };
  pp_image* fa = compose(a, true);
  pp_image* fb = compose(b, true);
  pp_image* ma = compose(a, false);
  pp_image* mb = compose(b, false);
  std::printf("    body change when an arm goes up: own box %.0f, fixed box %.0f\n", bodyDiff(ma, mb), bodyDiff(fa, fb));
  EXPECT_TRUE(bodyDiff(fa, fb) < 1.0);
  EXPECT_TRUE(bodyDiff(ma, mb) > 1000.0);
  // The raised arm, above the fixed box, is drawn too: the box sets the size
  // and place only, it does not cut off what rises above it (a head moving up).
  // Box top at y = 0.3 of 300 px, 1.5 frame px per cut-out px: the arm (cut-out
  // x 44..49, y 0..9) lands at x 221..230, y 75..90.
  const uint8_t* arm = at(fb, 225, 82);
  const uint8_t* none = at(fa, 225, 82);
  std::printf("    above the box: with the arm %d %d %d, without %d %d %d\n", arm[0], arm[1], arm[2], none[0], none[1], none[2]);
  EXPECT_TRUE(arm[0] > 150 && arm[1] < 90);
  EXPECT_TRUE(none[0] < 120);
  // Someone who left the frame: a fixed box composes just the scene.
  pp_image* empty = rgba(60, 140);
  std::memset(pp_image_pixels(empty), 0, static_cast<size_t>(strideOf(empty)) * 140);
  pp_image* fe = compose(empty, true);
  pp_image* plain = scene(90, 120, 160);
  EXPECT_TRUE(std::fabs(meanLuma(fe, 0, 0, 400, 300) - meanLuma(plain, 0, 0, 400, 300)) < 0.5);
  for (pp_image* i : {a, b, fa, fb, ma, mb, empty, fe, plain}) pp_image_destroy(i);
}

TEST(studio_compose_fixed_colour_match_repeats_the_measured_one) {
  pp_image* person = flatPerson();
  pp_image* bg = scene(200, 120, 60);
  pp_studio_placement pl{0.5f, 0.9f, 0.6f, 0, 0, 0};
  pp_studio_look lk = plainLook();
  lk.color_match = 1.0f;
  pp_studio_compose_report rep{};
  EXPECT_EQ(pp_studio_compose(bg, person, &pl, &lk, nullptr, &rep), PP_OK);
  pp_image* bg2 = scene(200, 120, 60);
  pp_studio_look fixed = plainLook();
  fixed.fixed_harmony = 1, fixed.gain = rep.exposure, fixed.contrast = rep.contrast;
  fixed.balance[0] = rep.balance[0], fixed.balance[1] = rep.balance[1], fixed.balance[2] = rep.balance[2];
  EXPECT_EQ(pp_studio_compose(bg2, person, &pl, &fixed, nullptr, nullptr), PP_OK);
  const uint8_t *p = at(bg, 200, 180), *q = at(bg2, 200, 180);
  std::printf("    measured %d %d %d, repeated %d %d %d\n", p[0], p[1], p[2], q[0], q[1], q[2]);
  EXPECT_TRUE(std::abs(p[0] - q[0]) <= 2 && std::abs(p[1] - q[1]) <= 2 && std::abs(p[2] - q[2]) <= 2);
  pp_image_destroy(bg);
  pp_image_destroy(bg2);
  pp_image_destroy(person);
}

namespace {
// A dark-haired person (an ellipse) before a background `bg`; a band `rimPx`
// wide inside their right edge is lit `rim` (a sunset through the hair).
// Returns the mean cut-out colour in that band and the photo's there.
struct RimResult {
  double cut[3], photo[3];
};
// `leftPart`: the colour and parts-map zone of the person's left half
// (nullptr: all `zone`).
RimResult rimScene(const uint8_t bg[3], const uint8_t person[3], const uint8_t rim[3], int rimPx, uint8_t zone = 1,
                   const uint8_t* leftColour = nullptr, uint8_t leftZone = 0) {
  Scene s;
  s.image = rgba(kW, kH);
  s.truth.assign(static_cast<size_t>(kW) * kH, 0.0f);
  s.strand.assign(static_cast<size_t>(kW) * kH, 0.0f);
  auto inRim = [&](int x, int y) {
    const float d = std::hypot((x - s.cx) / s.rx, (y - s.cy) / s.ry);
    return x > s.cx + 20 && d < 1.0f && (1.0f - d) * std::min(s.rx, s.ry) < static_cast<float>(rimPx);
  };
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const float a = ellipseAlpha(s, static_cast<float>(x), static_cast<float>(y));
      s.truth[static_cast<size_t>(y) * kW + x] = a;
      const uint8_t* f = inRim(x, y) ? rim : (leftColour != nullptr && x < s.cx) ? leftColour : person;
      uint8_t* p = at(s.image, x, y);
      for (int c = 0; c < 3; ++c) p[c] = static_cast<uint8_t>(std::lround(a * f[c] + (1 - a) * bg[c]));
      p[3] = 255;
    }
  pp_image* mask = coarseMask(s);
  pp_image* zones = gray(kW / 2, kH / 2);
  for (int y = 0; y < kH / 2; ++y)
    for (int x = 0; x < kW / 2; ++x) {
      const float fx = static_cast<float>(x * 2 + 1), fy = static_cast<float>(y * 2 + 1);
      const bool in = std::hypot((fx - s.cx) / s.rx, (fy - s.cy) / s.ry) < 1.0f;
      *at(zones, x, y, 1) = !in ? 0 : (leftColour != nullptr && fx < s.cx) ? leftZone : zone;
    }
  pp_image* out = cut(s, mask, zones, defaults());
  pp_image_destroy(zones);
  RimResult r{};
  int n = 0;
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      if (!inRim(x, y) || s.truth[static_cast<size_t>(y) * kW + x] < 0.99f) continue;
      for (int c = 0; c < 3; ++c) r.cut[c] += at(out, x, y)[c], r.photo[c] += at(s.image, x, y)[c];
      ++n;
    }
  for (int c = 0; c < 3; ++c) r.cut[c] /= std::max(n, 1), r.photo[c] /= std::max(n, 1);
  pp_image_destroy(out);
  pp_image_destroy(mask);
  pp_image_destroy(s.image);
  return r;
}
double lumOf3(const double c[3]) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }
}  // namespace

TEST(studio_cutout_takes_the_old_backgrounds_light_off_the_edges_only) {
  // Dark hair lit gold by a sunset behind: the gold does not come along.
  const uint8_t sunset[3] = {245, 200, 90}, hair[3] = {50, 32, 14}, gold[3] = {205, 160, 45};
  const RimResult lit = rimScene(sunset, hair, gold, 10);
  std::printf("    gold rim: photo %.0f %.0f %.0f -> cut-out %.0f %.0f %.0f\n", lit.photo[0], lit.photo[1], lit.photo[2], lit.cut[0],
              lit.cut[1], lit.cut[2]);
  EXPECT_TRUE(lumOf3(lit.cut) < 0.6 * lumOf3(lit.photo));
  // Warm skin beside a warm wall is the skin's own colour: it stays.
  const uint8_t wall[3] = {225, 175, 85}, skin[3] = {150, 96, 62};
  const RimResult warm = rimScene(wall, skin, skin, 10, 2);
  std::printf("    skin by a warm wall: photo %.0f %.0f %.0f -> cut-out %.0f %.0f %.0f\n", warm.photo[0], warm.photo[1], warm.photo[2],
              warm.cut[0], warm.cut[1], warm.cut[2]);
  for (int c = 0; c < 3; ++c) EXPECT_TRUE(std::fabs(warm.cut[c] - warm.photo[c]) < 6);
  // A white background has no colour to take away: a bright edge stays.
  const uint8_t white[3] = {235, 235, 232}, bright[3] = {190, 185, 180};
  const RimResult grey = rimScene(white, hair, bright, 10, 4);
  std::printf("    bright edge by a white wall: photo %.0f -> cut-out %.0f\n", lumOf3(grey.photo), lumOf3(grey.cut));
  EXPECT_TRUE(std::fabs(lumOf3(grey.cut) - lumOf3(grey.photo)) < 6);
  // Sunlit skin on an arm beside a green dress is compared with skin, not
  // with the dress: it stays skin-coloured (it does not turn green).
  const uint8_t dress[3] = {25, 85, 65}, litSkin[3] = {235, 170, 105};
  const RimResult arm = rimScene(sunset, skin, litSkin, 10, 2, dress, 4);
  std::printf("    sunlit arm by a green dress: photo %.0f %.0f %.0f -> cut-out %.0f %.0f %.0f\n", arm.photo[0], arm.photo[1], arm.photo[2],
              arm.cut[0], arm.cut[1], arm.cut[2]);
  EXPECT_TRUE(arm.cut[0] > arm.cut[1] + 30 && arm.cut[1] > arm.cut[2]);
}

TEST(studio_report_sizes_a_couple_by_one_face_and_reads_the_outfit) {
  Scene s = makeScene();
  // The person's clothes: navy above y = 215, white below (inside the person).
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      if (s.truth[static_cast<size_t>(y) * kW + x] < 0.999f) continue;
      uint8_t* p = at(s.image, x, y);
      if (y < 215) p[0] = 30, p[1] = 40, p[2] = 90;
      else p[0] = 235, p[1] = 235, p[2] = 232;
    }
  pp_image* mask = coarseMask(s);
  // Two faces at different heights, as in a couple's embrace.
  const int w = 180, h = 150;
  pp_image* zones = gray(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float fx = (static_cast<float>(x) + 0.5f) * kW / w, fy = (static_cast<float>(y) + 0.5f) * kH / h;
      const float d = std::hypot((fx - s.cx) / s.rx, (fy - s.cy) / s.ry);
      uint8_t v = d < 0.97f ? 4 : 0;
      if (fx >= 130 && fx < 170 && fy >= 95 && fy < 140) v = 3;   // the taller one's face, 45 px
      if (fx >= 192 && fx < 226 && fy >= 125 && fy < 163) v = 3;  // the other, 38 px, lower
      *at(zones, x, y, 1) = v;
    }
  pp_studio_cutout_report rep{};
  pp_image* o = cut(s, mask, zones, defaults(), {}, &rep);
  std::printf("    faces %d, face %.3f..%.3f; outfit %d: (%.0f %.0f %.0f) %.2f, (%.0f %.0f %.0f) %.2f\n", rep.faces, rep.face_top,
              rep.face_bottom, rep.outfit_count, rep.outfit[0], rep.outfit[1], rep.outfit[2], rep.outfit_share[0], rep.outfit[3],
              rep.outfit[4], rep.outfit[5], rep.outfit_share[1]);
  EXPECT_EQ(rep.face, 1);
  EXPECT_EQ(rep.faces, 2);
  // One face's height (45 px), not both faces' span (68 px).
  EXPECT_TRUE(std::fabs(rep.face_top - 95.0f / kH) < 0.015f && std::fabs(rep.face_bottom - 140.0f / kH) < 0.015f);
  EXPECT_TRUE(std::fabs(rep.face_left - 130.0f / kW) < 0.015f);
  // Navy first (more of it), white second.
  EXPECT_EQ(rep.outfit_count, 2);
  EXPECT_TRUE(std::fabs(rep.outfit[0] - 30) < 12 && std::fabs(rep.outfit[1] - 40) < 12 && std::fabs(rep.outfit[2] - 90) < 12);
  EXPECT_TRUE(std::fabs(rep.outfit[3] - 235) < 12 && std::fabs(rep.outfit[5] - 232) < 12);
  EXPECT_TRUE(rep.outfit_share[0] > rep.outfit_share[1] && rep.outfit_share[0] + rep.outfit_share[1] > 0.9f);
  pp_image_destroy(o);
  pp_image_destroy(zones);
  pp_image_destroy(mask);
  pp_image_destroy(s.image);
}

TEST(studio_compose_bounce_tints_the_person_with_the_backdrop_not_their_brightness) {
  pp_image* person = flatPerson(false, 150, 150, 150);
  auto run = [&](uint8_t r, uint8_t g, uint8_t b, float bounce) {
    pp_image* bg = scene(r, g, b);
    pp_studio_placement pl{0.5f, 0.9f, 0.6f, 0, 0, 0};
    pp_studio_look lk = plainLook();
    lk.bounce = bounce;
    EXPECT_EQ(pp_studio_compose(bg, person, &pl, &lk, nullptr, nullptr), PP_OK);
    const uint8_t* p = at(bg, 200, 180);
    const std::array<int, 3> c{p[0], p[1], p[2]};
    pp_image_destroy(bg);
    return c;
  };
  const auto off = run(200, 155, 45, 0.0f), on = run(200, 155, 45, 1.0f), grey = run(128, 128, 128, 1.0f);
  const auto luma = [](const std::array<int, 3>& c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; };
  std::printf("    on mustard: off (%d %d %d), bounce (%d %d %d); on grey (%d %d %d)\n", off[0], off[1], off[2], on[0], on[1], on[2],
              grey[0], grey[1], grey[2]);
  EXPECT_TRUE(off[0] == 150 && off[2] == 150);
  EXPECT_TRUE(on[0] - on[2] > 14);                            // warmer
  EXPECT_TRUE(std::fabs(luma(on) - luma(off)) < 0.04 * luma(off));  // as bright
  EXPECT_TRUE(std::abs(grey[0] - 150) <= 1 && std::abs(grey[2] - 150) <= 1);  // a grey scene tints nothing
  pp_image_destroy(person);
}

TEST(studio_backdrop_is_lit_like_paper_keeps_its_hue_and_never_bands) {
  const int W = 300, H = 400;
  auto paint = [&](uint8_t r, uint8_t g, uint8_t b, float light, uint32_t seed) {
    pp_image* img = rgba(W, H);
    EXPECT_EQ(pp_studio_backdrop(img, r, g, b, light, 0.5f, 0.42f, seed, nullptr), PP_OK);
    return img;
  };
  // Flat: exactly the colour.
  pp_image* flat = paint(200, 155, 45, 0.0f, 7);
  bool exact = true;
  for (int y = 0; y < H; y += 7)
    for (int x = 0; x < W; x += 5) {
      const uint8_t* p = at(flat, x, y);
      exact = exact && p[0] == 200 && p[1] == 155 && p[2] == 45 && p[3] == 255;
    }
  EXPECT_TRUE(exact);
  // Lit: brighter where the light falls, darker in the corners, same hue.
  pp_image* lit = paint(200, 155, 45, 1.0f, 7);
  const double centre = meanLuma(lit, 140, 158, 160, 178), corner = meanLuma(lit, 0, 0, 20, 20), base = meanLuma(flat, 0, 0, 20, 20);
  std::printf("    centre %.0f, flat %.0f, corner %.0f\n", centre, base, corner);
  EXPECT_TRUE(centre > base + 12 && corner < base - 20);
  for (const auto& xy : {std::pair<int, int>{150, 168}, {5, 5}, {150, 395}}) {
    const uint8_t* p = at(lit, xy.first, xy.second);
    // Linear-light ratios of the channels stay those of the colour.
    const double rg = std::pow(p[0] / 255.0, 2.2) / std::pow(p[1] / 255.0, 2.2), want = std::pow(200 / 155.0, 2.2);
    EXPECT_TRUE(p[0] > p[1] && p[1] > p[2] && std::fabs(rg / want - 1) < 0.12);
  }
  // No banding on a dark colour: the profile across the frame is smooth.
  pp_image* navy = paint(25, 35, 70, 1.0f, 3);
  double prev = -1, worst = 0;
  for (int x = 0; x < W; ++x) {
    double sum = 0;
    for (int y = 120; y < 220; ++y) sum += at(navy, x, y)[2];
    const double m = sum / 100.0;
    if (prev >= 0) worst = std::max(worst, std::fabs(m - prev));
    prev = m;
  }
  std::printf("    largest step across a dark backdrop: %.2f levels\n", worst);
  EXPECT_TRUE(worst < 0.6);
  // The same seed gives the same backdrop; another gives another texture.
  pp_image* again = paint(25, 35, 70, 1.0f, 3);
  pp_image* other = paint(25, 35, 70, 1.0f, 4);
  EXPECT_TRUE(std::memcmp(pp_image_pixels(navy), pp_image_pixels(again), static_cast<size_t>(strideOf(navy)) * H) == 0);
  EXPECT_TRUE(std::memcmp(pp_image_pixels(navy), pp_image_pixels(other), static_cast<size_t>(strideOf(navy)) * H) != 0);
  EXPECT_EQ(pp_studio_backdrop(nullptr, 1, 2, 3, 1, 0.5f, 0.5f, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
  for (pp_image* i : {flat, lit, navy, again, other}) pp_image_destroy(i);
}

namespace {
// A head for the hair tests, 240 x 300: dark hair (a big round shape) over
// a face, a body below; and its parts map. Hair is textured (stripes).
struct HairHead {
  pp_image* cut = nullptr;
  pp_image* zones = nullptr;
  float face[4] = {100.0f / 240, 110.0f / 300, 140.0f / 240, 170.0f / 300};
};
HairHead hairHead() {
  HairHead h;
  const int W = 240, H = 300;
  h.cut = rgba(W, H);
  h.zones = gray(W, H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      uint8_t* p = at(h.cut, x, y);
      uint8_t& z = *at(h.zones, x, y, 1);
      const bool faceIn = std::hypot((x - 120.0) / 22, (y - 140.0) / 32) < 1.0;
      const bool hairIn = std::hypot((x - 120.0) / 70, (y - 110.0) / 70) < 1.0 && !faceIn;
      const bool body = y > 190 && std::abs(x - 120) < 80;
      if (faceIn) p[0] = 120, p[1] = 80, p[2] = 60, p[3] = 255, z = 3;
      else if (hairIn) {
        const uint8_t v = (x / 3) % 2 ? 40 : 20;  // strands
        p[0] = v, p[1] = static_cast<uint8_t>(v * 0.7), p[2] = static_cast<uint8_t>(v * 0.5), p[3] = 255, z = 1;
      } else if (body) p[0] = 30, p[1] = 90, p[2] = 70, p[3] = 255, z = 4;
      else p[0] = p[1] = p[2] = 0, p[3] = 0, z = 0;
    }
  return h;
}
}  // namespace

TEST(studio_hair_colour_keeps_the_strands_and_the_face) {
  HairHead h = hairHead();
  // Copper: the hair takes the colour, still striped; the face is untouched.
  ASSERT_OK(pp_studio_hair(h.cut, h.zones, nullptr, 1, 184, 92, 40, 1.0f, h.face, 5, nullptr));
  const uint8_t* a = at(h.cut, 99, 70);  // a light strand
  const uint8_t* b = at(h.cut, 102, 70); // a dark one
  const uint8_t* f = at(h.cut, 120, 140);
  std::printf("    strands %d %d %d / %d %d %d, face %d %d %d\n", a[0], a[1], a[2], b[0], b[1], b[2], f[0], f[1], f[2]);
  EXPECT_TRUE(a[0] > a[1] + 30 && a[1] > a[2]);  // copper
  EXPECT_TRUE(std::abs(static_cast<int>(a[0]) - b[0]) > 15);  // the strands still show
  EXPECT_TRUE(f[0] == 120 && f[1] == 80 && f[2] == 60);
  pp_image_destroy(h.cut);
  pp_image_destroy(h.zones);
}

TEST(studio_hair_styles_replace_the_hair_and_keep_the_face) {
  int women = 0, men = 0;
  for (int i = 0; i < pp_studio_hair_style_count(); ++i) (pp_studio_hair_style_women(i) ? women : men)++;
  std::printf("    %d women's styles, %d men's\n", women, men);
  EXPECT_TRUE(women >= 20 && men >= 20);
  EXPECT_TRUE(pp_studio_hair_style_id(-1) == nullptr && pp_studio_hair_style_id(pp_studio_hair_style_count()) == nullptr);
  // Every style draws, keeps the face and the body, and takes out the old
  // hair far from the head (the big round shape's sides).
  for (int i = 0; i < pp_studio_hair_style_count(); ++i) {
    HairHead h = hairHead();
    const char* id = pp_studio_hair_style_id(i);
    ASSERT_OK(pp_studio_hair(h.cut, h.zones, id, 0, 0, 0, 0, 1.0f, h.face, 5, nullptr));
    const uint8_t* f = at(h.cut, 120, 150);
    const uint8_t* body = at(h.cut, 120, 260);
    if (!(f[3] == 255 && std::abs(f[0] - 120) < 3) || body[3] != 255) std::printf("    %s: face or body changed\n", id);
    EXPECT_TRUE(f[3] == 255 && std::abs(f[0] - 120) < 3);
    EXPECT_TRUE(body[3] == 255);
    double drawn = 0;
    for (int y = 40; y < 110; ++y)
      for (int x = 90; x < 150; ++x) drawn += at(h.cut, x, y)[3];
    EXPECT_TRUE(drawn > 0);  // something on the head
    pp_image_destroy(h.cut);
    pp_image_destroy(h.zones);
  }
  // A short cut: the old hair's far sides are gone.
  HairHead h = hairHead();
  ASSERT_OK(pp_studio_hair(h.cut, h.zones, "buzz-cut", 0, 0, 0, 0, 1.0f, h.face, 5, nullptr));
  EXPECT_EQ(at(h.cut, 55, 110)[3], 0);
  EXPECT_EQ(at(h.cut, 185, 110)[3], 0);
  // Bad input.
  EXPECT_EQ(pp_studio_hair(h.cut, h.zones, "no-such-style", 0, 0, 0, 0, 1.0f, h.face, 5, nullptr), PP_ERROR_INVALID_ARGUMENT);
  const float noFace[4] = {0, 0, 0, 0};
  EXPECT_EQ(pp_studio_hair(h.cut, h.zones, "bob", 0, 0, 0, 0, 1.0f, noFace, 5, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_studio_hair(h.cut, nullptr, "bob", 0, 0, 0, 0, 1.0f, h.face, 5, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image_destroy(h.cut);
  pp_image_destroy(h.zones);
}

TEST(studio_rejects_bad_input) {
  Scene s = makeScene();
  pp_image* mask = coarseMask(s);
  pp_studio_cutout_params p = defaults();
  pp_image* small = rgba(20, 20);
  EXPECT_EQ(pp_studio_cutout(s.image, mask, nullptr, nullptr, 0, &p, small, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image* out = rgba(kW, kH);
  EXPECT_EQ(pp_studio_cutout(nullptr, mask, nullptr, nullptr, 0, &p, out, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_studio_cutout(s.image, mask, nullptr, nullptr, 2, &p, out, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  const float pt[] = {0.5f, 0.5f};
  pp_studio_stroke bad{7, 0.02f, 1, pt};
  EXPECT_EQ(pp_studio_cutout(s.image, mask, nullptr, &bad, 1, &p, out, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  p.shift = std::nanf("");
  EXPECT_EQ(pp_studio_cutout(s.image, mask, nullptr, nullptr, 0, &p, out, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  // Compose: an empty cut-out, a bad size.
  pp_image* empty = rgba(30, 30);
  std::memset(pp_image_pixels(empty), 0, static_cast<size_t>(strideOf(empty)) * 30);
  pp_image* bg = scene(10, 10, 10);
  pp_studio_placement pl{0.5f, 0.9f, 0.6f, 0, 0, 0};
  pp_studio_look lk = plainLook();
  EXPECT_EQ(pp_studio_compose(bg, empty, &pl, &lk, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image* person = flatPerson();
  pl.height = std::nanf("");
  EXPECT_EQ(pp_studio_compose(bg, person, &pl, &lk, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pl.height = 0.0f;
  EXPECT_EQ(pp_studio_compose(bg, person, &pl, &lk, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_studio_compose(bg, nullptr, &pl, &lk, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image_destroy(person);
  pp_image_destroy(bg);
  pp_image_destroy(empty);
  pp_image_destroy(out);
  pp_image_destroy(small);
  pp_image_destroy(mask);
  pp_image_destroy(s.image);
}
