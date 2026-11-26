// Masters (art.hpp). What a person would notice:
//  - every artist turns the photo into a painting (it changes, and keeps its
//    alpha), and no two artists paint alike;
//  - the same seed paints the same picture; another seed another picture;
//  - the artists look like themselves: Caravaggio's background falls into
//    black, Dürer engraves in black ink on white, Michelangelo draws in red
//    chalk, Hokusai prints in a few flat colours, Monet stays light;
//  - a masterpiece goes further than a study;
//  - bad input is refused; a cancelled painting says so.
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

constexpr int kW = 220, kH = 260;

// A portrait: a person (skin face, dark hair, red top) before a light green wall.
pp_image* portrait() {
  pp_image* img = nullptr;
  pp_image_create(kW, kH, PP_FORMAT_RGBA8, &img);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      unsigned char* p = px + static_cast<size_t>(y) * info.stride + x * 4;
      float r = 150 + 0.2f * static_cast<float>(x), g = 185, b = 150 + 0.1f * static_cast<float>(y);
      const float fx = (static_cast<float>(x) - 110) / 42, fy = (static_cast<float>(y) - 95) / 55;
      const float body = std::fabs(static_cast<float>(x) - 110) < 75 && y > 150 ? 1.0f : 0.0f;
      if (fx * fx + fy * fy < 1.4f && y < 70) r = 40, g = 28, b = 22;  // hair
      if (fx * fx + fy * fy < 1.0f) r = 205 - 0.3f * static_cast<float>(y - 60), g = 160, b = 130;  // face
      if (body > 0) r = 170, g = 40, b = 45;  // top
      if (std::fabs(static_cast<float>(x) - 95) < 5 && std::fabs(static_cast<float>(y) - 88) < 3) r = g = b = 30;  // eyes
      if (std::fabs(static_cast<float>(x) - 125) < 5 && std::fabs(static_cast<float>(y) - 88) < 3) r = g = b = 30;
      p[0] = static_cast<unsigned char>(std::clamp(r, 0.0f, 255.0f));
      p[1] = static_cast<unsigned char>(std::clamp(g, 0.0f, 255.0f));
      p[2] = static_cast<unsigned char>(std::clamp(b, 0.0f, 255.0f));
      p[3] = static_cast<unsigned char>(200 + (x % 50));
    }
  return img;
}

pp_image* personMask() {
  pp_image* m = nullptr;
  pp_image_create(kW / 2, kH / 2, PP_FORMAT_GRAY8, &m);
  pp_image_info info{};
  pp_image_get_info(m, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(m));
  for (int y = 0; y < kH / 2; ++y)
    for (int x = 0; x < kW / 2; ++x) {
      const float fx = (static_cast<float>(x) * 2 - 110) / 48, fy = (static_cast<float>(y) * 2 - 95) / 70;
      const bool body = std::fabs(static_cast<float>(x) * 2 - 110) < 75 && y * 2 > 150;
      px[static_cast<size_t>(y) * info.stride + x] = (fx * fx + fy * fy < 1.0f || body) ? 255 : 0;
    }
  return m;
}

std::vector<unsigned char> pixels(pp_image* img) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  const auto* px = static_cast<const unsigned char*>(pp_image_pixels(img));
  std::vector<unsigned char> out;
  for (int y = 0; y < info.height; ++y) out.insert(out.end(), px + static_cast<size_t>(y) * info.stride, px + static_cast<size_t>(y) * info.stride + info.width * 4);
  return out;
}

double meanDiff(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
  double d = 0;
  size_t n = 0;
  for (size_t i = 0; i < a.size(); i += 4, ++n)
    for (int k = 0; k < 3; ++k) d += std::fabs(static_cast<double>(a[i + k]) - b[i + k]);
  return d / static_cast<double>(n * 3);
}

std::vector<unsigned char> paint(int artist, int level = 1, uint32_t seed = 7, bool mask = true, const pp_art_controls* controls = nullptr) {
  pp_image* img = portrait();
  pp_image* m = mask ? personMask() : nullptr;
  const float face[4] = {68.0f / kW, 40.0f / kH, 84.0f / kW, 110.0f / kH};
  EXPECT_EQ(pp_art_paint(img, artist, level, m, face, 1, seed, controls, nullptr), PP_OK);
  auto out = pixels(img);
  pp_image_destroy(img);
  if (m) pp_image_destroy(m);
  return out;
}

// Mean of a channel over a box.
double meanIn(const std::vector<unsigned char>& px, int ch, int x0, int y0, int x1, int y1) {
  double s = 0;
  int n = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x, ++n) s += px[(static_cast<size_t>(y) * kW + x) * 4 + ch];
  return s / n;
}
double lumaIn(const std::vector<unsigned char>& px, int x0, int y0, int x1, int y1) {
  return 0.299 * meanIn(px, 0, x0, y0, x1, y1) + 0.587 * meanIn(px, 1, x0, y0, x1, y1) + 0.114 * meanIn(px, 2, x0, y0, x1, y1);
}

}  // namespace

TEST(art_every_artist_paints_and_no_two_alike) {
  pp_image* src = portrait();
  const auto original = pixels(src);
  pp_image_destroy(src);
  std::vector<std::vector<unsigned char>> all;
  for (int a = 0; a < PP_ART_ARTISTS; ++a) {
    auto out = paint(a);
    EXPECT_TRUE(meanDiff(out, original) > 6.0);
    for (size_t i = 3; i < out.size(); i += 4) EXPECT_EQ(out[i], original[i]);  // alpha kept
    for (const auto& other : all) EXPECT_TRUE(meanDiff(out, other) > 3.0);
    all.push_back(std::move(out));
  }
}

TEST(art_is_deterministic_per_seed) {
  EXPECT_TRUE(meanDiff(paint(17, 1, 5), paint(17, 1, 5)) == 0.0);
  EXPECT_TRUE(meanDiff(paint(17, 1, 5), paint(17, 1, 6)) > 1.0);
}

TEST(art_artists_look_like_themselves) {
  // Caravaggio: the wall behind falls into black; Monet keeps it light.
  const auto car = paint(5), mon = paint(12);
  EXPECT_TRUE(lumaIn(car, 0, 0, 30, 60) < 45);
  EXPECT_TRUE(lumaIn(mon, 0, 0, 30, 60) > 140);
  // Dürer: black ink on white paper, no colour.
  const auto dur = paint(3);
  double sat = 0;
  for (size_t i = 0; i < dur.size(); i += 4) {
    const int mx = std::max({dur[i], dur[i + 1], dur[i + 2]}), mn = std::min({dur[i], dur[i + 1], dur[i + 2]});
    sat += mx - mn;
  }
  EXPECT_TRUE(sat / (dur.size() / 4.0) < 25);
  // Michelangelo: red chalk — red over green over blue in the drawn face.
  const auto mic = paint(1);
  EXPECT_TRUE(meanIn(mic, 0, 80, 60, 140, 130) > meanIn(mic, 1, 80, 60, 140, 130) + 8);
  EXPECT_TRUE(meanIn(mic, 1, 80, 60, 140, 130) >= meanIn(mic, 2, 80, 60, 140, 130) - 2);
  // Hokusai: a print in a few flat colours.
  const auto hok = paint(10);
  std::set<int> colours;
  for (size_t i = 0; i < hok.size(); i += 4) colours.insert((hok[i] >> 4) << 8 | (hok[i + 1] >> 4) << 4 | (hok[i + 2] >> 4));
  const auto ren = paint(14);
  std::set<int> renColours;
  for (size_t i = 0; i < ren.size(); i += 4) renColours.insert((ren[i] >> 4) << 8 | (ren[i + 1] >> 4) << 4 | (ren[i + 2] >> 4));
  EXPECT_TRUE(colours.size() < renColours.size());
}

TEST(art_masterpiece_goes_further_than_a_study) {
  pp_image* src = portrait();
  const auto original = pixels(src);
  pp_image_destroy(src);
  // Closer to the sitter in the face: finer brushes, more layers.
  const auto study = paint(6, 0), master = paint(6, 2);
  const double dStudy = meanDiff(study, original), dMaster = meanDiff(master, original);
  EXPECT_TRUE(meanDiff(study, master) > 1.0);
  EXPECT_TRUE(dStudy > 0 && dMaster > 0);
}

TEST(art_works_without_a_mask_or_faces) {
  pp_image* img = portrait();
  EXPECT_EQ(pp_art_paint(img, 19, 1, nullptr, nullptr, 0, 3, nullptr, nullptr), PP_OK);
  pp_image_destroy(img);
}

TEST(art_refuses_bad_input_and_honours_cancel) {
  pp_image* img = portrait();
  EXPECT_EQ(pp_art_paint(nullptr, 0, 1, nullptr, nullptr, 0, 1, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_art_paint(img, PP_ART_ARTISTS, 1, nullptr, nullptr, 0, 1, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_art_paint(img, 0, 3, nullptr, nullptr, 0, 1, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_art_paint(img, 0, 1, nullptr, nullptr, 1, 1, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image* wrong = nullptr;
  pp_image_create(20, 20, PP_FORMAT_RGBA8, &wrong);
  EXPECT_EQ(pp_art_paint(img, 0, 1, wrong, nullptr, 0, 1, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_cancel_token* t = pp_cancel_token_create();
  pp_cancel_token_cancel(t);
  EXPECT_EQ(pp_art_paint(img, 6, 1, nullptr, nullptr, 0, 1, nullptr, t), PP_ERROR_CANCELLED);
  pp_cancel_token_destroy(t);
  pp_image_destroy(wrong);
  pp_image_destroy(img);
}

// The person's controls: the defaults are the artist's own way; each control
// changes what it says; out-of-range values are refused.
TEST(art_controls_change_what_they_say) {
  const pp_art_controls same{1, 1, 0, 0.5f, 1, 1, 1};
  const auto base = paint(6);
  EXPECT_EQ(meanDiff(base, paint(6, 1, 7, true, &same)), 0.0);

  // Bolder brushes paint a different picture.
  pp_art_controls k = same;
  k.brush = 1.6f;
  EXPECT_TRUE((meanDiff(base, paint(6, 1, 7, true, &k))) > (1.0));

  // The photo's own colours: the red top stays red under Hokusai's palette, and closer to the photo.
  pp_art_controls own = same;
  own.palette = 0;
  const auto h = paint(10), hOwn = paint(10, 1, 7, true, &own);
  const double redArtist = meanIn(h, 0, 60, 180, 160, 250) - meanIn(h, 1, 60, 180, 160, 250);
  const double redOwn = meanIn(hOwn, 0, 60, 180, 160, 250) - meanIn(hOwn, 1, 60, 180, 160, 250);
  EXPECT_TRUE((redOwn) > (redArtist - 1.0));
  EXPECT_TRUE((redOwn) > (60.0));

  // Vivid and muted.
  pp_art_controls vivid = same, muted = same;
  vivid.vivid = 1, muted.vivid = -1;
  auto chroma = [](const std::vector<unsigned char>& px) {
    double s = 0;
    for (size_t i = 0; i < px.size(); i += 4) s += std::max({px[i], px[i + 1], px[i + 2]}) - std::min({px[i], px[i + 1], px[i + 2]});
    return s / static_cast<double>(px.size() / 4);
  };
  EXPECT_TRUE((chroma(paint(12, 1, 7, true, &vivid))) > (chroma(paint(12)) + 2));
  EXPECT_TRUE((chroma(paint(12, 1, 7, true, &muted))) < (chroma(paint(12)) - 2));

  // The photo's own background: Caravaggio no longer plunges the wall into black.
  pp_art_controls keep = same;
  keep.background = 0;
  EXPECT_TRUE((lumaIn(paint(5, 1, 7, true, &keep), 0, 0, 40, 60)) > (lumaIn(paint(5), 0, 0, 40, 60) + 25));

  // Age: no varnish, no cracks — a fresher, less yellow Leonardo.
  pp_art_controls fresh = same;
  fresh.age = 0;
  const auto leo = paint(0), leoFresh = paint(0, 1, 7, true, &fresh);
  EXPECT_TRUE((meanIn(leoFresh, 2, 0, 0, kW, kH) - meanIn(leoFresh, 0, 0, 0, kW, kH)) > (meanIn(leo, 2, 0, 0, kW, kH) - meanIn(leo, 0, 0, 0, kW, kH)));

  // Texture and likeness change the picture.
  pp_art_controls smooth = same, close = same;
  smooth.texture = 0, close.likeness = 1;
  EXPECT_TRUE((meanDiff(base, paint(6, 1, 7, true, &smooth))) > (0.3));
  EXPECT_TRUE((meanDiff(base, paint(6, 1, 7, true, &close))) > (0.1));

  // Refused when out of range.
  pp_image* img = portrait();
  pp_art_controls bad = same;
  bad.brush = 3;
  EXPECT_EQ(pp_art_paint(img, 0, 1, nullptr, nullptr, 0, 1, &bad, nullptr), PP_ERROR_INVALID_ARGUMENT);
  bad = same;
  bad.vivid = NAN;
  EXPECT_EQ(pp_art_paint(img, 0, 1, nullptr, nullptr, 0, 1, &bad, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image_destroy(img);
}
