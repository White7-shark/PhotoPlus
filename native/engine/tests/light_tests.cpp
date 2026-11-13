// Smart lighting. What a person would notice:
//  meter  — a bright room takes a normal shot; a dim room a Moonlight burst
//           (longer as the light fades); a very dark room the flash, but only
//           when there is something near to light (a selfie, a face, a close
//           subject); a skyline gets a long burst instead. Harsh or backlit
//           light gets Bright Balance with a lowered exposure. The screen
//           "flash" is tinted toward the room's own light, never strongly.
//  tone   — night stays night (a dark sky stays dark); a harsh day keeps
//           its sky and opens its shadows; an evenly lit scene is barely
//           touched; a low-light colour cast is corrected; strength 0 is a
//           no-op.
//  flash  — Natural Flash keeps the room's colour and mood, takes the flash
//           frame's clean detail (less noise), lines up a moved flash frame,
//           and fills flash shadows from the ambient photo instead of black.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

float toLin(float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
float toGam(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// Deterministic noise (Box–Muller on a small LCG).
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 2654435761u + 1) {}
  float uni() {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return (static_cast<float>(s >> 40) + 0.5f) / 16777216.0f;
  }
  float gauss() { return std::sqrt(-2.0f * std::log(uni())) * std::cos(6.2831853f * uni()); }
};

struct Img {
  pp_image* p = nullptr;
  int w, h;
  Img(int w_, int h_, int32_t fmt = PP_FORMAT_RGBA8) : w(w_), h(h_) { pp_image_create(w, h, fmt, &p); }
  ~Img() { pp_image_destroy(p); }
  Img(const Img&) = delete;
  Img& operator=(const Img&) = delete;
  uint8_t* px(int x, int y) {
    pp_image_info i{};
    pp_image_get_info(p, &i);
    return static_cast<uint8_t*>(pp_image_pixels(p)) + static_cast<size_t>(y) * i.stride + static_cast<size_t>(x) * 4;
  }
  // Linear RGB, stored as sRGB.
  void set(int x, int y, float r, float g, float b) {
    uint8_t* q = px(x, y);
    q[0] = static_cast<uint8_t>(std::lround(toGam(r) * 255.0f));
    q[1] = static_cast<uint8_t>(std::lround(toGam(g) * 255.0f));
    q[2] = static_cast<uint8_t>(std::lround(toGam(b) * 255.0f));
    q[3] = 255;
  }
  float gam(int x, int y, int c) { return px(x, y)[c] / 255.0f; }
  float lin(int x, int y, int c) { return toLin(gam(x, y, c)); }
  float luma(int x, int y) { return 0.2126f * lin(x, y, 0) + 0.7152f * lin(x, y, 1) + 0.0722f * lin(x, y, 2); }
};

// Surface detail (multi-scale, no noise): 0.25..0.85.
float albedo(float x, float y) {
  return 0.55f + 0.18f * std::sin(x * 0.31f) * std::sin(y * 0.23f) + 0.12f * std::sin(x * 0.067f + y * 0.051f);
}

struct Box {
  int x0, y0, x1, y1;
};
template <class F>
double meanOver(const Box& b, F f) {
  double s = 0;
  int n = 0;
  for (int y = b.y0; y < b.y1; ++y)
    for (int x = b.x0; x < b.x1; ++x) s += f(x, y), ++n;
  return s / std::max(1, n);
}
// Fine detail: std of the value minus its 5×5 mean.
template <class F>
double detailOver(const Box& b, F f) {
  double s = 0;
  int n = 0;
  for (int y = b.y0 + 2; y < b.y1 - 2; ++y)
    for (int x = b.x0 + 2; x < b.x1 - 2; ++x) {
      double m = 0;
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) m += f(x + dx, y + dy);
      const double d = f(x, y) - m / 25.0;
      s += d * d, ++n;
    }
  return std::sqrt(s / std::max(1, n));
}

// A scene of uniform light `level` (linear) and a colour.
void fillScene(Img& im, float level, float cr, float cg, float cb, uint64_t seed = 1, float noise = 0.0f) {
  Rng r(seed);
  for (int y = 0; y < im.h; ++y)
    for (int x = 0; x < im.w; ++x) {
      const float a = albedo(static_cast<float>(x), static_cast<float>(y)) * level;
      const float n = noise * r.gauss();
      im.set(x, y, a * cr * (1 + n), a * cg * (1 + n), a * cb * (1 + n));
    }
}

pp_light_meter_input exposure(float iso, float t, float fn = 1.8f) {
  pp_light_meter_input in{};
  in.iso = iso, in.exposure_s = t, in.f_number = fn;
  in.face_luma = -1, in.focus_diopters = -1;
  return in;
}

}  // namespace

// ------------------------------------------------------------------ meter

TEST(light_meter_picks_a_plan_from_the_light) {
  Img prev(160, 120);
  // A bright room: auto exposure at ISO 100, 1/100 s; the preview at a normal key.
  fillScene(prev, 0.3f, 1, 1, 1);
  pp_light_meter_result m{};
  pp_light_meter_input in = exposure(100, 0.01f);
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_EQ(m.exposure_known, 1);
  EXPECT_TRUE(m.lux > 300.0f);
  EXPECT_EQ(m.plan, PP_LIGHT_NORMAL);
  EXPECT_EQ(m.frames, 1);

  // A dim room (a few lux): the exposure runs out and the preview is dark.
  fillScene(prev, 0.15f, 1, 1, 1);
  in = exposure(3200, 1.0f / 15.0f);
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_TRUE(m.lux > 1.0f && m.lux < 40.0f);
  EXPECT_EQ(m.plan, PP_LIGHT_MOONLIGHT);
  const int dimFrames = m.frames;
  EXPECT_TRUE(dimFrames >= 8 && dimFrames <= 12);
  EXPECT_TRUE(m.night > 0.7f);

  // An evening room (~20 lux): a shorter burst, gentle night look.
  fillScene(prev, 0.18f, 1, 1, 1);
  in = exposure(800, 1.0f / 30.0f);
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_EQ(m.plan, PP_LIGHT_MOONLIGHT);
  EXPECT_TRUE(m.frames < dimFrames);
  EXPECT_TRUE(m.night < 0.5f);

  // The same dim room with a face: a clear portrait, less of the night look.
  fillScene(prev, 0.15f, 1, 1, 1);
  in = exposure(3200, 1.0f / 15.0f);
  in.face_luma = 0.3f;
  pp_light_meter_result face{};
  ASSERT_OK(pp_light_meter(prev.p, &in, &face));
  EXPECT_EQ(face.plan, PP_LIGHT_MOONLIGHT);
  EXPECT_TRUE(face.night < 0.5f);
}

TEST(light_meter_uses_the_flash_only_for_something_near) {
  Img prev(160, 120);
  fillScene(prev, 0.02f, 1, 1, 1);  // below 1 lux
  pp_light_meter_result m{};
  // A selfie: the screen lights the face.
  pp_light_meter_input in = exposure(6400, 0.1f);
  in.front = 1;
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_TRUE(m.lux < 1.0f);
  EXPECT_EQ(m.plan, PP_LIGHT_FLASH);
  // The rear camera focused at a metre with a flash unit: flash.
  in = exposure(6400, 0.1f);
  in.has_flash = 1, in.focus_diopters = 1.0f;
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_EQ(m.plan, PP_LIGHT_FLASH);
  // The rear camera at infinity (a skyline): the flash would light nothing.
  in.focus_diopters = 0.0f;
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_EQ(m.plan, PP_LIGHT_MOONLIGHT);
  EXPECT_TRUE(m.frames >= 11);
  EXPECT_NEAR(m.night, 1.0, 1e-3);
  // No flash unit and no face: a long burst too.
  in.has_flash = 0, in.focus_diopters = 1.0f;
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_EQ(m.plan, PP_LIGHT_MOONLIGHT);
}

TEST(light_meter_protects_harsh_and_backlit_light) {
  Img prev(160, 120);
  // A burnt-out sky over a dark ground.
  for (int y = 0; y < prev.h; ++y)
    for (int x = 0; x < prev.w; ++x) {
      const float a = albedo(static_cast<float>(x), static_cast<float>(y));
      if (y < prev.h / 2) prev.set(x, y, 1.0f, 1.0f, 1.0f);
      else prev.set(x, y, 0.004f * a, 0.004f * a, 0.004f * a);
    }
  pp_light_meter_result m{};
  pp_light_meter_input in = exposure(50, 1.0f / 2000.0f);
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_EQ(m.plan, PP_LIGHT_BRIGHT_BALANCE);
  EXPECT_TRUE(m.exposure_bias <= -1.0f && m.exposure_bias >= -2.0f);
  EXPECT_TRUE(m.range_stops > 7.0f);

  // Backlit: a bright window behind a dim face, little else at the extremes.
  for (int y = 0; y < prev.h; ++y)
    for (int x = 0; x < prev.w; ++x) {
      const float a = albedo(static_cast<float>(x), static_cast<float>(y));
      const bool window = y < prev.h / 30;  // a small bright window
      prev.set(x, y, window ? 1.0f : 0.25f * a, window ? 1.0f : 0.25f * a, window ? 1.0f : 0.25f * a);
    }
  in = exposure(100, 1.0f / 250.0f);
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_EQ(m.plan, PP_LIGHT_NORMAL);  // without a face, an ordinary bright scene
  in.face_luma = 0.12f;
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_EQ(m.backlit, 1);
  EXPECT_EQ(m.plan, PP_LIGHT_BRIGHT_BALANCE);
  in.face_luma = 0.6f;  // a well-lit face is not backlit
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_EQ(m.backlit, 0);
}

TEST(light_meter_tints_the_screen_flash_toward_the_room) {
  Img prev(160, 120);
  pp_light_meter_result m{};
  pp_light_meter_input in = exposure(3200, 1.0f / 15.0f);
  // A warm lamp-lit room.
  fillScene(prev, 0.15f, 1.0f, 0.62f, 0.3f);
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_EQ(m.screen_flash[0], 255);
  EXPECT_TRUE(m.screen_flash[1] < 250 && m.screen_flash[2] < m.screen_flash[1]);
  EXPECT_TRUE(m.screen_flash[2] >= 195);  // never strongly coloured
  // A neutral room: white.
  fillScene(prev, 0.15f, 1, 1, 1);
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_TRUE(m.screen_flash[0] >= 253 && m.screen_flash[1] >= 253 && m.screen_flash[2] >= 253);
}

TEST(light_meter_works_without_exposure_data_and_checks_input) {
  Img prev(160, 120);
  fillScene(prev, 0.01f, 1, 1, 1);
  pp_light_meter_result m{};
  pp_light_meter_input in = exposure(0, 0);
  in.iso = std::nanf("");
  ASSERT_OK(pp_light_meter(prev.p, &in, &m));
  EXPECT_EQ(m.exposure_known, 0);
  EXPECT_EQ(m.plan, PP_LIGHT_MOONLIGHT);  // judged by how dark the preview is
  EXPECT_EQ(pp_light_meter(nullptr, &in, &m), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_light_meter(prev.p, nullptr, &m), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_light_meter(prev.p, &in, nullptr), PP_ERROR_INVALID_ARGUMENT);
  Img grey(160, 120, PP_FORMAT_GRAY8);
  EXPECT_EQ(pp_light_meter(grey.p, &in, &m), PP_ERROR_INVALID_ARGUMENT);
  Img tiny(8, 8);
  EXPECT_EQ(pp_light_meter(tiny.p, &in, &m), PP_ERROR_INVALID_ARGUMENT);
}

// ------------------------------------------------------------------- tone

namespace {
// A night street: a dark, slightly noisy sky over lit buildings with a lamp.
void nightScene(Img& im) {
  Rng r(3);
  for (int y = 0; y < im.h; ++y)
    for (int x = 0; x < im.w; ++x) {
      const float a = albedo(static_cast<float>(x), static_cast<float>(y));
      float v;
      if (y < im.h * 2 / 5) v = 0.0025f * (1.0f + 0.25f * r.gauss());  // sky
      else v = 0.06f * a;                                              // buildings
      const float dx = static_cast<float>(x - im.w * 3 / 4), dy = static_cast<float>(y - im.h / 2);
      v += 0.9f * std::exp(-(dx * dx + dy * dy) / 60.0f);  // a lamp
      im.set(x, y, v * 1.05f, v, v * 0.85f);
    }
}
// A harsh day: a bright, textured sky over a deep-shadowed ground.
void harshScene(Img& im) {
  for (int y = 0; y < im.h; ++y)
    for (int x = 0; x < im.w; ++x) {
      const float a = albedo(static_cast<float>(x), static_cast<float>(y));
      const float v = y < im.h / 2 ? 0.55f + 0.4f * (a - 0.55f) : 0.006f * a;
      im.set(x, y, v, v, v * 1.02f);
    }
}
}  // namespace

TEST(light_tone_keeps_night_looking_like_night) {
  Img a(320, 240), b(320, 240);
  nightScene(a);
  nightScene(b);
  const Box sky{0, 0, 320, 80}, town{0, 120, 200, 240};
  const double skyBefore = meanOver(sky, [&](int x, int y) { return a.gam(x, y, 1); });
  const double townBefore = meanOver(town, [&](int x, int y) { return a.gam(x, y, 1); });
  pp_light_tone_params p;
  pp_light_tone_default_params(&p);
  p.night = 1.0f;
  float rep[6] = {};
  ASSERT_OK(pp_light_tone(a.p, &p, nullptr, rep));
  p.night = 0.0f;
  ASSERT_OK(pp_light_tone(b.p, &p, nullptr, nullptr));
  const double skyNight = meanOver(sky, [&](int x, int y) { return a.gam(x, y, 1); });
  const double skyDay = meanOver(sky, [&](int x, int y) { return b.gam(x, y, 1); });
  const double townNight = meanOver(town, [&](int x, int y) { return a.gam(x, y, 1); });
  std::printf("    night: sky %.3f -> %.3f (day render %.3f), town %.3f -> %.3f\n", skyBefore, skyNight, skyDay, townBefore,
              townNight);
  EXPECT_TRUE(skyNight < 0.12);           // the sky stays dark
  EXPECT_TRUE(skyNight < skyDay - 0.03);  // darker than a "daylight" rendering of it
  EXPECT_TRUE(townNight > townBefore);    // the lit town comes up
  EXPECT_TRUE(townNight - skyNight > townBefore - skyBefore);  // more separation, not less
  // The lamp stays the brightest thing.
  EXPECT_TRUE(a.gam(240, 120, 1) > 0.9f);
  // Sky noise is calmer than before.
  const double nBefore = detailOver(sky, [&](int x, int y) { return b.gam(x, y, 1); });
  const double nAfter = detailOver(sky, [&](int x, int y) { return a.gam(x, y, 1); });
  EXPECT_TRUE(nAfter < nBefore);
}

TEST(light_tone_balances_a_harsh_day) {
  Img im(320, 240);
  harshScene(im);
  const Box sky{0, 0, 320, 110}, low{0, 130, 320, 240};
  const double skyDetailBefore = detailOver(sky, [&](int x, int y) { return im.gam(x, y, 1); });
  const double groundBefore = meanOver(low, [&](int x, int y) { return im.gam(x, y, 1); });
  const double groundDetailBefore = detailOver(low, [&](int x, int y) { return im.gam(x, y, 1); });
  pp_light_tone_params p;
  pp_light_tone_default_params(&p);
  float rep[6] = {};
  ASSERT_OK(pp_light_tone(im.p, &p, nullptr, rep));
  const double skyDetail = detailOver(sky, [&](int x, int y) { return im.gam(x, y, 1); });
  const double ground = meanOver(low, [&](int x, int y) { return im.gam(x, y, 1); });
  const double groundDetail = detailOver(low, [&](int x, int y) { return im.gam(x, y, 1); });
  std::printf("    harsh: range %.1f shadows %.2f highlights %.2f clipped %.4f; ground %.3f -> %.3f\n", rep[2], rep[3], rep[4],
              rep[5], groundBefore, ground);
  EXPECT_TRUE(rep[2] > 6.0f);
  EXPECT_TRUE(rep[3] > 0.2f);              // shadows opened automatically
  EXPECT_TRUE(ground > groundBefore + 0.08);
  EXPECT_TRUE(groundDetail > groundDetailBefore);  // detail comes up with them
  EXPECT_TRUE(skyDetail > 0.6 * skyDetailBefore);  // the sky keeps its clouds
  EXPECT_TRUE(rep[5] < 0.01f);                     // nothing burns out
}

TEST(light_tone_barely_touches_an_evenly_lit_scene) {
  Img im(320, 240);
  fillScene(im, 0.32f, 1.0f, 0.95f, 0.9f);
  std::vector<uint8_t> before;
  for (int y = 0; y < im.h; ++y)
    for (int x = 0; x < im.w; ++x) before.push_back(im.px(x, y)[1]);
  pp_light_tone_params p;
  pp_light_tone_default_params(&p);
  float rep[6] = {};
  ASSERT_OK(pp_light_tone(im.p, &p, nullptr, rep));
  EXPECT_TRUE(rep[3] < 0.05f && rep[4] < 0.05f);
  double diff = 0;
  size_t i = 0;
  for (int y = 0; y < im.h; ++y)
    for (int x = 0; x < im.w; ++x) diff += std::abs(static_cast<int>(im.px(x, y)[1]) - before[i++]);
  diff /= static_cast<double>(before.size());
  std::printf("    even scene: mean change %.2f levels\n", diff);
  EXPECT_TRUE(diff < 10.0);
}

TEST(light_tone_corrects_a_low_light_cast_but_keeps_it_warm) {
  Img im(320, 240);
  fillScene(im, 0.12f, 0.75f, 1.0f, 0.55f);  // a yellow-green sodium cast
  pp_light_tone_params p;
  pp_light_tone_default_params(&p);
  p.white_balance = 1.0f;
  const Box all{0, 0, 320, 240};
  auto ratio = [&](int c) { return meanOver(all, [&](int x, int y) { return im.lin(x, y, c); }); };
  const double gbBefore = ratio(1) / ratio(2), grBefore = ratio(1) / ratio(0);
  ASSERT_OK(pp_light_tone(im.p, &p, nullptr, nullptr));
  const double r = ratio(0), g = ratio(1), b = ratio(2);
  std::printf("    cast: g/b %.2f -> %.2f, g/r %.2f -> %.2f\n", gbBefore, g / b, grBefore, g / r);
  EXPECT_TRUE(g / b < gbBefore * 0.75);
  EXPECT_TRUE(g / r < grBefore * 0.85);
  EXPECT_TRUE(r > b);  // a touch warm, not cold
}

TEST(light_tone_strength_zero_changes_nothing_and_input_is_checked) {
  Img im(64, 48);
  harshScene(im);
  std::vector<uint8_t> before;
  for (int y = 0; y < im.h; ++y)
    for (int x = 0; x < im.w; ++x)
      for (int c = 0; c < 4; ++c) before.push_back(im.px(x, y)[c]);
  pp_light_tone_params p;
  pp_light_tone_default_params(&p);
  p.strength = 0.0f;
  ASSERT_OK(pp_light_tone(im.p, &p, nullptr, nullptr));
  size_t i = 0, same = 0;
  for (int y = 0; y < im.h; ++y)
    for (int x = 0; x < im.w; ++x)
      for (int c = 0; c < 4; ++c) same += im.px(x, y)[c] == before[i++] ? 1u : 0u;
  EXPECT_EQ(same, before.size());

  pp_light_tone_default_params(&p);
  EXPECT_EQ(pp_light_tone(nullptr, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_light_tone(im.p, nullptr, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_light_tone_params bad = p;
  bad.night = std::nanf("");
  EXPECT_EQ(pp_light_tone(im.p, &bad, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  bad = p;
  bad.white_balance = std::numeric_limits<float>::infinity();
  EXPECT_EQ(pp_light_tone(im.p, &bad, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  Img grey(64, 48, PP_FORMAT_GRAY8);
  EXPECT_EQ(pp_light_tone(grey.p, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  Img tiny(8, 8);
  EXPECT_EQ(pp_light_tone(tiny.p, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_cancel_token* token = pp_cancel_token_create();
  pp_cancel_token_cancel(token);
  EXPECT_EQ(pp_light_tone(im.p, &p, token, nullptr), PP_ERROR_CANCELLED);
  pp_cancel_token_destroy(token);
  // Out-of-range values are clamped, not refused.
  p.night = 5.0f, p.strength = 3.0f, p.exposure = 40.0f, p.detail = -2.0f, p.shadows = 9.0f;
  ASSERT_OK(pp_light_tone(im.p, &p, nullptr, nullptr));
}

// ------------------------------------------------------------ flash fusion

namespace {
// A dim warm room lit by a lamp, and the same room with a white flash from
// the camera. `truth` is the clean ambient photo; `ambient` is that photo
// as a phone gives it (merged, still noisy); `flash` the flash frame
// (clean, cold, a fall-off, a shadow behind the subject, moved by a hand).
struct FlashPair {
  int w = 320, h = 240;
  Img truth{320, 240}, ambient{320, 240}, flash{320, 240};
  Box shadow{200, 60, 260, 200};
  FlashPair(int shiftX, int shiftY) {
    Rng r(11);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float a = albedo(static_cast<float>(x), static_cast<float>(y));
        const float lamp = 0.18f * (0.75f + 0.25f * static_cast<float>(x) / static_cast<float>(w));
        const float tr = a * lamp * 1.0f, tg = a * lamp * 0.7f, tb = a * lamp * 0.42f;
        truth.set(x, y, tr, tg, tb);
        const float n = 0.12f * r.gauss(), nc = 0.06f * r.gauss();
        ambient.set(x, y, tr * (1 + n + nc), tg * (1 + n), tb * (1 + n - nc));
        // The flash frame sees the scene moved by (shiftX, shiftY).
        const float sx = static_cast<float>(x - shiftX), sy = static_cast<float>(y - shiftY);
        const float fa = albedo(sx, sy);
        const int ix = x - shiftX, iy = y - shiftY;
        const bool inShadow = ix >= shadow.x0 && ix < shadow.x1 && iy >= shadow.y0 && iy < shadow.y1;
        const float falloff = 0.5f * (1.0f - 0.3f * std::abs(sx - 160.0f) / 160.0f);
        const float fl = inShadow ? 0.0f : falloff;
        const float ar = fa * 0.18f * (0.75f + 0.25f * sx / static_cast<float>(w));
        const float fn = 0.02f * r.gauss();
        flash.set(x, y, (fl * fa + ar) * (1 + fn), (fl * fa + 0.7f * ar) * (1 + fn), (fl * fa + 0.42f * ar) * (1 + fn));
      }
  }
};
double rmsError(Img& a, Img& b, const Box& box) {
  double s = 0;
  int n = 0;
  for (int y = box.y0; y < box.y1; ++y)
    for (int x = box.x0; x < box.x1; ++x)
      for (int c = 0; c < 3; ++c) {
        const double d = a.gam(x, y, c) - b.gam(x, y, c);
        s += d * d, ++n;
      }
  return std::sqrt(s / n);
}
}  // namespace

TEST(flash_fuse_keeps_the_room_light_and_takes_the_flash_detail) {
  FlashPair s(4, -3);
  Img out(s.w, s.h);
  pp_flash_fuse_params p{};
  p.fill = 0.0f, p.strength = 1.0f;
  float rep[4] = {};
  ASSERT_OK(pp_flash_fuse(s.ambient.p, s.flash.p, out.p, &p, nullptr, rep));
  const Box lit{20, 20, 180, 220};
  const double errAmbient = rmsError(s.ambient, s.truth, lit);
  const double errOut = rmsError(out, s.truth, lit);
  const double rbTruth = meanOver(lit, [&](int x, int y) { return s.truth.lin(x, y, 0); }) /
                         meanOver(lit, [&](int x, int y) { return s.truth.lin(x, y, 2); });
  const double rbOut = meanOver(lit, [&](int x, int y) { return out.lin(x, y, 0); }) /
                       meanOver(lit, [&](int x, int y) { return out.lin(x, y, 2); });
  const double keyTruth = meanOver(lit, [&](int x, int y) { return s.truth.luma(x, y); });
  const double keyOut = meanOver(lit, [&](int x, int y) { return out.luma(x, y); });
  std::printf("    fuse: aligned %.0f gain %.2f shadow %.3f hot %.3f; error %.4f -> %.4f; r/b %.2f vs %.2f; key %.3f vs %.3f\n",
              rep[0], rep[1], rep[2], rep[3], errAmbient, errOut, rbOut, rbTruth, keyOut, keyTruth);
  EXPECT_EQ(rep[0], 1.0f);                     // the moved flash frame was lined up
  EXPECT_TRUE(rep[1] > 2.0f);                  // and found brighter
  EXPECT_TRUE(errOut < 0.6 * errAmbient);      // cleaner than the ambient photo
  EXPECT_NEAR(rbOut / rbTruth, 1.0, 0.12);     // the room's warm colour, not the flash's
  EXPECT_NEAR(keyOut / keyTruth, 1.0, 0.15);   // and its brightness (the mood)
  // In the flash's shadow, the ambient photo (not black).
  const Box sh{s.shadow.x0 + 12, s.shadow.y0 + 12, s.shadow.x1 - 8, s.shadow.y1 - 12};
  const double shOut = meanOver(sh, [&](int x, int y) { return out.luma(x, y); });
  const double shTruth = meanOver(sh, [&](int x, int y) { return s.truth.luma(x, y); });
  EXPECT_TRUE(rep[2] > 0.03f);
  EXPECT_NEAR(shOut / shTruth, 1.0, 0.2);

  // Fill adds some of the flash's own light (brighter), still warm.
  p.fill = 1.0f;
  Img filled(s.w, s.h);
  ASSERT_OK(pp_flash_fuse(s.ambient.p, s.flash.p, filled.p, &p, nullptr, nullptr));
  const double keyFill = meanOver(lit, [&](int x, int y) { return filled.luma(x, y); });
  const double rbFill = meanOver(lit, [&](int x, int y) { return filled.lin(x, y, 0); }) /
                        meanOver(lit, [&](int x, int y) { return filled.lin(x, y, 2); });
  EXPECT_TRUE(keyFill > keyOut * 1.05);
  EXPECT_TRUE(rbFill > 1.5);
}

TEST(flash_fuse_strength_zero_is_the_ambient_photo_and_input_is_checked) {
  FlashPair s(0, 0);
  Img out(s.w, s.h);
  pp_flash_fuse_params p{};
  p.fill = 0.3f, p.strength = 0.0f;
  ASSERT_OK(pp_flash_fuse(s.ambient.p, s.flash.p, out.p, &p, nullptr, nullptr));
  EXPECT_NEAR(rmsError(out, s.ambient, Box{0, 0, s.w, s.h}), 0.0, 1e-3);

  p.strength = 1.0f;
  Img small(160, 120);
  EXPECT_EQ(pp_flash_fuse(s.ambient.p, small.p, out.p, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_flash_fuse(s.ambient.p, s.flash.p, small.p, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_flash_fuse(nullptr, s.flash.p, out.p, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_flash_fuse(s.ambient.p, s.flash.p, out.p, nullptr, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  Img grey(s.w, s.h, PP_FORMAT_GRAY8);
  EXPECT_EQ(pp_flash_fuse(s.ambient.p, grey.p, out.p, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_flash_fuse_params bad = p;
  bad.fill = std::nanf("");
  EXPECT_EQ(pp_flash_fuse(s.ambient.p, s.flash.p, out.p, &bad, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  Img t1(16, 16), t2(16, 16), t3(16, 16);
  EXPECT_EQ(pp_flash_fuse(t1.p, t2.p, t3.p, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_cancel_token* token = pp_cancel_token_create();
  pp_cancel_token_cancel(token);
  EXPECT_EQ(pp_flash_fuse(s.ambient.p, s.flash.p, out.p, &p, token, nullptr), PP_ERROR_CANCELLED);
  pp_cancel_token_destroy(token);
  // In place (out is the ambient image) is allowed.
  ASSERT_OK(pp_flash_fuse(s.ambient.p, s.flash.p, s.ambient.p, &p, nullptr, nullptr));
}

TEST(light_tone_restores_held_back_exposure_and_lifts_a_dim_scene_gently) {
  // An evenly lit scene two stops under a normal exposure.
  pp_light_tone_params p;
  pp_light_tone_default_params(&p);
  float held[6] = {}, dim[6] = {};
  {
    Img im(320, 240);
    fillScene(im, 0.08f, 1.0f, 0.95f, 0.9f);
    p.capture_bias = -2.0f;  // the camera held back two stops on purpose
    ASSERT_OK(pp_light_tone(im.p, &p, nullptr, held));
  }
  {
    Img im(320, 240);
    fillScene(im, 0.08f, 1.0f, 0.95f, 0.9f);
    p.capture_bias = 0.0f;  // the room itself is dim
    ASSERT_OK(pp_light_tone(im.p, &p, nullptr, dim));
  }
  std::printf("    key %.3f: held back -> %.3f, dim room -> %.3f\n", held[0], held[1], dim[1]);
  EXPECT_NEAR(held[1] / 0.18f, 1.0, 0.25);                      // back to a normal exposure
  EXPECT_TRUE(dim[1] > dim[0] * 1.4f && dim[1] < held[1] * 0.8f);  // brighter, still a dim room
  p.capture_bias = std::nanf("");
  Img im(64, 48);
  EXPECT_EQ(pp_light_tone(im.p, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
}
