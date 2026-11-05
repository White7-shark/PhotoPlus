// portrait_demo — runs the portrait tools on a PPM photo (developer tool,
// not shipped). The subject comes from tap points, as with the in-app
// fallback; face/feature areas can be given as ellipses (on the phone they
// come from face detection).
//
//   portrait_demo IN.ppm OUT.ppm [MASK.pgm] [--tap x,y]... [--face cx,cy,rx,ry]...
//                 [--protect cx,cy,rx,ry]... [--blur v] [--retouch v] [--brighten v] [--pop v]
//                 [--look REF.ppm] [--strength v]
//                 [--subject-mask M.pgm] [--faces-mask F.pgm] [--protect-mask P.pgm]
// Mask files replace taps/ellipses (e.g. masks from an on-device model).
// Coordinates are normalised to 0..1. Prints a one-line JSON report.

#include <chrono>
#include <cmath>
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

void writePgm(const std::string& path, pp_image* img) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  std::ofstream out(path, std::ios::binary);
  out << "P5\n" << info.width << " " << info.height << "\n255\n";
  const auto* px = static_cast<const unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < info.height; ++y) out.write(reinterpret_cast<const char*>(px + static_cast<size_t>(y) * info.stride), info.width);
}

pp_image* ellipses(int w, int h, const std::vector<std::vector<float>>& es) {
  if (es.empty()) return nullptr;
  pp_image* m = nullptr;
  pp_image_create(w, h, PP_FORMAT_GRAY8, &m);
  pp_image_info info{};
  pp_image_get_info(m, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(m));
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float v = 0.0f;
      for (const auto& e : es) {
        const float dx = (static_cast<float>(x) / static_cast<float>(w) - e[0]) / e[2];
        const float dy = (static_cast<float>(y) / static_cast<float>(h) - e[1]) / e[3];
        const float d = std::sqrt(dx * dx + dy * dy);
        v = std::fmax(v, std::fmin(1.0f, std::fmax(0.0f, (1.05f - d) / 0.1f)));  // soft edge
      }
      px[static_cast<size_t>(y) * info.stride + x] = static_cast<unsigned char>(v * 255.0f);
    }
  return m;
}

std::vector<float> nums(const char* s) {
  std::vector<float> v;
  char* end = nullptr;
  for (const char* p = s; *p;) {
    v.push_back(std::strtof(p, &end));
    p = (*end == ',') ? end + 1 : end;
    if (end == p && *p) break;
  }
  return v;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: portrait_demo IN.ppm OUT.ppm [MASK.pgm] [options]\n");
    return 2;
  }
  pp_image* img = readPpm(argv[1]);
  if (!img) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }
  std::string maskOut;
  int i = 3;
  if (argc > 3 && argv[3][0] != '-') maskOut = argv[i++];
  std::vector<float> taps;
  std::vector<std::vector<float>> faces, protect;
  pp_portrait_params params{};
  pp_portrait_default_params(&params);
  std::string look, subjectFile, facesFile, protectFile;
  float strength = 0.8f;
  for (; i + 1 < argc; i += 2) {
    const std::string k = argv[i];
    const auto v = nums(argv[i + 1]);
    if (k == "--tap" && v.size() == 2) taps.insert(taps.end(), v.begin(), v.end());
    else if (k == "--face" && v.size() == 4) faces.push_back(v);
    else if (k == "--protect" && v.size() == 4) protect.push_back(v);
    else if (k == "--blur") params.blur = v.at(0);
    else if (k == "--retouch") params.retouch = v.at(0);
    else if (k == "--brighten") params.brighten = v.at(0);
    else if (k == "--pop") params.pop = v.at(0);
    else if (k == "--look") look = argv[i + 1];
    else if (k == "--strength") strength = v.at(0);
    else if (k == "--subject-mask") subjectFile = argv[i + 1];
    else if (k == "--faces-mask") facesFile = argv[i + 1];
    else if (k == "--protect-mask") protectFile = argv[i + 1];
  }
  pp_image_info info{};
  pp_image_get_info(img, &info);
  const auto t0 = std::chrono::steady_clock::now();
  if (!look.empty()) {
    pp_image* ref = readPpm(look);
    pp_style st{};
    if (!ref || pp_style_analyze(ref, &st) != PP_OK || pp_style_apply(img, &st, strength, nullptr) != PP_OK) {
      std::fprintf(stderr, "look failed: %s\n", pp_last_error_message());
      return 1;
    }
    pp_image_destroy(ref);
  }
  pp_image* subject = subjectFile.empty() ? nullptr : readPgm(subjectFile);
  if (subject == nullptr && pp_subject_mask(img, taps.data(), taps.size() / 2, nullptr, &subject) != PP_OK) {
    std::fprintf(stderr, "cut-out failed: %s\n", pp_last_error_message());
    return 1;
  }
  pp_image* faceMask = facesFile.empty() ? ellipses(info.width / 2, info.height / 2, faces) : readPgm(facesFile);
  pp_image* protectMask = protectFile.empty() ? ellipses(info.width / 2, info.height / 2, protect) : readPgm(protectFile);
  pp_portrait_report r{};
  if (pp_portrait(img, subject, faceMask, protectMask, &params, nullptr, &r) != PP_OK) {
    std::fprintf(stderr, "portrait failed: %s\n", pp_last_error_message());
    return 1;
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (!maskOut.empty()) writePgm(maskOut, subject);
  writePpm(argv[2], img);
  std::printf("{\"ms\":%.1f,\"subject\":%.3f,\"skin\":%.4f,\"spots\":%d,\"blurRadius\":%.1f}\n", ms, r.subject_coverage,
              r.skin_coverage, r.spots_removed, r.blur_radius);
  pp_image_destroy(subject);
  pp_image_destroy(faceMask);
  pp_image_destroy(protectMask);
  pp_image_destroy(img);
  return 0;
}
