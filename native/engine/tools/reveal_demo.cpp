// The live studio on files: reveal_demo work.ppm blank.ppm outdir tool artist [seconds] [fps]
// Writes frame_%04d.ppm (the work being made under the moving pencil or brush).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "photoplus/pp_engine.h"

namespace {
pp_image* readPpm(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::string magic;
  int w = 0, h = 0, maxv = 0;
  in >> magic >> w >> h >> maxv;
  in.get();
  if (!in || magic != "P6" || maxv != 255) return nullptr;
  std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
  in.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned char* p = px + static_cast<size_t>(y) * info.stride + x * 4;
      const size_t i = (static_cast<size_t>(y) * w + x) * 3;
      p[0] = rgb[i], p[1] = rgb[i + 1], p[2] = rgb[i + 2], p[3] = 255;
    }
  return img;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 6) {
    std::fprintf(stderr, "reveal_demo work.ppm blank.ppm outdir tool artist [seconds] [fps]\n");
    return 2;
  }
  pp_image* work = readPpm(argv[1]);
  pp_image* blank = readPpm(argv[2]);
  if (!work || !blank) return 1;
  const float seconds = argc > 6 ? std::strtof(argv[6], nullptr) : 7.0f;
  const int fps = argc > 7 ? std::atoi(argv[7]) : 30;
  // REVEAL_INNER="x0 y0 x1 y1": the picture inside a frame.
  float inner[4] = {0, 0, 1, 1};
  const char* in = std::getenv("REVEAL_INNER");
  if (in) std::sscanf(in, "%f %f %f %f", &inner[0], &inner[1], &inner[2], &inner[3]);
  pp_art_reveal* r = nullptr;
  auto t0 = std::chrono::steady_clock::now();
  if (pp_art_reveal_create(work, blank, std::atoi(argv[4]), std::atoi(argv[5]), inner, 7, nullptr, &r) != PP_OK) {
    std::fprintf(stderr, "failed: %s\n", pp_last_error_message());
    return 1;
  }
  const double plan = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  pp_image_info info{};
  pp_image_get_info(work, &info);
  pp_image* frame = nullptr;
  pp_image_create(info.width, info.height, PP_FORMAT_RGBA8, &frame);
  const int frames = static_cast<int>(seconds * fps);
  double total = 0;
  for (int i = 0; i <= frames; ++i) {
    auto a = std::chrono::steady_clock::now();
    pp_art_reveal_frame(r, static_cast<float>(i) / frames, frame);
    total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
    char name[512];
    std::snprintf(name, sizeof name, "%s/frame_%04d.ppm", argv[3], i);
    std::ofstream out(name, std::ios::binary);
    out << "P6\n" << info.width << " " << info.height << "\n255\n";
    pp_image_info fi{};
    pp_image_get_info(frame, &fi);
    const auto* px = static_cast<const unsigned char*>(pp_image_pixels(frame));
    for (int y = 0; y < info.height; ++y)
      for (int x = 0; x < info.width; ++x) out.write(reinterpret_cast<const char*>(px + static_cast<size_t>(y) * fi.stride + x * 4), 3);
  }
  std::fprintf(stderr, "%d strokes, plan %.0f ms, %.1f ms per frame\n", pp_art_reveal_strokes(r), plan, total / (frames + 1));
  pp_art_reveal_destroy(r);
  pp_image_destroy(frame);
  pp_image_destroy(work);
  pp_image_destroy(blank);
  return 0;
}
