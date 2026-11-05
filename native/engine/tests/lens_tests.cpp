// Lens quality ("Transform") tests.
//
// The scene is a colour dead-leaves chart (random overlapping discs with a
// 1/r³ size distribution), the texture camera makers use to measure how much
// fine detail a camera keeps (ISO 19567-2), photographed through two
// simulated lenses:
//  - a basic lens: blur growing towards the corners, red and blue imaged at
//    a slightly different scale than green (colour fringes), dark corners and
//    small-sensor noise;
//  - a good lens: crisp, no fringes, even light, little noise.
// Checks: the analysis tells them apart (and is not fooled by an
// out-of-focus background); Transform removes the fringes and the dark
// corners and recovers more detail than Clarity alone; a photo from a good
// lens is barely touched; styled colours can be kept; bad input, cancel and
// memory behave.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

struct Rgbf {
  float r, g, b;
};

float srgbToLinear(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
float linearToSrgb(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// A colour image as three float planes (sRGB-encoded 0..1).
struct Img {
  int w = 0, h = 0;
  std::vector<float> r, g, b;
  Img(int w_, int h_) : w(w_), h(h_), r(static_cast<size_t>(w_) * h_), g(r.size()), b(r.size()) {}
  std::vector<float>& ch(int c) { return c == 0 ? r : c == 1 ? g : b; }
  const std::vector<float>& ch(int c) const { return c == 0 ? r : c == 1 ? g : b; }
};

Img deadLeaves(int w, int h, unsigned seed) {
  Img im(w, h);
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> U(0.0f, 1.0f);
  std::vector<float> cover(im.r.size(), 0.0f);  // painted front to back
  const float rmin = 2.0f, rmax = static_cast<float>(std::min(w, h)) / 6.0f;
  for (int n = 0; n < 60000; ++n) {
    // Radius with density ∝ 1/r³ on [rmin, rmax] (inverse CDF).
    const float u = U(rng);
    const float rad = 1.0f / std::sqrt(1.0f / (rmin * rmin) - u * (1.0f / (rmin * rmin) - 1.0f / (rmax * rmax)));
    const float cx = U(rng) * static_cast<float>(w), cy = U(rng) * static_cast<float>(h);
    // Natural colour statistics: mostly brightness, with a moderate tint.
    const float l = 0.1f + 0.8f * U(rng);
    const Rgbf c{std::clamp(l + 0.18f * (U(rng) - 0.5f), 0.02f, 0.98f), std::clamp(l + 0.12f * (U(rng) - 0.5f), 0.02f, 0.98f),
                 std::clamp(l + 0.18f * (U(rng) - 0.5f), 0.02f, 0.98f)};
    const int x0 = std::max(0, static_cast<int>(cx - rad - 1)), x1 = std::min(w - 1, static_cast<int>(cx + rad + 1));
    const int y0 = std::max(0, static_cast<int>(cy - rad - 1)), y1 = std::min(h - 1, static_cast<int>(cy + rad + 1));
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        const size_t i = static_cast<size_t>(y) * w + x;
        if (cover[i] >= 1.0f) continue;
        const float d = std::hypot(static_cast<float>(x) + 0.5f - cx, static_cast<float>(y) + 0.5f - cy);
        const float a = std::clamp(rad - d + 0.5f, 0.0f, 1.0f) * (1.0f - cover[i]);  // anti-aliased
        if (a <= 0.0f) continue;
        im.r[i] += a * c.r;
        im.g[i] += a * c.g;
        im.b[i] += a * c.b;
        cover[i] += a;
      }
  }
  for (size_t i = 0; i < cover.size(); ++i) {
    const float rest = std::max(0.0f, 1.0f - cover[i]);
    im.r[i] += rest * 0.5f;
    im.g[i] += rest * 0.5f;
    im.b[i] += rest * 0.5f;
  }
  return im;
}

std::vector<float> gaussian(const std::vector<float>& src, int w, int h, float sigma) {
  if (sigma <= 0.05f) return src;
  const int rad = static_cast<int>(std::ceil(3.0f * sigma));
  std::vector<float> k(static_cast<size_t>(2 * rad + 1));
  float sum = 0.0f;
  for (int i = -rad; i <= rad; ++i) sum += k[static_cast<size_t>(i + rad)] = std::exp(-0.5f * i * i / (sigma * sigma));
  for (float& v : k) v /= sum;
  std::vector<float> tmp(src.size()), out(src.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float a = 0.0f;
      for (int i = -rad; i <= rad; ++i) a += k[static_cast<size_t>(i + rad)] * src[static_cast<size_t>(y) * w + std::clamp(x + i, 0, w - 1)];
      tmp[static_cast<size_t>(y) * w + x] = a;
    }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float a = 0.0f;
      for (int i = -rad; i <= rad; ++i) a += k[static_cast<size_t>(i + rad)] * tmp[static_cast<size_t>(std::clamp(y + i, 0, h - 1)) * w + x];
      out[static_cast<size_t>(y) * w + x] = a;
    }
  return out;
}

float bilinear(const std::vector<float>& p, int w, int h, float x, float y) {
  x = std::clamp(x, 0.0f, static_cast<float>(w - 1));
  y = std::clamp(y, 0.0f, static_cast<float>(h - 1));
  const int x0 = std::min(static_cast<int>(x), w - 2), y0 = std::min(static_cast<int>(y), h - 2);
  const float tx = x - static_cast<float>(x0), ty = y - static_cast<float>(y0);
  auto at = [&](int xx, int yy) { return p[static_cast<size_t>(yy) * w + xx]; };
  const float top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * tx;
  const float bot = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * tx;
  return top + (bot - top) * ty;
}

struct Lens {
  float centreBlur, cornerBlur;  // px
  float fringePx;                // red outwards / blue inwards at the corners
  float darkCorners;             // light lost at the corners
  float noise;
};

// Phones correct darker corners in their own processing, so the basic lens
// has none; the vignetted one checks that Transform leaves them alone.
const Lens kBasic{1.2f, 2.0f, 2.0f, 0.0f, 0.010f};
const Lens kBasicVignetted{1.2f, 2.0f, 2.0f, 0.35f, 0.010f};
const Lens kGood{0.45f, 0.5f, 0.0f, 0.0f, 0.003f};

// Photographs `scene` through `lens`, as an RGBA8 pp_image.
pp_image* shoot(const Img& scene, const Lens& lens, unsigned seed = 3) {
  const int w = scene.w, h = scene.h;
  const float cx = 0.5f * static_cast<float>(w - 1), cy = 0.5f * static_cast<float>(h - 1);
  const float half = 0.5f * std::hypot(static_cast<float>(w), static_cast<float>(h));
  std::mt19937 rng(seed);
  std::normal_distribution<float> N(0.0f, 1.0f);
  Img out(w, h);
  for (int c = 0; c < 3; ++c) {
    // Blur in linear light, growing with radius² like field curvature.
    std::vector<float> lin(scene.ch(c).size());
    for (size_t i = 0; i < lin.size(); ++i) lin[i] = srgbToLinear(scene.ch(c)[i]);
    const auto a = gaussian(lin, w, h, lens.centreBlur), b = gaussian(lin, w, h, lens.cornerBlur);
    const float k = c == 0 ? lens.fringePx / half : c == 2 ? -lens.fringePx / half : 0.0f;
    const float vig = -std::log(1.0f - lens.darkCorners);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy;
        const float r2 = (dx * dx + dy * dy) / (half * half);
        // Red imaged larger (sample nearer the centre), blue smaller.
        const float sx = cx + dx / (1.0f + k), sy = cy + dy / (1.0f + k);
        const float v = bilinear(a, w, h, sx, sy) * (1.0f - r2) + bilinear(b, w, h, sx, sy) * r2;
        out.ch(c)[static_cast<size_t>(y) * w + x] = linearToSrgb(v * std::exp(-vig * r2));
      }
  }
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<uint8_t*>(pp_image_pixels(img));
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      uint8_t* p = px + static_cast<size_t>(y) * info.stride + static_cast<size_t>(x) * 4;
      const float n = lens.noise * N(rng);
      for (int c = 0; c < 3; ++c) {
        const float v = out.ch(c)[static_cast<size_t>(y) * w + x] + n + 0.4f * lens.noise * N(rng);
        p[c] = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
      }
      p[3] = 255;
    }
  return img;
}

pp_lens_report analyse(const pp_image* img) {
  pp_lens_report r{};
  EXPECT_EQ(pp_lens_analyze(img, nullptr, &r), PP_OK);
  return r;
}

pp_image* blank(int w, int h) {
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
  return img;
}

// Fidelity tests switch the finishing touches off (auto tone, local
// contrast), so only the lens restoration is measured against the truth.
pp_transform_params finishOnly(bool finish) {
  pp_transform_params p{};
  pp_transform_default_params(&p);
  p.enrich = finish ? 1 : 0;
  if (!finish) p.detail = 0.0f;
  return p;
}

// Mean squared error vs the scene seen through a perfect lens, by region.
struct Err {
  double centre, corners, all;
};
Err errorVs(const pp_image* img, const Img& truth) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  const auto* px = static_cast<const uint8_t*>(pp_image_pixels(const_cast<pp_image*>(img)));
  const float cx = 0.5f * static_cast<float>(truth.w - 1), cy = 0.5f * static_cast<float>(truth.h - 1);
  const float half = 0.5f * std::hypot(static_cast<float>(truth.w), static_cast<float>(truth.h));
  double sc = 0, sk = 0, sa = 0;
  size_t nc = 0, nk = 0, na = 0;
  for (int y = 8; y < truth.h - 8; ++y)
    for (int x = 8; x < truth.w - 8; ++x) {
      const uint8_t* p = px + static_cast<size_t>(y) * info.stride + static_cast<size_t>(x) * 4;
      double e = 0;
      for (int c = 0; c < 3; ++c) {
        const double d = p[c] / 255.0 - truth.ch(c)[static_cast<size_t>(y) * truth.w + x];
        e += d * d;
      }
      const float r = std::hypot(static_cast<float>(x) - cx, static_cast<float>(y) - cy) / half;
      sa += e;
      ++na;
      if (r < 0.35f) {
        sc += e;
        ++nc;
      } else if (r > 0.75f) {
        sk += e;
        ++nk;
      }
    }
  return {sc / (3.0 * nc), sk / (3.0 * nk), sa / (3.0 * na)};
}
double psnr(double mse) { return 10.0 * std::log10(1.0 / std::max(mse, 1e-12)); }

uint64_t liveBytes() {
  pp_memory_stats s{};
  pp_memory_get_stats(&s);
  return s.current_bytes;
}

const Img& chart() {
  static const Img scene = deadLeaves(1200, 900, 11);
  return scene;
}

}  // namespace

TEST(lens_analysis_tells_a_basic_lens_from_a_good_one) {
  pp_image* basic = shoot(chart(), kBasic);
  pp_image* good = shoot(chart(), kGood);
  const pp_lens_report b = analyse(basic), g = analyse(good);
  std::printf("    basic: detail %.2f px, corners %.2f px, fringes %.2f px, noise %.4f, grade %.2f (%d regions)\n",
              b.detail_blur, b.corner_blur, b.fringe_px, b.noise, b.grade, b.measured_tiles);
  std::printf("    good:  detail %.2f px, corners %.2f px, fringes %.2f px, noise %.4f, grade %.2f\n", g.detail_blur,
              g.corner_blur, g.fringe_px, g.noise, g.grade);
  EXPECT_TRUE(b.reliable == 1 && g.reliable == 1);
  EXPECT_TRUE(std::fabs(b.fringe_px - kBasic.fringePx) < 0.5f);     // fringe width measured
  EXPECT_TRUE(g.fringe_px < 0.5f);
  EXPECT_TRUE(b.detail_blur > g.detail_blur + 0.4f);  // softer detail
  EXPECT_TRUE(b.corner_blur > b.detail_blur + 0.2f);  // and softer corners
  EXPECT_TRUE(b.grade < 0.55f);
  EXPECT_TRUE(g.grade > 0.75f);
  pp_image_destroy(basic);
  pp_image_destroy(good);
}

TEST(lens_analysis_is_not_fooled_by_a_soft_background) {
  // A good lens, but the outer part of the frame is out of focus (a portrait
  // with a blurred background): the lens itself must still read as sharp.
  Img scene = chart();
  const int w = scene.w, h = scene.h;
  for (int c = 0; c < 3; ++c) {
    const auto soft = gaussian(scene.ch(c), w, h, 3.0f);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        if (x < w / 5 || x > 4 * w / 5) scene.ch(c)[static_cast<size_t>(y) * w + x] = soft[static_cast<size_t>(y) * w + x];
  }
  pp_image* img = shoot(scene, kGood);
  const pp_lens_report r = analyse(img);
  std::printf("    detail %.2f px (corners %.2f px, scene blur there), grade %.2f\n", r.detail_blur, r.corner_blur, r.grade);
  EXPECT_TRUE(r.detail_blur < 0.8f);
  pp_image_destroy(img);
}

TEST(transform_makes_a_basic_lens_look_like_a_good_one) {
  pp_image* basic = shoot(chart(), kBasic);
  pp_image* good = shoot(chart(), kGood);
  // Clarity alone, with the same finish, for comparison.
  pp_enhance_params ep{};
  pp_enhance_default_params(&ep);
  ep.auto_tone = 0;
  ep.vibrance = 0.0f;
  ep.detail = 0.0f;
  pp_image* clarity = blank(chart().w, chart().h);
  pp_enhance_report er{};
  EXPECT_EQ(pp_enhance_into(&basic, 1, &ep, nullptr, clarity, &er), PP_OK);
  pp_image* out = blank(chart().w, chart().h);
  const pp_transform_params p = finishOnly(false);
  pp_transform_report r{};
  EXPECT_EQ(pp_transform(basic, out, &p, nullptr, &r), PP_OK);

  const Err eb = errorVs(basic, chart()), ec = errorVs(clarity, chart()), et = errorVs(out, chart()), eg = errorVs(good, chart());
  const pp_lens_report after = analyse(out);
  std::printf("    fixed: fringes %.2f px, deblur %.2f px (+%.2f px corners); Clarity alone: %.2f px\n", r.fringe_fixed_px,
              r.deblur_centre, r.deblur_corner, er.deblur_sigma);
  std::printf("    PSNR centre/corners/all — basic %.1f/%.1f/%.1f, Clarity %.1f/%.1f/%.1f, Transform %.1f/%.1f/%.1f, good lens %.1f/%.1f/%.1f dB\n",
              psnr(eb.centre), psnr(eb.corners), psnr(eb.all), psnr(ec.centre), psnr(ec.corners), psnr(ec.all),
              psnr(et.centre), psnr(et.corners), psnr(et.all), psnr(eg.centre), psnr(eg.corners), psnr(eg.all));
  std::printf("    after: fringes %.2f px, detail %.2f px, grade %.2f -> %.2f\n", after.fringe_px, after.detail_blur,
              r.before.grade, after.grade);
  EXPECT_TRUE(after.fringe_px < 0.5f);          // colour fringes gone
  EXPECT_TRUE(psnr(et.all) > psnr(ec.all) + 1.0);          // clearly better than Clarity alone
  EXPECT_TRUE(psnr(et.corners) > psnr(ec.corners) + 1.5);  // most of all in the corners
  EXPECT_TRUE(psnr(et.centre) >= psnr(ec.centre) - 0.2);   // and never worse in the centre
  EXPECT_TRUE(after.grade > r.before.grade + 0.2f);
  pp_image_destroy(basic);
  pp_image_destroy(good);
  pp_image_destroy(clarity);
  pp_image_destroy(out);
}

TEST(transform_barely_touches_a_photo_from_a_good_lens) {
  pp_image* good = shoot(chart(), kGood);
  pp_image* out = blank(chart().w, chart().h);
  pp_transform_params p = finishOnly(false);
  pp_transform_report r{};
  EXPECT_EQ(pp_transform(good, out, &p, nullptr, &r), PP_OK);
  const Err eg = errorVs(good, chart()), et = errorVs(out, chart());
  std::printf("    fringes fixed %.2f px, PSNR %.1f -> %.1f dB\n", r.fringe_fixed_px, psnr(eg.all), psnr(et.all));
  EXPECT_TRUE(r.fringe_fixed_px == 0.0f);  // nothing to re-align
  EXPECT_TRUE(psnr(et.all) > psnr(eg.all) - 1.5);  // no damage
  pp_image_destroy(good);
  pp_image_destroy(out);
}

TEST(transform_can_keep_a_styled_photos_colours_and_vignette) {
  pp_image* basic = shoot(chart(), kBasicVignetted);
  pp_image* out = blank(chart().w, chart().h);
  // Darker corners are left as they are (from one photo they can't be told
  // apart from a scene that is darker at the edges, or a style's vignette).
  pp_transform_params p = finishOnly(false);
  pp_transform_report r{};
  EXPECT_EQ(pp_transform(basic, out, &p, nullptr, &r), PP_OK);
  auto means = [](const pp_image* img, bool corners) {
    pp_image_info info{};
    pp_image_get_info(img, &info);
    const auto* px = static_cast<const uint8_t*>(pp_image_pixels(const_cast<pp_image*>(img)));
    std::array<double, 3> m{};
    size_t n = 0;
    for (int y = 0; y < info.height; ++y)
      for (int x = 0; x < info.width; ++x) {
        const bool corner = (x < info.width / 6 || x > 5 * info.width / 6) && (y < info.height / 6 || y > 5 * info.height / 6);
        if (corner != corners) continue;
        for (int c = 0; c < 3; ++c) m[static_cast<size_t>(c)] += px[static_cast<size_t>(y) * info.stride + static_cast<size_t>(x) * 4 + c];
        ++n;
      }
    for (double& v : m) v /= static_cast<double>(n);
    return m;
  };
  const auto a = means(basic, false), b = means(out, false), ac = means(basic, true), bc = means(out, true);
  double worst = 0;
  for (int c = 0; c < 3; ++c) worst = std::max(worst, std::fabs(a[static_cast<size_t>(c)] - b[static_cast<size_t>(c)]));
  std::printf("    colour shift %.2f/255, corner light %.1f -> %.1f\n", worst, (ac[0] + ac[1] + ac[2]) / 3, (bc[0] + bc[1] + bc[2]) / 3);
  EXPECT_TRUE(worst < 3.0);                          // the photo's colours are kept
  EXPECT_TRUE(std::fabs((bc[0] + bc[1] + bc[2]) - (ac[0] + ac[1] + ac[2])) / 3 < 4.0);  // its darker corners too
  pp_image_destroy(basic);
  pp_image_destroy(out);
}

TEST(transform_doubles_detail_in_place_or_not_and_is_leak_free) {
  const uint64_t base = liveBytes();
  pp_image* basic = shoot(chart(), kBasic);
  // In place (same size).
  pp_transform_params p = finishOnly(true);
  EXPECT_EQ(pp_transform(basic, basic, &p, nullptr, nullptr), PP_OK);
  // Twice the size.
  pp_image* small = shoot(deadLeaves(400, 300, 5), kBasic);
  pp_image* big = blank(800, 600);
  p.upscale = 2;
  pp_transform_report r{};
  EXPECT_EQ(pp_transform(small, big, &p, nullptr, &r), PP_OK);
  // Deterministic.
  pp_image* big2 = blank(800, 600);
  EXPECT_EQ(pp_transform(small, big2, &p, nullptr, nullptr), PP_OK);
  EXPECT_TRUE(std::memcmp(pp_image_pixels(big), pp_image_pixels(big2), 800u * 600u * 4u) == 0);
  pp_image_destroy(basic);
  pp_image_destroy(small);
  pp_image_destroy(big);
  pp_image_destroy(big2);
  EXPECT_EQ(liveBytes(), base);
}

TEST(transform_rejects_bad_input_and_honours_cancel) {
  pp_image* small = shoot(deadLeaves(400, 300, 5), kBasic);
  pp_image* wrong = blank(500, 300);
  pp_transform_params p = finishOnly(true);
  EXPECT_EQ(pp_transform(small, wrong, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  p.upscale = 3;
  EXPECT_EQ(pp_transform(small, small, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  p.upscale = 1;
  p.strength = NAN;
  EXPECT_EQ(pp_transform(small, small, &p, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_transform(nullptr, small, nullptr, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image* gray = nullptr;
  pp_image_create(400, 300, PP_FORMAT_GRAY8, &gray);
  pp_lens_report lr{};
  EXPECT_EQ(pp_lens_analyze(gray, nullptr, &lr), PP_ERROR_UNSUPPORTED_FORMAT);
  pp_cancel_token* token = pp_cancel_token_create();
  pp_cancel_token_cancel(token);
  p.strength = 1.0f;
  EXPECT_EQ(pp_transform(small, small, &p, token, nullptr), PP_ERROR_CANCELLED);
  pp_cancel_token_destroy(token);
  pp_image_destroy(small);
  pp_image_destroy(wrong);
  pp_image_destroy(gray);
}

namespace {
// The chart graded like a golden-hour photo, with a black garment (as in a
// portrait): warm light, deep shadows.
pp_image* goldenHour() {
  Img scene = deadLeaves(800, 600, 21);
  for (size_t i = 0; i < scene.r.size(); ++i) {
    scene.r[i] = std::min(1.0f, scene.r[i] * 1.08f + 0.03f);
    scene.b[i] = scene.b[i] * 0.72f;
  }
  for (int y = 380; y < 600; ++y)
    for (int x = 250; x < 550; ++x) {
      const size_t i = static_cast<size_t>(y) * scene.w + x;
      const float t = 0.03f + 0.02f * scene.g[i];  // black fabric with faint texture
      scene.r[i] = t;
      scene.g[i] = t * 0.95f;
      scene.b[i] = t * 0.9f;
    }
  return shoot(scene, kBasic);
}

struct Colour {
  double hueDeg, chroma, blackChroma;
};
Colour colourOf(const pp_image* img) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  const auto* px = static_cast<const uint8_t*>(pp_image_pixels(const_cast<pp_image*>(img)));
  double cb = 0, cr = 0, bk = 0;
  size_t n = 0, nb = 0;
  for (int y = 0; y < info.height; ++y)
    for (int x = 0; x < info.width; ++x) {
      const uint8_t* p = px + static_cast<size_t>(y) * info.stride + static_cast<size_t>(x) * 4;
      const double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0;
      const double Y = 0.299 * r + 0.587 * g + 0.114 * b;
      const double vb = -0.168736 * r - 0.331264 * g + 0.5 * b, vr = 0.5 * r - 0.418688 * g - 0.081312 * b;
      const bool garment = y > 420 && y < 580 && x > 290 && x < 510;
      if (garment) {
        bk += std::hypot(vb, vr);
        ++nb;
      } else if (Y > 0.2 && Y < 0.85) {
        cb += vb;
        cr += vr;
        ++n;
      }
    }
  return {std::atan2(cr / n, cb / n) * 180.0 / 3.14159265, std::hypot(cb / n, cr / n), bk / nb};
}
}  // namespace

TEST(transform_keeps_golden_hour_golden) {
  pp_image* warm = goldenHour();
  pp_image* out = blank(800, 600);
  pp_transform_params p{};
  pp_transform_default_params(&p);  // full Transform, finishing included
  EXPECT_EQ(pp_transform(warm, out, &p, nullptr, nullptr), PP_OK);
  const Colour a = colourOf(warm), b = colourOf(out);
  std::printf("    light hue %.1f -> %.1f deg, warmth x%.2f, black garment colour %.3f -> %.3f\n", a.hueDeg, b.hueDeg,
              b.chroma / a.chroma, a.blackChroma, b.blackChroma);
  EXPECT_TRUE(std::fabs(b.hueDeg - a.hueDeg) < 4.0);              // same colour of light
  EXPECT_TRUE(b.chroma / a.chroma > 0.92 && b.chroma / a.chroma < 1.25);  // warmth kept (a touch richer at most)
  EXPECT_TRUE(b.blackChroma < a.blackChroma + 0.01);             // black stays black
  pp_image_destroy(warm);
  pp_image_destroy(out);
}

TEST(clarity_white_balance_never_tints_black_clothing) {
  pp_image* warm = goldenHour();
  pp_image* out = blank(800, 600);
  pp_enhance_params ep{};
  pp_enhance_default_params(&ep);  // auto tone on, as in the app
  EXPECT_EQ(pp_enhance_into(&warm, 1, &ep, nullptr, out, nullptr), PP_OK);
  const Colour a = colourOf(warm), b = colourOf(out);
  std::printf("    black garment colour %.3f -> %.3f (light hue %.1f -> %.1f deg)\n", a.blackChroma, b.blackChroma, a.hueDeg,
              b.hueDeg);
  EXPECT_TRUE(b.blackChroma < a.blackChroma + 0.012);  // no blue cast on black fabric
  pp_image_destroy(warm);
  pp_image_destroy(out);
}

TEST(clarity_white_balance_keeps_skin_warm_and_fixes_a_real_cast) {
  // A portrait-like frame full of warm skin tones (no grey in it): Clarity
  // must not push it towards cyan.
  Img skin = deadLeaves(600, 450, 31);
  for (size_t i = 0; i < skin.r.size(); ++i) {
    const float l = 0.35f + 0.45f * skin.g[i];
    skin.r[i] = std::min(1.0f, l * 1.18f);
    skin.g[i] = l * 0.86f;
    skin.b[i] = l * 0.7f;
  }
  // A grey scene under a blue-ish light: a real cast to correct.
  Img grey = deadLeaves(600, 450, 32);
  for (size_t i = 0; i < grey.r.size(); ++i) {
    const float l = 0.25f + 0.6f * grey.g[i];
    grey.r[i] = l * 0.94f;
    grey.g[i] = l;
    grey.b[i] = std::min(1.0f, l * 1.1f);
  }
  pp_enhance_params ep{};
  pp_enhance_default_params(&ep);
  auto meanChroma = [](const pp_image* img) {
    pp_image_info info{};
    pp_image_get_info(img, &info);
    const auto* px = static_cast<const uint8_t*>(pp_image_pixels(const_cast<pp_image*>(img)));
    double cb = 0, cr = 0;
    for (int y = 0; y < info.height; ++y)
      for (int x = 0; x < info.width; ++x) {
        const uint8_t* p = px + static_cast<size_t>(y) * info.stride + static_cast<size_t>(x) * 4;
        cb += -0.168736 * p[0] - 0.331264 * p[1] + 0.5 * p[2];
        cr += 0.5 * p[0] - 0.418688 * p[1] - 0.081312 * p[2];
      }
    const double n = 255.0 * info.width * info.height;
    return std::array<double, 2>{cb / n, cr / n};
  };
  for (const bool isSkin : {true, false}) {
    pp_image* in = shoot(isSkin ? skin : grey, kGood);
    pp_image* out = blank(600, 450);
    EXPECT_EQ(pp_enhance_into(&in, 1, &ep, nullptr, out, nullptr), PP_OK);
    const auto a = meanChroma(in), b = meanChroma(out);
    const double before = std::hypot(a[0], a[1]), after = std::hypot(b[0], b[1]);
    std::printf("    %s: colour cast %.3f -> %.3f\n", isSkin ? "warm skin" : "blue light on grey", before, after);
    if (isSkin) {
      EXPECT_TRUE(after > 0.9 * before);  // skin stays warm
    } else {
      EXPECT_TRUE(after < 0.75 * before);  // a real cast is reduced
    }
    pp_image_destroy(in);
    pp_image_destroy(out);
  }
}
