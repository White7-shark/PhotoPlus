// Toon (toon.hpp). What a person would notice:
//  - the picture becomes flat colour areas (far fewer distinct colours), with
//    dark ink lines on its edges; manga is black and white;
//  - anime eyes are bigger and get catchlights; skin goes smooth;
//  - a painted sky replaces the background but never the person;
//  - every style differs, a seed repeats; bad input is refused.
#include <cmath>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

constexpr int kW = 160, kH = 200;

pp_image* portrait() {
  pp_image* img = nullptr;
  pp_image_create(kW, kH, PP_FORMAT_RGBA8, &img);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      unsigned char* p = px + static_cast<size_t>(y) * info.stride + x * 4;
      // A noisy, graded wall; a face; a shirt.
      const int n = static_cast<int>((x * 7 + y * 13) % 11) - 5;
      int r = 150 + x / 4 + n, g = 170 + n, b = 190 - y / 5 + n;
      const float dx = (x - 80) / 30.0f, dy = (y - 70) / 38.0f;
      if (dx * dx + dy * dy < 1) r = 200 + n - y / 6, g = 150 + n - y / 8, b = 120 + n;  // a shaded face
      if (y > 125 && x > 30 && x < 130) r = 40 + n, g = 60 + n, b = 140 + n;           // a blue shirt
      if ((std::abs(x - 68) < 3 || std::abs(x - 92) < 3) && std::abs(y - 62) < 3) r = 25, g = 20, b = 20;
      p[0] = static_cast<unsigned char>(r), p[1] = static_cast<unsigned char>(g), p[2] = static_cast<unsigned char>(b), p[3] = 255;
    }
  return img;
}

pp_image* person() {
  pp_image* m = nullptr;
  pp_image_create(kW, kH, PP_FORMAT_GRAY8, &m);
  pp_image_info info{};
  pp_image_get_info(m, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(m));
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const float dx = (x - 80) / 32.0f, dy = (y - 70) / 40.0f;
      px[static_cast<size_t>(y) * info.stride + x] = (dx * dx + dy * dy < 1 || (y > 120 && x > 28 && x < 132)) ? 255 : 0;
    }
  return m;
}

const float kFace[12] = {50.0f / kW, 32.0f / kH, 60.0f / kW, 76.0f / kH, 0.43f, 0.47f, 0.57f, 0.51f, 68.0f / kW, 62.0f / kH, 92.0f / kW, 62.0f / kH};

std::vector<unsigned char> run(int style, int background, float eyes = 0.4f, uint32_t seed = 3, bool withPerson = true) {
  pp_image* img = portrait();
  pp_image* m = withPerson ? person() : nullptr;
  pp_toon_options o{style, background, 0.5f, 3, 0.3f, 0.5f, eyes, 0.6f, 0.3f};
  EXPECT_EQ(pp_toon_render(img, &o, m, kFace, 1, 12, seed, nullptr), PP_OK);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  const auto* px = static_cast<const unsigned char*>(pp_image_pixels(img));
  std::vector<unsigned char> out;
  for (int y = 0; y < kH; ++y) out.insert(out.end(), px + static_cast<size_t>(y) * info.stride, px + static_cast<size_t>(y) * info.stride + kW * 4);
  pp_image_destroy(img);
  if (m) pp_image_destroy(m);
  return out;
}

// How much the colour varies pixel to pixel (texture and noise): the mean step between neighbours.
double texture(const std::vector<unsigned char>& p, int x0, int y0, int x1, int y1) {
  double s = 0;
  int n = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1 - 1; ++x, ++n)
      for (int k = 0; k < 3; ++k)
        s += std::fabs(static_cast<double>(p[(static_cast<size_t>(y) * kW + x) * 4 + k]) - p[(static_cast<size_t>(y) * kW + x + 1) * 4 + k]);
  return s / (n * 3.0);
}

double diff(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
  double d = 0;
  for (size_t i = 0; i < a.size(); ++i)
    if (i % 4 != 3) d += std::fabs(static_cast<double>(a[i]) - b[i]);
  return d / (a.size() * 0.75);
}

std::vector<unsigned char> original() {
  pp_image* img = portrait();
  pp_image_info info{};
  pp_image_get_info(img, &info);
  const auto* px = static_cast<const unsigned char*>(pp_image_pixels(img));
  std::vector<unsigned char> out;
  for (int y = 0; y < kH; ++y) out.insert(out.end(), px + static_cast<size_t>(y) * info.stride, px + static_cast<size_t>(y) * info.stride + kW * 4);
  pp_image_destroy(img);
  return out;
}

}  // namespace

TEST(toon_flattens_colour_and_inks_the_edges) {
  const auto photo = original();
  const auto anime = run(0, 0);
  // The shirt and the wall become flat areas: the photo's grain is gone.
  EXPECT_TRUE(texture(anime, 40, 140, 120, 175) * 3 < texture(photo, 40, 140, 120, 175));
  EXPECT_TRUE(texture(anime, 2, 2, 28, 60) * 3 < texture(photo, 2, 2, 28, 60));
  // Ink on the shirt's edge: darker than the shirt either side of it.
  auto lum = [&](const std::vector<unsigned char>& p, int x, int y) {
    const unsigned char* q = &p[(static_cast<size_t>(y) * kW + x) * 4];
    return 0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2];
  };
  double edge = 255;
  for (int x = 27; x <= 33; ++x) edge = std::min(edge, lum(anime, x, 160));
  EXPECT_TRUE(edge < lum(anime, 60, 160) - 10);
  // Manga is black and white.
  const auto manga = run(1, 0);
  double chroma = 0;
  for (size_t i = 0; i < manga.size(); i += 4)
    chroma = std::max(chroma, static_cast<double>(std::max({manga[i], manga[i + 1], manga[i + 2]}) - std::min({manga[i], manga[i + 1], manga[i + 2]})));
  EXPECT_TRUE(chroma < 12);
}

TEST(toon_eyes_get_bigger_and_catch_the_light) {
  const auto plain = run(0, 0, 0.0f), big = run(0, 0, 1.0f);
  EXPECT_TRUE(diff(plain, big) > 0.2);
  // A catchlight: the brightest point near the eye is near white.
  int best = 0;
  for (int y = 54; y < 70; ++y)
    for (int x = 60; x < 76; ++x) best = std::max(best, static_cast<int>(big[(static_cast<size_t>(y) * kW + x) * 4]));
  EXPECT_TRUE(best > 225);
}

TEST(toon_paints_a_sky_behind_but_never_over_the_person) {
  const auto keep = run(0, 0), sky = run(0, 2);
  // The top corner (background) turns sky blue; the face is the same either way.
  const unsigned char* c = &sky[(static_cast<size_t>(4) * kW + 4) * 4];
  EXPECT_TRUE(c[2] > c[0] + 40);
  double faceDiff = 0;
  for (int y = 60; y < 95; ++y)
    for (int x = 70; x < 90; ++x)
      for (int k = 0; k < 3; ++k)
        faceDiff += std::fabs(static_cast<double>(keep[(static_cast<size_t>(y) * kW + x) * 4 + k]) - sky[(static_cast<size_t>(y) * kW + x) * 4 + k]);
  EXPECT_TRUE(faceDiff / (35.0 * 20 * 3) < 3);
  // Without a person mask, the background can't be told apart: it is kept.
  EXPECT_EQ(diff(run(0, 2, 0.4f, 3, false), run(0, 0, 0.4f, 3, false)), 0.0);
}

TEST(toon_styles_differ_and_a_seed_repeats) {
  const auto anime = run(0, 0), comic = run(2, 0), cartoon = run(3, 0), webtoon = run(4, 0), retro = run(5, 0);
  EXPECT_TRUE(diff(anime, comic) > 2);
  EXPECT_TRUE(diff(anime, cartoon) > 2);
  EXPECT_TRUE(diff(anime, webtoon) > 2);
  EXPECT_TRUE(diff(anime, retro) > 2);
  EXPECT_EQ(diff(run(5, 3, 0.4f, 9), run(5, 3, 0.4f, 9)), 0.0);
}

TEST(toon_refuses_bad_input) {
  pp_image* img = portrait();
  pp_toon_options o{6, 0, 0.5f, 3, 0.3f, 0.5f, 0.4f, 0.6f, 0.3f};
  EXPECT_EQ(pp_toon_render(img, &o, nullptr, nullptr, 0, 4, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
  o = {0, 0, 0.5f, 9, 0.3f, 0.5f, 0.4f, 0.6f, 0.3f};
  EXPECT_EQ(pp_toon_render(img, &o, nullptr, nullptr, 0, 4, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
  o = {0, 0, 2.0f, 3, 0.3f, 0.5f, 0.4f, 0.6f, 0.3f};
  EXPECT_EQ(pp_toon_render(img, &o, nullptr, nullptr, 0, 4, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_toon_render(img, nullptr, nullptr, nullptr, 0, 4, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image_destroy(img);
}
