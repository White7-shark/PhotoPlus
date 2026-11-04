// enhance_bench — runs the Photo Plus enhancement engine on PPM images.
//
//   enhance_bench [--upscale N] [--restore-only] OUT.ppm IN1.ppm [IN2.ppm ...]
//
// Several inputs are treated as a handheld burst of the same scene. Prints a
// one-line JSON report. Used by scripts/enhance-benchmark.py; not shipped.

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
    px[i * 4 + 0] = rgb[i * 3 + 0];
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
  for (int y = 0; y < info.height; ++y) {
    for (int x = 0; x < info.width; ++x) {
      out.write(reinterpret_cast<const char*>(px + static_cast<size_t>(y) * info.stride + x * 4), 3);
    }
  }
  return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char** argv) {
  pp_enhance_params params{};
  pp_enhance_default_params(&params);
  std::vector<std::string> paths;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--upscale") == 0 && i + 1 < argc) {
      params.upscale = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--restore-only") == 0) {
      params.auto_tone = 0;
      params.detail = 0.0f;
      params.vibrance = 0.0f;
    } else {
      paths.emplace_back(argv[i]);
    }
  }
  if (paths.size() < 2) {
    std::fprintf(stderr, "usage: enhance_bench [--upscale N] [--restore-only] OUT.ppm IN.ppm [IN.ppm ...]\n");
    return 2;
  }
  std::vector<pp_image*> frames;
  for (size_t i = 1; i < paths.size(); ++i) {
    pp_image* f = readPpm(paths[i]);
    if (f == nullptr) {
      std::fprintf(stderr, "cannot read %s\n", paths[i].c_str());
      return 1;
    }
    frames.push_back(f);
  }
  pp_profiler_reset();
  pp_image* out = nullptr;
  pp_enhance_report report{};
  const auto start = std::chrono::steady_clock::now();
  const pp_status status = pp_enhance(frames.data(), frames.size(), &params, nullptr, &out, &report);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  for (auto* f : frames) pp_image_destroy(f);
  if (status != PP_OK) {
    std::fprintf(stderr, "enhance failed: %s: %s\n", pp_status_name(status), pp_last_error_message());
    return 1;
  }
  if (std::getenv("PP_PROFILE") != nullptr) {
    char buf[8192];
    pp_profiler_report_json(buf, sizeof(buf));
    std::fprintf(stderr, "%s\n", buf);
  }
  const bool ok = writePpm(paths[0], out);
  pp_image_destroy(out);
  std::printf(
      "{\"ms\":%.1f,\"framesInput\":%d,\"framesUsed\":%d,\"noiseBefore\":%.4f,\"noiseAfter\":%.4f,"
      "\"blurBefore\":%.3f,\"deblurSigma\":%.3f}\n",
      ms, report.frames_input, report.frames_used, report.noise_before, report.noise_after, report.blur_before,
      report.deblur_sigma);
  return ok ? 0 : 1;
}
