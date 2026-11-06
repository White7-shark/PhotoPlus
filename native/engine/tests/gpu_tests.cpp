// Phase 4: GPU acceleration. On the host the same shaders run on Mesa's
// software rasteriser (llvmpipe), so every build checks GPU/CPU parity.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

struct Img {
  pp_image* ptr = nullptr;
  ~Img() { pp_image_destroy(ptr); }
};

pp_image* photo(int w, int h, int format = PP_FORMAT_RGBA8) {
  pp_image* img = nullptr;
  pp_image_create(w, h, format, &img);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* base = static_cast<uint8_t*>(pp_image_pixels(img));
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint8_t* p = base + y * info.stride + x * 4;
      p[0] = static_cast<uint8_t>(40 + 180.0 * x / w + 20 * std::sin(x * 0.7));
      p[1] = static_cast<uint8_t>(30 + 200.0 * y / h);
      p[2] = static_cast<uint8_t>((x * 13 + y * 7) % 256);
      p[3] = static_cast<uint8_t>(((x / 9 + y / 9) % 5) == 0 ? 140 : 255);
    }
  }
  return img;
}

pp_op op(int type, float a = 0, float b = 0, float c = 0, float d = 0) {
  pp_op o{};
  o.type = type;
  o.p[0] = a, o.p[1] = b, o.p[2] = c, o.p[3] = d;
  return o;
}

int maxDiff(pp_image* a, pp_image* b) {
  pp_image_info ia{}, ib{};
  pp_image_get_info(a, &ia);
  pp_image_get_info(b, &ib);
  if (ia.width != ib.width || ia.height != ib.height) return 999;
  int worst = 0;
  for (int y = 0; y < ia.height; ++y) {
    const auto* ra = static_cast<const uint8_t*>(pp_image_pixels(a)) + y * ia.stride;
    const auto* rb = static_cast<const uint8_t*>(pp_image_pixels(b)) + y * ib.stride;
    for (int x = 0; x < ia.width * 4; ++x) worst = std::max(worst, std::abs(ra[x] - rb[x]));
  }
  return worst;
}

bool gpuReady() {
  pp_gpu_status s{};
  pp_gpu_init(&s);
  return s.available != 0;
}

}  // namespace

TEST(gpu_initialises_and_reports_its_self_test) {
  pp_gpu_status s{};
  const pp_status st = pp_gpu_init(&s);
  std::printf("    compiled %d, available %d, renderer \"%s\", self-test max difference %d%s%s\n", s.compiled, s.available,
              s.renderer, s.max_difference, s.reason[0] ? ", reason: " : "", s.reason);
  if (!s.compiled) return;  // a build without GPU support: nothing else to check
  EXPECT_TRUE(st == PP_OK || st == PP_ERROR_UNSUPPORTED_FORMAT);
  if (s.available) {
    EXPECT_TRUE(s.max_difference >= 0 && s.max_difference <= 2);
  }
  // A second call reports the same result.
  pp_gpu_status again{};
  pp_gpu_init(&again);
  EXPECT_EQ(again.available, s.available);
}

TEST(gpu_results_match_the_cpu) {
  if (!gpuReady()) return;
  const std::vector<std::vector<pp_op>> chains = {
      {op(PP_OP_TONE, 0.4f, 0.05f, 0.2f)},
      {op(PP_OP_WARMTH, -0.5f, 0.4f), op(PP_OP_SATURATION, 0.6f)},
      {op(PP_OP_BLUR, 3.0f), op(PP_OP_TONE, -0.3f, 0, -0.2f), op(PP_OP_GRAYSCALE)},
      {op(PP_OP_BLUR, 7.9f)},
      {op(PP_OP_SATURATION, -1.0f), op(PP_OP_WARMTH, 1.0f, -1.0f), op(PP_OP_TONE, 2.5f, 0.3f, 0.6f)},
  };
  for (int format : {PP_FORMAT_RGBA8, PP_FORMAT_BGRA8}) {
    Img in;
    in.ptr = photo(517, 389, format);
    for (const auto& chain : chains) {
      pp_render_options cpu{}, gpu{};
      cpu.backend = PP_BACKEND_CPU;
      gpu.backend = PP_BACKEND_GPU;
      Img a, b;
      ASSERT_OK(pp_process_ex(in.ptr, chain.data(), chain.size(), &cpu, nullptr, &a.ptr));
      ASSERT_OK(pp_process_ex(in.ptr, chain.data(), chain.size(), &gpu, nullptr, &b.ptr));
      const int d = maxDiff(a.ptr, b.ptr);
      EXPECT_TRUE(d <= 2);
      if (d > 2) std::printf("    chain of %zu ops (format %d): difference %d\n", chain.size(), format, d);
    }
  }
}

TEST(gpu_mixes_with_cpu_operations_and_falls_back) {
  if (!gpuReady()) return;
  Img in;
  in.ptr = photo(800, 600);
  // GPU-supported runs around CPU-only operations (crop, sharpen, a large blur).
  const pp_op chain[] = {op(PP_OP_TONE, 0.3f), op(PP_OP_CROP, 20, 10, 700, 500), op(PP_OP_BLUR, 2.0f),
                         op(PP_OP_SATURATION, 0.3f), op(PP_OP_SHARPEN, 1.0f), op(PP_OP_BLUR, 20.0f)};
  pp_render_options cpu{}, aut{};
  cpu.backend = PP_BACKEND_CPU;
  aut.backend = PP_BACKEND_AUTO;
  aut.max_edge = 400;
  cpu.max_edge = 400;
  Img a, b;
  ASSERT_OK(pp_process_ex(in.ptr, chain, 6, &cpu, nullptr, &a.ptr));
  ASSERT_OK(pp_process_ex(in.ptr, chain, 6, &aut, nullptr, &b.ptr));
  EXPECT_TRUE(maxDiff(a.ptr, b.ptr) <= 3);
  // Gray images are not GPU material: AUTO quietly uses the CPU.
  Img gray;
  pp_image_create(64, 64, PP_FORMAT_GRAY8, &gray.ptr);
  Img g;
  const pp_op t[] = {op(PP_OP_TONE, 0.5f)};
  ASSERT_OK(pp_process_ex(gray.ptr, t, 1, &aut, nullptr, &g.ptr));
}

TEST(gpu_can_be_shut_down_and_restarted) {
  if (!gpuReady()) return;
  pp_gpu_shutdown();
  pp_gpu_status s{};
  pp_gpu_get_status(&s);
  EXPECT_EQ(s.available, 0);
  // While off, AUTO renders on the CPU.
  Img in, out;
  in.ptr = photo(100, 80);
  const pp_op t[] = {op(PP_OP_TONE, 0.5f)};
  pp_render_options aut{};
  aut.max_edge = 64;
  ASSERT_OK(pp_process_ex(in.ptr, t, 1, &aut, nullptr, &out.ptr));
  ASSERT_OK(pp_gpu_init(&s));
  EXPECT_EQ(s.available, 1);
}
