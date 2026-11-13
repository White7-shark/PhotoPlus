// skintone_demo — renders skin tone shades on a PPM photo (developer tool).
//
//   skintone_demo IN.ppm FACE.pgm SUBJECT.pgm|- PROTECT.pgm|- SHADES.txt OUT_PREFIX [strength]
//
// SHADES.txt: one shade per line, "gain_r gain_g gain_b glow" (linear-light gains). Writes
// OUT_PREFIX<index>.ppm per shade and prints one JSON line each.
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
  if (argc < 7) {
    std::fprintf(stderr, "usage: skintone_demo IN.ppm FACE.pgm SUBJECT.pgm|- PROTECT.pgm|- SHADES.txt OUT_PREFIX [strength]\n");
    return 2;
  }
  pp_image* src = readPpm(argv[1]);
  pp_image* face = readPgm(argv[2]);
  pp_image* subject = std::strcmp(argv[3], "-") == 0 ? nullptr : readPgm(argv[3]);
  pp_image* protect = std::strcmp(argv[4], "-") == 0 ? nullptr : readPgm(argv[4]);
  if (!src || !face) {
    std::fprintf(stderr, "cannot read inputs\n");
    return 1;
  }
  std::ifstream shades(argv[5]);
  const std::string prefix = argv[6];
  const float strength = argc > 7 ? std::strtof(argv[7], nullptr) : 1.0f;
  pp_image_info info{};
  pp_image_get_info(src, &info);
  float r, g, b, glow;
  int index = 0;
  while (shades >> r >> g >> b >> glow) {
    pp_image* img = nullptr;
    pp_image_create(info.width, info.height, PP_FORMAT_RGBA8, &img);
    pp_image_copy_pixels(src, img);
    pp_skin_tone_params p{};
    p.gain_r = r, p.gain_g = g, p.gain_b = b, p.glow = glow, p.strength = strength;
    p.evenness = 0.5f;
    int32_t hasSkin = 0;
    float cov = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const pp_status st = pp_skin_tone(img, subject, face, protect, &p, nullptr, &hasSkin, &cov);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (st != PP_OK) {
      std::fprintf(stderr, "shade %d failed: %d\n", index, static_cast<int>(st));
      return 1;
    }
    char name[16];
    std::snprintf(name, sizeof name, "%02d.ppm", index);
    writePpm(prefix + name, img);
    std::printf("{\"shade\":%d,\"ms\":%.0f,\"has_skin\":%d,\"coverage\":%.3f}\n", index, ms, hasSkin, cov);
    pp_image_destroy(img);
    ++index;
  }
  pp_image_destroy(src);
  pp_image_destroy(face);
  if (subject) pp_image_destroy(subject);
  if (protect) pp_image_destroy(protect);
  return 0;
}
