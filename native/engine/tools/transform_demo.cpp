// transform_demo — lens analysis and Transform on a PPM photo (developer
// tool, not shipped).
//
//   transform_demo IN.ppm [OUT.ppm] [--upscale 2] [--keep-style] [--strength v]
// Without OUT, only prints the lens analysis. Prints one-line JSON.

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
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned char* p = px + static_cast<size_t>(y) * info.stride + static_cast<size_t>(x) * 4;
      const unsigned char* s = rgb.data() + (static_cast<size_t>(y) * w + x) * 3;
      p[0] = s[0];
      p[1] = s[1];
      p[2] = s[2];
      p[3] = 255;
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

void printLens(const char* key, const pp_lens_report& r) {
  std::printf("\"%s\":{\"detail\":%.2f,\"corner\":%.2f,\"fringe\":%.2f,\"noise\":%.4f,\"grade\":%.2f,\"tiles\":%d,\"reliable\":%d}",
              key, r.detail_blur, r.corner_blur, r.fringe_px, r.noise, r.grade, r.measured_tiles, r.reliable);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: transform_demo IN.ppm [OUT.ppm] [--upscale 2] [--keep-style] [--strength v]\n");
    return 2;
  }
  pp_image* img = readPpm(argv[1]);
  if (!img) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }
  std::string out;
  pp_transform_params p{};
  pp_transform_default_params(&p);
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--upscale" && i + 1 < argc) p.upscale = std::atoi(argv[++i]);
    else if (a == "--strength" && i + 1 < argc) p.strength = std::strtof(argv[++i], nullptr);
    else if (a == "--keep-style") p.enrich = 0;
    else if (a[0] != '-') out = a;
  }
  if (out.empty()) {
    pp_lens_report r{};
    if (pp_lens_analyze(img, nullptr, &r) != PP_OK) {
      std::fprintf(stderr, "analysis failed: %s\n", pp_last_error_message());
      return 1;
    }
    std::printf("{");
    printLens("lens", r);
    std::printf("}\n");
    pp_image_destroy(img);
    return 0;
  }
  pp_image_info info{};
  pp_image_get_info(img, &info);
  pp_image* dst = nullptr;
  pp_image_create(info.width * p.upscale, info.height * p.upscale, PP_FORMAT_RGBA8, &dst);
  pp_transform_report r{};
  const auto t0 = std::chrono::steady_clock::now();
  if (pp_transform(img, dst, &p, nullptr, &r) != PP_OK) {
    std::fprintf(stderr, "transform failed: %s\n", pp_last_error_message());
    return 1;
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  writePpm(out, dst);
  pp_lens_report after{};
  pp_lens_analyze(dst, nullptr, &after);
  std::printf("{\"ms\":%.0f,", ms);
  printLens("before", r.before);
  std::printf(",");
  printLens("after", after);
  std::printf(",\"fringeFixed\":%.2f,\"deblur\":%.2f,\"deblurCorner\":%.2f,\"noise\":[%.4f,%.4f]}\n",
              r.fringe_fixed_px, r.deblur_centre, r.deblur_corner, r.noise_before, r.noise_after);
  pp_image_destroy(img);
  pp_image_destroy(dst);
  return 0;
}
