// Photo Motion (motion.hpp). What a person would notice:
//  - a still camera with nothing moving shows the same picture every frame;
//  - every move loops: the last frame leads back into the first;
//  - arrows move what they point at, anchors hold it, far away stays still;
//  - with depth, the background slides more than the person (parallax);
//  - every effect animates, and the photo's alpha is kept;
//  - a blue sky is found; bad input is refused; cancel is honoured.
#include <cmath>
#include <cstdio>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

constexpr int kW = 200, kH = 160;

// Stripes everywhere (motion is visible), a blue sky band on top, a "person" block in the middle.
pp_image* scene() {
  pp_image* img = nullptr;
  pp_image_create(kW, kH, PP_FORMAT_RGBA8, &img);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      unsigned char* p = px + static_cast<size_t>(y) * info.stride + x * 4;
      const bool stripe = ((x / 6) + (y / 6)) % 2 == 0;
      int r = stripe ? 200 : 60, g = stripe ? 170 : 80, b = stripe ? 120 : 50;
      if (y < 40) {
        // sky: smooth blue with soft cloud bands
        r = 110 + (x % 40), g = 160 + (x % 40) / 2, b = 230;
      }
      if (x > 80 && x < 120 && y > 60 && y < 150) r = 190, g = 140, b = 110;
      p[0] = static_cast<unsigned char>(r), p[1] = static_cast<unsigned char>(g), p[2] = static_cast<unsigned char>(b);
      p[3] = static_cast<unsigned char>(180 + x % 70);
    }
  return img;
}

pp_image* mask(bool depth) {
  pp_image* m = nullptr;
  pp_image_create(kW, kH, PP_FORMAT_GRAY8, &m);
  pp_image_info info{};
  pp_image_get_info(m, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(m));
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const bool person = x > 80 && x < 120 && y > 60 && y < 150;
      px[static_cast<size_t>(y) * info.stride + x] = depth ? (person ? 220 : static_cast<unsigned char>(40 + y / 2)) : (person ? 255 : 0);
    }
  return m;
}

std::vector<unsigned char> frame(pp_motion* m, float t) {
  pp_image* f = nullptr;
  pp_image_create(kW, kH, PP_FORMAT_RGBA8, &f);
  EXPECT_EQ(pp_motion_render(m, t, f, nullptr), PP_OK);
  pp_image_info info{};
  pp_image_get_info(f, &info);
  const auto* px = static_cast<const unsigned char*>(pp_image_pixels(f));
  std::vector<unsigned char> out;
  for (int y = 0; y < kH; ++y) out.insert(out.end(), px + static_cast<size_t>(y) * info.stride, px + static_cast<size_t>(y) * info.stride + kW * 4);
  pp_image_destroy(f);
  return out;
}

double diffIn(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, int x0, int y0, int x1, int y1) {
  double d = 0;
  int n = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x, ++n)
      for (int k = 0; k < 3; ++k) d += std::fabs(static_cast<double>(a[(static_cast<size_t>(y) * kW + x) * 4 + k]) - b[(static_cast<size_t>(y) * kW + x) * 4 + k]);
  return d / (n * 3.0);
}

pp_motion* make(pp_motion_params p, const std::vector<float>& arrows = {}, const std::vector<float>& anchors = {}, bool layered = false) {
  pp_image* img = scene();
  pp_image* sub = layered ? mask(false) : nullptr;
  pp_image* dep = layered ? mask(true) : nullptr;
  pp_motion* m = nullptr;
  EXPECT_EQ(pp_motion_create(img, dep, sub, arrows.data(), arrows.size() / 4, anchors.data(), anchors.size() / 3, &p, nullptr, &m), PP_OK);
  pp_image_destroy(img);
  if (sub) pp_image_destroy(sub);
  if (dep) pp_image_destroy(dep);
  return m;
}

}  // namespace

TEST(motion_still_is_still_and_loops_return) {
  pp_motion* still = make({0, 0.5f, 1, 0, 0.5f, 0, 0.6f, 1, 7});
  EXPECT_TRUE(diffIn(frame(still, 0.0f), frame(still, 0.5f), 0, 0, kW, kH) < 0.01);
  pp_motion_destroy(still);
  for (int cam = 1; cam < PP_MOTION_CAMERAS; ++cam) {
    pp_motion* m = make({cam, 0.8f, 1, 0, 0.5f, 0, 0.6f, 1, 7}, {}, {}, true);
    const auto a = frame(m, 0.0f), mid = frame(m, 0.5f), end = frame(m, 0.999f);
    EXPECT_TRUE(diffIn(a, mid, 0, 0, kW, kH) > 1.0);        // it moves
    const double back = diffIn(a, end, 0, 0, kW, kH);
    if (!(back < 1.5)) std::fprintf(stderr, "camera %d returns with %.2f\n", cam, back);
    EXPECT_TRUE(back < 1.5);  // and comes back: a seamless loop
    pp_motion_destroy(m);
  }
}

TEST(motion_arrows_move_anchors_hold_and_alpha_is_kept) {
  // Arrow over the lower left; an anchor over its right part; the top right is far away.
  pp_motion* m = make({0, 0, 1, 0, 0.5f, 0, 0.6f, 1, 7}, {0.15f, 0.8f, 0.35f, 0.8f}, {0.3f, 0.8f, 0.04f});
  const auto a = frame(m, 0.0f), b = frame(m, 0.3f);
  EXPECT_TRUE(diffIn(a, b, 10, 115, 40, 145) > 8.0);   // the arrow's start flows
  EXPECT_TRUE(diffIn(a, b, 57, 125, 63, 131) < 1.0);   // the anchor holds
  EXPECT_TRUE(diffIn(a, b, 160, 60, 195, 100) < 0.5);  // far away is still
  for (size_t i = 3; i < a.size(); i += 4) EXPECT_EQ(a[i], static_cast<unsigned char>(180 + ((i / 4) % kW) % 70));
  pp_motion_destroy(m);
}

TEST(motion_parallax_moves_the_background_more_than_the_person) {
  pp_motion* m = make({3, 1.0f, 1, 0, 0.5f, 0, 0.6f, 1, 7}, {}, {}, true);  // orbit
  const auto a = frame(m, 0.25f), b = frame(m, 0.75f);
  const double person = diffIn(a, b, 90, 80, 110, 140), back = diffIn(a, b, 10, 60, 60, 150);
  EXPECT_TRUE(back > person * 1.5);
  pp_motion_destroy(m);
}

TEST(motion_effects_animate_and_sky_is_found) {
  for (int e = 1; e < PP_MOTION_EFFECTS; ++e) {
    pp_motion* m = make({0, 0, 1, 0, 0.5f, e, 0.8f, 1, 7}, {}, {}, true);
    const auto a = frame(m, 0.1f), b = frame(m, 0.6f);
    EXPECT_TRUE(diffIn(a, b, 0, 0, kW, kH) > 0.05);
    pp_motion_destroy(m);
  }
  pp_motion* sky = make({0, 0, 1, 1, 0.8f, 0, 0.6f, 1, 7});
  EXPECT_TRUE(pp_motion_sky_share(sky) > 0.1f);
  const auto a = frame(sky, 0.0f), b = frame(sky, 0.4f);
  EXPECT_TRUE(diffIn(a, b, 0, 0, kW, 30) > 1.0);    // the sky drifts
  EXPECT_TRUE(diffIn(a, b, 0, 100, kW, kH) < 0.5);  // the ground doesn't
  pp_motion_destroy(sky);
}

TEST(motion_refuses_bad_input_and_honours_cancel) {
  pp_image* img = scene();
  pp_motion* m = nullptr;
  pp_motion_params bad{9, 0.5f, 1, 0, 0.5f, 0, 0.6f, 1, 7};
  EXPECT_EQ(pp_motion_create(img, nullptr, nullptr, nullptr, 0, nullptr, 0, &bad, nullptr, &m), PP_ERROR_INVALID_ARGUMENT);
  bad = {0, 2.0f, 1, 0, 0.5f, 0, 0.6f, 1, 7};
  EXPECT_EQ(pp_motion_create(img, nullptr, nullptr, nullptr, 0, nullptr, 0, &bad, nullptr, &m), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_motion_create(img, nullptr, nullptr, nullptr, 2, nullptr, 0, nullptr, nullptr, &m), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_motion_create(nullptr, nullptr, nullptr, nullptr, 0, nullptr, 0, nullptr, nullptr, &m), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_motion_create(img, nullptr, nullptr, nullptr, 0, nullptr, 0, nullptr, nullptr, &m), PP_OK);
  pp_image* small = nullptr;
  pp_image_create(10, 10, PP_FORMAT_RGBA8, &small);
  EXPECT_EQ(pp_motion_render(m, 0.2f, small, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image* f = nullptr;
  pp_image_create(kW, kH, PP_FORMAT_RGBA8, &f);
  pp_cancel_token* t = pp_cancel_token_create();
  pp_cancel_token_cancel(t);
  EXPECT_EQ(pp_motion_render(m, 0.2f, f, t), PP_ERROR_CANCELLED);
  pp_cancel_token_destroy(t);
  pp_image_destroy(f);
  pp_image_destroy(small);
  pp_motion_destroy(m);
  pp_image_destroy(img);
}
