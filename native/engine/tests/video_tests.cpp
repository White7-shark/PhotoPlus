// Glide (Phase 6) video building blocks, tested on synthetic handheld clips
// whose true camera motion is known.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "photoplus/pp_engine.h"
#include "scene.hpp"
#include "test_harness.hpp"

namespace {

struct Canvas {
  photoplus::Plane p;
  explicit Canvas(int w, int h, unsigned seed) : p(scene::makeScene(w, h, seed)) {}
  float at(double x, double y) const {
    const double fx = std::clamp(x - 0.5, 0.0, static_cast<double>(p.width() - 1));
    const double fy = std::clamp(y - 0.5, 0.0, static_cast<double>(p.height() - 1));
    const auto ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const int jx = std::min(p.width() - 1, ix + 1), jy = std::min(p.height() - 1, iy + 1);
    const double tx = fx - ix, ty = fy - iy;
    return static_cast<float>((p.row(iy)[ix] * (1 - tx) + p.row(iy)[jx] * tx) * (1 - ty) +
                              (p.row(jy)[ix] * (1 - tx) + p.row(jy)[jx] * tx) * ty);
  }
};

// The camera's pose: the frame shows canvas content around (cx, cy), turned by
// `angle` and zoomed by `scale`. Frame pixel (centred) q shows canvas point
// c + R(-angle)·q / scale.
struct Pose {
  double cx, cy, angle, scale;
};

pp_image* render(const Canvas& c, const Pose& pose, int w, int h, unsigned noiseSeed, float noise = 0.004f) {
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
  auto* px = static_cast<uint8_t*>(pp_image_pixels(img));
  std::mt19937 rng(noiseSeed);
  std::normal_distribution<float> n(0.0f, noise);
  const double cs = std::cos(-pose.angle) / pose.scale, sn = std::sin(-pose.angle) / pose.scale;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const double qx = x + 0.5 - w * 0.5, qy = y + 0.5 - h * 0.5;
      const float v = c.at(pose.cx + cs * qx - sn * qy, pose.cy + sn * qx + cs * qy) + n(rng);
      uint8_t* q = px + (static_cast<size_t>(y) * w + x) * 4;
      const auto b = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255));
      q[0] = b, q[1] = static_cast<uint8_t>(std::min(255, b + 10)), q[2] = static_cast<uint8_t>(b * 0.9f), q[3] = 255;
    }
  return img;
}

// Where content at centred position p of pose A appears in pose B (centred).
void project(const Pose& A, const Pose& B, double px, double py, double& ox, double& oy) {
  // Canvas point shown at p in A.
  const double ca = std::cos(-A.angle) / A.scale, sa = std::sin(-A.angle) / A.scale;
  const double wx = A.cx + ca * px - sa * py, wy = A.cy + sa * px + ca * py;
  // Its position in B: q = scale·R(angle)·(w − c).
  const double cb = std::cos(B.angle) * B.scale, sb = std::sin(B.angle) * B.scale;
  const double dx = wx - B.cx, dy = wy - B.cy;
  ox = cb * dx - sb * dy;
  oy = sb * dx + cb * dy;
}

}  // namespace

TEST(video_motion_recovers_shift_rotation_and_zoom) {
  const Canvas c(900, 700, 3);
  const int w = 480, h = 360;
  const Pose A{450, 350, 0.0, 1.0}, B{450 - 6.3, 350 + 3.7, 0.014, 1.006};
  pp_image* a = render(c, A, w, h, 1);
  pp_image* b = render(c, B, w, h, 2);
  pp_video_motion m{};
  ASSERT_OK(pp_video_estimate_motion(a, b, &m));
  double worst = 0;
  for (const double px : {-200.0, 0.0, 200.0})
    for (const double py : {-150.0, 0.0, 150.0}) {
      double ex, ey;
      project(A, B, px, py, ex, ey);
      const double gx = m.a * px - m.b * py + m.tx, gy = m.b * px + m.a * py + m.ty;
      worst = std::max(worst, std::hypot(gx - ex, gy - ey));
    }
  std::printf("    worst position error %.2f px, confidence %.2f\n", worst, static_cast<double>(m.confidence));
  EXPECT_TRUE(worst < 0.6);
  EXPECT_TRUE(m.confidence > 0.7f);
  pp_image_destroy(a);
  pp_image_destroy(b);
}

TEST(video_motion_follows_the_camera_not_a_person_walking_through) {
  const Canvas c(900, 700, 9);
  const int w = 480, h = 360;
  const Pose A{450, 350, 0, 1}, B{445, 348, 0, 1};  // camera: content moves (+5, +2)
  pp_image* a = render(c, A, w, h, 3);
  pp_image* b = render(c, B, w, h, 4);
  // A bright "person" (a fifth of the frame) moving the other way.
  auto stamp = [&](pp_image* img, int x0, int y0) {
    auto* px = static_cast<uint8_t*>(pp_image_pixels(img));
    for (int y = y0; y < y0 + 160; ++y)
      for (int x = x0; x < x0 + 110; ++x) {
        uint8_t* q = px + (static_cast<size_t>(y) * w + x) * 4;
        const auto v = static_cast<uint8_t>(120 + ((x * 7 + y * 3) % 23) * 4);
        q[0] = v, q[1] = static_cast<uint8_t>(v / 2), q[2] = static_cast<uint8_t>(v / 3);
      }
  };
  stamp(a, 200, 120);
  stamp(b, 185, 128);
  pp_video_motion m{};
  ASSERT_OK(pp_video_estimate_motion(a, b, &m));
  std::printf("    camera shift (%.2f, %.2f), expected (5, 2)\n", static_cast<double>(m.tx), static_cast<double>(m.ty));
  EXPECT_NEAR(m.tx, 5.0, 0.5);
  EXPECT_NEAR(m.ty, 2.0, 0.5);
  pp_image_destroy(a);
  pp_image_destroy(b);
}

namespace {
// A 2-second handheld clip: a slow pan to the right with hand shake.
std::vector<Pose> shakyPath(int n, double shake, unsigned seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<double> j(0.0, 1.0);
  std::vector<Pose> poses;
  double sx = 0, sy = 0, sa = 0;
  for (int k = 0; k < n; ++k) {
    // Shake: a jittery random walk pulled back toward the pan.
    sx = 0.6 * sx + shake * j(rng);
    sy = 0.6 * sy + shake * j(rng);
    sa = 0.6 * sa + 0.004 * j(rng);
    poses.push_back({450 + 2.0 * k + sx, 350 + sy, sa, 1.0});
  }
  return poses;
}
}  // namespace

TEST(video_stabilize_removes_shake_and_keeps_the_pan) {
  const Canvas c(1100, 700, 5);
  const int w = 320, h = 240, n = 60;
  const auto poses = shakyPath(n, 5.0, 7);
  std::vector<pp_video_motion> motions(n);
  pp_image* prev = render(c, poses[0], w, h, 100);
  for (int k = 1; k < n; ++k) {
    pp_image* cur = render(c, poses[k], w, h, 100 + static_cast<unsigned>(k));
    ASSERT_OK(pp_video_estimate_motion(prev, cur, &motions[static_cast<size_t>(k)]));
    pp_image_destroy(prev);
    prev = cur;
  }
  pp_image_destroy(prev);
  std::vector<float> warps(static_cast<size_t>(n) * 6);
  float report[3];
  ASSERT_OK(pp_video_stabilize(motions.data(), n, w, h, 1.0f, 1.2f, warps.data(), report));
  std::printf("    zoom %.3f, shake %.2f -> %.2f px/frame\n", static_cast<double>(report[0]), static_cast<double>(report[1]),
              static_cast<double>(report[2]));
  EXPECT_TRUE(report[2] < 0.35f * report[1]);
  EXPECT_TRUE(report[0] >= 1.0f && report[0] <= 1.2f);
  // Measure the shown result directly: the canvas point at the output centre
  // should glide (steady 2 px/frame), not jitter.
  std::vector<double> cx(n);
  for (int k = 0; k < n; ++k) {
    const float* m = &warps[static_cast<size_t>(k) * 6];
    const double sx = m[0] * w * 0.5 + m[1] * h * 0.5 + m[2] - w * 0.5, sy = m[3] * w * 0.5 + m[4] * h * 0.5 + m[5] - h * 0.5;
    const Pose& p = poses[static_cast<size_t>(k)];
    const double ca = std::cos(-p.angle) / p.scale, sa = std::sin(-p.angle) / p.scale;
    cx[static_cast<size_t>(k)] = p.cx + ca * sx - sa * sy;
  }
  double jitter = 0, rawJitter = 0;
  for (int k = 2; k < n; ++k) {
    jitter += std::fabs(cx[k] - 2 * cx[k - 1] + cx[k - 2]);
    rawJitter += std::fabs(poses[k].cx - 2 * poses[k - 1].cx + poses[k - 2].cx);
  }
  const double pan = (cx[n - 1] - cx[0]) / (n - 1);
  std::printf("    shown path: acceleration %.2f -> %.2f px/frame², pan %.2f px/frame (camera 2.0)\n", rawJitter / (n - 2),
              jitter / (n - 2), pan);
  EXPECT_TRUE(jitter < 0.3 * rawJitter);
  EXPECT_NEAR(pan, 2.0, 0.35);
  // Every warp keeps the output inside the source (no empty edges).
  for (int k = 0; k < n; ++k) {
    const float* m = &warps[static_cast<size_t>(k) * 6];
    for (const double x : {0.0, static_cast<double>(w)})
      for (const double y : {0.0, static_cast<double>(h)}) {
        const double sx = m[0] * x + m[1] * y + m[2], sy = m[3] * x + m[4] * y + m[5];
        EXPECT_TRUE(sx >= -0.51 && sx <= w + 0.51 && sy >= -0.51 && sy <= h + 0.51);
      }
  }
}

TEST(video_stabilize_respects_the_zoom_cap_under_violent_shake) {
  std::vector<pp_video_motion> motions(40);
  std::mt19937 rng(3);
  std::normal_distribution<float> j(0.0f, 25.0f);
  for (auto& m : motions) m = {1, 0, j(rng), j(rng), 1, {0, 0, 0}};
  std::vector<float> warps(motions.size() * 6);
  float report[3];
  ASSERT_OK(pp_video_stabilize(motions.data(), motions.size(), 640, 360, 1.0f, 1.1f, warps.data(), report));
  EXPECT_TRUE(report[0] <= 1.1001f);
  for (size_t k = 0; k < motions.size(); ++k) {
    const float* m = &warps[k * 6];
    for (const double x : {0.0, 640.0})
      for (const double y : {0.0, 360.0}) {
        const double sx = m[0] * x + m[1] * y + m[2], sy = m[3] * x + m[4] * y + m[5];
        EXPECT_TRUE(sx >= -0.51 && sx <= 640.51 && sy >= -0.51 && sy <= 360.51);
      }
  }
  std::printf("    zoom %.3f, shake %.1f -> %.1f px/frame (part kept to stay in frame)\n", static_cast<double>(report[0]),
              static_cast<double>(report[1]), static_cast<double>(report[2]));
  EXPECT_TRUE(report[2] < report[1]);
}

TEST(video_tone_curves_reproduce_a_grade_exactly) {
  const Canvas c(320, 240, 4);
  pp_image* a = render(c, {160, 120, 0, 1}, 320, 240, 9);
  pp_image* b = nullptr;
  ASSERT_OK(pp_image_create(320, 240, PP_FORMAT_RGBA8, &b));
  const auto* pa = static_cast<const uint8_t*>(pp_image_pixels(a));
  auto* pb = static_cast<uint8_t*>(pp_image_pixels(b));
  auto grade = [](int v, int ch) {
    const double g = std::pow(v / 255.0, ch == 2 ? 0.8 : 0.9) * (ch == 0 ? 0.95 : 1.0);
    return static_cast<uint8_t>(std::lround(std::clamp(g, 0.0, 1.0) * 255));
  };
  for (int i = 0; i < 320 * 240; ++i) {
    for (int ch = 0; ch < 3; ++ch) pb[i * 4 + ch] = grade(pa[i * 4 + ch], ch);
    pb[i * 4 + 3] = 255;
  }
  uint8_t curves[768];
  ASSERT_OK(pp_video_fit_curves(a, b, curves));
  pp_image* again = render(c, {170, 125, 0.01, 1}, 320, 240, 10);  // another frame of the clip
  pp_image* expect = nullptr;
  ASSERT_OK(pp_image_create(320, 240, PP_FORMAT_RGBA8, &expect));
  const auto* pg = static_cast<const uint8_t*>(pp_image_pixels(again));
  auto* pe = static_cast<uint8_t*>(pp_image_pixels(expect));
  for (int i = 0; i < 320 * 240; ++i)
    for (int ch = 0; ch < 3; ++ch) pe[i * 4 + ch] = grade(pg[i * 4 + ch], ch);
  ASSERT_OK(pp_video_apply_curves(again, curves, nullptr));
  double worst = 0, mean = 0;
  for (int i = 0; i < 320 * 240; ++i)
    for (int ch = 0; ch < 3; ++ch) {
      const double d = std::abs(pg[i * 4 + ch] - pe[i * 4 + ch]);
      worst = std::max(worst, d);
      mean += d;
    }
  mean /= 320.0 * 240 * 3;
  std::printf("    curve error: mean %.2f, worst %.0f levels\n", mean, worst);
  EXPECT_TRUE(mean < 1.0);
  EXPECT_TRUE(worst <= 4);
  for (pp_image* img : {a, b, again, expect}) pp_image_destroy(img);
}

TEST(video_steady_mask_calms_edge_flicker_but_follows_real_movement) {
  pp_image* prev = nullptr;
  pp_image* cur = nullptr;
  ASSERT_OK(pp_image_create(64, 1, PP_FORMAT_GRAY8, &prev));
  ASSERT_OK(pp_image_create(64, 1, PP_FORMAT_GRAY8, &cur));
  auto* p = static_cast<uint8_t*>(pp_image_pixels(prev));
  auto* c = static_cast<uint8_t*>(pp_image_pixels(cur));
  for (int x = 0; x < 64; ++x) p[x] = 128;
  for (int x = 0; x < 32; ++x) c[x] = 148;   // flicker: +20 levels
  for (int x = 32; x < 64; ++x) c[x] = 255;  // the person moved here
  ASSERT_OK(pp_video_steady_mask(prev, cur, 0.6f));
  std::printf("    flicker 20 -> %d levels; real change -> %d of 127\n", p[0] - 128, p[40] - 128);
  EXPECT_TRUE(p[0] - 128 <= 10);
  EXPECT_TRUE(p[40] == 255);
  EXPECT_EQ(pp_video_steady_mask(prev, nullptr, 0.5f), PP_ERROR_INVALID_ARGUMENT);
  pp_image_destroy(prev);
  pp_image_destroy(cur);
}

// A segmentation mistake in one frame (a patch of wall taken for the person)
// must not reach the picture; a person moving steadily must.
TEST(video_consensus_mask_outvotes_a_one_frame_mistake_and_keeps_movement) {
  const int W = 64;
  pp_image *a = nullptr, *b = nullptr, *c = nullptr, *out = nullptr;
  for (pp_image** m : {&a, &b, &c, &out}) ASSERT_OK(pp_image_create(W, 1, PP_FORMAT_GRAY8, m));
  auto px = [](pp_image* m) { return static_cast<uint8_t*>(pp_image_pixels(m)); };
  // The person's edge moves right by 2 px a frame; frame b also has a
  // spurious blob at 50..55.
  for (int x = 0; x < W; ++x) {
    px(a)[x] = x < 20 ? 255 : 0;
    px(b)[x] = x < 22 || (x >= 50 && x < 56) ? 255 : 0;
    px(c)[x] = x < 24 ? 255 : 0;
  }
  ASSERT_OK(pp_video_consensus_mask(a, b, c, out));
  int edge = 0;
  while (edge < W && px(out)[edge] == 255) ++edge;
  int blob = 0;
  for (int x = 50; x < 56; ++x) blob += px(out)[x];
  std::printf("    edge at %d (the middle frame's 22); blob %d (gone: 0)\n", edge, blob);
  EXPECT_EQ(edge, 22);
  EXPECT_EQ(blob, 0);
  // At the ends of a clip the frame is its own neighbour.
  ASSERT_OK(pp_video_consensus_mask(nullptr, b, c, out));
  EXPECT_EQ(px(out)[52], 255);
  EXPECT_EQ(pp_video_consensus_mask(a, nullptr, c, out), PP_ERROR_INVALID_ARGUMENT);
  pp_image* small = nullptr;
  ASSERT_OK(pp_image_create(8, 1, PP_FORMAT_GRAY8, &small));
  EXPECT_EQ(pp_video_consensus_mask(small, b, c, out), PP_ERROR_INVALID_ARGUMENT);
  for (pp_image* m : {a, b, c, out, small}) pp_image_destroy(m);
}

TEST(video_rejects_bad_input) {
  pp_image* a = nullptr;
  pp_image* b = nullptr;
  ASSERT_OK(pp_image_create(64, 64, PP_FORMAT_RGBA8, &a));
  ASSERT_OK(pp_image_create(32, 64, PP_FORMAT_RGBA8, &b));
  pp_video_motion m{};
  EXPECT_EQ(pp_video_estimate_motion(a, b, &m), PP_ERROR_INVALID_ARGUMENT);
  pp_video_motion bad[2] = {{1, 0, 0, 0, 1, {0, 0, 0}}, {1, 0, std::nanf(""), 0, 1, {0, 0, 0}}};
  float warps[12];
  EXPECT_EQ(pp_video_stabilize(bad, 2, 64, 64, 0.5f, 1.1f, warps, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_video_stabilize(bad, 0, 64, 64, 0.5f, 1.1f, warps, nullptr), PP_ERROR_INVALID_ARGUMENT);
  const float nanWarp[6] = {1, 0, std::nanf(""), 0, 1, 0};
  EXPECT_EQ(pp_video_warp(a, nanWarp, a, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image_destroy(a);
  pp_image_destroy(b);
}

TEST(video_frame_enhance_cleans_noise_without_ghosting_and_sharpens_soft_clips) {
  const Canvas c(900, 700, 21);
  const int w = 320, h = 240;
  const Pose P0{450, 350, 0, 1}, P1{447, 351, 0.003, 1}, P2{444, 352, 0.006, 1};  // camera drifting
  const float noise = 0.03f;
  pp_image* f0 = render(c, P0, w, h, 31, noise);
  pp_image* f1 = render(c, P1, w, h, 32, noise);
  pp_image* f2 = render(c, P2, w, h, 33, noise);
  pp_image* clean = render(c, P1, w, h, 34, 0.0f);
  // A person who moved between the frames: a block present only in the middle frame.
  auto* p1 = static_cast<uint8_t*>(pp_image_pixels(f1));
  auto* pc = static_cast<uint8_t*>(pp_image_pixels(clean));
  for (int y = 80; y < 160; ++y)
    for (int x = 120; x < 180; ++x)
      for (int ch = 0; ch < 3; ++ch) p1[(y * w + x) * 4 + ch] = pc[(y * w + x) * 4 + ch] = static_cast<uint8_t>(ch == 0 ? 230 : 40);
  pp_video_motion m01{}, m12{};
  ASSERT_OK(pp_video_estimate_motion(f0, f1, &m01));
  ASSERT_OK(pp_video_estimate_motion(f1, f2, &m12));
  pp_video_frame_params fp{};
  fp.noise_sigma = noise;
  fp.blur_sigma = 0.5f;  // a sharp clip: no deblur
  fp.denoise = 1.0f;
  fp.sharpen = 1.0f;
  pp_image* out = nullptr;
  ASSERT_OK(pp_image_create(w, h, PP_FORMAT_RGBA8, &out));
  ASSERT_OK(pp_video_enhance_frame(f1, f0, f2, &m01, &m12, &fp, nullptr, out, nullptr));
  auto err = [&](pp_image* img, bool block) {
    const auto* a = static_cast<const uint8_t*>(pp_image_pixels(img));
    double s = 0;
    int n = 0;
    for (int y = 20; y < h - 20; ++y)
      for (int x = 20; x < w - 20; ++x) {
        const bool in = x >= 120 && x < 180 && y >= 80 && y < 160;
        if (in != block) continue;
        s += std::abs(a[(y * w + x) * 4 + 1] - pc[(y * w + x) * 4 + 1]);
        ++n;
      }
    return s / n;
  };
  std::printf("    noise (error vs clean): %.2f -> %.2f levels; moving block error %.2f (no ghost)\n", err(f1, false),
              err(out, false), err(out, true));
  EXPECT_TRUE(err(out, false) < 0.75 * err(f1, false));
  EXPECT_TRUE(err(out, true) < 2.0);  // the block is not smeared with the background behind it
  // A soft clip is sharpened in proportion to its measured blur.
  auto sharpness = [&](pp_image* img) {
    const auto* a = static_cast<const uint8_t*>(pp_image_pixels(img));
    double s = 0;
    for (int y = 1; y < h - 1; ++y)
      for (int x = 1; x < w - 1; ++x) s += std::abs(4 * a[(y * w + x) * 4 + 1] - a[(y * w + x - 1) * 4 + 1] - a[(y * w + x + 1) * 4 + 1] - a[((y - 1) * w + x) * 4 + 1] - a[((y + 1) * w + x) * 4 + 1]);
    return s;
  };
  fp.blur_sigma = 1.6f;
  fp.denoise = 0.0f;
  pp_image* sharp = nullptr;
  ASSERT_OK(pp_image_create(w, h, PP_FORMAT_RGBA8, &sharp));
  ASSERT_OK(pp_video_enhance_frame(clean, nullptr, nullptr, nullptr, nullptr, &fp, nullptr, sharp, nullptr));
  std::printf("    soft clip: sharpness x%.2f\n", sharpness(sharp) / sharpness(clean));
  EXPECT_TRUE(sharpness(sharp) > 1.3 * sharpness(clean));
  for (pp_image* img : {f0, f1, f2, clean, out, sharp}) pp_image_destroy(img);
}
