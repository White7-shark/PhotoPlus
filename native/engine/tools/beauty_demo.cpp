// beauty_demo — runs beauty recipes on a PPM photo (developer tool, not
// shipped). Masks and landmarks come from desktop stand-ins for the phone's
// on-device models (scripts/beauty-demo.mjs prepares them).
//
//   beauty_demo IN.ppm SUBJECT.pgm|- FACES.pgm|- PROTECT.pgm|- LANDMARKS.txt|- RECIPES.txt OUT_PREFIX
//
// RECIPES.txt: one recipe per line — an id, then 6 portrait floats (blur,
// smooth, blemish, even, brighten, edge), 13 face floats (eyes, teeth, lips,
// under-eye, shine, face light, studio, blush, blush R G B, contour,
// highlight) and PP_BEAUTY_GRADE_FLOATS grade floats in pp_beauty_grade
// order. Writes OUT_PREFIX<id>.ppm per recipe and prints one JSON line each.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
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

std::vector<pp_face> readFaces(const std::string& path) {
  std::vector<pp_face> faces;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    for (char& c : line) if (c == ',') c = ' ';
    std::istringstream ss(line);
    pp_face f{};
    int n = 0;
    float v = 0;
    while (n < PP_FACE_POINTS * 2 && ss >> v) f.points[n++] = v;
    if (n == PP_FACE_POINTS * 2) {
      f.detailed = 1;
      faces.push_back(f);
    }
  }
  return faces;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 8) {
    std::fprintf(stderr, "usage: beauty_demo IN.ppm SUBJECT.pgm|- FACES.pgm|- PROTECT.pgm|- LANDMARKS.txt|- RECIPES.txt OUT_PREFIX\n");
    return 2;
  }
  pp_image* src = readPpm(argv[1]);
  if (!src) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }
  auto mask = [](const char* p) { return std::strcmp(p, "-") == 0 ? nullptr : readPgm(p); };
  pp_image* subject = mask(argv[2]);
  pp_image* facesMask = mask(argv[3]);
  pp_image* protect = mask(argv[4]);
  const std::vector<pp_face> faces = std::strcmp(argv[5], "-") == 0 ? std::vector<pp_face>{} : readFaces(argv[5]);
  {
    // What the grade measures (the live shader's scene uniforms).
    pp_beauty_scene sc{};
    if (pp_beauty_measure(src, subject, facesMask, protect, faces.empty() ? nullptr : faces.data(), faces.size(), &sc) == PP_OK) {
      std::printf("{\"scene\":[%d,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f]}\n", sc.has_skin, sc.skin_y, sc.skin_cb, sc.skin_cr,
                  sc.skin_spread, sc.subject_x, sc.subject_w, sc.light_angle, sc.skin_y_spread);
    }
  }
  std::ifstream recipes(argv[6]);
  std::string line;
  pp_image_info info{};
  pp_image_get_info(src, &info);
  while (std::getline(recipes, line)) {
    std::istringstream ss(line);
    std::string id;
    if (!(ss >> id)) continue;
    std::vector<float> v;
    float x = 0;
    while (ss >> x) v.push_back(x);
    if (v.size() != 6 + 13 + PP_BEAUTY_GRADE_FLOATS) {
      std::fprintf(stderr, "recipe %s: %zu numbers\n", id.c_str(), v.size());
      return 1;
    }
    pp_beauty_params p{};
    pp_beauty_default_params(&p);
    p.portrait.blur = v[0];
    p.portrait.retouch = 0;
    p.portrait.smooth = v[1];
    p.portrait.blemish = v[2];
    p.portrait.even = v[3];
    p.portrait.brighten = v[4];
    p.portrait.edge = v[5];
    const float* f = v.data() + 6;
    p.face.eyes = f[0];
    p.face.teeth = f[1];
    p.face.lips = f[2];
    p.face.under_eye = f[3];
    p.face.shine = f[4];
    p.face.face_light = f[5];
    p.face.studio_light = f[6];
    p.face.blush = f[7];
    p.face.blush_color[0] = f[8];
    p.face.blush_color[1] = f[9];
    p.face.blush_color[2] = f[10];
    p.face.contour = f[11];
    p.face.highlight = f[12];
    std::memcpy(&p.grade, v.data() + 19, sizeof(pp_beauty_grade));
    pp_image* img = nullptr;
    pp_image_create(info.width, info.height, PP_FORMAT_RGBA8, &img);
    pp_image_info oi{};
    pp_image_get_info(img, &oi);
    for (int y = 0; y < info.height; ++y)
      std::memcpy(static_cast<char*>(pp_image_pixels(img)) + static_cast<size_t>(y) * oi.stride,
                  static_cast<const char*>(pp_image_pixels(src)) + static_cast<size_t>(y) * info.stride, static_cast<size_t>(info.width) * 4);
    pp_beauty_report r{};
    const auto t0 = std::chrono::steady_clock::now();
    const pp_status st = pp_beauty(img, subject, facesMask, protect, nullptr, faces.empty() ? nullptr : faces.data(), faces.size(), &p, nullptr, &r);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (st != PP_OK) {
      std::fprintf(stderr, "recipe %s failed: %s\n", id.c_str(), pp_last_error_message());
      return 1;
    }
    writePpm(std::string(argv[7]) + id + ".ppm", img);
    std::printf("{\"id\":\"%s\",\"ms\":%.0f,\"lightAngle\":%.2f,\"skin\":%.3f,\"subject\":%.3f,\"clippedBefore\":%.4f,\"clippedAfter\":%.4f,\"spots\":%d,\"faces\":%d,\"layers\":%d}\n",
                id.c_str(), ms, r.light_angle, r.skin_coverage, r.subject_coverage, r.clipped_before, r.clipped_after,
                r.spots_removed, r.faces_adjusted, r.layers);
    pp_image_destroy(img);
  }
  for (pp_image* m : {src, subject, facesMask, protect}) if (m) pp_image_destroy(m);
  return 0;
}
