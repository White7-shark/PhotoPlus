// Phase 4: the core pipeline (resampling, filters, geometry, colour,
// compositing, masks, codec) and its rendering infrastructure (thread pool,
// progress, buffer reuse, previews, memory estimates, predictable errors).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "photoplus/parallel.hpp"
#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

struct Img {
  pp_image* ptr = nullptr;
  Img() = default;
  explicit Img(pp_image* p) : ptr(p) {}
  ~Img() { pp_image_destroy(ptr); }
  Img(const Img&) = delete;
  Img& operator=(const Img&) = delete;
};
struct Token {
  pp_cancel_token* ptr = pp_cancel_token_create();
  ~Token() { pp_cancel_token_destroy(ptr); }
};

pp_image_info infoOf(const pp_image* img) {
  pp_image_info i{};
  pp_image_get_info(img, &i);
  return i;
}

int bppOf(int format) { return format == PP_FORMAT_GRAY8 ? 1 : format == PP_FORMAT_RGB8 ? 3 : 4; }

uint8_t* px(pp_image* img, int x, int y) {
  const auto i = infoOf(img);
  return static_cast<uint8_t*>(pp_image_pixels(img)) + y * i.stride + x * bppOf(i.format);
}

pp_image* make(int w, int h, int format = PP_FORMAT_RGBA8) {
  pp_image* img = nullptr;
  pp_image_create(w, h, format, &img);
  return img;
}

template <typename F>
pp_image* fill(int w, int h, F&& f, int format = PP_FORMAT_RGBA8) {
  pp_image* img = make(w, h, format);
  const int bpp = bppOf(format);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint8_t v[4] = {0, 0, 0, 255};
      f(x, y, v);
      std::memcpy(px(img, x, y), v, static_cast<size_t>(bpp));
    }
  }
  return img;
}

// A textured "photo": gradients, edges and fine detail.
pp_image* scene(int w, int h) {
  return fill(w, h, [&](int x, int y, uint8_t* v) {
    const float fx = static_cast<float>(x) / w, fy = static_cast<float>(y) / h;
    float r = 60 + 150 * fx, g = 80 + 120 * fy, b = 120 + 60 * std::sin(fx * 12.0f);
    if (((x / 37) + (y / 29)) % 2 == 0) r += 40, g -= 20;
    if (std::abs(x - w / 2) < w / 8 && std::abs(y - h / 2) < h / 8) r = 230, g = 210, b = 60;
    r += 18 * std::sin(x * 0.9f) * std::cos(y * 0.7f);
    v[0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f));
    v[1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f));
    v[2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f));
    v[3] = 255;
  });
}

pp_op op(int type, float a = 0, float b = 0, float c = 0, float d = 0) {
  pp_op o{};
  o.type = type;
  o.p[0] = a, o.p[1] = b, o.p[2] = c, o.p[3] = d;
  return o;
}

double meanAbsDiff(pp_image* a, pp_image* b, int channels = 3) {
  const auto ia = infoOf(a), ib = infoOf(b);
  if (ia.width != ib.width || ia.height != ib.height) return 1e9;
  double sum = 0;
  for (int y = 0; y < ia.height; ++y) {
    for (int x = 0; x < ia.width; ++x) {
      for (int c = 0; c < channels; ++c) sum += std::abs(px(a, x, y)[c] - px(b, x, y)[c]);
    }
  }
  return sum / (static_cast<double>(ia.width) * ia.height * channels);
}

uint64_t liveBytes() {
  pp_memory_stats s{};
  pp_memory_get_stats(&s);
  return s.current_bytes;
}

}  // namespace

// ---- infrastructure -----------------------------------------------------------------

TEST(core_thread_pool_runs_nested_loops_without_deadlock) {
  std::atomic<long> total{0};
  photoplus::parallelFor(64, nullptr, [&](int32_t a, int32_t b) {
    for (int32_t i = a; i < b; ++i) {
      // Each task runs another parallel loop: must not deadlock.
      photoplus::parallelFor(200, nullptr, [&](int32_t c, int32_t d) { total += d - c; });
    }
  });
  EXPECT_EQ(total.load(), 64L * 200L);
  // Many tiny loops cost little: no threads are started per call.
  const auto t0 = std::chrono::steady_clock::now();
  std::atomic<long> n{0};
  for (int i = 0; i < 2000; ++i) photoplus::parallelFor(256, nullptr, [&](int32_t a, int32_t b) { n += b - a; });
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("    2000 small parallel loops: %.1f ms\n", ms);
  EXPECT_EQ(n.load(), 2000L * 256L);
  EXPECT_TRUE(ms < 2000.0);
}

TEST(core_progress_rises_to_completion_and_cancel_stops_quickly) {
  Img in{scene(1600, 1200)};
  const pp_op ops[] = {op(PP_OP_BLUR, 3), op(PP_OP_SHARPEN, 1, 1.5f), op(PP_OP_DENOISE, 0.5f), op(PP_OP_RESIZE, 800, 600)};
  Token tok;
  std::atomic<bool> done{false};
  std::vector<float> seen;
  std::thread watcher([&] {
    while (!done.load()) {
      seen.push_back(pp_cancel_token_progress(tok.ptr));
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  });
  Img out;
  const pp_status st = pp_process(in.ptr, ops, 4, tok.ptr, &out.ptr);
  done = true;
  watcher.join();
  ASSERT_OK(st);
  bool monotonic = true;
  for (size_t i = 1; i < seen.size(); ++i) monotonic = monotonic && seen[i] >= seen[i - 1];
  int distinct = 0;
  for (size_t i = 1; i < seen.size(); ++i) distinct += seen[i] != seen[i - 1] ? 1 : 0;
  std::printf("    progress samples %zu, distinct steps %d, final %.2f\n", seen.size(), distinct,
              static_cast<double>(pp_cancel_token_progress(tok.ptr)));
  EXPECT_TRUE(monotonic);
  EXPECT_TRUE(distinct >= 4);
  EXPECT_NEAR(pp_cancel_token_progress(tok.ptr), 1.0, 1e-6);

  // Cancelling mid-way returns CANCELLED promptly and leaks nothing.
  const uint64_t before = liveBytes();
  Token cancel;
  std::thread canceller([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    pp_cancel_token_cancel(cancel.ptr);
  });
  const auto t0 = std::chrono::steady_clock::now();
  Img out2;
  const pp_status cst = pp_process(in.ptr, ops, 4, cancel.ptr, &out2.ptr);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  canceller.join();
  std::printf("    cancelled after %.0f ms\n", ms);
  EXPECT_TRUE(cst == PP_ERROR_CANCELLED || cst == PP_OK);
  EXPECT_TRUE(out2.ptr == nullptr || cst == PP_OK);
  EXPECT_EQ(liveBytes(), before);
}

TEST(core_buffers_are_reused_and_trimmed) {
  pp_memory_trim();
  Img in{scene(800, 600)};
  const pp_op ops[] = {op(PP_OP_BLUR, 2), op(PP_OP_SHARPEN, 0.8f)};
  pp_memory_stats a{}, b{};
  {
    Img out;
    ASSERT_OK(pp_process(in.ptr, ops, 2, nullptr, &out.ptr));
  }
  pp_memory_get_stats(&a);
  {
    Img out;
    ASSERT_OK(pp_process(in.ptr, ops, 2, nullptr, &out.ptr));
  }
  pp_memory_get_stats(&b);
  std::printf("    reused %llu allocations, %llu KB kept\n", static_cast<unsigned long long>(b.reused_allocations - a.reused_allocations),
              static_cast<unsigned long long>(b.cached_bytes / 1024));
  EXPECT_TRUE(b.reused_allocations > a.reused_allocations);
  EXPECT_TRUE(b.cached_bytes > 0);
  // Kept buffers give way to a tight budget instead of failing allocations.
  const uint64_t live = liveBytes();
  pp_memory_set_budget(live + 800 * 600 * 4 * 3);
  {
    Img out;
    ASSERT_OK(pp_process(in.ptr, ops, 1, nullptr, &out.ptr));
  }
  pp_memory_set_budget(0);
  pp_memory_trim();
  pp_memory_stats c{};
  pp_memory_get_stats(&c);
  EXPECT_EQ(c.cached_bytes, 0ull);
}

TEST(core_estimates_bound_the_real_peak) {
  struct Case {
    const char* name;
    std::vector<pp_op> ops;
  };
  const std::vector<Case> cases = {
      {"colour", {op(PP_OP_TONE, 0.3f, 0.1f, 0.2f), op(PP_OP_SATURATION, 0.2f), op(PP_OP_FILTER, 3, 0.8f)}},
      {"geometry", {op(PP_OP_ROTATE90, 1), op(PP_OP_CROP, 50, 40, 700, 900), op(PP_OP_PERSPECTIVE, 0.4f, -0.2f)}},
      {"detail", {op(PP_OP_SHARPEN, 1.2f, 2.0f, 0.02f), op(PP_OP_BLUR, 1.5f), op(PP_OP_DENOISE, 0.7f)}},
      {"resample", {op(PP_OP_RESIZE, 2400, 1800, PP_RESIZE_LANCZOS), op(PP_OP_BLUR, 24), op(PP_OP_RESIZE, 300, 200)}},
  };
  Img in{scene(1200, 900)};
  for (const auto& c : cases) {
    uint64_t est = 0;
    ASSERT_OK(pp_process_estimate(1200, 900, PP_FORMAT_RGBA8, c.ops.data(), c.ops.size(), nullptr, &est));
    pp_memory_trim();
    const uint64_t base = liveBytes() - 1200ull * 900 * 4;  // the input is part of the estimate
    pp_memory_reset_peak();
    Img out;
    ASSERT_OK(pp_process(in.ptr, c.ops.data(), c.ops.size(), nullptr, &out.ptr));
    pp_memory_stats s{};
    pp_memory_get_stats(&s);
    const uint64_t peak = s.peak_bytes - base;
    std::printf("    %-9s peak %6.1f MB, estimate %6.1f MB\n", c.name, peak / 1e6, est / 1e6);
    EXPECT_TRUE(peak <= est);
    EXPECT_TRUE(est < peak * 4 + (64ull << 20));  // useful, not absurdly loose
  }
}

TEST(core_invalid_requests_fail_predictably) {
  Img in{scene(64, 48)};
  Img out;
  const pp_op nan[] = {op(PP_OP_TONE, std::nanf(""))};
  EXPECT_EQ(pp_process(in.ptr, nan, 1, nullptr, &out.ptr), PP_ERROR_INVALID_ARGUMENT);
  const pp_op badCrop[] = {op(PP_OP_CROP, 10, 10, 100, 10)};
  EXPECT_EQ(pp_process(in.ptr, badCrop, 1, nullptr, &out.ptr), PP_ERROR_INVALID_ARGUMENT);
  const pp_op badPersp[] = {op(PP_OP_PERSPECTIVE, 2.0f)};
  EXPECT_EQ(pp_process(in.ptr, badPersp, 1, nullptr, &out.ptr), PP_ERROR_INVALID_ARGUMENT);
  const pp_op badFilter[] = {op(PP_OP_RESIZE, 10, 10, 9)};
  EXPECT_EQ(pp_process(in.ptr, badFilter, 1, nullptr, &out.ptr), PP_ERROR_INVALID_ARGUMENT);
  const pp_op unknown[] = {op(999)};
  EXPECT_EQ(pp_process(in.ptr, unknown, 1, nullptr, &out.ptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_TRUE(std::strstr(pp_last_error_message(), "#0") != nullptr);
  pp_render_options opts{};
  opts.max_edge = -1;
  EXPECT_EQ(pp_process_ex(in.ptr, nullptr, 0, &opts, nullptr, &out.ptr), PP_ERROR_INVALID_ARGUMENT);
  uint64_t est = 0;
  EXPECT_EQ(pp_process_estimate(0, 10, PP_FORMAT_RGBA8, nullptr, 0, nullptr, &est), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_process_estimate(10, 10, 42, nullptr, 0, nullptr, &est), PP_ERROR_UNSUPPORTED_FORMAT);
  EXPECT_TRUE(out.ptr == nullptr);
  // Null arguments everywhere.
  EXPECT_EQ(pp_blend(nullptr, in.ptr, nullptr, 0, 0, 0, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_blend(in.ptr, in.ptr, nullptr, 0, 0, 77, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_mask_adjust(nullptr, PP_MASK_INVERT, 0, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_mask_adjust(in.ptr, PP_MASK_INVERT, 0, nullptr), PP_ERROR_UNSUPPORTED_FORMAT);
  EXPECT_EQ(pp_apply_lut3d(in.ptr, nullptr, 17, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_warp_perspective(in.ptr, nullptr, 10, 10, nullptr, &out.ptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_decode_memory(nullptr, 0, nullptr, nullptr, &out.ptr, nullptr), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_TRUE(std::strcmp(pp_status_name(PP_ERROR_IO), "IO_ERROR") == 0);
}

// ---- previews ---------------------------------------------------------------------------

TEST(core_previews_match_the_full_render) {
  Img in{scene(2400, 1800)};
  const pp_op ops[] = {op(PP_OP_CROP, 200, 150, 2000, 1500), op(PP_OP_BLUR, 6), op(PP_OP_TONE, 0.3f, 0.05f, 0.1f),
                       op(PP_OP_SHARPEN, 1.0f, 3.0f), op(PP_OP_PERSPECTIVE, 0.3f, 0)};
  Img full, preview;
  ASSERT_OK(pp_process(in.ptr, ops, 5, nullptr, &full.ptr));
  pp_render_options o{};
  o.max_edge = 600;
  o.backend = PP_BACKEND_CPU;
  ASSERT_OK(pp_process_ex(in.ptr, ops, 5, &o, nullptr, &preview.ptr));
  const auto ip = infoOf(preview.ptr), ifu = infoOf(full.ptr);
  std::printf("    full %dx%d, preview %dx%d\n", ifu.width, ifu.height, ip.width, ip.height);
  EXPECT_TRUE(std::max(ip.width, ip.height) <= 600);
  EXPECT_NEAR(static_cast<double>(ip.width) / ip.height, static_cast<double>(ifu.width) / ifu.height, 0.01);
  // The full render, reduced to the preview's size, looks the same.
  const pp_op shrink[] = {op(PP_OP_RESIZE, static_cast<float>(ip.width), static_cast<float>(ip.height), PP_RESIZE_AREA)};
  Img reduced;
  ASSERT_OK(pp_process(full.ptr, shrink, 1, nullptr, &reduced.ptr));
  const double d = meanAbsDiff(preview.ptr, reduced.ptr);
  std::printf("    preview vs reduced full render: mean difference %.2f levels\n", d);
  EXPECT_TRUE(d < 3.0);
}

// ---- resampling and filters ---------------------------------------------------------------

TEST(core_resize_filters_avoid_aliasing_and_fringes) {
  // A one-pixel checkerboard shrunk 3x: area/Lanczos give flat grey; bilinear aliases.
  Img checker{fill(300, 300, [](int x, int y, uint8_t* v) {
    const uint8_t c = ((x + y) % 2) ? 255 : 0;
    v[0] = v[1] = v[2] = c;
  })};
  auto spread = [&](int filter) {
    Img out;
    const pp_op ops[] = {op(PP_OP_RESIZE, 100, 100, static_cast<float>(filter))};
    pp_process(checker.ptr, ops, 1, nullptr, &out.ptr);
    int lo = 255, hi = 0;
    for (int y = 0; y < 100; ++y) {
      for (int x = 0; x < 100; ++x) lo = std::min<int>(lo, px(out.ptr, x, y)[0]), hi = std::max<int>(hi, px(out.ptr, x, y)[0]);
    }
    return hi - lo;
  };
  const int area = spread(PP_RESIZE_AREA), lanczos = spread(PP_RESIZE_LANCZOS), bilinear = spread(PP_RESIZE_BILINEAR);
  std::printf("    checkerboard /3 spread: area %d, lanczos %d, bilinear %d\n", area, lanczos, bilinear);
  EXPECT_TRUE(area <= 30);
  EXPECT_TRUE(lanczos <= 30);
  EXPECT_TRUE(bilinear > 100);
  // Transparent pixels never bleed their colour: opaque blue next to clear red.
  Img edge{fill(64, 64, [](int x, int, uint8_t* v) {
    if (x < 32) {
      v[0] = 255, v[1] = 0, v[2] = 0, v[3] = 0;
    } else {
      v[0] = 0, v[1] = 0, v[2] = 255, v[3] = 255;
    }
  })};
  Img up;
  const pp_op ops[] = {op(PP_OP_RESIZE, 256, 256, PP_RESIZE_LANCZOS)};
  ASSERT_OK(pp_process(edge.ptr, ops, 1, nullptr, &up.ptr));
  int worst = 0;
  for (int x = 100; x < 156; ++x) {
    const uint8_t* p = px(up.ptr, x, 128);
    if (p[3] > 8) worst = std::max<int>(worst, p[0]);
  }
  std::printf("    red fringe on the blue edge: %d\n", worst);
  EXPECT_TRUE(worst <= 8);
}

TEST(core_blur_is_gaussian_at_any_size) {
  // A sharp vertical edge: the 10-90 % rise is 2.56 sigma for a Gaussian.
  Img step{fill(400, 64, [](int x, int, uint8_t* v) { v[0] = v[1] = v[2] = x < 200 ? 20 : 230; })};
  for (float sigma : {2.0f, 6.0f, 20.0f}) {
    Img out;
    const pp_op ops[] = {op(PP_OP_BLUR, sigma)};
    ASSERT_OK(pp_process(step.ptr, ops, 1, nullptr, &out.ptr));
    int x10 = 0, x90 = 0;
    for (int x = 0; x < 400; ++x) {
      const int v = px(out.ptr, x, 32)[0];
      if (v < 20 + 0.1 * 210) x10 = x;
      if (v < 20 + 0.9 * 210) x90 = x;
    }
    const double width = x90 - x10;
    std::printf("    sigma %4.1f: 10-90%% rise %.0f px (expected %.1f)\n", static_cast<double>(sigma), width, 2.56 * sigma);
    EXPECT_NEAR(width, 2.56 * sigma, 0.2 * 2.56 * sigma + 1.5);
  }
}

TEST(core_sharpening_works_on_luminance_only) {
  // A coloured flat field keeps its colour; a grey edge gains contrast.
  Img img{fill(200, 100, [](int x, int, uint8_t* v) {
    if (x < 100) {
      v[0] = 200, v[1] = 60, v[2] = 40;
    } else {
      v[0] = v[1] = v[2] = x < 150 ? 80 : 170;
    }
  })};
  Img out;
  const pp_op ops[] = {op(PP_OP_SHARPEN, 1.5f, 1.5f, 0)};
  ASSERT_OK(pp_process(img.ptr, ops, 1, nullptr, &out.ptr));
  const uint8_t* flat = px(out.ptr, 50, 50);
  EXPECT_TRUE(flat[0] == 200 && flat[1] == 60 && flat[2] == 40);
  EXPECT_TRUE(px(out.ptr, 148, 50)[0] < 80);   // dark side of the edge darker
  EXPECT_TRUE(px(out.ptr, 151, 50)[0] > 170);  // bright side brighter
  const uint8_t* e = px(out.ptr, 151, 50);
  EXPECT_TRUE(e[0] == e[1] && e[1] == e[2]);    // still grey: no colour fringe
  // A threshold leaves fine noise alone.
  std::mt19937 rng(3);
  std::normal_distribution<float> n(0, 2.0f);
  Img noisy{fill(128, 128, [&](int, int, uint8_t* v) { v[0] = v[1] = v[2] = static_cast<uint8_t>(128 + n(rng)); })};
  Img calm;
  const pp_op thr[] = {op(PP_OP_SHARPEN, 2.0f, 1.0f, 0.05f)};
  ASSERT_OK(pp_process(noisy.ptr, thr, 1, nullptr, &calm.ptr));
  EXPECT_TRUE(meanAbsDiff(noisy.ptr, calm.ptr) < 0.05);
}

TEST(core_denoise_cleans_noise_and_keeps_edges) {
  std::mt19937 rng(7);
  std::normal_distribution<float> n(0, 12.0f);
  auto clean = [](int x, int, uint8_t* v) {
    const float base = x < 128 ? 70.0f : 180.0f;
    v[0] = static_cast<uint8_t>(base), v[1] = static_cast<uint8_t>(base * 0.9f), v[2] = static_cast<uint8_t>(base * 0.8f), v[3] = 180;
  };
  Img ref{fill(256, 256, clean)};
  Img noisy{fill(256, 256, [&](int x, int y, uint8_t* v) {
    clean(x, y, v);
    for (int c = 0; c < 3; ++c) v[c] = static_cast<uint8_t>(std::clamp(v[c] + n(rng), 0.0f, 255.0f));
  })};
  const double before = meanAbsDiff(noisy.ptr, ref.ptr);
  Img out;
  const pp_op ops[] = {op(PP_OP_DENOISE, 0.8f)};
  ASSERT_OK(pp_process(noisy.ptr, ops, 1, nullptr, &out.ptr));
  const double after = meanAbsDiff(out.ptr, ref.ptr);
  // The edge stays sharp: two pixels either side are still far apart.
  const int jump = px(out.ptr, 130, 128)[0] - px(out.ptr, 125, 128)[0];
  std::printf("    error %.1f -> %.1f levels, edge jump %d\n", before, after, jump);
  EXPECT_TRUE(after < before * 0.6);
  EXPECT_TRUE(jump > 80);
  EXPECT_EQ(static_cast<int>(px(out.ptr, 10, 10)[3]), 180);  // alpha kept
}

// ---- perspective -------------------------------------------------------------------------

TEST(core_perspective_warps_and_detects_keystone) {
  // Homography round trip.
  const float rect[8] = {0, 0, 100, 0, 100, 80, 0, 80};
  const float quad[8] = {10, 5, 90, 12, 95, 70, 4, 75};
  Img src{scene(120, 90)};
  Img warped;
  ASSERT_OK(pp_warp_perspective(src.ptr, quad, 100, 80, nullptr, &warped.ptr));
  EXPECT_EQ(infoOf(warped.ptr).width, 100);
  // The warped image's corners come from the quad's corners.
  EXPECT_TRUE(std::abs(px(warped.ptr, 0, 0)[0] - px(src.ptr, 10, 5)[0]) < 40);
  const float concave[8] = {0, 0, 100, 0, 20, 20, 0, 100};
  EXPECT_EQ(pp_warp_perspective(src.ptr, concave, 100, 80, nullptr, &warped.ptr), PP_ERROR_INVALID_ARGUMENT);
  (void)rect;

  // Vertical stripes made to converge upwards (as when shooting a building
  // from below) are detected, and the suggested correction straightens them.
  Img stripes{fill(800, 600, [](int x, int, uint8_t* v) { v[0] = v[1] = v[2] = (x / 40) % 2 ? 230 : 30; })};
  Img leaning;
  const pp_op lean[] = {op(PP_OP_PERSPECTIVE, -0.6f, 0)};  // the inverse distortion
  ASSERT_OK(pp_process(stripes.ptr, lean, 1, nullptr, &leaning.ptr));
  float v = 0;
  ASSERT_OK(pp_detect_keystone(leaning.ptr, nullptr, &v));
  float flat = 0;
  ASSERT_OK(pp_detect_keystone(stripes.ptr, nullptr, &flat));
  std::printf("    detected keystone %.2f on converging lines, %.2f on straight ones\n", static_cast<double>(v),
              static_cast<double>(flat));
  EXPECT_TRUE(v > 0.25f);
  EXPECT_NEAR(flat, 0.0, 0.08);
  Img fixed;
  const pp_op fix[] = {op(PP_OP_PERSPECTIVE, v, 0)};
  ASSERT_OK(pp_process(leaning.ptr, fix, 1, nullptr, &fixed.ptr));
  float after = 0;
  ASSERT_OK(pp_detect_keystone(fixed.ptr, nullptr, &after));
  std::printf("    after correction: %.2f\n", static_cast<double>(after));
  EXPECT_TRUE(std::fabs(after) < std::fabs(v) * 0.5f);
}

// ---- colour --------------------------------------------------------------------------------

TEST(core_luts_and_matrices_transform_colour_exactly) {
  const int n = 17;
  std::vector<float> identity(static_cast<size_t>(n * n * n * 3)), invert(identity.size());
  for (int b = 0; b < n; ++b) {
    for (int g = 0; g < n; ++g) {
      for (int r = 0; r < n; ++r) {
        const size_t i = (static_cast<size_t>(b) * n * n + g * n + r) * 3;
        const float fr = r / 16.0f, fg = g / 16.0f, fb = b / 16.0f;
        identity[i] = fr, identity[i + 1] = fg, identity[i + 2] = fb;
        invert[i] = 1 - fr, invert[i + 1] = 1 - fg, invert[i + 2] = 1 - fb;
      }
    }
  }
  Img ref{scene(160, 120)};
  Img a{scene(160, 120)};
  ASSERT_OK(pp_apply_lut3d(a.ptr, identity.data(), n, 1.0f, nullptr));
  EXPECT_TRUE(meanAbsDiff(a.ptr, ref.ptr) < 0.6);
  Img b{scene(160, 120)};
  ASSERT_OK(pp_apply_lut3d(b.ptr, invert.data(), n, 1.0f, nullptr));
  EXPECT_TRUE(std::abs(px(b.ptr, 5, 5)[0] - (255 - px(ref.ptr, 5, 5)[0])) <= 1);
  Img half{scene(160, 120)};
  ASSERT_OK(pp_apply_lut3d(half.ptr, invert.data(), n, 0.5f, nullptr));
  EXPECT_TRUE(std::abs(px(half.ptr, 5, 5)[0] - 128) <= 2);
  std::vector<float> bad(identity);
  bad[10] = std::nanf("");
  EXPECT_EQ(pp_apply_lut3d(a.ptr, bad.data(), n, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
  // Matrices: identity keeps, luminance gives grey (in linear light too).
  const float ident[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  Img c{scene(160, 120)};
  ASSERT_OK(pp_color_matrix(c.ptr, ident, 1, nullptr));
  EXPECT_TRUE(meanAbsDiff(c.ptr, ref.ptr) < 0.6);
  const float grey[12] = {0.2126f, 0.7152f, 0.0722f, 0, 0.2126f, 0.7152f, 0.0722f, 0, 0.2126f, 0.7152f, 0.0722f, 0};
  ASSERT_OK(pp_color_matrix(c.ptr, grey, 1, nullptr));
  const uint8_t* g = px(c.ptr, 30, 30);
  EXPECT_TRUE(g[0] == g[1] && g[1] == g[2]);
}

// ---- compositing and masks -------------------------------------------------------------------

TEST(core_blend_modes_follow_the_compositing_rules) {
  auto solid = [](int r, int g, int b, int a) {
    return fill(8, 8, [=](int, int, uint8_t* v) { v[0] = static_cast<uint8_t>(r), v[1] = static_cast<uint8_t>(g), v[2] = static_cast<uint8_t>(b), v[3] = static_cast<uint8_t>(a); });
  };
  auto blendOne = [&](int mode, int br, int sr, int sa = 255, float opacity = 1.0f) {
    Img dst{solid(br, br, br, 255)};
    Img src{solid(sr, sr, sr, sa)};
    pp_blend(dst.ptr, src.ptr, nullptr, 0, 0, mode, opacity, nullptr);
    return static_cast<int>(px(dst.ptr, 4, 4)[0]);
  };
  EXPECT_EQ(blendOne(PP_BLEND_NORMAL, 50, 200), 200);
  EXPECT_NEAR(blendOne(PP_BLEND_NORMAL, 50, 200, 128), 50 + (200 - 50) * 128 / 255.0, 1.0);
  EXPECT_NEAR(blendOne(PP_BLEND_NORMAL, 50, 200, 255, 0.25f), 50 + (200 - 50) * 0.25, 1.0);
  EXPECT_EQ(blendOne(PP_BLEND_MULTIPLY, 120, 255), 120);  // white multiplies to nothing
  EXPECT_EQ(blendOne(PP_BLEND_SCREEN, 120, 0), 120);      // black screens to nothing
  EXPECT_NEAR(blendOne(PP_BLEND_MULTIPLY, 128, 128), 64, 1.0);
  EXPECT_EQ(blendOne(PP_BLEND_DIFFERENCE, 200, 200), 0);
  EXPECT_EQ(blendOne(PP_BLEND_DARKEN, 90, 200), 90);
  EXPECT_EQ(blendOne(PP_BLEND_LIGHTEN, 90, 200), 200);
  EXPECT_EQ(blendOne(PP_BLEND_ADD, 200, 100), 255);
  for (int m = 0; m < 12; ++m) {
    const int v = blendOne(m, 77, 180, 200, 0.7f);
    EXPECT_TRUE(v >= 0 && v <= 255);
  }
  // Background replacement: a matte picks the subject from one image and keeps the other elsewhere.
  Img backdrop{solid(10, 200, 10, 255)};
  Img subject{solid(220, 30, 30, 255)};
  Img matte{fill(8, 8, [](int x, int, uint8_t* v) { v[0] = x < 4 ? 255 : 0; }, PP_FORMAT_GRAY8)};
  ASSERT_OK(pp_blend(backdrop.ptr, subject.ptr, matte.ptr, 0, 0, PP_BLEND_NORMAL, 1.0f, nullptr));
  EXPECT_EQ(static_cast<int>(px(backdrop.ptr, 1, 1)[0]), 220);
  EXPECT_EQ(static_cast<int>(px(backdrop.ptr, 6, 1)[1]), 200);
  // Clipped placement is fine; a wrong-size matte is not.
  ASSERT_OK(pp_blend(backdrop.ptr, subject.ptr, nullptr, 6, 6, PP_BLEND_NORMAL, 1.0f, nullptr));
  Img wrong{make(4, 4, PP_FORMAT_GRAY8)};
  EXPECT_EQ(pp_blend(backdrop.ptr, subject.ptr, wrong.ptr, 0, 0, 0, 1, nullptr), PP_ERROR_INVALID_ARGUMENT);
}

TEST(core_masks_grow_feather_and_snap_to_edges) {
  auto square = [] {
    return fill(100, 100, [](int x, int y, uint8_t* v) { v[0] = (x >= 40 && x < 60 && y >= 40 && y < 60) ? 255 : 0; }, PP_FORMAT_GRAY8);
  };
  Img m{square()};
  ASSERT_OK(pp_mask_adjust(m.ptr, PP_MASK_GROW, 3, nullptr));
  EXPECT_EQ(static_cast<int>(px(m.ptr, 37, 50)[0]), 255);
  EXPECT_EQ(static_cast<int>(px(m.ptr, 36, 50)[0]), 0);
  EXPECT_EQ(static_cast<int>(px(m.ptr, 62, 62)[0]), 255);
  ASSERT_OK(pp_mask_adjust(m.ptr, PP_MASK_GROW, -5, nullptr));
  EXPECT_EQ(static_cast<int>(px(m.ptr, 41, 50)[0]), 0);
  EXPECT_EQ(static_cast<int>(px(m.ptr, 42, 50)[0]), 255);
  Img f{square()};
  ASSERT_OK(pp_mask_adjust(f.ptr, PP_MASK_FEATHER, 3, nullptr));
  const int mid = px(f.ptr, 40, 50)[0];
  EXPECT_TRUE(mid > 60 && mid < 200);
  ASSERT_OK(pp_mask_adjust(f.ptr, PP_MASK_THRESHOLD, 0.5f, nullptr));
  EXPECT_TRUE(px(f.ptr, 40, 50)[0] == 0 || px(f.ptr, 40, 50)[0] == 255);
  ASSERT_OK(pp_mask_adjust(f.ptr, PP_MASK_INVERT, 0, nullptr));
  EXPECT_EQ(static_cast<int>(px(f.ptr, 5, 5)[0]), 255);

  // A coarse (1/8 size, blocky) mask snaps to the photo's real edge.
  const int W = 640, H = 480;
  Img photo{fill(W, H, [&](int x, int y, uint8_t* v) {
    const bool in = (x - 320) * (x - 320) + (y - 240) * (y - 240) < 150 * 150;
    v[0] = in ? 220 : 40, v[1] = in ? 180 : 60, v[2] = in ? 140 : 90;
  })};
  Img coarse{fill(W / 8, H / 8, [&](int x, int y, uint8_t* v) {
    const int cx = x * 8 + 4, cy = y * 8 + 4;
    v[0] = (cx - 320) * (cx - 320) + (cy - 240) * (cy - 240) < 150 * 150 ? 255 : 0;
  }, PP_FORMAT_GRAY8)};
  Img refined;
  ASSERT_OK(pp_mask_refine(coarse.ptr, photo.ptr, 8, 1e-4f, nullptr, &refined.ptr));
  EXPECT_EQ(infoOf(refined.ptr).width, W);
  // Along the horizontal through the centre, the mask crosses 50 % at the circle's edge (x = 170).
  int crossing = -1;
  for (int x = 140; x < 200; ++x) {
    if (px(refined.ptr, x, 240)[0] >= 128) {
      crossing = x;
      break;
    }
  }
  std::printf("    refined edge at x=%d (true edge 170)\n", crossing);
  EXPECT_TRUE(crossing >= 168 && crossing <= 173);
}

// ---- codec ---------------------------------------------------------------------------------

TEST(core_codec_round_trips_and_applies_orientation) {
  Img src{scene(320, 200)};
  for (int y = 0; y < 200; ++y) px(src.ptr, 3, y)[3] = 90;  // some translucency
  void* data = nullptr;
  size_t size = 0;
  ASSERT_OK(pp_encode_memory(src.ptr, PP_CODEC_PNG, 0, &data, &size));
  Img png;
  pp_decode_info info{};
  ASSERT_OK(pp_decode_memory(data, size, nullptr, nullptr, &png.ptr, &info));
  pp_free(data);
  EXPECT_EQ(info.codec, PP_CODEC_PNG);
  EXPECT_EQ(info.has_alpha, 1);
  EXPECT_TRUE(meanAbsDiff(png.ptr, src.ptr, 4) == 0.0);  // lossless

  ASSERT_OK(pp_encode_memory(src.ptr, PP_CODEC_JPEG, 95, &data, &size));
  std::vector<uint8_t> jpeg(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
  pp_free(data);
  Img jpg;
  ASSERT_OK(pp_decode_memory(jpeg.data(), jpeg.size(), nullptr, nullptr, &jpg.ptr, &info));
  const double err = meanAbsDiff(jpg.ptr, src.ptr);
  std::printf("    JPEG q95 mean error %.2f levels, %zu bytes\n", err, jpeg.size());
  EXPECT_TRUE(err < 3.0);

  // EXIF orientation 6 (turn 90 deg clockwise): pixels come out upright.
  const uint8_t app1[] = {0xFF, 0xE1, 0x00, 0x22, 'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0,
                          1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0};
  std::vector<uint8_t> rotated(jpeg.begin(), jpeg.begin() + 2);
  rotated.insert(rotated.end(), app1, app1 + sizeof(app1));
  rotated.insert(rotated.end(), jpeg.begin() + 2, jpeg.end());
  Img up;
  ASSERT_OK(pp_decode_memory(rotated.data(), rotated.size(), nullptr, nullptr, &up.ptr, &info));
  EXPECT_EQ(info.orientation, 6);
  EXPECT_EQ(infoOf(up.ptr).width, 200);
  EXPECT_EQ(infoOf(up.ptr).height, 320);
  // ...unless asked not to; and decoding can shrink on the way in.
  pp_decode_options opts{};
  opts.apply_orientation = 0;
  opts.max_edge = 160;
  Img raw;
  ASSERT_OK(pp_decode_memory(rotated.data(), rotated.size(), &opts, nullptr, &raw.ptr, &info));
  EXPECT_EQ(infoOf(raw.ptr).width, 160);
  EXPECT_EQ(infoOf(raw.ptr).height, 100);
  EXPECT_EQ(info.source_width, 320);
}

TEST(core_codec_rejects_hostile_files_safely) {
  const uint64_t before = liveBytes();
  Img src{scene(64, 64)};
  void* data = nullptr;
  size_t size = 0;
  ASSERT_OK(pp_encode_memory(src.ptr, PP_CODEC_JPEG, 80, &data, &size));
  std::vector<uint8_t> jpeg(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
  pp_free(data);
  Img out;
  // Truncated at every length, and random garbage: errors, never crashes.
  for (size_t n = 1; n < jpeg.size(); n += 7) {
    const pp_status st = pp_decode_memory(jpeg.data(), n, nullptr, nullptr, &out.ptr, nullptr);
    if (st == PP_OK) {
      pp_image_destroy(out.ptr);
      out.ptr = nullptr;
    } else {
      EXPECT_TRUE(out.ptr == nullptr);
    }
  }
  std::mt19937 rng(11);
  std::vector<uint8_t> junk(4096);
  for (auto& b : junk) b = static_cast<uint8_t>(rng());
  EXPECT_EQ(pp_decode_memory(junk.data(), junk.size(), nullptr, nullptr, &out.ptr, nullptr), PP_ERROR_UNSUPPORTED_FORMAT);
  junk[0] = 0xFF, junk[1] = 0xD8, junk[2] = 0xFF;  // looks like a JPEG, isn't
  EXPECT_TRUE(pp_decode_memory(junk.data(), junk.size(), nullptr, nullptr, &out.ptr, nullptr) != PP_OK);
  // A PNG header claiming 60000 x 60000 pixels is refused before decoding.
  uint8_t bomb[33] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                      0, 0, 0xEA, 0x60, 0, 0, 0xEA, 0x60, 8, 6, 0, 0, 0, 0, 0, 0, 0};
  EXPECT_EQ(pp_decode_memory(bomb, sizeof(bomb), nullptr, nullptr, &out.ptr, nullptr), PP_ERROR_UNSUPPORTED_FORMAT);
  // Within limits but over the memory budget: a clean budget error.
  Img big{scene(1000, 1000)};
  ASSERT_OK(pp_encode_memory(big.ptr, PP_CODEC_PNG, 0, &data, &size));
  pp_memory_set_budget(liveBytes() + 2'000'000);
  EXPECT_EQ(pp_decode_memory(data, size, nullptr, nullptr, &out.ptr, nullptr), PP_ERROR_BUDGET_EXCEEDED);
  pp_memory_set_budget(0);
  pp_free(data);
  // Files: missing input, unwritable output.
  EXPECT_EQ(pp_decode_file("/nonexistent/photo.jpg", nullptr, nullptr, &out.ptr, nullptr), PP_ERROR_IO);
  EXPECT_EQ(pp_encode_file(src.ptr, "/nonexistent/dir/out.png", PP_CODEC_PNG, 0), PP_ERROR_IO);
  EXPECT_EQ(pp_encode_memory(src.ptr, PP_CODEC_GIF, 0, &data, &size), PP_ERROR_UNSUPPORTED_FORMAT);
  EXPECT_TRUE(out.ptr == nullptr);
  big.ptr ? (void)0 : (void)0;
  pp_image_destroy(big.ptr);
  big.ptr = nullptr;
  pp_image_destroy(src.ptr);
  src.ptr = nullptr;
  EXPECT_EQ(liveBytes(), before);
}

TEST(core_codec_files_are_written_atomically) {
  Img src{scene(90, 60)};
  const char* path = "/tmp/pp_codec_test.png";
  ASSERT_OK(pp_encode_file(src.ptr, path, PP_CODEC_PNG, 0));
  Img back;
  pp_decode_info info{};
  ASSERT_OK(pp_decode_file(path, nullptr, nullptr, &back.ptr, &info));
  EXPECT_EQ(info.width, 90);
  EXPECT_TRUE(meanAbsDiff(back.ptr, src.ptr, 4) == 0.0);
  std::FILE* tmp = std::fopen("/tmp/pp_codec_test.png.tmp", "rb");
  EXPECT_TRUE(tmp == nullptr);  // no temporary left behind
  if (tmp) std::fclose(tmp);
  std::remove(path);
}

// ---- machine-learning helpers ------------------------------------------------------------

TEST(core_tensors_are_prepared_and_read_back_exactly) {
  // A 4x2 photo, normalised to NCHW with ImageNet statistics.
  Img img{fill(4, 2, [](int x, int y, uint8_t* v) { v[0] = static_cast<uint8_t>(x * 60), v[1] = static_cast<uint8_t>(y * 200), v[2] = 128; })};
  pp_tensor_spec spec{};
  spec.width = 4, spec.height = 2, spec.layout = PP_TENSOR_NCHW, spec.fit = PP_FIT_STRETCH;
  const float mean[3] = {0.485f, 0.456f, 0.406f}, stdv[3] = {0.229f, 0.224f, 0.225f};
  for (int c = 0; c < 3; ++c) spec.mean[c] = mean[c], spec.std[c] = stdv[c];
  std::vector<float> t(4 * 2 * 3);
  ASSERT_OK(pp_image_to_tensor(img.ptr, &spec, t.data(), t.size(), nullptr, nullptr));
  EXPECT_NEAR(t[3], (180 / 255.0 - 0.485) / 0.229, 1e-4);          // R plane, x=3, y=0
  EXPECT_NEAR(t[8 + 4], (200 / 255.0 - 0.456) / 0.224, 1e-4);      // G plane, x=0, y=1
  EXPECT_NEAR(t[16 + 5], (128 / 255.0 - 0.406) / 0.225, 1e-4);     // B plane
  EXPECT_EQ(pp_image_to_tensor(img.ptr, &spec, t.data(), 5, nullptr, nullptr), PP_ERROR_INVALID_ARGUMENT);

  // Letterbox: a 2:1 photo into a square keeps its shape, padded with zeros.
  Img wide{scene(200, 100)};
  pp_tensor_spec sq{};
  sq.width = 64, sq.height = 64, sq.layout = PP_TENSOR_NHWC, sq.fit = PP_FIT_LETTERBOX;
  for (int c = 0; c < 3; ++c) sq.mean[c] = 0, sq.std[c] = 1;
  std::vector<float> s(64 * 64 * 3);
  pp_tensor_placement place{};
  ASSERT_OK(pp_image_to_tensor(wide.ptr, &sq, s.data(), s.size(), nullptr, &place));
  EXPECT_NEAR(place.offset_y, 16, 0.5);
  EXPECT_NEAR(place.scale_x, 0.32, 0.01);
  EXPECT_EQ(s[0], 0.0f);                       // padding row
  EXPECT_TRUE(s[(32 * 64 + 32) * 3] > 0.1f);   // photo inside

  // A score map back to a mask: sigmoid, the padding cropped away, at photo size.
  std::vector<float> logits(64 * 64);
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) logits[y * 64 + x] = x < 32 ? 6.0f : -6.0f;
  }
  Img mask;
  ASSERT_OK(pp_tensor_to_mask(logits.data(), 64, 64, 1, PP_TENSOR_NHWC, 0, PP_ACTIVATION_SIGMOID, 200, 100, &place, 200, 100,
                              nullptr, &mask.ptr));
  EXPECT_EQ(infoOf(mask.ptr).width, 200);
  EXPECT_TRUE(px(mask.ptr, 20, 50)[0] > 250);
  EXPECT_TRUE(px(mask.ptr, 180, 50)[0] < 5);
  // Softmax picks a class; NaN output is a predictable error.
  std::vector<float> two(8 * 8 * 2, 0.0f);
  for (int i = 0; i < 64; ++i) two[i * 2 + 1] = 3.0f;
  Img cls;
  ASSERT_OK(pp_tensor_to_mask(two.data(), 8, 8, 2, PP_TENSOR_NHWC, 1, PP_ACTIVATION_SOFTMAX, 8, 8, nullptr, 0, 0, nullptr, &cls.ptr));
  EXPECT_TRUE(px(cls.ptr, 3, 3)[0] > 240);
  two[5] = std::nanf("");
  Img bad;
  EXPECT_EQ(pp_tensor_to_mask(two.data(), 8, 8, 2, PP_TENSOR_NHWC, 1, PP_ACTIVATION_SOFTMAX, 8, 8, nullptr, 0, 0, nullptr, &bad.ptr),
            PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_tensor_to_mask(two.data(), 8, 8, 2, PP_TENSOR_NHWC, 2, PP_ACTIVATION_NONE, 8, 8, nullptr, 0, 0, nullptr, &bad.ptr),
            PP_ERROR_INVALID_ARGUMENT);
}
