// Quantitative tests for the computational-photography pipeline. Each test
// builds a known ground truth, simulates a weak camera, and asserts that the
// engine measurably recovers quality (PSNR in dB against the ground truth).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "photoplus/enhance.hpp"
#include "photoplus/pp_engine.h"
#include "scene.hpp"
#include "test_harness.hpp"

using photoplus::Plane;
namespace enh = photoplus::enhance;

namespace {

uint64_t liveBytes() {
  pp_memory_stats s{};
  pp_memory_get_stats(&s);
  return s.current_bytes;
}

pp_image* toImage(const Plane& p) {
  pp_image* img = nullptr;
  if (pp_image_create(p.width(), p.height(), PP_FORMAT_RGBA8, &img) != PP_OK) return nullptr;
  auto* px = static_cast<uint8_t*>(pp_image_pixels(img));
  for (int y = 0; y < p.height(); ++y)
    for (int x = 0; x < p.width(); ++x) {
      const auto v = static_cast<uint8_t>(std::lround(std::fmin(1.0f, std::fmax(0.0f, p.row(y)[x])) * 255.0f));
      uint8_t* q = px + (static_cast<size_t>(y) * p.width() + x) * 4;
      q[0] = q[1] = q[2] = v;
      q[3] = 255;
    }
  return img;
}

Plane lumaOf(pp_image* img) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto p = Plane::allocate(info.width, info.height);
  const auto* px = static_cast<const uint8_t*>(pp_image_pixels(img));
  for (int y = 0; y < info.height; ++y)
    for (int x = 0; x < info.width; ++x) {
      const uint8_t* q = px + static_cast<size_t>(y) * info.stride + x * 4;
      p->row(y)[x] = (0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2]) / 255.0f;
    }
  return std::move(p).value();
}

// Restoration-only parameters, so PSNR against ground truth is meaningful
// (tone stages intentionally change brightness/contrast).
pp_enhance_params restorationOnly(int upscale) {
  pp_enhance_params p{};
  pp_enhance_default_params(&p);
  p.upscale = upscale;
  p.auto_tone = 0;
  p.detail = 0.0f;
  p.vibrance = 0.0f;
  return p;
}

struct Burst {
  std::vector<pp_image*> frames;
  ~Burst() {
    for (auto* f : frames) pp_image_destroy(f);
  }
};

}  // namespace

TEST(noise_estimate_tracks_true_noise) {
  const Plane clean = scene::makeScene(256, 256);
  for (float sigma : {0.01f, 0.03f, 0.06f}) {
    const Plane noisy = scene::degrade(clean, 0, 0, 0.0f, 1, sigma, 3);
    const float est = enh::estimateNoise(noisy);
    EXPECT_NEAR(est, sigma, sigma * 0.35 + 0.004);
  }
}

TEST(blur_estimate_tracks_true_blur) {
  const Plane clean = scene::makeScene(320, 320);
  const Plane sharpish = scene::degrade(clean, 0, 0, 0.6f, 1, 0.002f, 4);
  const Plane soft = scene::degrade(clean, 0, 0, 2.0f, 1, 0.002f, 4);
  const Plane softer = scene::degrade(clean, 0, 0, 3.0f, 1, 0.002f, 4);
  auto a = enh::estimateBlurSigma(sharpish, 0.002f, nullptr);
  auto b = enh::estimateBlurSigma(soft, 0.002f, nullptr);
  auto c = enh::estimateBlurSigma(softer, 0.002f, nullptr);
  EXPECT_TRUE(a && b && c);
  EXPECT_TRUE(*a < *b && *b < *c);  // monotonic
  EXPECT_NEAR(*b, 2.0, 0.7);
  EXPECT_NEAR(*c, 3.0, 1.0);
}

TEST(alignment_recovers_subpixel_shift) {
  const Plane clean = scene::makeScene(256, 256);
  const Plane ref = scene::degrade(clean, 0, 0, 0.8f, 1, 0.01f, 1);
  for (auto [dx, dy] : {std::pair{0.37f, -0.62f}, std::pair{2.25f, 1.5f}, std::pair{-5.4f, 3.1f}}) {
    const Plane frame = scene::degrade(clean, dx, dy, 0.8f, 1, 0.01f, 2);
    auto s = enh::alignTranslation(ref, frame, nullptr);
    EXPECT_TRUE(s && s->valid);
    EXPECT_NEAR(s->dx, dx, 0.08);
    EXPECT_NEAR(s->dy, dy, 0.08);
  }
}

TEST(burst_fusion_removes_noise) {
  const Plane clean = scene::makeScene(256, 256);
  // What a noise-free version of the same capture would look like: the
  // target of pure denoising.
  const Plane noiseFree = scene::degrade(clean, 0, 0, 0.7f, 1, 0.0f, 1);
  Burst burst;
  const float shifts[6][2] = {{0, 0}, {0.6f, -0.3f}, {-1.2f, 0.8f}, {1.7f, 1.1f}, {-0.4f, -1.6f}, {2.3f, -0.9f}};
  for (int i = 0; i < 6; ++i) {
    burst.frames.push_back(toImage(scene::degrade(clean, shifts[i][0], shifts[i][1], 0.7f, 1, 0.05f, 10 + i)));
  }
  const Plane noisy = lumaOf(burst.frames[0]);

  // (a) Merging alone: noise removal measured against the noise-free capture.
  pp_enhance_params mergeOnly = restorationOnly(1);
  mergeOnly.denoise = 0.0f;
  mergeOnly.sharpen = 0.0f;
  pp_image* merged = nullptr;
  pp_enhance_report report{};
  ASSERT_OK(pp_enhance(burst.frames.data(), burst.frames.size(), &mergeOnly, nullptr, &merged, &report));
  const double noisyVsNf = scene::psnr(noisy, noiseFree);
  const double mergedVsNf = scene::psnr(lumaOf(merged), noiseFree);
  pp_image_destroy(merged);

  // (b) Full restoration against the true scene, single frame vs burst.
  pp_enhance_params full = restorationOnly(1);
  pp_image* oneFrame = nullptr;
  ASSERT_OK(pp_enhance(burst.frames.data(), 1, &full, nullptr, &oneFrame, nullptr));
  const double singleEnhanced = scene::psnr(lumaOf(oneFrame), clean);
  pp_image_destroy(oneFrame);
  pp_image* fused = nullptr;
  ASSERT_OK(pp_enhance(burst.frames.data(), burst.frames.size(), &full, nullptr, &fused, &report));
  const double burstPsnr = scene::psnr(lumaOf(fused), clean);
  pp_image_destroy(fused);
  const double noisyVsClean = scene::psnr(noisy, clean);

  std::printf("    noise removal: %.2f -> %.2f dB vs noise-free capture\n", noisyVsNf, mergedVsNf);
  std::printf("    vs true scene: noisy %.2f | single-frame enhance %.2f | 6-frame burst %.2f dB (used %d)\n",
              noisyVsClean, singleEnhanced, burstPsnr, report.frames_used);
  EXPECT_EQ(report.frames_used, 6);
  EXPECT_TRUE(mergedVsNf > noisyVsNf + 5.0);
  EXPECT_TRUE(burstPsnr > noisyVsClean + 1.8);
  EXPECT_TRUE(burstPsnr > singleEnhanced + 1.2);
  EXPECT_TRUE(report.noise_before > 0.04f && report.noise_after < 0.5f * report.noise_before);
}

TEST(multi_frame_super_resolution_beats_upscaling) {
  // Ground truth at 2x; the "camera" captures half resolution with lens blur
  // and noise, from hand-shake-shifted positions.
  const Plane truth = scene::makeScene(384, 384);
  Burst burst;
  const float shifts[8][2] = {{0, 0},   {1.0f, 0.5f},  {0.5f, 1.0f},  {1.5f, 1.5f},
                              {-1.0f, 0.3f}, {0.3f, -1.1f}, {-0.6f, -0.6f}, {1.2f, -0.4f}};
  for (int i = 0; i < 8; ++i) {
    burst.frames.push_back(toImage(scene::degrade(truth, shifts[i][0], shifts[i][1], 1.0f, 2, 0.02f, 40 + i)));
  }
  // Baseline: what an ordinary app does — bicubic 2x of one frame.
  const Plane lr = lumaOf(burst.frames[0]);
  auto bicubic = photoplus::resizeCubic(lr, 384, 384, nullptr);
  const double baseline = scene::psnr(*bicubic, truth, 16);

  pp_enhance_params p = restorationOnly(2);
  pp_image* single = nullptr;
  ASSERT_OK(pp_enhance(burst.frames.data(), 1, &p, nullptr, &single, nullptr));
  const double singleSr = scene::psnr(lumaOf(single), truth, 16);
  pp_image_destroy(single);

  pp_image* sr = nullptr;
  pp_enhance_report report{};
  ASSERT_OK(pp_enhance(burst.frames.data(), burst.frames.size(), &p, nullptr, &sr, &report));
  pp_image_info info{};
  pp_image_get_info(sr, &info);
  EXPECT_EQ(info.width, 384);
  const double multiSr = scene::psnr(lumaOf(sr), truth, 16);
  pp_image_destroy(sr);

  std::printf("    bicubic %.2f dB | single-frame SR %.2f dB | 8-frame SR %.2f dB\n", baseline, singleSr, multiSr);
  EXPECT_TRUE(singleSr > baseline + 0.3);
  EXPECT_TRUE(multiSr > baseline + 1.6);
  EXPECT_TRUE(multiSr > singleSr + 0.7);
}

TEST(deconvolution_recovers_blurred_detail) {
  const Plane clean = scene::makeScene(256, 256);
  Plane blurred = scene::degrade(clean, 0, 0, 2.0f, 1, 0.003f, 5);
  const double before = scene::psnr(blurred, clean);
  ASSERT_OK(enh::deconvolve(blurred, 2.0f, 20, 0.0005f, nullptr).status);
  const double after = scene::psnr(blurred, clean);
  std::printf("    blurred %.2f dB -> deconvolved %.2f dB\n", before, after);
  EXPECT_TRUE(after > before + 1.3);
}

TEST(single_frame_enhance_improves_weak_capture) {
  const Plane clean = scene::makeScene(256, 256);
  Burst burst;
  burst.frames.push_back(toImage(scene::degrade(clean, 0, 0, 1.6f, 1, 0.035f, 77)));
  const double before = scene::psnr(lumaOf(burst.frames[0]), clean);
  pp_enhance_params p = restorationOnly(1);
  pp_image* out = nullptr;
  pp_enhance_report report{};
  ASSERT_OK(pp_enhance(burst.frames.data(), 1, &p, nullptr, &out, &report));
  const double after = scene::psnr(lumaOf(out), clean);
  pp_image_destroy(out);
  std::printf("    weak capture %.2f dB -> enhanced %.2f dB (blur est %.2f px, noise est %.3f)\n", before, after,
              report.blur_before, report.noise_before);
  EXPECT_TRUE(after > before + 0.9);
  EXPECT_NEAR(report.blur_before, 1.6, 0.35);
  EXPECT_NEAR(report.noise_before, 0.035, 0.006);
}

TEST(moving_subject_does_not_ghost) {
  const Plane clean = scene::makeScene(192, 192);
  Burst burst;
  for (int i = 0; i < 5; ++i) {
    Plane f = scene::degrade(clean, 0.3f * static_cast<float>(i), -0.2f * static_cast<float>(i), 0.7f, 1, 0.02f, 90 + i);
    if (i == 3) {  // something crossed the frame in one shot
      for (int y = 80; y < 120; ++y)
        for (int x = 80; x < 120; ++x) f.row(y)[x] = 1.0f;
    }
    burst.frames.push_back(toImage(f));
  }
  pp_enhance_params p = restorationOnly(1);
  p.sharpen = 0.0f;
  pp_image* out = nullptr;
  ASSERT_OK(pp_enhance(burst.frames.data(), burst.frames.size(), &p, nullptr, &out, nullptr));
  const Plane fused = lumaOf(out);
  pp_image_destroy(out);
  double err = 0.0;
  for (int y = 85; y < 115; ++y)
    for (int x = 85; x < 115; ++x) err += std::fabs(fused.row(y)[x] - clean.row(y)[x]);
  err /= 30.0 * 30.0;
  // A naive average would add ~0.1-0.15 brightness here (1 of 5 frames white).
  EXPECT_TRUE(err < 0.04);
}

TEST(enhance_rejects_bad_input_and_does_not_leak) {
  const uint64_t before = liveBytes();
  {
    Burst burst;
    burst.frames.push_back(toImage(scene::makeScene(64, 64)));
    burst.frames.push_back(toImage(scene::makeScene(80, 64)));
    pp_image* out = nullptr;
    EXPECT_EQ(pp_enhance(burst.frames.data(), 2, nullptr, nullptr, &out, nullptr), PP_ERROR_INVALID_ARGUMENT);
    pp_enhance_params p{};
    pp_enhance_default_params(&p);
    p.upscale = 3;
    EXPECT_EQ(pp_enhance(burst.frames.data(), 1, &p, nullptr, &out, nullptr), PP_ERROR_INVALID_ARGUMENT);
    p.upscale = 1;
    p.denoise = NAN;
    EXPECT_EQ(pp_enhance(burst.frames.data(), 1, &p, nullptr, &out, nullptr), PP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(pp_enhance(nullptr, 0, nullptr, nullptr, &out, nullptr), PP_ERROR_INVALID_ARGUMENT);
    EXPECT_TRUE(out == nullptr);
  }
  EXPECT_EQ(liveBytes(), before);
}

TEST(enhance_is_cancellable_and_returns_memory) {
  const uint64_t before = liveBytes();
  {
    Burst burst;
    for (int i = 0; i < 4; ++i) burst.frames.push_back(toImage(scene::degrade(scene::makeScene(256, 256), 0.5f * i, 0, 1.0f, 1, 0.03f, i)));
    pp_cancel_token* token = pp_cancel_token_create();
    pp_cancel_token_cancel(token);
    pp_enhance_params p{};
    pp_enhance_default_params(&p);
    pp_image* out = nullptr;
    EXPECT_EQ(pp_enhance(burst.frames.data(), burst.frames.size(), &p, token, &out, nullptr), PP_ERROR_CANCELLED);
    pp_cancel_token_destroy(token);
    // Full run with tone stages also completes and frees all scratch planes.
    ASSERT_OK(pp_enhance(burst.frames.data(), burst.frames.size(), &p, nullptr, &out, nullptr));
    pp_image_destroy(out);
  }
  EXPECT_EQ(liveBytes(), before);
}

TEST(enhance_respects_memory_budget) {
  Burst burst;
  burst.frames.push_back(toImage(scene::makeScene(512, 512)));
  const uint64_t before = liveBytes();
  pp_memory_set_budget(before + 2 * 1024 * 1024);  // far too little for 2x SR of 512px
  pp_enhance_params p{};
  pp_enhance_default_params(&p);
  p.upscale = 2;
  pp_image* out = nullptr;
  EXPECT_EQ(pp_enhance(burst.frames.data(), 1, &p, nullptr, &out, nullptr), PP_ERROR_BUDGET_EXCEEDED);
  pp_memory_set_budget(0);
  EXPECT_EQ(liveBytes(), before);
}

TEST(quality_analysis_flags_blur_and_noise) {
  pp_image* sharp = toImage(scene::degrade(scene::makeScene(256, 256), 0, 0, 0.6f, 1, 0.005f, 1));
  pp_image* bad = toImage(scene::degrade(scene::makeScene(256, 256), 0, 0, 2.5f, 1, 0.05f, 2));
  pp_quality_report a{}, b{};
  ASSERT_OK(pp_analyze_quality(sharp, &a));
  ASSERT_OK(pp_analyze_quality(bad, &b));
  EXPECT_TRUE(b.noise_sigma > 3.0f * a.noise_sigma);
  EXPECT_TRUE(b.blur_sigma > a.blur_sigma + 1.0f);
  EXPECT_TRUE(a.sharpness > b.sharpness);  // noise must not masquerade as detail
  pp_image_destroy(sharp);
  pp_image_destroy(bad);
}

TEST(focus_peaking_marks_edges_in_place) {
  // Simulate a CameraX Y plane with row padding (stride > width).
  const int w = 120, h = 80, stride = 128;
  std::vector<uint8_t> y(static_cast<size_t>(stride) * h, 40);
  for (int r = 0; r < h; ++r)
    for (int c = 60; c < w; ++c) y[static_cast<size_t>(r) * stride + c] = 220;  // vertical edge at x=60
  std::vector<uint32_t> out(60 * 40, 0xDEADBEEF);
  float sharp = -1.0f;
  ASSERT_OK(pp_focus_peaking(y.data(), w, h, stride, 1, 0.15f, 0xFFFF3B6Bu, out.data(), 60, 40, &sharp));
  EXPECT_EQ(out[20 * 60 + 30], 0xFFFF3B6Bu);  // on the edge
  EXPECT_EQ(out[20 * 60 + 5], 0u);            // flat
  EXPECT_TRUE(sharp > 0.0f);
  EXPECT_EQ(pp_focus_peaking(y.data(), w, h, 100, 1, 0.1f, 0, out.data(), 60, 40, nullptr), PP_ERROR_INVALID_ARGUMENT);
}

TEST(enhance_into_writes_caller_buffer_without_extra_allocation) {
  Burst burst;
  burst.frames.push_back(toImage(scene::degrade(scene::makeScene(128, 96), 0, 0, 1.0f, 1, 0.02f, 3)));
  pp_enhance_params p = restorationOnly(2);
  std::vector<uint8_t> host(256 * 192 * 4 + 64, 0);
  pp_image* dst = nullptr;
  ASSERT_OK(pp_image_wrap(host.data(), 256, 192, 256 * 4, PP_FORMAT_RGBA8, &dst));
  pp_image* ref = nullptr;
  ASSERT_OK(pp_enhance(burst.frames.data(), 1, &p, nullptr, &ref, nullptr));
  ASSERT_OK(pp_enhance_into(burst.frames.data(), 1, &p, nullptr, dst, nullptr));
  // Same pixels as the allocating variant.
  EXPECT_TRUE(std::memcmp(pp_image_pixels(ref), host.data(), 256 * 192 * 4) == 0);
  pp_image_destroy(ref);
  pp_image* wrong = nullptr;
  ASSERT_OK(pp_image_wrap(host.data(), 128, 96, 128 * 4, PP_FORMAT_RGBA8, &wrong));
  EXPECT_EQ(pp_enhance_into(burst.frames.data(), 1, &p, nullptr, wrong, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_image_destroy(wrong);
  pp_image_destroy(dst);
}

// ---- Real-phone findings (Phase 6) ------------------------------------------------------

namespace {
double meanLaplacian(const Plane& p) {
  double s = 0;
  size_t n = 0;
  for (int y = 1; y < p.height() - 1; ++y)
    for (int x = 1; x < p.width() - 1; ++x, ++n)
      s += std::fabs(4 * p.row(y)[x] - p.row(y)[x - 1] - p.row(y)[x + 1] - p.row(y - 1)[x] - p.row(y + 1)[x]);
  return s / static_cast<double>(n);
}
}  // namespace

TEST(enhance_does_not_oversharpen_a_photo_the_phone_already_sharpened) {
  // A phone JPEG: optics soften the scene, then the phone's own processing
  // sharpens it again (unsharp mask). Deblurring that on top must not turn
  // fine texture crunchy: the detail budget caps the gain.
  const Plane scene = scene::makeScene(480, 360, 11);
  // Optics and the phone's noise reduction leave edges soft and fine
  // texture flat; then the phone's sharpening steepens the edges again.
  Plane soft = scene::degrade(scene, 0.0f, 0.0f, 2.0f, 1, 0.0015f, 3);
  auto blurred = Plane::allocate(soft.width(), soft.height());
  ASSERT_OK(photoplus::gaussianBlur(soft, *blurred, 2.5f, nullptr).status);
  for (size_t i = 0; i < soft.size(); ++i) soft.data()[i] += 0.8f * (soft.data()[i] - blurred->data()[i]);
  pp_image* in = toImage(soft);
  pp_enhance_params p = restorationOnly(1);
  pp_image* out = nullptr;
  ASSERT_OK(pp_enhance(&in, 1, &p, nullptr, &out, nullptr));
  const double before = meanLaplacian(lumaOf(in)), after = meanLaplacian(lumaOf(out));
  std::printf("    fine texture x%.2f\n", after / before);
  EXPECT_TRUE(after / before < 1.8);
  pp_image_destroy(in);
  pp_image_destroy(out);
}

TEST(white_balance_neutralises_the_light_but_keeps_skin_warm) {
  // Warm light on a grey wall with a person: the wall becomes neutral, the
  // skin keeps most of its warmth (a fully neutralised skin reads ashy).
  const int w = 320, h = 240;
  pp_image* img = nullptr;
  ASSERT_OK(pp_image_create(w, h, PP_FORMAT_RGBA8, &img));
  auto* px = static_cast<uint8_t*>(pp_image_pixels(img));
  auto inSkin = [](int x, int y) { return (x - 160) * (x - 160) / 3600.0 + (y - 120) * (y - 120) / 6400.0 < 1.0; };
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      uint8_t* q = px + (static_cast<size_t>(y) * w + x) * 4;
      const int t = static_cast<int>((x * 7 + y * 13) % 9) - 4;
      if (inSkin(x, y)) {
        q[0] = static_cast<uint8_t>(118 + t), q[1] = static_cast<uint8_t>(76 + t), q[2] = static_cast<uint8_t>(54 + t);  // deep skin
      } else {
        q[0] = static_cast<uint8_t>(160 + t), q[1] = static_cast<uint8_t>(154 + t), q[2] = static_cast<uint8_t>(140 + t);  // grey wall in warm light
      }
      q[3] = 255;
    }
  pp_enhance_params p{};
  pp_enhance_default_params(&p);
  p.denoise = p.sharpen = p.detail = p.vibrance = 0.0f;  // white balance (and levels) only
  pp_image* out = nullptr;
  ASSERT_OK(pp_enhance(&img, 1, &p, nullptr, &out, nullptr));
  auto warmth = [&](pp_image* im, bool skin) {  // (R - B) / luma, averaged
    const auto* d = static_cast<const uint8_t*>(pp_image_pixels(im));
    pp_image_info info{};
    pp_image_get_info(im, &info);
    double s = 0;
    int n = 0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        if (inSkin(x, y) != skin) continue;
        const uint8_t* q = d + static_cast<size_t>(y) * info.stride + x * 4;
        s += (q[0] - q[2]) / (0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2] + 1.0);
        ++n;
      }
    return s / n;
  };
  const double wall0 = warmth(img, false), wall1 = warmth(out, false), skin0 = warmth(img, true), skin1 = warmth(out, true);
  std::printf("    wall warmth %.3f -> %.3f, skin warmth %.3f -> %.3f\n", wall0, wall1, skin0, skin1);
  EXPECT_TRUE(wall1 < wall0 * 0.8);  // the light's cast is corrected
  EXPECT_TRUE(skin1 > skin0 * 0.93);  // the skin keeps its own warmth (0.89 without protection)
  pp_image_destroy(img);
  pp_image_destroy(out);
}

TEST(detail_budget_caps_texture_gain_and_leaves_moderate_restoration_alone) {
  const Plane scene = scene::makeScene(200, 150, 5);
  auto soft = Plane::allocate(200, 150);
  ASSERT_OK(photoplus::gaussianBlur(scene, *soft, 1.0f, nullptr).status);
  // Over-restored: three times the fine detail of the soft image.
  auto harsh = clonePlane(*soft);
  for (size_t i = 0; i < harsh->size(); ++i) harsh->data()[i] += 2.0f * (soft->data()[i] - scene.data()[i]) * -1.0f;
  const float before = photoplus::enhance::fineTexture(*soft);
  const float kept = photoplus::enhance::applyDetailBudget(*soft, *harsh, 1.7f);
  const float after = photoplus::enhance::fineTexture(*harsh);
  std::printf("    capped to x%.2f (kept %.0f%% of the change)\n", after / before, 100.0 * kept);
  EXPECT_TRUE(kept < 1.0f);
  EXPECT_NEAR(after / before, 1.7, 0.05);
  // Moderate restoration is not touched.
  auto gentle = clonePlane(*soft);
  for (size_t i = 0; i < gentle->size(); ++i) gentle->data()[i] += 0.2f * (scene.data()[i] - soft->data()[i]);
  auto copy = clonePlane(*gentle);
  EXPECT_EQ(photoplus::enhance::applyDetailBudget(*soft, *gentle, 1.7f), 1.0f);
  for (size_t i = 0; i < gentle->size(); ++i) EXPECT_TRUE(gentle->data()[i] == copy->data()[i]);
}

TEST(auto_levels_brighten_a_dim_photo_without_lifting_its_blacks) {
  // A dim indoor frame (median ~0.22): mid-tones come up, deep shadows stay
  // deep (a gamma lift would raise them steeply: a hazy, flat look).
  const Plane scene = scene::makeScene(320, 240, 8);
  pp_image* img = nullptr;
  ASSERT_OK(pp_image_create(320, 240, PP_FORMAT_RGBA8, &img));
  auto* px = static_cast<uint8_t*>(pp_image_pixels(img));
  for (int i = 0; i < 320 * 240; ++i) {
    const float v = 0.03f + 0.45f * scene.data()[i] * scene.data()[i];  // dim, with real blacks
    px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = static_cast<uint8_t>(std::lround(v * 255));
    px[i * 4 + 3] = 255;
  }
  pp_enhance_params p{};
  pp_enhance_default_params(&p);
  p.denoise = p.sharpen = p.detail = p.vibrance = 0.0f;
  pp_image* out = nullptr;
  ASSERT_OK(pp_enhance(&img, 1, &p, nullptr, &out, nullptr));
  auto pct = [](pp_image* im, float q) {
    const auto* d = static_cast<const uint8_t*>(pp_image_pixels(im));
    std::vector<int> v(320 * 240);
    for (int i = 0; i < 320 * 240; ++i) v[static_cast<size_t>(i)] = d[i * 4 + 1];
    std::nth_element(v.begin(), v.begin() + static_cast<long>(q * (v.size() - 1)), v.end());
    return v[static_cast<size_t>(q * (v.size() - 1))];
  };
  std::printf("    median %d -> %d, shadows (5%%) %d -> %d\n", pct(img, 0.5f), pct(out, 0.5f), pct(img, 0.05f), pct(out, 0.05f));
  EXPECT_TRUE(pct(out, 0.5f) > pct(img, 0.5f) + 10);       // brighter mid-tones
  EXPECT_TRUE(pct(out, 0.05f) <= pct(img, 0.05f) * 1.6 + 2);  // blacks stay deep
  pp_image_destroy(img);
  pp_image_destroy(out);
}
