#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

// RAII helpers so a failing assertion can never leak and pollute the
// memory-accounting tests that follow.
struct ImageHandle {
  pp_image* ptr = nullptr;
  ~ImageHandle() { pp_image_destroy(ptr); }
};
struct TokenHandle {
  pp_cancel_token* ptr = pp_cancel_token_create();
  ~TokenHandle() { pp_cancel_token_destroy(ptr); }
};

// Creates a deterministic RGBA gradient test image.
pp_image* makeGradient(int w, int h) {
  pp_image* img = nullptr;
  if (pp_image_create(w, h, PP_FORMAT_RGBA8, &img) != PP_OK) return nullptr;
  auto* px = static_cast<uint8_t*>(pp_image_pixels(img));
  pp_image_info info{};
  pp_image_get_info(img, &info);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint8_t* p = px + y * info.stride + x * 4;
      p[0] = static_cast<uint8_t>(x * 255 / (w > 1 ? w - 1 : 1));
      p[1] = static_cast<uint8_t>(y * 255 / (h > 1 ? h - 1 : 1));
      p[2] = static_cast<uint8_t>((x + y) % 256);
      p[3] = 200;
    }
  }
  return img;
}

const uint8_t* pixelAt(pp_image* img, int x, int y) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  const int bpp = info.format == PP_FORMAT_GRAY8 ? 1 : (info.format == PP_FORMAT_RGB8 ? 3 : 4);
  return static_cast<const uint8_t*>(pp_image_pixels(img)) + y * info.stride + x * bpp;
}

pp_op op(int type, float a = 0, float b = 0, float c = 0, float d = 0) {
  pp_op o{};
  o.type = type;
  o.p[0] = a;
  o.p[1] = b;
  o.p[2] = c;
  o.p[3] = d;
  return o;
}

uint64_t liveBytes() {
  pp_memory_stats s{};
  pp_memory_get_stats(&s);
  return s.current_bytes;
}

}  // namespace

TEST(version_is_reported) { EXPECT_TRUE(std::strcmp(pp_version(), "0.1.0") == 0); }

TEST(create_rejects_invalid_dimensions) {
  pp_image* img = nullptr;
  EXPECT_EQ(pp_image_create(0, 10, PP_FORMAT_RGBA8, &img), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_TRUE(img == nullptr);
  EXPECT_EQ(pp_image_create(-5, 10, PP_FORMAT_RGBA8, &img), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_image_create(20000, 10, PP_FORMAT_RGBA8, &img), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_image_create(10, 10, static_cast<pp_pixel_format>(99), &img),
            PP_ERROR_UNSUPPORTED_FORMAT);
  EXPECT_TRUE(std::strlen(pp_last_error_message()) > 0);
  EXPECT_EQ(pp_image_create(10, 10, PP_FORMAT_RGBA8, nullptr), PP_ERROR_INVALID_ARGUMENT);
}

TEST(create_and_destroy_returns_memory) {
  const uint64_t before = liveBytes();
  {
    ImageHandle img;
    ASSERT_OK(pp_image_create(64, 32, PP_FORMAT_RGBA8, &img.ptr));
    EXPECT_EQ(liveBytes() - before, 64u * 32u * 4u);
    pp_image_info info{};
    ASSERT_OK(pp_image_get_info(img.ptr, &info));
    EXPECT_EQ(info.width, 64);
    EXPECT_EQ(info.height, 32);
    EXPECT_EQ(info.stride, 256);
    EXPECT_EQ(info.owns_pixels, 1);
  }
  EXPECT_EQ(liveBytes(), before);
}

TEST(wrap_does_not_own_or_track_memory) {
  std::vector<uint8_t> host(16 * 8 * 4 + 64, 7);
  const uint64_t before = liveBytes();
  ImageHandle img;
  ASSERT_OK(pp_image_wrap(host.data(), 16, 8, 16 * 4 + 8, PP_FORMAT_RGBA8, &img.ptr));
  EXPECT_EQ(liveBytes(), before);
  pp_image_info info{};
  pp_image_get_info(img.ptr, &info);
  EXPECT_EQ(info.owns_pixels, 0);
  EXPECT_EQ(info.stride, 72);
  pp_image* bad = nullptr;
  EXPECT_EQ(pp_image_wrap(host.data(), 16, 8, 10, PP_FORMAT_RGBA8, &bad),
            PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_image_wrap(nullptr, 16, 8, 64, PP_FORMAT_RGBA8, &bad), PP_ERROR_INVALID_ARGUMENT);
}

TEST(empty_pipeline_is_identity_copy) {
  ImageHandle in{makeGradient(31, 17)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, nullptr, 0, nullptr, &out.ptr));
  for (int y = 0; y < 17; ++y) {
    for (int x = 0; x < 31; ++x) {
      EXPECT_TRUE(std::memcmp(pixelAt(in.ptr, x, y), pixelAt(out.ptr, x, y), 4) == 0);
    }
  }
}

TEST(input_is_never_mutated) {
  ImageHandle in{makeGradient(20, 20)};
  const uint8_t before = pixelAt(in.ptr, 10, 10)[0];
  pp_op ops[] = {op(PP_OP_TONE, 2.0f, 0.3f, 0.5f), op(PP_OP_GRAYSCALE), op(PP_OP_FLIP, 1, 1)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 3, nullptr, &out.ptr));
  EXPECT_EQ(pixelAt(in.ptr, 10, 10)[0], before);
}

TEST(tone_neutral_params_are_identity) {
  ImageHandle in{makeGradient(40, 3)};
  pp_op ops[] = {op(PP_OP_TONE, 0, 0, 0)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 1, nullptr, &out.ptr));
  for (int x = 0; x < 40; ++x) {
    for (int c = 0; c < 4; ++c) {
      EXPECT_NEAR(pixelAt(out.ptr, x, 1)[c], pixelAt(in.ptr, x, 1)[c], 1);
    }
  }
}

TEST(tone_exposure_brightens_and_preserves_alpha) {
  ImageHandle in{makeGradient(40, 3)};
  pp_op ops[] = {op(PP_OP_TONE, 1.0f, 0, 0)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 1, nullptr, &out.ptr));
  EXPECT_TRUE(pixelAt(out.ptr, 20, 1)[0] > pixelAt(in.ptr, 20, 1)[0]);
  EXPECT_EQ(pixelAt(out.ptr, 20, 1)[3], 200);
  // Pure white must stay white (no wrap-around).
  EXPECT_EQ(pixelAt(out.ptr, 39, 1)[0], 255);
}

TEST(tone_contrast_spreads_values_around_mid_grey) {
  ImageHandle in;
  ASSERT_OK(pp_image_create(2, 1, PP_FORMAT_GRAY8, &in.ptr));
  auto* px = static_cast<uint8_t*>(pp_image_pixels(in.ptr));
  px[0] = 64;
  px[1] = 192;
  pp_op ops[] = {op(PP_OP_TONE, 0, 0, 0.5f)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 1, nullptr, &out.ptr));
  EXPECT_TRUE(pixelAt(out.ptr, 0, 0)[0] < 64);
  EXPECT_TRUE(pixelAt(out.ptr, 1, 0)[0] > 192);
}

TEST(tone_rejects_non_finite_params) {
  ImageHandle in{makeGradient(4, 4)};
  pp_op ops[] = {op(PP_OP_TONE, NAN, 0, 0)};
  ImageHandle out;
  EXPECT_EQ(pp_process(in.ptr, ops, 1, nullptr, &out.ptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_TRUE(out.ptr == nullptr);
}

TEST(grayscale_equalises_channels) {
  ImageHandle in{makeGradient(16, 16)};
  pp_op ops[] = {op(PP_OP_GRAYSCALE)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 1, nullptr, &out.ptr));
  const uint8_t* p = pixelAt(out.ptr, 7, 9);
  EXPECT_EQ(p[0], p[1]);
  EXPECT_EQ(p[1], p[2]);
}

TEST(saturation_minus_one_is_grey) {
  ImageHandle in{makeGradient(16, 16)};
  pp_op ops[] = {op(PP_OP_SATURATION, -1.0f)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 1, nullptr, &out.ptr));
  const uint8_t* p = pixelAt(out.ptr, 12, 3);
  EXPECT_NEAR(p[0], p[1], 1);
  EXPECT_NEAR(p[1], p[2], 1);
}

TEST(rotate_clockwise_maps_corners) {
  ImageHandle in{makeGradient(5, 3)};
  pp_op ops[] = {op(PP_OP_ROTATE90, 1)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 1, nullptr, &out.ptr));
  pp_image_info info{};
  pp_image_get_info(out.ptr, &info);
  EXPECT_EQ(info.width, 3);
  EXPECT_EQ(info.height, 5);
  // Top-left of the source becomes top-right after a clockwise turn.
  EXPECT_TRUE(std::memcmp(pixelAt(in.ptr, 0, 0), pixelAt(out.ptr, 2, 0), 4) == 0);
  // Bottom-left of the source becomes top-left.
  EXPECT_TRUE(std::memcmp(pixelAt(in.ptr, 0, 2), pixelAt(out.ptr, 0, 0), 4) == 0);
}

TEST(rotate_four_quarter_turns_round_trips) {
  ImageHandle in{makeGradient(9, 4)};
  pp_op ops[] = {op(PP_OP_ROTATE90, 1), op(PP_OP_ROTATE90, -1), op(PP_OP_ROTATE90, 2),
                 op(PP_OP_ROTATE90, 2), op(PP_OP_ROTATE90, 7), op(PP_OP_ROTATE90, 1)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 6, nullptr, &out.ptr));
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 9; ++x)
      EXPECT_TRUE(std::memcmp(pixelAt(in.ptr, x, y), pixelAt(out.ptr, x, y), 4) == 0);
}

TEST(flip_horizontal_mirrors_rows) {
  ImageHandle in{makeGradient(6, 2)};
  pp_op ops[] = {op(PP_OP_FLIP, 1, 0)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 1, nullptr, &out.ptr));
  EXPECT_TRUE(std::memcmp(pixelAt(in.ptr, 0, 1), pixelAt(out.ptr, 5, 1), 4) == 0);
}

TEST(crop_extracts_region_and_validates_bounds) {
  ImageHandle in{makeGradient(50, 40)};
  pp_op ops[] = {op(PP_OP_CROP, 10, 5, 20, 15)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 1, nullptr, &out.ptr));
  pp_image_info info{};
  pp_image_get_info(out.ptr, &info);
  EXPECT_EQ(info.width, 20);
  EXPECT_EQ(info.height, 15);
  EXPECT_TRUE(std::memcmp(pixelAt(in.ptr, 10, 5), pixelAt(out.ptr, 0, 0), 4) == 0);

  pp_op bad[] = {op(PP_OP_CROP, 40, 0, 20, 10)};
  ImageHandle out2;
  EXPECT_EQ(pp_process(in.ptr, bad, 1, nullptr, &out2.ptr), PP_ERROR_INVALID_ARGUMENT);
  pp_op negative[] = {op(PP_OP_CROP, -1, 0, 5, 5)};
  EXPECT_EQ(pp_process(in.ptr, negative, 1, nullptr, &out2.ptr), PP_ERROR_INVALID_ARGUMENT);
  pp_op huge[] = {op(PP_OP_CROP, 1, 1, 3e9f, 5)};
  EXPECT_EQ(pp_process(in.ptr, huge, 1, nullptr, &out2.ptr), PP_ERROR_INVALID_ARGUMENT);
}

TEST(resize_produces_requested_size_and_smooth_values) {
  ImageHandle in{makeGradient(100, 50)};
  pp_op ops[] = {op(PP_OP_RESIZE, 25, 10)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 1, nullptr, &out.ptr));
  pp_image_info info{};
  pp_image_get_info(out.ptr, &info);
  EXPECT_EQ(info.width, 25);
  EXPECT_EQ(info.height, 10);
  // The horizontal ramp must remain monotonic.
  for (int x = 1; x < 25; ++x) EXPECT_TRUE(pixelAt(out.ptr, x, 5)[0] >= pixelAt(out.ptr, x - 1, 5)[0]);

  pp_op upscale[] = {op(PP_OP_RESIZE, 300, 150)};
  ImageHandle big;
  ASSERT_OK(pp_process(in.ptr, upscale, 1, nullptr, &big.ptr));
  pp_op zero[] = {op(PP_OP_RESIZE, 0, 10)};
  ImageHandle none;
  EXPECT_EQ(pp_process(in.ptr, zero, 1, nullptr, &none.ptr), PP_ERROR_INVALID_ARGUMENT);
}

TEST(sharpen_increases_local_contrast_on_edges) {
  ImageHandle in;
  ASSERT_OK(pp_image_create(8, 1, PP_FORMAT_GRAY8, &in.ptr));
  auto* px = static_cast<uint8_t*>(pp_image_pixels(in.ptr));
  for (int x = 0; x < 8; ++x) px[x] = x < 4 ? 80 : 160;
  pp_op ops[] = {op(PP_OP_SHARPEN, 1.0f)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 1, nullptr, &out.ptr));
  EXPECT_TRUE(pixelAt(out.ptr, 3, 0)[0] < 80);
  EXPECT_TRUE(pixelAt(out.ptr, 4, 0)[0] > 160);
  EXPECT_EQ(pixelAt(out.ptr, 0, 0)[0], 80);  // flat area unchanged
}

TEST(unknown_operation_is_rejected_without_leaking) {
  const uint64_t before = liveBytes();
  {
    ImageHandle in{makeGradient(32, 32)};
    pp_op ops[] = {op(PP_OP_GRAYSCALE), op(999)};
    ImageHandle out;
    EXPECT_EQ(pp_process(in.ptr, ops, 2, nullptr, &out.ptr), PP_ERROR_INVALID_ARGUMENT);
    EXPECT_TRUE(std::string(pp_last_error_message()).find("#1") != std::string::npos);
  }
  EXPECT_EQ(liveBytes(), before);
}

TEST(pre_cancelled_token_stops_immediately) {
  ImageHandle in{makeGradient(64, 64)};
  TokenHandle token;
  pp_cancel_token_cancel(token.ptr);
  EXPECT_EQ(pp_cancel_token_is_cancelled(token.ptr), 1);
  pp_op ops[] = {op(PP_OP_GRAYSCALE)};
  ImageHandle out;
  EXPECT_EQ(pp_process(in.ptr, ops, 1, token.ptr, &out.ptr), PP_ERROR_CANCELLED);
  EXPECT_TRUE(out.ptr == nullptr);
}

TEST(cancel_from_another_thread_interrupts_long_job) {
  const uint64_t before = liveBytes();
  {
    ImageHandle in{makeGradient(2048, 2048)};
    TokenHandle token;
    std::vector<pp_op> ops;
    for (int i = 0; i < 200; ++i) ops.push_back(op(PP_OP_SHARPEN, 0.5f));
    std::thread canceller([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      pp_cancel_token_cancel(token.ptr);
    });
    const auto start = std::chrono::steady_clock::now();
    ImageHandle out;
    const pp_status status = pp_process(in.ptr, ops.data(), ops.size(), token.ptr, &out.ptr);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    canceller.join();
    EXPECT_EQ(status, PP_ERROR_CANCELLED);
    // 200 full sharpen passes would take many seconds; cancel must be prompt.
    EXPECT_TRUE(elapsed < std::chrono::seconds(3));
  }
  EXPECT_EQ(liveBytes(), before);
}

TEST(memory_budget_fails_cleanly_instead_of_overcommitting) {
  const uint64_t before = liveBytes();
  pp_memory_set_budget(before + 100 * 100 * 4 + 1024);
  {
    ImageHandle small;
    ASSERT_OK(pp_image_create(100, 100, PP_FORMAT_RGBA8, &small.ptr));
    ImageHandle big;
    EXPECT_EQ(pp_image_create(1000, 1000, PP_FORMAT_RGBA8, &big.ptr), PP_ERROR_BUDGET_EXCEEDED);
    EXPECT_TRUE(big.ptr == nullptr);
    // A pipeline needing a second buffer must also fail cleanly.
    ImageHandle out;
    EXPECT_EQ(pp_process(small.ptr, nullptr, 0, nullptr, &out.ptr), PP_ERROR_BUDGET_EXCEEDED);
  }
  pp_memory_set_budget(0);
  EXPECT_EQ(liveBytes(), before);
  pp_memory_stats stats{};
  pp_memory_get_stats(&stats);
  EXPECT_TRUE(stats.failed_allocations >= 2);
}

TEST(repeated_processing_has_no_memory_growth) {
  // Acceptance criterion: process test images without uncontrolled memory
  // growth. 300 iterations of a mixed pipeline must return to baseline and
  // never exceed a bounded peak.
  pp_memory_reset_peak();
  const uint64_t before = liveBytes();
  ImageHandle in{makeGradient(640, 480)};
  const uint64_t inputBytes = 640u * 480u * 4u;
  pp_memory_reset_peak();
  pp_op ops[] = {op(PP_OP_TONE, 0.5f, 0.1f, 0.2f), op(PP_OP_ROTATE90, 1),
                 op(PP_OP_CROP, 10, 10, 400, 600), op(PP_OP_RESIZE, 320, 240),
                 op(PP_OP_SHARPEN, 0.8f), op(PP_OP_SATURATION, 0.3f)};
  for (int i = 0; i < 300; ++i) {
    ImageHandle out;
    ASSERT_OK(pp_process(in.ptr, ops, 6, nullptr, &out.ptr));
  }
  EXPECT_EQ(liveBytes(), before + inputBytes);
  pp_memory_stats stats{};
  pp_memory_get_stats(&stats);
  // Peak: input + working clone + one replacement buffer.
  EXPECT_TRUE(stats.peak_bytes <= before + inputBytes * 3);
}

TEST(concurrent_jobs_are_isolated) {
  const uint64_t before = liveBytes();
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&failures, t] {
      for (int i = 0; i < 25; ++i) {
        pp_image* in = makeGradient(128 + t, 96);
        pp_op ops[] = {op(PP_OP_TONE, 0.2f * t, 0, 0), op(PP_OP_RESIZE, 64, 48)};
        pp_image* out = nullptr;
        if (in == nullptr || pp_process(in, ops, 2, nullptr, &out) != PP_OK) failures++;
        pp_image_destroy(out);
        pp_image_destroy(in);
      }
    });
  }
  for (auto& th : threads) th.join();
  EXPECT_EQ(failures.load(), 0);
  EXPECT_EQ(liveBytes(), before);
}

TEST(errors_are_thread_local) {
  pp_image* img = nullptr;
  EXPECT_EQ(pp_image_create(0, 0, PP_FORMAT_RGBA8, &img), PP_ERROR_INVALID_ARGUMENT);
  std::string other;
  std::thread([&] { other = pp_last_error_message(); }).join();
  EXPECT_TRUE(other.empty());
  EXPECT_TRUE(std::strlen(pp_last_error_message()) > 0);
}

TEST(copy_pixels_requires_matching_images) {
  ImageHandle a{makeGradient(10, 10)};
  ImageHandle b;
  ASSERT_OK(pp_image_create(10, 10, PP_FORMAT_RGBA8, &b.ptr));
  ASSERT_OK(pp_image_copy_pixels(a.ptr, b.ptr));
  EXPECT_TRUE(std::memcmp(pixelAt(a.ptr, 9, 9), pixelAt(b.ptr, 9, 9), 4) == 0);
  ImageHandle c;
  ASSERT_OK(pp_image_create(11, 10, PP_FORMAT_RGBA8, &c.ptr));
  EXPECT_EQ(pp_image_copy_pixels(a.ptr, c.ptr), PP_ERROR_INVALID_ARGUMENT);
}

TEST(profiler_reports_operation_timings) {
  pp_profiler_reset();
  ImageHandle in{makeGradient(64, 64)};
  pp_op ops[] = {op(PP_OP_TONE, 1, 0, 0), op(PP_OP_RESIZE, 32, 32)};
  ImageHandle out;
  ASSERT_OK(pp_process(in.ptr, ops, 2, nullptr, &out.ptr));
  char buffer[1024];
  const size_t needed = pp_profiler_report_json(buffer, sizeof(buffer));
  EXPECT_TRUE(needed > 0 && needed < sizeof(buffer));
  const std::string json(buffer);
  EXPECT_TRUE(json.find("\"op.tone\":{\"calls\":1") != std::string::npos);
  EXPECT_TRUE(json.find("\"pipeline.total\"") != std::string::npos);
  // Truncated buffers stay NUL terminated.
  char tiny[8];
  pp_profiler_report_json(tiny, sizeof(tiny));
  EXPECT_EQ(std::strlen(tiny), 7u);
}
