// studio_demo — the photoshoot studio on PPM/PGM files (developer tool).
//
//   studio_demo cutout IN.ppm MASK.pgm ZONES.pgm|- OUT_PREFIX [softness shift hair clean] [STROKES.txt]
//       writes OUT_PREFIX_fg.ppm (the person's colours) and OUT_PREFIX_alpha.pgm,
//       and prints the report as one JSON line. STROKES.txt: one stroke per
//       line, "brush radius x0 y0 x1 y1 ..." (brush 0 keep, 1 remove, 2 hair).
//   studio_demo compose BG.ppm FG.ppm ALPHA.pgm OUT.ppm x y height
//       [tilt angle flip colorMatch wrap shadow cast lightX horizon blur grain rim]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
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

pp_image* rgba(int w, int h) {
  pp_image* img = nullptr;
  return pp_image_create(w, h, PP_FORMAT_RGBA8, &img) == PP_OK ? img : nullptr;
}

pp_image* readPpm(const std::string& path, const std::vector<unsigned char>* alpha = nullptr) {
  int w = 0, h = 0;
  std::vector<unsigned char> rgb;
  if (!readNetpbm(path, "P6", w, h, rgb, 3)) return nullptr;
  pp_image* img = rgba(w, h);
  if (img == nullptr) return nullptr;
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  pp_image_info info{};
  pp_image_get_info(img, &info);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      unsigned char* p = px + static_cast<size_t>(y) * info.stride + x * 4;
      p[0] = rgb[i * 3], p[1] = rgb[i * 3 + 1], p[2] = rgb[i * 3 + 2];
      p[3] = alpha != nullptr && alpha->size() == static_cast<size_t>(w) * h ? (*alpha)[i] : 255;
    }
  return img;
}

pp_image* readPgm(const std::string& path) {
  int w = 0, h = 0;
  std::vector<unsigned char> g;
  if (!readNetpbm(path, "P5", w, h, g, 1)) return nullptr;
  pp_image* img = nullptr;
  if (pp_image_create(w, h, PP_FORMAT_GRAY8, &img) != PP_OK) return nullptr;
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < h; ++y) std::memcpy(px + static_cast<size_t>(y) * info.stride, g.data() + static_cast<size_t>(y) * w, w);
  return img;
}

bool write(const std::string& path, pp_image* img, int channel) {  // channel -1: RGB (P6), else that byte (P5)
  pp_image_info info{};
  pp_image_get_info(img, &info);
  std::ofstream out(path, std::ios::binary);
  out << (channel < 0 ? "P6\n" : "P5\n") << info.width << " " << info.height << "\n255\n";
  const auto* px = static_cast<const unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < info.height; ++y)
    for (int x = 0; x < info.width; ++x) {
      const unsigned char* p = px + static_cast<size_t>(y) * info.stride + x * 4;
      if (channel < 0) out.write(reinterpret_cast<const char*>(p), 3);
      else out.put(static_cast<char>(p[channel]));
    }
  return static_cast<bool>(out);
}

double ms(std::chrono::steady_clock::time_point a) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
}

int cutout(int argc, char** argv) {
  if (argc < 6) return 2;
  pp_image* img = readPpm(argv[2]);
  pp_image* mask = readPgm(argv[3]);
  pp_image* zones = std::strcmp(argv[4], "-") != 0 ? readPgm(argv[4]) : nullptr;
  if (img == nullptr || mask == nullptr) {
    std::fprintf(stderr, "cannot read inputs\n");
    return 1;
  }
  pp_studio_cutout_params p{0.0f, 0.0f, 0.5f, 1};
  if (argc > 6) p.softness = std::strtof(argv[6], nullptr);
  if (argc > 7) p.shift = std::strtof(argv[7], nullptr);
  if (argc > 8) p.hair_detail = std::strtof(argv[8], nullptr);
  if (argc > 9) p.clean_colours = std::atoi(argv[9]);
  std::vector<std::vector<float>> pts;
  std::vector<pp_studio_stroke> strokes;
  if (argc > 10) {
    std::ifstream in(argv[10]);
    std::string line;
    while (std::getline(in, line)) {
      std::istringstream ls(line);
      int brush = 0;
      float radius = 0;
      if (!(ls >> brush >> radius)) continue;
      std::vector<float> v;
      float x, y;
      while (ls >> x >> y) v.push_back(x), v.push_back(y);
      pts.push_back(std::move(v));
      strokes.push_back(pp_studio_stroke{brush, radius, 0, nullptr});
    }
    for (size_t i = 0; i < strokes.size(); ++i) {
      strokes[i].count = static_cast<int32_t>(pts[i].size() / 2);
      strokes[i].points = pts[i].data();
    }
  }
  pp_image_info info{};
  pp_image_get_info(img, &info);
  pp_image* out = rgba(info.width, info.height);
  pp_studio_cutout_report r{};
  const auto t0 = std::chrono::steady_clock::now();
  const pp_status st = pp_studio_cutout(img, mask, zones, strokes.data(), static_cast<int32_t>(strokes.size()), &p, out, nullptr, &r);
  const double t = ms(t0);
  if (st != PP_OK) {
    std::fprintf(stderr, "cutout failed: %s\n", pp_last_error_message());
    return 1;
  }
  const std::string prefix = argv[5];
  write(prefix + "_fg.ppm", out, -1);
  write(prefix + "_alpha.pgm", out, 3);
  std::printf("{\"ms\":%.1f,\"coverage\":%.4f,\"edge\":%.4f,\"hairEdge\":%.3f,\"box\":[%.4f,%.4f,%.4f,%.4f],\"cut\":[%d,%d,%d,%d],\"face\":%d,\"faceBox\":[%.4f,%.4f,%.4f,%.4f]}\n", t,
              r.coverage, r.edge, r.hair_edge, r.left, r.top, r.right, r.bottom, r.cut_left, r.cut_top, r.cut_right, r.cut_bottom, r.face,
              r.face_left, r.face_top, r.face_right, r.face_bottom);
  return 0;
}

int compose(int argc, char** argv) {
  if (argc < 9) return 2;
  pp_image* bg = readPpm(argv[2]);
  int aw = 0, ah = 0;
  std::vector<unsigned char> alpha;
  if (bg == nullptr || !readNetpbm(argv[4], "P5", aw, ah, alpha, 1)) {
    std::fprintf(stderr, "cannot read inputs\n");
    return 1;
  }
  pp_image* fg = readPpm(argv[3], &alpha);
  if (fg == nullptr) return 1;
  auto arg = [&](int i, float d) { return argc > i ? std::strtof(argv[i], nullptr) : d; };
  pp_studio_placement pl{arg(6, 0.5f), arg(7, 0.95f), arg(8, 0.8f), arg(9, 0.0f), arg(10, 0.0f), static_cast<int32_t>(arg(11, 0.0f))};
  pp_studio_look lk{arg(12, 0.7f), arg(13, 0.35f), arg(14, 0.6f), arg(15, 0.0f), arg(16, 0.0f), arg(17, 0.45f), arg(18, 0.0f), arg(19, 1.0f), 11u, arg(20, 0.0f), arg(21, 0.0f), 0, 1, 1, {1, 1, 1}};
  pp_studio_compose_report r{};
  const auto t0 = std::chrono::steady_clock::now();
  const pp_status st = pp_studio_compose(bg, fg, &pl, &lk, nullptr, &r);
  const double t = ms(t0);
  if (st != PP_OK) {
    std::fprintf(stderr, "compose failed: %s\n", pp_last_error_message());
    return 1;
  }
  write(argv[5], bg, -1);
  std::printf("{\"ms\":%.1f,\"box\":[%.4f,%.4f,%.4f,%.4f],\"exposure\":%.3f,\"balance\":[%.3f,%.3f,%.3f],\"grounded\":%d}\n", t, r.left,
              r.top, r.right, r.bottom, r.exposure, r.balance[0], r.balance[1], r.balance[2], r.grounded);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  int rc = 2;
  if (argc > 1 && std::strcmp(argv[1], "cutout") == 0) rc = cutout(argc, argv);
  else if (argc > 1 && std::strcmp(argv[1], "compose") == 0) rc = compose(argc, argv);
  if (rc == 2) {
    std::fprintf(stderr,
                 "usage: studio_demo cutout IN.ppm MASK.pgm ZONES.pgm|- OUT_PREFIX [softness shift hair clean] [STROKES.txt]\n"
                 "       studio_demo compose BG.ppm FG.ppm ALPHA.pgm OUT.ppm x y height [tilt angle flip colorMatch wrap "
                 "shadow cast lightX horizon blur grain rim spotlight]\n");
  }
  return rc;
}
