// backdrop_demo — renders every backdrop on a PPM photo (developer tool).
//
//   backdrop_demo IN.ppm SUBJECT.pgm DEPTH.pgm|- OUT_PREFIX [strength] [design]
//
// Writes OUT_PREFIX<index>.ppm for each design (or only `design`).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
  if (!in || magic != "P6" || maxv != 255 || w <= 0 || h <= 0) return nullptr;
  std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
  in.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
  if (!in) return nullptr;
  pp_image* img = nullptr;
  if (pp_image_create(w, h, PP_FORMAT_RGBA8, &img) != PP_OK) return nullptr;
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    px[i * 4] = rgb[i * 3];
    px[i * 4 + 1] = rgb[i * 3 + 1];
    px[i * 4 + 2] = rgb[i * 3 + 2];
    px[i * 4 + 3] = 255;
  }
  return img;
}

bool writePpm(const std::string& path, pp_image* img) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << info.width << " " << info.height << "\n255\n";
  const auto* px = static_cast<const unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < info.height; ++y)
    for (int x = 0; x < info.width; ++x) out.write(reinterpret_cast<const char*>(px + static_cast<size_t>(y) * info.stride + x * 4), 3);
  return static_cast<bool>(out);
}

pp_image* readPgm(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::string magic;
  int w = 0, h = 0, maxv = 0;
  in >> magic >> w >> h >> maxv;
  in.get();
  if (!in || magic != "P5" || maxv != 255 || w <= 0 || h <= 0) return nullptr;
  pp_image* img = nullptr;
  if (pp_image_create(w, h, PP_FORMAT_GRAY8, &img) != PP_OK) return nullptr;
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<char*>(pp_image_pixels(img));
  for (int y = 0; y < h; ++y) in.read(px + static_cast<size_t>(y) * info.stride, w);
  return in ? img : nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 5) {
    std::fprintf(stderr, "usage: backdrop_demo IN.ppm SUBJECT.pgm DEPTH.pgm|- OUT_PREFIX [strength] [design]\n");
    return 2;
  }
  pp_image* src = readPpm(argv[1]);
  pp_image* subject = readPgm(argv[2]);
  pp_image* depth = std::strcmp(argv[3], "-") == 0 ? nullptr : readPgm(argv[3]);
  if (!src || !subject) {
    std::fprintf(stderr, "cannot read inputs\n");
    return 1;
  }
  const float strength = argc > 5 ? std::strtof(argv[5], nullptr) : 1.0f;
  const int only = argc > 6 ? std::atoi(argv[6]) : -1;
  pp_image_info info{};
  pp_image_get_info(src, &info);
  for (int d = 0; d < PP_BACKDROP_COUNT; ++d) {
    if (only >= 0 && d != only) continue;
    pp_image* img = nullptr;
    pp_image_create(info.width, info.height, PP_FORMAT_RGBA8, &img);
    pp_image_info oi{};
    pp_image_get_info(img, &oi);
    for (int y = 0; y < info.height; ++y)
      std::memcpy(static_cast<char*>(pp_image_pixels(img)) + static_cast<size_t>(y) * oi.stride,
                  static_cast<const char*>(pp_image_pixels(src)) + static_cast<size_t>(y) * info.stride, static_cast<size_t>(info.width) * 4);
    pp_backdrop_params p{};
    p.design = d;
    p.strength = strength;
    p.seed = 7;
    float cov[2] = {0, 0};
    const auto t0 = std::chrono::steady_clock::now();
    const pp_status st = pp_backdrop(img, subject, depth, &p, nullptr, cov);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (st != PP_OK) {
      std::fprintf(stderr, "design %d failed: %s\n", d, pp_last_error_message());
      return 1;
    }
    char name[16];
    std::snprintf(name, sizeof name, "%02d", d);
    writePpm(std::string(argv[4]) + name + ".ppm", img);
    std::printf("{\"design\":%d,\"ms\":%.0f,\"subject\":%.3f,\"edge\":%.3f}\n", d, ms, cov[0], cov[1]);
    pp_image_destroy(img);
  }
  return 0;
}
