// Toon (toon.hpp). What a person would notice:
//  - the picture becomes flat colour areas (far fewer distinct colours), with
//    dark ink lines on its edges; manga is black and white;
//  - anime eyes are bigger and get catchlights; skin goes smooth;
//  - a painted sky replaces the background but never the person;
//  - every style differs, a seed repeats; bad input is refused.
#include <algorithm>
#include <array>
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

// ---- Toon with contours (pp_toon_render_ex): the acceptance checks on a synthetic portrait.
//  - the face is drawn from its contour points: eyes have whites and coloured
//    irises, never a black blob; brows and lips keep their colour;
//  - skin keeps its hue (no grey or purple shift), dark skin in low light too;
//  - the outline sits on the person's edge even when the mask is coarse and off;
//  - halftone dots stay off lit skin; colour noise never becomes blotches;
//  - Strength blends with the photo; the six styles are clearly different.
namespace {

constexpr int kW2 = 240, kH2 = 300;
constexpr float kCx = 120, kCy = 120, kRx = 52, kRy = 66;  // the face

struct Photo {
  std::vector<unsigned char> px;  // RGBA
};

// A face (skin of the given colour) with dark eye sockets (where edge detectors make blobs), brows,
// lips and a shine on the chin, over a cluttered, noisy wall; scaled by `light` (low light < 1).
Photo face(float r, float g, float b, float light, int noise) {
  Photo p;
  p.px.resize(static_cast<size_t>(kW2) * kH2 * 4);
  for (int y = 0; y < kH2; ++y)
    for (int x = 0; x < kW2; ++x) {
      const int n = noise ? static_cast<int>(((static_cast<unsigned>(x) * 73856093U) ^ (static_cast<unsigned>(y) * 19349663U)) % static_cast<unsigned>(2 * noise + 1)) - noise : 0;
      // A cluttered wall in blue-greens (it differs from the skin, as a real scene does, or no edge can be found).
      float c[3] = {40.0f + static_cast<float>((x / 12 + y / 9) % 3) * 22, 80.0f + static_cast<float>((x / 7) % 4) * 20, 90.0f + static_cast<float>((y / 11) % 3) * 30};
      const float dx = (static_cast<float>(x) - kCx) / kRx, dy = (static_cast<float>(y) - kCy) / kRy;
      if (dx * dx + dy * dy < 1) {
        const float shade = 1 - 0.25f * std::max(0.0f, (static_cast<float>(x) - kCx) / kRx);
        c[0] = r * shade, c[1] = g * shade, c[2] = b * shade;
        for (int e = -1; e <= 1; e += 2) {  // dark eyes
          const float ex = (static_cast<float>(x) - (kCx + 22.0f * static_cast<float>(e))) / 11, ey = (static_cast<float>(y) - 105) / 5;
          if (ex * ex + ey * ey < 1) c[0] = 30, c[1] = 22, c[2] = 20;
          const float bx = (static_cast<float>(x) - (kCx + 22.0f * static_cast<float>(e))) / 13, by = (static_cast<float>(y) - 92) / 2.5f;
          if (bx * bx + by * by < 1) c[0] = 45, c[1] = 30, c[2] = 25;  // brows
        }
        const float lx = (static_cast<float>(x) - kCx) / 17, ly = (static_cast<float>(y) - 152) / 6;
        if (lx * lx + ly * ly < 1) c[0] = r * 0.8f + 30, c[1] = g * 0.6f, c[2] = b * 0.7f;  // lips
        const float sx = (static_cast<float>(x) - kCx) / 8, sy = (static_cast<float>(y) - 172) / 4;
        if (sx * sx + sy * sy < 1) c[0] = c[1] = c[2] = 250;  // shine on the chin
      }
      if (y > 200 && x > 50 && x < 190) c[0] = 50, c[1] = 70, c[2] = 150;  // shirt
      unsigned char* q = &p.px[(static_cast<size_t>(y) * kW2 + x) * 4];
      for (int k = 0; k < 3; ++k) q[k] = static_cast<unsigned char>(std::clamp(static_cast<int>(c[k] * light) + n * (k == 1 ? 1 : -1), 0, 255));
      q[3] = 255;
    }
  return p;
}

// The face's contour points in ML Kit's order (PP_TOON_CONTOUR_POINTS pairs, normalised).
std::vector<float> contours() {
  std::vector<float> c;
  auto add = [&](float x, float y) { c.push_back(x / kW2), c.push_back(y / kH2); };
  auto ellipse = [&](float cx, float cy, float rx, float ry, int n) {
    for (int i = 0; i < n; ++i) {
      const float a = static_cast<float>(i) / static_cast<float>(n) * 6.2831853f;
      add(cx + rx * std::cos(a), cy + ry * std::sin(a));
    }
  };
  auto arc = [&](float x0, float x1, float y, float bend, int n) {
    for (int i = 0; i < n; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(n - 1);
      add(x0 + (x1 - x0) * t, y - bend * std::sin(t * 3.14159f));
    }
  };
  ellipse(kCx, kCy, kRx, kRy, 36);                                       // oval
  arc(kCx + 9, kCx + 35, 90, 3, 5), arc(kCx + 9, kCx + 35, 94, 2, 5);    // left brow top, bottom
  arc(kCx - 35, kCx - 9, 90, 3, 5), arc(kCx - 35, kCx - 9, 94, 2, 5);    // right brow
  ellipse(kCx + 22, 105, 11, 5, 16), ellipse(kCx - 22, 105, 11, 5, 16);  // eyes
  arc(kCx - 17, kCx + 17, 148, 3, 11), arc(kCx - 14, kCx + 14, 152, 0, 9);  // upper lip top, bottom
  arc(kCx - 14, kCx + 14, 152, 0, 9), arc(kCx - 14, kCx + 14, 158, -3, 9);  // lower lip top, bottom
  add(kCx, 105), add(kCx, 132);                                          // nose bridge
  add(kCx - 8, 134), add(kCx, 137), add(kCx + 8, 134);                   // nose bottom
  return c;
}

const float kFace2[12] = {(kCx - kRx) / kW2, (kCy - kRy) / kH2, 2 * kRx / kW2, 2 * kRy / kH2, (kCx - 14) / kW2, 152.0f / kH2, (kCx + 14) / kW2, 152.0f / kH2,
                          (kCx - 22) / kW2, 105.0f / kH2, (kCx + 22) / kW2, 105.0f / kH2};

// The person mask: the head and the shirt; `scale` coarser and moved `shift` px right (as a real mask can be).
pp_image* mask2(int scale, int shift) {
  pp_image* m = nullptr;
  pp_image_create(kW2 / scale, kH2 / scale, PP_FORMAT_GRAY8, &m);
  pp_image_info info{};
  pp_image_get_info(m, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(m));
  for (int y = 0; y < kH2 / scale; ++y)
    for (int x = 0; x < kW2 / scale; ++x) {
      const float X = static_cast<float>(x * scale + scale / 2 - shift), Y = static_cast<float>(y * scale + scale / 2);
      const float dx = (X - kCx) / kRx, dy = (Y - kCy) / kRy;
      px[static_cast<size_t>(y) * info.stride + x] = (dx * dx + dy * dy < 1 || (Y > 200 && X > 50 && X < 190)) ? 255 : 0;
    }
  return m;
}

std::vector<unsigned char> render2(const Photo& photo, int style, float strength = 1, bool withContours = true, int maskScale = 1, int maskShift = 0,
                                   float lines = 0.5f) {
  pp_image* img = nullptr;
  pp_image_create(kW2, kH2, PP_FORMAT_RGBA8, &img);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < kH2; ++y) std::copy_n(&photo.px[static_cast<size_t>(y) * kW2 * 4], kW2 * 4, px + static_cast<size_t>(y) * info.stride);
  pp_image* m = mask2(maskScale, maskShift);
  const std::vector<float> c = contours();
  const pp_toon_settings s{{style, 0, lines, 3, 0.3f, 0.5f, 0.4f, 0.7f, 0.3f}, strength};
  EXPECT_EQ(pp_toon_render_ex(img, &s, m, kFace2, 1, 12, withContours ? c.data() : nullptr, withContours ? 1 : 0, 7, nullptr), PP_OK);
  std::vector<unsigned char> out(static_cast<size_t>(kW2) * kH2 * 4);
  for (int y = 0; y < kH2; ++y) std::copy_n(px + static_cast<size_t>(y) * info.stride, kW2 * 4, &out[static_cast<size_t>(y) * kW2 * 4]);
  pp_image_destroy(img);
  pp_image_destroy(m);
  return out;
}

void toLab(const unsigned char* p, double lab[3]) {
  double c[3];
  for (int k = 0; k < 3; ++k) {
    const double v = p[k] / 255.0;
    c[k] = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  }
  auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116; };
  const double X = f((0.4124 * c[0] + 0.3576 * c[1] + 0.1805 * c[2]) / 0.95047), Y = f(0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]),
               Z = f((0.0193 * c[0] + 0.1192 * c[1] + 0.9505 * c[2]) / 1.08883);
  lab[0] = 116 * Y - 16, lab[1] = 500 * (X - Y), lab[2] = 200 * (Y - Z);
}

// The median skin colour (Lab) on the cheeks.
std::array<double, 3> cheeks(const std::vector<unsigned char>& p) {
  std::vector<double> L, a, b;
  for (int y = 112; y < 140; ++y)
    for (int x : {static_cast<int>(kCx) - 34, static_cast<int>(kCx) - 30, static_cast<int>(kCx) + 30, static_cast<int>(kCx) + 34}) {
      double l[3];
      toLab(&p[(static_cast<size_t>(y) * kW2 + x) * 4], l);
      L.push_back(l[0]), a.push_back(l[1]), b.push_back(l[2]);
    }
  auto med = [](std::vector<double> v) {
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
  };
  return {med(L), med(a), med(b)};
}

double lum2(const std::vector<unsigned char>& p, int x, int y) {
  const unsigned char* q = &p[(static_cast<size_t>(y) * kW2 + x) * 4];
  return 0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2];
}

}  // namespace

TEST(toon_draws_the_eyes_and_lips_from_contours_never_black_blobs) {
  const Photo photo = face(196, 150, 120, 1.0f, 3);
  for (int style : {0, 2, 3, 4, 5}) {
    const auto out = render2(photo, style);
    for (int e = -1; e <= 1; e += 2) {
      // Inside the eye opening: almost nothing near-black (the photo's dark socket was all black there),
      // and an eye white.
      int black = 0, white = 0, n = 0;
      const float ecx = kCx + 22.0f * static_cast<float>(e);
      for (int y = 101; y <= 109; ++y)
        for (int x = static_cast<int>(ecx) - 10; x <= static_cast<int>(ecx) + 10; ++x) {
          const float dx = (static_cast<float>(x) - ecx) / 10, dy = (static_cast<float>(y) - 105) / 4.0f;
          if (dx * dx + dy * dy >= 1) continue;
          ++n;
          if (lum2(out, x, y) < 30) ++black;
          if (lum2(out, x, y) > 180) ++white;
        }
      EXPECT_TRUE(black * 10 < n);
      EXPECT_TRUE(white > 0);
    }
    // The brows are kept (darker than the forehead), and the lips are redder than the skin.
    EXPECT_TRUE(lum2(out, static_cast<int>(kCx) + 22, 92) < lum2(out, static_cast<int>(kCx) + 22, 80) - 25);
    double lip[3], skin[3];
    toLab(&out[(static_cast<size_t>(154) * kW2 + static_cast<int>(kCx) + 6) * 4], lip);
    toLab(&out[(static_cast<size_t>(125) * kW2 + static_cast<int>(kCx) - 32) * 4], skin);
    EXPECT_TRUE(lip[1] > skin[1] + 3);
    // No white shine patch on the chin: it is drawn in the skin's own light.
    EXPECT_TRUE(lum2(out, static_cast<int>(kCx), 172) < 235);
  }
}

TEST(toon_keeps_the_skins_hue_dark_skin_in_low_light_too) {
  for (const auto& skin : {std::array<float, 3>{196, 150, 120}, std::array<float, 3>{120, 75, 52}}) {
    for (float light : {1.0f, 0.45f}) {
      const Photo photo = face(skin[0], skin[1], skin[2], light, light < 1 ? 6 : 2);
      const auto in = cheeks(photo.px);
      const double hueIn = std::atan2(in[2], in[1]) * 57.29578, chromaIn = std::hypot(in[1], in[2]);
      for (int style : {0, 2, 3, 4, 5}) {
        const auto out = cheeks(render2(photo, style));
        const double hueOut = std::atan2(out[2], out[1]) * 57.29578, chromaOut = std::hypot(out[1], out[2]);
        EXPECT_TRUE(std::fabs(hueOut - hueIn) < 12);   // no grey or purple shift
        EXPECT_TRUE(chromaOut > 0.75 * chromaIn);      // never ashy
        EXPECT_TRUE(hueOut > 15 && hueOut < 80);       // still a skin hue
      }
    }
  }
}

TEST(toon_outline_sits_on_the_persons_edge_even_with_a_coarse_offset_mask) {
  const Photo photo = face(196, 150, 120, 1.0f, 2);
  // A mask at a quarter of the size and 4 px off to the right: the outline still lands on the face's real edge.
  const auto out = render2(photo, 3, 1, true, 4, 4, 0.8f);
  for (int y : {110, 120, 130}) {
    const float half = kRx * std::sqrt(std::max(0.0f, 1 - ((static_cast<float>(y) - kCy) / kRy) * ((static_cast<float>(y) - kCy) / kRy)));
    for (int side = -1; side <= 1; side += 2) {
      const int edge = static_cast<int>(std::lround(kCx + static_cast<float>(side) * half));
      int darkest = edge;
      for (int x = edge - 9; x <= edge + 9; ++x)
        if (lum2(out, x, y) < lum2(out, darkest, y)) darkest = x;
      EXPECT_TRUE(std::abs(darkest - edge) <= 3);
      EXPECT_TRUE(lum2(out, darkest, y) < 80);
    }
  }
}

TEST(toon_dots_stay_off_lit_skin_and_noise_never_becomes_blotches) {
  const Photo noisy = face(196, 150, 120, 0.5f, 14);
  const auto comic = render2(noisy, 2);
  // On the lit cheek: flat (no Ben-Day dots, no noise).
  double step = 0;
  int n = 0;
  for (int y = 112; y < 132; ++y)
    for (int x = static_cast<int>(kCx) - 36; x < static_cast<int>(kCx) - 24; ++x, ++n) step += std::fabs(lum2(comic, x, y) - lum2(comic, x + 1, y));
  EXPECT_TRUE(step / n < 2.5);
  // The noisy photo's skin varies far more than that.
  double photoStep = 0;
  for (int y = 112; y < 132; ++y)
    for (int x = static_cast<int>(kCx) - 36; x < static_cast<int>(kCx) - 24; ++x) photoStep += std::fabs(lum2(noisy.px, x, y) - lum2(noisy.px, x + 1, y));
  EXPECT_TRUE(photoStep / n > 4 * step / n);
}

TEST(toon_strength_blends_with_the_photo) {
  const Photo photo = face(196, 150, 120, 1.0f, 3);
  EXPECT_TRUE(render2(photo, 0, 0.0f) == photo.px);  // the photo itself
  const auto full = render2(photo, 0, 1.0f), half = render2(photo, 0, 0.5f);
  int off = 0;
  for (size_t i = 0; i < half.size(); ++i) {
    if (i % 4 == 3) continue;
    const int lo = std::min(photo.px[i], full[i]) - 1, hi = std::max(photo.px[i], full[i]) + 1;
    if (half[i] < lo || half[i] > hi) ++off;
  }
  EXPECT_EQ(off, 0);
}

TEST(toon_styles_are_clearly_different) {
  const Photo photo = face(150, 100, 75, 0.8f, 4);
  std::vector<std::vector<unsigned char>> s;
  for (int style = 0; style < 6; ++style) s.push_back(render2(photo, style));
  for (size_t a = 0; a < s.size(); ++a)
    for (size_t b = a + 1; b < s.size(); ++b) {
      double d = 0;
      for (size_t i = 0; i < s[a].size(); ++i)
        if (i % 4 != 3) d += std::fabs(static_cast<double>(s[a][i]) - s[b][i]);
      EXPECT_TRUE(d / (s[a].size() * 0.75) > 6);
    }
}

TEST(toon_ex_refuses_bad_input) {
  pp_image* img = portrait();
  pp_toon_settings s{{0, 0, 0.5f, 3, 0.3f, 0.5f, 0.4f, 0.6f, 0.3f}, 1.5f};
  EXPECT_EQ(pp_toon_render_ex(img, &s, nullptr, nullptr, 0, 4, nullptr, 0, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);  // strength
  s.strength = 1;
  EXPECT_EQ(pp_toon_render_ex(img, &s, nullptr, nullptr, 0, 4, nullptr, 2, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);  // contours missing
  EXPECT_EQ(pp_toon_render_ex(img, nullptr, nullptr, nullptr, 0, 4, nullptr, 0, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
  // Contour points that are unknown (negative) are fine: that face uses its box.
  std::vector<float> unknown(PP_TOON_CONTOUR_POINTS * 2, -1.0f);
  EXPECT_EQ(pp_toon_render_ex(img, &s, nullptr, kFace, 1, 12, unknown.data(), 1, 1, nullptr), PP_OK);
  pp_image_destroy(img);
}
