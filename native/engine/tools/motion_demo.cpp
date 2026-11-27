// Photo Motion demo: motion_demo in.ppm outdir frames [mask.pgm|-] [depth.pgm|-]
// MOTION="camera amount flow sky skySpeed effect effectAmount loop" ARROWS="x0 y0 x1 y1 ..." ANCHORS="x y r ..."
// Writes outdir/frame_0000.ppm ... (one loop).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "photoplus/pp_engine.h"

static bool readNetpbm(const char* path, const char* magic, int& w, int& h, std::vector<unsigned char>& px, int ch) {
  std::ifstream f(path, std::ios::binary);
  std::string m;
  int maxv = 0;
  f >> m >> w >> h >> maxv;
  if (m != magic || maxv != 255) return false;
  f.get();
  px.resize(static_cast<size_t>(w) * h * ch);
  f.read(reinterpret_cast<char*>(px.data()), static_cast<std::streamsize>(px.size()));
  return static_cast<bool>(f);
}

static pp_image* gray(const char* path) {
  if (path == nullptr || std::strcmp(path, "-") == 0) return nullptr;
  int w, h;
  std::vector<unsigned char> g;
  if (!readNetpbm(path, "P5", w, h, g, 1)) return nullptr;
  pp_image* m = nullptr;
  pp_image_create(w, h, PP_FORMAT_GRAY8, &m);
  pp_image_info info{};
  pp_image_get_info(m, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(m));
  for (int y = 0; y < h; ++y) std::memcpy(px + static_cast<size_t>(y) * info.stride, g.data() + static_cast<size_t>(y) * w, static_cast<size_t>(w));
  return m;
}

static std::vector<float> floats(const char* env) {
  std::vector<float> v;
  if (env == nullptr) return v;
  char* p = const_cast<char*>(env);
  for (;;) {
    char* end = nullptr;
    const float f = std::strtof(p, &end);
    if (end == p) break;
    v.push_back(f);
    p = end;
  }
  return v;
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: motion_demo in.ppm outdir frames [mask.pgm|-] [depth.pgm|-]\n");
    return 2;
  }
  int w, h;
  std::vector<unsigned char> rgb;
  if (!readNetpbm(argv[1], "P6", w, h, rgb, 3)) return 1;
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned char* p = px + static_cast<size_t>(y) * info.stride + x * 4;
      const unsigned char* s = rgb.data() + (static_cast<size_t>(y) * w + x) * 3;
      p[0] = s[0], p[1] = s[1], p[2] = s[2], p[3] = 255;
    }
  pp_image* mask = gray(argc > 4 ? argv[4] : nullptr);
  pp_image* depth = gray(argc > 5 ? argv[5] : nullptr);
  pp_motion_params prm{6, 0.5f, 1.0f, 0, 0.5f, 0, 0.6f, 1, 7};
  const std::vector<float> m = floats(std::getenv("MOTION"));
  if (m.size() >= 8)
    prm = {static_cast<int>(m[0]), m[1], m[2], static_cast<int>(m[3]), m[4], static_cast<int>(m[5]), m[6], static_cast<int>(m[7]), 7};
  const std::vector<float> arrows = floats(std::getenv("ARROWS")), anchors = floats(std::getenv("ANCHORS"));
  const auto t0 = std::chrono::steady_clock::now();
  pp_motion* motion = nullptr;
  if (pp_motion_create(img, depth, mask, arrows.data(), arrows.size() / 4, anchors.data(), anchors.size() / 3, &prm, nullptr, &motion) != PP_OK) {
    std::fprintf(stderr, "create failed: %s\n", pp_last_error_message());
    return 1;
  }
  const double prep = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  const int frames = std::atoi(argv[3]);
  pp_image* frame = nullptr;
  pp_image_create(w, h, PP_FORMAT_RGBA8, &frame);
  pp_image_info fi{};
  pp_image_get_info(frame, &fi);
  const auto t1 = std::chrono::steady_clock::now();
  for (int i = 0; i < frames; ++i) {
    if (pp_motion_render(motion, static_cast<float>(i) / static_cast<float>(frames), frame, nullptr) != PP_OK) return 1;
    char name[512];
    std::snprintf(name, sizeof name, "%s/frame_%04d.ppm", argv[2], i);
    std::ofstream out(name, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    const auto* fp = static_cast<const unsigned char*>(pp_image_pixels(frame));
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) out.write(reinterpret_cast<const char*>(fp + static_cast<size_t>(y) * fi.stride + x * 4), 3);
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
  std::fprintf(stderr, "%dx%d: prepare %.0f ms, %d frames %.1f ms each, sky %.2f\n", w, h, prep, frames, ms / frames, pp_motion_sky_share(motion));
  pp_motion_destroy(motion);
  pp_image_destroy(frame);
  pp_image_destroy(img);
  if (mask) pp_image_destroy(mask);
  if (depth) pp_image_destroy(depth);
  return 0;
}
