// Masters on files: art_demo in.ppm out.ppm artist level [mask.pgm|-] [x y w h] [seed]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "photoplus/pp_engine.h"

namespace {
bool readNetpbm(const std::string& path, const char* want, int& w, int& h, std::vector<unsigned char>& data, int channels) {
  std::ifstream in(path, std::ios::binary);
  std::string magic;
  int maxv = 0;
  in >> magic >> w >> h >> maxv;
  in.get();
  if (!in || magic != want || maxv != 255 || w <= 0 || h <= 0) return false;
  data.resize(static_cast<size_t>(w) * h * channels);
  in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(in);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 5) {
    std::fprintf(stderr, "art_demo in.ppm out.ppm artist level [mask.pgm|-] [x y w h] [seed]\n");
    return 2;
  }
  int w = 0, h = 0;
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
      const size_t i = (static_cast<size_t>(y) * w + x) * 3;
      p[0] = rgb[i], p[1] = rgb[i + 1], p[2] = rgb[i + 2], p[3] = 255;
    }
  pp_image* mask = nullptr;
  if (argc > 5 && std::strcmp(argv[5], "-") != 0) {
    int mw = 0, mh = 0;
    std::vector<unsigned char> g;
    if (readNetpbm(argv[5], "P5", mw, mh, g, 1)) {
      pp_image_create(mw, mh, PP_FORMAT_GRAY8, &mask);
      pp_image_info mi{};
      pp_image_get_info(mask, &mi);
      auto* mp = static_cast<unsigned char*>(pp_image_pixels(mask));
      for (int y = 0; y < mh; ++y) std::memcpy(mp + static_cast<size_t>(y) * mi.stride, g.data() + static_cast<size_t>(y) * mw, mw);
    }
  }
  float face[96] = {0};
  size_t faces = 0, stride = 4;
  if (argc > 9) {
    for (int i = 0; i < 4; ++i) face[i] = std::strtof(argv[6 + i], nullptr);
    faces = face[2] > 0 ? 1 : 0;
  }
  // ART_FACES="x y w h [x y w h ...]" (normalised): several faces (up to 8);
  // ART_FACE_STRIDE=12 adds each face's mouth box and eye centres.
  if (const char* st = std::getenv("ART_FACE_STRIDE")) stride = std::strtoul(st, nullptr, 10) == 12 ? 12 : 4;
  if (const char* fe = std::getenv("ART_FACES")) {
    char* p = const_cast<char*>(fe);
    faces = 0;
    while (faces < 8) {
      char* end = nullptr;
      float v[12];
      int got = 0;
      for (; got < static_cast<int>(stride); ++got) {
        v[got] = std::strtof(p, &end);
        if (end == p) break;
        p = end;
      }
      if (got < static_cast<int>(stride)) break;
      for (size_t i = 0; i < stride; ++i) face[faces * stride + i] = v[i];
      ++faces;
    }
  }
  const uint32_t seed = argc > 10 ? static_cast<uint32_t>(std::strtoul(argv[10], nullptr, 10)) : 7U;
  const auto t0 = std::chrono::steady_clock::now();
  // ART_CONTROLS="brush palette vivid likeness texture age background" for the person's controls.
  pp_art_controls k{1, 1, 0, 0.5f, 0.4f, 1, 1};
  const char* env = std::getenv("ART_CONTROLS");
  if (env != nullptr)
    std::sscanf(env, "%f %f %f %f %f %f %f", &k.brush, &k.palette, &k.vivid, &k.likeness, &k.texture, &k.age, &k.background);
  const pp_status s = pp_art_paint(img, std::atoi(argv[3]), std::atoi(argv[4]), mask, faces ? face : nullptr, faces, stride, seed, &k, nullptr);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (s != PP_OK) {
    std::fprintf(stderr, "failed: %s\n", pp_last_error_message());
    return 1;
  }
  std::fprintf(stderr, "artist %s level %s: %dx%d in %.0f ms\n", argv[3], argv[4], w, h, ms);
  std::ofstream out(argv[2], std::ios::binary);
  out << "P6\n" << w << " " << h << "\n255\n";
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) out.write(reinterpret_cast<const char*>(px + static_cast<size_t>(y) * info.stride + x * 4), 3);
  pp_image_destroy(img);
  if (mask) pp_image_destroy(mask);
  return 0;
}
