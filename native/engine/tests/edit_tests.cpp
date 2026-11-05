// Photo editor tests (Phase 3): adjustments, looks, focus, props, frames and
// the watermark overlay, written around what people see:
//  - straightening never leaves empty corners; looks at strength 0 change
//    nothing; black stays black when warming;
//  - focus keeps the chosen area sharp and softens the rest;
//  - a prop lands where asked, takes on the scene's light and colour, casts
//    a contact shadow, mirrors and turns correctly;
//  - frames show the photo inside the shape with clean edges, the designed
//    backdrop outside, borders, film holes;
//  - the watermark overlay counts exactly what is visible (the export check).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

pp_image* solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
  auto* px = static_cast<uint8_t*>(pp_image_pixels(img));
  pp_image_info info{};
  pp_image_get_info(img, &info);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      uint8_t* p = px + static_cast<size_t>(y) * info.stride + static_cast<size_t>(x) * 4;
      p[0] = r;
      p[1] = g;
      p[2] = b;
      p[3] = a;
    }
  return img;
}

// Fine texture over mid grey (so blur and sharpness are measurable).
pp_image* textured(int w, int h, float key = 0.5f) {
  pp_image* img = solid(w, h, 0, 0, 0);
  auto* px = static_cast<uint8_t*>(pp_image_pixels(img));
  pp_image_info info{};
  pp_image_get_info(img, &info);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      uint8_t* p = px + static_cast<size_t>(y) * info.stride + static_cast<size_t>(x) * 4;
      const float v = key + 0.25f * (((x / 3 + y / 3) % 2) ? 1.0f : -1.0f) * 0.6f + 0.05f * std::sin(x * 0.7f) * std::cos(y * 0.5f);
      const auto b = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255));
      p[0] = p[1] = p[2] = b;
    }
  return img;
}

struct Px {
  int r, g, b, a;
};
Px at(pp_image* img, int x, int y) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  const auto* p = static_cast<const uint8_t*>(pp_image_pixels(img)) + static_cast<size_t>(y) * info.stride + static_cast<size_t>(x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

pp_op op(int type, std::initializer_list<float> v) {
  pp_op o{};
  o.type = type;
  int i = 0;
  for (float f : v) o.p[i++] = f;
  return o;
}

pp_image* run(pp_image* in, std::initializer_list<pp_op> ops, pp_status* status = nullptr) {
  std::vector<pp_op> list(ops);
  pp_image* out = nullptr;
  const pp_status st = pp_process(in, list.data(), list.size(), nullptr, &out);
  if (status != nullptr) *status = st;
  else EXPECT_EQ(st, PP_OK);
  return out;
}

// Local texture energy (mean |Laplacian|) in a square.
double texture(pp_image* img, int cx, int cy, int r) {
  double s = 0;
  int n = 0;
  for (int y = cy - r; y < cy + r; ++y)
    for (int x = cx - r; x < cx + r; ++x) {
      s += std::fabs(4.0 * at(img, x, y).g - at(img, x - 1, y).g - at(img, x + 1, y).g - at(img, x, y - 1).g - at(img, x, y + 1).g);
      ++n;
    }
  return s / n;
}

uint64_t liveBytes() {
  pp_memory_stats s{};
  pp_memory_get_stats(&s);
  return s.current_bytes;
}

}  // namespace

TEST(edit_straighten_leaves_no_empty_corners) {
  pp_image* in = solid(800, 600, 40, 180, 90);
  for (float deg : {3.0f, -12.0f, 45.0f}) {
    pp_image* out = run(in, {op(PP_OP_STRAIGHTEN, {deg})});
    pp_image_info info{};
    pp_image_get_info(out, &info);
    int worst = 0;
    for (int y = 0; y < info.height; ++y)
      for (int x = 0; x < info.width; ++x) {
        const Px p = at(out, x, y);
        worst = std::max({worst, std::abs(p.r - 40), std::abs(p.g - 180), std::abs(p.b - 90)});
      }
    const double aspect = static_cast<double>(info.width) / info.height;
    std::printf("    %+.0f deg: %dx%d, aspect %.3f, worst deviation %d\n", deg, info.width, info.height, aspect, worst);
    EXPECT_TRUE(worst <= 1);                          // only photo, no empty corners
    EXPECT_TRUE(std::fabs(aspect - 800.0 / 600.0) < 0.01);  // same shape
    EXPECT_TRUE(info.width < 800);
    pp_image_destroy(out);
  }
  pp_image_destroy(in);
}

TEST(edit_looks_and_warmth_change_the_mood_and_stay_faithful_at_zero) {
  pp_image* in = textured(240, 160);
  // Strength 0 changes nothing, for every look.
  for (int id = 1; id <= 12; ++id) {
    pp_image* out = run(in, {op(PP_OP_FILTER, {static_cast<float>(id), 0.0f})});
    EXPECT_TRUE(std::memcmp(pp_image_pixels(out), pp_image_pixels(in), 240u * 160u * 4u) == 0);
    pp_image_destroy(out);
  }
  pp_image* colour = solid(64, 64, 170, 120, 90);
  pp_image* mono = run(colour, {op(PP_OP_FILTER, {PP_FILTER_MONO, 1.0f})});
  const Px m = at(mono, 30, 30);
  EXPECT_TRUE(m.r == m.g && m.g == m.b);  // black and white
  pp_image* warm = run(colour, {op(PP_OP_FILTER, {PP_FILTER_WARM, 1.0f})});
  pp_image* cool = run(colour, {op(PP_OP_FILTER, {PP_FILTER_COOL, 1.0f})});
  const Px w = at(warm, 30, 30), c = at(cool, 30, 30);
  std::printf("    warm %d/%d/%d, cool %d/%d/%d, mono %d\n", w.r, w.g, w.b, c.r, c.g, c.b, m.r);
  EXPECT_TRUE(static_cast<double>(w.r) / w.b > static_cast<double>(c.r) / c.b + 0.3);
  // Warmth: warmer reddens, and black stays black.
  pp_image* black = solid(16, 16, 0, 0, 0);
  pp_image* warmed = run(black, {op(PP_OP_WARMTH, {1.0f, 0.5f})});
  EXPECT_EQ(at(warmed, 5, 5).r, 0);
  pp_image* warmedColour = run(colour, {op(PP_OP_WARMTH, {0.8f, 0.0f})});
  EXPECT_TRUE(at(warmedColour, 5, 5).r > 170 && at(warmedColour, 5, 5).b < 90);
  // Bad input is refused.
  pp_status st{};
  pp_image* bad = run(colour, {op(PP_OP_FILTER, {99.0f, 1.0f})}, &st);
  EXPECT_EQ(st, PP_ERROR_INVALID_ARGUMENT);
  EXPECT_TRUE(bad == nullptr);
  run(colour, {op(PP_OP_WARMTH, {NAN, 0.0f})}, &st);
  EXPECT_EQ(st, PP_ERROR_INVALID_ARGUMENT);
  for (pp_image* i : {in, colour, mono, warm, cool, black, warmed, warmedColour}) pp_image_destroy(i);
}

TEST(edit_focus_keeps_the_subject_sharp_and_softens_the_rest) {
  pp_image* in = textured(600, 400);
  pp_image* radial = run(in, {op(PP_OP_FOCUS, {1, 0.5f, 0.5f, 0.2f, 0, 0.8f, 0.3f})});
  pp_image* tilt = run(in, {op(PP_OP_FOCUS, {2, 0.5f, 0.5f, 0.1f, 0, 0.8f, 0.3f})});
  const double centre0 = texture(in, 300, 200, 20), edge0 = texture(in, 40, 40, 20);
  const double rc = texture(radial, 300, 200, 20), re = texture(radial, 40, 40, 20);
  const double tc = texture(tilt, 500, 200, 20), te = texture(tilt, 300, 30, 20);
  std::printf("    radial: centre %.0f%% kept, corner %.0f%%; tilt: band %.0f%%, top %.0f%%\n", 100 * rc / centre0,
              100 * re / edge0, 100 * tc / centre0, 100 * te / edge0);
  EXPECT_TRUE(rc > 0.97 * centre0);  // the subject stays sharp
  EXPECT_TRUE(re < 0.3 * edge0);     // the rest melts
  EXPECT_TRUE(tc > 0.97 * centre0);  // tilt: the band stays sharp across the width
  EXPECT_TRUE(te < 0.3 * edge0);
  for (pp_image* i : {in, radial, tilt}) pp_image_destroy(i);
}

namespace {
// A test prop: left half red, right half blue, opaque ellipse, soft edge.
pp_image* testProp(int w, int h) {
  pp_image* img = solid(w, h, 0, 0, 0, 0);
  auto* px = static_cast<uint8_t*>(pp_image_pixels(img));
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      uint8_t* p = px + (static_cast<size_t>(y) * w + x) * 4;
      const float dx = (x + 0.5f - w / 2.0f) / (w / 2.0f), dy = (y + 0.5f - h / 2.0f) / (h / 2.0f);
      const float d = std::sqrt(dx * dx + dy * dy);
      const float a = std::clamp((1.0f - d) * 20.0f, 0.0f, 1.0f);
      p[0] = x < w / 2 ? 200 : 30;
      p[1] = 40;
      p[2] = x < w / 2 ? 30 : 200;
      p[3] = static_cast<uint8_t>(a * 255);
    }
  return img;
}
pp_prop_params propAt(float cx, float cy, float width) {
  pp_prop_params p{};
  p.cx = cx;
  p.cy = cy;
  p.width = width;
  p.opacity = 1.0f;
  p.blend = 1.0f;
  p.shadow = 1.0f;
  return p;
}
}  // namespace

TEST(edit_prop_lands_where_asked_and_belongs_to_the_scene) {
  pp_image* prop = testProp(400, 200);
  // Placement, size and coverage.
  pp_image* bright = solid(800, 600, 200, 200, 200);
  pp_prop_params p = propAt(400, 300, 200);  // 200 x 100 px ellipse
  int64_t covered = 0;
  EXPECT_EQ(pp_place_prop(bright, prop, &p, nullptr, &covered), PP_OK);
  const double area = 3.14159 * 100 * 50;
  std::printf("    covered %lld px (ellipse %.0f px)\n", static_cast<long long>(covered), area);
  EXPECT_TRUE(std::fabs(covered - area) < 0.07 * area);  // the test prop's soft rim is ~2.5% of its radius
  EXPECT_TRUE(at(bright, 340, 300).r > at(bright, 340, 300).b);  // red half on the left
  EXPECT_TRUE(at(bright, 460, 300).b > at(bright, 460, 300).r);
  EXPECT_EQ(at(bright, 50, 50).r, 200);                           // nothing else touched
  // A soft contact shadow just below.
  const Px under = at(bright, 400, 356), side = at(bright, 650, 356);
  std::printf("    below the prop %d vs open %d\n", under.g, side.g);
  EXPECT_TRUE(under.g < side.g - 8);
  // Mirrored: the colours swap sides. Turned 90 deg: red goes on top.
  pp_image* flipped = solid(800, 600, 200, 200, 200);
  p.flip = 1;
  pp_place_prop(flipped, prop, &p, nullptr, nullptr);
  EXPECT_TRUE(at(flipped, 340, 300).b > at(flipped, 340, 300).r);
  pp_image* turned = solid(800, 600, 200, 200, 200);
  p = propAt(400, 300, 200);
  p.rotation_deg = 90;
  pp_place_prop(turned, prop, &p, nullptr, nullptr);
  EXPECT_TRUE(at(turned, 400, 260).r > at(turned, 400, 260).b);
  // The scene's light: darker in a dim photo, warmer under warm light.
  pp_image* dim = solid(800, 600, 50, 50, 50);
  pp_image* warmLight = solid(800, 600, 210, 190, 160);
  p = propAt(400, 300, 200);
  p.shadow = 0;
  pp_place_prop(dim, prop, &p, nullptr, nullptr);
  pp_place_prop(warmLight, prop, &p, nullptr, nullptr);
  pp_image* neutral = solid(800, 600, 190, 190, 190);
  pp_place_prop(neutral, prop, &p, nullptr, nullptr);
  const Px pd = at(dim, 340, 300), pn = at(neutral, 340, 300), pw = at(warmLight, 460, 300), pnb = at(neutral, 460, 300);
  std::printf("    red half: dim scene %d vs neutral %d; blue half under warm light %d/%d vs neutral %d/%d\n", pd.r, pn.r,
              pw.r, pw.b, pnb.r, pnb.b);
  EXPECT_TRUE(pd.r < pn.r - 15);
  EXPECT_TRUE((static_cast<double>(pw.r) / pw.b) / (static_cast<double>(pnb.r) / pnb.b) > 1.05);  // warmer
  // Blend 0 keeps the prop's own colours exactly where opaque.
  pp_image* plain = solid(800, 600, 50, 50, 50);
  p.blend = 0;
  pp_place_prop(plain, prop, &p, nullptr, nullptr);
  EXPECT_TRUE(std::abs(at(plain, 340, 300).r - 200) <= 2);
  // Bad input.
  p.width = NAN;
  EXPECT_EQ(pp_place_prop(plain, prop, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  for (pp_image* i : {prop, bright, flipped, turned, dim, warmLight, neutral, plain}) pp_image_destroy(i);
}

TEST(edit_prop_matches_the_photos_softness) {
  // A crisp prop on a soft photo gets softened to match (no pasted-on look).
  pp_image* prop = testProp(400, 200);
  auto edgeWidth = [&](bool softPhoto) {
    // Large blocks: real, isolated edges, like objects in a photo.
    pp_image* photo = solid(800, 600, 0, 0, 0);
    {
      auto* px = static_cast<uint8_t*>(pp_image_pixels(photo));
      for (int y = 0; y < 600; ++y)
        for (int x = 0; x < 800; ++x) {
          const uint8_t v = ((x / 40 + y / 40) % 2) ? 170 : 90;
          uint8_t* q = px + (static_cast<size_t>(y) * 800 + x) * 4;
          q[0] = q[1] = q[2] = v;
        }
    }
    if (softPhoto) {
      pp_image* blurred = run(photo, {op(PP_OP_FOCUS, {1, 0.5f, 0.5f, 0.001f, 0, 0.12f, 0.0f})});
      pp_image_destroy(photo);
      photo = blurred;
    }
    pp_prop_params p = propAt(400, 300, 300);
    p.shadow = 0;
    pp_place_prop(photo, prop, &p, nullptr, nullptr);
    // Width of the red-to-blue transition across the middle.
    int lastRed = -1, firstBlue = -1;
    for (int x = 360; x < 440; ++x) {
      const Px q = at(photo, x, 300);
      const double t = static_cast<double>(q.b) / (q.r + q.b + 1);
      if (t < 0.3) lastRed = x;
      if (t > 0.7 && firstBlue < 0) firstBlue = x;
    }
    pp_image_destroy(photo);
    return firstBlue - lastRed;
  };
  const int crisp = edgeWidth(false), soft = edgeWidth(true);
  std::printf("    colour edge across the prop: %d px on a sharp photo, %d px on a soft one\n", crisp, soft);
  EXPECT_TRUE(soft > crisp);
  pp_image_destroy(prop);
}

TEST(edit_frames_hold_the_photo_cleanly_in_every_shape) {
  pp_image* photo = solid(600, 750, 230, 60, 60);  // red photo
  for (int shape = 0; shape <= 6; ++shape) {
    pp_image* canvas = solid(1000, 1250, 0, 0, 0);
    pp_frame_params f{};
    f.shape = shape;
    f.shape_param = shape == 0 ? 0.1f : 0.0f;
    f.window[0] = 0.15f;
    f.window[1] = 0.12f;
    f.window[2] = 0.7f;
    f.window[3] = 0.7f;
    f.backdrop = 0;
    f.color_a = 0xFFF2EDE4;  // warm paper white
    f.zoom = 1.0f;
    EXPECT_EQ(pp_frame(photo, canvas, &f, nullptr), PP_OK);
    const Px centre = at(canvas, 500, 150 + 437), corner = at(canvas, 10, 10);
    // Somewhere on the edge there are in-between (anti-aliased) values.
    int soft = 0;
    for (int x = 0; x < 1000; ++x) {
      const Px q = at(canvas, x, 150 + 437);
      if (q.g > 70 && q.g < 225) ++soft;
    }
    std::printf("    shape %d: centre %d/%d/%d, corner %d/%d/%d, soft edge px %d\n", shape, centre.r, centre.g, centre.b,
                corner.r, corner.g, corner.b, soft);
    EXPECT_TRUE(std::abs(centre.r - 230) <= 2 && std::abs(centre.g - 60) <= 2);  // the photo, untouched
    EXPECT_TRUE(corner.r == 0xF2 && corner.g == 0xED);                          // the designed backdrop
    EXPECT_TRUE(soft >= 1 && soft <= 12);                                       // clean, smooth edges
    EXPECT_EQ(corner.a, 255);
    pp_image_destroy(canvas);
  }
  // Border, film holes and a blurred-photo backdrop.
  pp_image* canvas = solid(1000, 800, 0, 0, 0);
  pp_frame_params f{};
  f.shape = 0;
  f.window[0] = 0.05f;
  f.window[1] = 0.15f;
  f.window[2] = 0.9f;
  f.window[3] = 0.7f;
  f.backdrop = 0;
  f.color_a = 0xFF111111;
  f.border = 0.02f;
  f.border_color = 0xFFFFFFFF;
  f.decoration = 1;
  f.zoom = 1.0f;
  EXPECT_EQ(pp_frame(photo, canvas, &f, nullptr), PP_OK);
  EXPECT_TRUE(at(canvas, 500, 124).g > 230);  // white border just inside the window edge
  int holes = 0;
  for (int x = 0; x < 1000; ++x) holes += at(canvas, x, 60).g > 150 ? 1 : 0;
  std::printf("    film holes across the top: %d px\n", holes);
  EXPECT_TRUE(holes > 100 && holes < 700);
  f.backdrop = 2;
  f.decoration = 0;
  EXPECT_EQ(pp_frame(photo, canvas, &f, nullptr), PP_OK);
  EXPECT_TRUE(at(canvas, 10, 10).r > at(canvas, 10, 10).g + 40);  // the photo's own colour, blurred
  f.shape = 9;
  EXPECT_EQ(pp_frame(photo, canvas, &f, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image_destroy(canvas);
  pp_image_destroy(photo);
}

TEST(edit_overlay_counts_exactly_what_is_visible) {
  pp_image* image = solid(400, 300, 10, 10, 10);
  pp_image* stamp = solid(100, 40, 255, 255, 255, 255);
  int64_t visible = -1;
  EXPECT_EQ(pp_overlay(image, stamp, 290, 250, 1.0f, &visible), PP_OK);
  EXPECT_EQ(visible, 100 * 40);
  EXPECT_EQ(at(image, 300, 260).r, 255);
  // Half off the edge: only the part inside counts.
  pp_image* image2 = solid(400, 300, 10, 10, 10);
  EXPECT_EQ(pp_overlay(image2, stamp, 350, 280, 1.0f, &visible), PP_OK);
  EXPECT_EQ(visible, 50 * 20);
  // Too faint to count; nothing at all when fully transparent.
  EXPECT_EQ(pp_overlay(image2, stamp, 0, 0, 0.2f, &visible), PP_OK);
  EXPECT_EQ(visible, 0);
  pp_image* clear = solid(100, 40, 255, 255, 255, 0);
  EXPECT_EQ(pp_overlay(image2, clear, 0, 0, 1.0f, &visible), PP_OK);
  EXPECT_EQ(visible, 0);
  // Opacity blends.
  pp_image* image3 = solid(10, 10, 0, 0, 0);
  pp_image* small = solid(10, 10, 200, 200, 200, 255);
  pp_overlay(image3, small, 0, 0, 0.5f, nullptr);
  EXPECT_TRUE(std::abs(at(image3, 5, 5).r - 100) <= 1);
  for (pp_image* i : {image, stamp, image2, clear, image3, small}) pp_image_destroy(i);
}

TEST(edit_operations_are_leak_free) {
  const uint64_t base = liveBytes();
  pp_image* in = textured(300, 200);
  pp_image* out = run(in, {op(PP_OP_STRAIGHTEN, {5}), op(PP_OP_WARMTH, {0.3f, 0}), op(PP_OP_FILTER, {PP_FILTER_FILM, 0.8f}),
                           op(PP_OP_FOCUS, {1, 0.5f, 0.5f, 0.2f, 0, 0.5f, 0.5f})});
  pp_image* prop = testProp(120, 60);
  pp_prop_params p = propAt(100, 80, 80);
  pp_place_prop(out, prop, &p, nullptr, nullptr);
  pp_image* canvas = solid(400, 500, 0, 0, 0);
  pp_frame_params f{};
  f.shape = 3;
  f.window[2] = f.window[3] = 0.8f;
  f.window[0] = f.window[1] = 0.1f;
  f.backdrop = 2;
  f.glow = 0.5f;
  f.zoom = 1.2f;
  EXPECT_EQ(pp_frame(out, canvas, &f, nullptr), PP_OK);
  for (pp_image* i : {in, out, prop, canvas}) pp_image_destroy(i);
  EXPECT_EQ(liveBytes(), base);
}
