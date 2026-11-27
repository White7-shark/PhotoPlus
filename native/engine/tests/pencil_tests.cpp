// Pencil art and the live studio (art.hpp). What a person would notice:
//  - a pencil drawing has no colour (graphite is grey; red chalk is one hue),
//    keeps the photo's light and dark, and draws lines on the edges;
//  - where nothing is drawn, the drawing is the blank sheet, pixel for pixel;
//  - every master draws differently, and the same seed draws the same;
//  - the live studio starts on the blank sheet, reveals more as it goes,
//    ends on the finished work exactly, and is the same going forward;
//  - bad input is refused.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

constexpr int kW = 160, kH = 200;

// A light wall; a dark coat; a mid-tone face with eyes; a hard edge.
pp_image* portrait() {
  pp_image* img = nullptr;
  pp_image_create(kW, kH, PP_FORMAT_RGBA8, &img);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      unsigned char* p = px + static_cast<size_t>(y) * info.stride + x * 4;
      int r = 225, g = 215, b = 200;  // wall
      const float dx = (x - 80) / 30.0f, dy = (y - 70) / 38.0f;
      if (y > 120 && x > 30 && x < 130) r = 35, g = 40, b = 70;  // coat
      if (dx * dx + dy * dy < 1) r = 190, g = 140, b = 110;      // face
      if ((std::abs(x - 68) < 4 || std::abs(x - 92) < 4) && std::abs(y - 62) < 3) r = 30, g = 25, b = 20;  // eyes
      p[0] = static_cast<unsigned char>(r), p[1] = static_cast<unsigned char>(g), p[2] = static_cast<unsigned char>(b), p[3] = 255;
    }
  return img;
}

std::vector<unsigned char> pixels(pp_image* img) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  const auto* px = static_cast<const unsigned char*>(pp_image_pixels(img));
  std::vector<unsigned char> out;
  for (int y = 0; y < info.height; ++y) out.insert(out.end(), px + static_cast<size_t>(y) * info.stride, px + static_cast<size_t>(y) * info.stride + info.width * 4);
  return out;
}

double meanLuma(const std::vector<unsigned char>& p, int x0, int y0, int x1, int y1) {
  double s = 0;
  int n = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x, ++n) {
      const unsigned char* q = &p[(static_cast<size_t>(y) * kW + x) * 4];
      s += 0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2];
    }
  return s / n;
}

std::vector<unsigned char> draw(int artist, int medium, int paper, uint32_t seed) {
  pp_image* img = portrait();
  const float face[4] = {50.0f / kW, 32.0f / kH, 60.0f / kW, 76.0f / kH};
  pp_art_pencil_options o{medium, paper, 0.6f, 1, 1};
  EXPECT_EQ(pp_art_pencil(img, artist, 1, nullptr, face, 1, 4, seed, &o, nullptr), PP_OK);
  std::vector<unsigned char> out = pixels(img);
  pp_image_destroy(img);
  return out;
}

double diff(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
  double d = 0;
  for (size_t i = 0; i < a.size(); ++i)
    if (i % 4 != 3) d += std::fabs(static_cast<double>(a[i]) - b[i]);
  return d / (a.size() * 0.75);
}

}  // namespace

TEST(pencil_is_grey_keeps_values_and_draws_edges) {
  const auto d = draw(0, 0, 0, 7);
  double chroma = 0;
  for (size_t i = 0; i < d.size(); i += 4) chroma = std::max(chroma, static_cast<double>(std::max({d[i], d[i + 1], d[i + 2]}) - std::min({d[i], d[i + 1], d[i + 2]})));
  EXPECT_TRUE(chroma < 30);  // graphite on white paper: no colour
  // The dark coat is darker than the face, the face darker than the wall.
  // (Away from the sheet's edges, where a drawing fades into bare paper.)
  const double coat = meanLuma(d, 45, 124, 115, 150), face = meanLuma(d, 66, 75, 94, 100), wall = meanLuma(d, 5, 5, 30, 40);
  EXPECT_TRUE(coat < face - 15);
  EXPECT_TRUE(face < wall);
  // Red chalk is one warm hue.
  const auto red = draw(0, 2, 0, 7);
  const double coatR = meanLuma(red, 45, 124, 115, 150);
  size_t i = (static_cast<size_t>(135) * kW + 80) * 4;
  EXPECT_TRUE(red[i] > red[i + 2] + 15);
  EXPECT_TRUE(coatR < 200);
}

TEST(pencil_blank_sheet_matches_the_bare_paper) {
  pp_image* img = portrait();
  pp_art_pencil_options o{0, 1, 0.6f, 1, 1};
  EXPECT_EQ(pp_art_blank(img, 1, 3, 9, &o), PP_OK);
  const auto sheet = pixels(img);
  pp_image_destroy(img);
  img = portrait();
  EXPECT_EQ(pp_art_pencil(img, 3, 1, nullptr, nullptr, 0, 4, 9, &o, nullptr), PP_OK);
  const auto drawn = pixels(img);
  pp_image_destroy(img);
  // The corner fades into bare paper: identical to the sheet.
  int same = 0, n = 0;
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x, ++n) {
      const size_t k = (static_cast<size_t>(y) * kW + x) * 4;
      if (std::abs(sheet[k] - drawn[k]) <= 1 && std::abs(sheet[k + 1] - drawn[k + 1]) <= 1) ++same;
    }
  EXPECT_TRUE(same >= n - 1);
  // A painting's ground is the artist's priming: Rembrandt's is a dark brown imprimatura, Raphael's a light one.
  img = portrait();
  EXPECT_EQ(pp_art_blank(img, 0, 6, 1, nullptr), PP_OK);
  const double rembrandt = meanLuma(pixels(img), 0, 0, kW, kH);
  EXPECT_EQ(pp_art_blank(img, 0, 2, 1, nullptr), PP_OK);
  const double raphael = meanLuma(pixels(img), 0, 0, kW, kH);
  pp_image_destroy(img);
  EXPECT_TRUE(rembrandt < raphael - 60);
}

TEST(pencil_masters_differ_and_a_seed_repeats) {
  const auto leonardo = draw(0, 0, 0, 7), durer = draw(3, 0, 0, 7), again = draw(3, 0, 0, 7), seurat = draw(16, 0, 0, 7);
  EXPECT_TRUE(diff(leonardo, durer) > 3);
  EXPECT_TRUE(diff(durer, seurat) > 3);
  EXPECT_EQ(diff(durer, again), 0.0);
  // Toned paper: the wall (the lights) gets white chalk, lighter than the bare grey paper.
  const auto toned = draw(0, 0, 2, 7);
  pp_image* img = portrait();
  pp_art_pencil_options o{0, 2, 0.6f, 1, 1};
  pp_art_blank(img, 1, 0, 7, &o);
  const auto grey = pixels(img);
  pp_image_destroy(img);
  EXPECT_TRUE(meanLuma(toned, 40, 5, 120, 25) > meanLuma(grey, 40, 5, 120, 25));
}

TEST(reveal_starts_blank_grows_and_ends_on_the_work) {
  for (int tool = 0; tool < 2; ++tool) {
    pp_image* work = portrait();
    pp_image* blank = portrait();
    pp_art_pencil_options o{0, 0, 0.6f, 1, 1};
    if (tool == 0) {
      EXPECT_EQ(pp_art_pencil(work, 0, 1, nullptr, nullptr, 0, 4, 3, &o, nullptr), PP_OK);
      EXPECT_EQ(pp_art_blank(blank, 1, 0, 3, &o), PP_OK);
    } else {
      EXPECT_EQ(pp_art_paint(work, 7, 0, nullptr, nullptr, 0, 4, 3, nullptr, nullptr), PP_OK);
      EXPECT_EQ(pp_art_blank(blank, 0, 7, 3, nullptr), PP_OK);
    }
    const auto W = pixels(work), Bk = pixels(blank);
    pp_art_reveal* r = nullptr;
    EXPECT_EQ(pp_art_reveal_create(work, blank, tool, 0, nullptr, 5, nullptr, &r), PP_OK);
    EXPECT_TRUE(pp_art_reveal_strokes(r) > 10);
    pp_image* f = nullptr;
    pp_image_create(kW, kH, PP_FORMAT_RGBA8, &f);
    double last = diff(Bk, W);
    // Away from the moving tool (the top-left quarter), the sheet only ever comes closer to the work.
    auto quarter = [&](const std::vector<unsigned char>& a) {
      double d = 0;
      for (int y = 0; y < kH / 2; ++y)
        for (int x = 0; x < kW / 2; ++x)
          for (int k = 0; k < 3; ++k) d += std::fabs(static_cast<double>(a[(static_cast<size_t>(y) * kW + x) * 4 + k]) - W[(static_cast<size_t>(y) * kW + x) * 4 + k]);
      return d;
    };
    double lastQ = 1e18;
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 0.95f}) {
      EXPECT_EQ(pp_art_reveal_frame(r, t, f), PP_OK);
      const auto px = pixels(f);
      const double q = quarter(px);
      EXPECT_TRUE(q <= lastQ + 1);
      lastQ = q;
      if (t > 0.2f) EXPECT_TRUE(diff(px, W) < last + 0.01 || t > 0.9f);
      last = diff(px, W);
    }
    EXPECT_EQ(pp_art_reveal_frame(r, 1.0f, f), PP_OK);
    EXPECT_EQ(diff(pixels(f), W), 0.0);
    // Back to the start, then forward again: the same frame as before.
    EXPECT_EQ(pp_art_reveal_frame(r, 0.5f, f), PP_OK);
    const auto mid1 = pixels(f);
    EXPECT_EQ(pp_art_reveal_frame(r, 0.2f, f), PP_OK);
    EXPECT_EQ(pp_art_reveal_frame(r, 0.5f, f), PP_OK);
    EXPECT_EQ(diff(pixels(f), mid1), 0.0);
    pp_image_destroy(f);
    pp_art_reveal_destroy(r);
    pp_image_destroy(work);
    pp_image_destroy(blank);
  }
}

TEST(pencil_and_reveal_refuse_bad_input) {
  pp_image* img = portrait();
  pp_art_pencil_options bad{5, 0, 0.6f, 1, 1};
  EXPECT_EQ(pp_art_pencil(img, 0, 1, nullptr, nullptr, 0, 4, 1, &bad, nullptr), PP_ERROR_INVALID_ARGUMENT);
  bad = {0, 0, 2.0f, 1, 1};
  EXPECT_EQ(pp_art_pencil(img, 0, 1, nullptr, nullptr, 0, 4, 1, &bad, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_art_pencil(img, 20, 1, nullptr, nullptr, 0, 4, 1, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_art_blank(img, 2, 0, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image* small = nullptr;
  pp_image_create(40, 40, PP_FORMAT_RGBA8, &small);
  pp_art_reveal* r = nullptr;
  EXPECT_EQ(pp_art_reveal_create(img, small, 0, 0, nullptr, 1, nullptr, &r), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_TRUE(r == nullptr);
  const float inner[4] = {0.5f, 0.5f, 0.55f, 0.55f};
  EXPECT_EQ(pp_art_reveal_create(img, img, 0, 0, inner, 1, nullptr, &r), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_art_reveal_create(img, img, 3, 0, nullptr, 1, nullptr, &r), PP_ERROR_INVALID_ARGUMENT);
  pp_image_destroy(small);
  pp_image_destroy(img);
}
