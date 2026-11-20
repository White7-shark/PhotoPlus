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
#include <map>
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

// hair IN.ppm MASK.pgm ZONES.pgm OUT_PREFIX STYLE|- [R G B [STRENGTH]]: a cut-out, then a hairstyle and/or colour.
int hair(int argc, char** argv) {
  if (argc < 7) return 2;
  pp_image* img = readPpm(argv[2]);
  pp_image* mask = readPgm(argv[3]);
  pp_image* zones = readPgm(argv[4]);
  if (img == nullptr || mask == nullptr || zones == nullptr) {
    std::fprintf(stderr, "cannot read inputs\n");
    return 1;
  }
  pp_image_info info{};
  pp_image_get_info(img, &info);
  pp_image* out = rgba(info.width, info.height);
  const pp_studio_cutout_params p{0.0f, 0.0f, 0.5f, 1};
  pp_studio_cutout_report r{};
  if (pp_studio_cutout(img, mask, zones, nullptr, 0, &p, out, nullptr, &r) != PP_OK) {
    std::fprintf(stderr, "cutout failed: %s\n", pp_last_error_message());
    return 1;
  }
  const char* style = std::strcmp(argv[6], "-") == 0 ? nullptr : argv[6];
  const bool recolor = argc > 9;
  const auto c = [&](int i) { return static_cast<uint8_t>(argc > i ? std::atoi(argv[i]) : 0); };
  const float face[4] = {r.face_left, r.face_top, r.face_right, r.face_bottom};
  const auto t0 = std::chrono::steady_clock::now();
  // A photographer's edit: PP_HAIR_EDIT="length=0.5,volume=-0.3,..." (keys as in
  // pp_studio_hair_edit) and brushes PP_HAIR_STROKES="brush radius x y x y ...;...".
  pp_studio_hair_edit ed{};
  ed.size = 1.0f;
  if (const char* e = std::getenv("PP_HAIR_EDIT")) {
    std::stringstream ss(e);
    std::string kv;
    while (std::getline(ss, kv, ',')) {
      const auto eq = kv.find('=');
      if (eq == std::string::npos) continue;
      const std::string k = kv.substr(0, eq);
      const float v = std::strtof(kv.c_str() + eq + 1, nullptr);
      if (k == "length") ed.length = v;
      else if (k == "volume") ed.volume = v;
      else if (k == "height") ed.height = v;
      else if (k == "fringe") ed.fringe = v;
      else if (k == "part") ed.part_set = 1, ed.part = v;
      else if (k == "waves") ed.waves = v;
      else if (k == "shine") ed.shine = v;
      else if (k == "moveX") ed.move_x = v;
      else if (k == "moveY") ed.move_y = v;
      else if (k == "size") ed.size = v;
      else if (k == "rotate") ed.rotate = v;
      else if (k == "depth") ed.depth = v;
      else if (k == "roots") ed.roots = v;
      else if (k == "ends") ed.ends = v;
    }
  }
  std::vector<std::vector<float>> pts;
  std::vector<pp_studio_stroke> strokes;
  if (const char* e = std::getenv("PP_HAIR_STROKES")) {
    std::stringstream ss(e);
    std::string one;
    while (std::getline(ss, one, ';')) {
      std::stringstream ls(one);
      int brush = 0;
      float radius = 0.02f;
      ls >> brush >> radius;
      std::vector<float> v;
      float x = 0;
      while (ls >> x) v.push_back(x);
      pts.push_back(v);
      strokes.push_back({brush, radius, static_cast<int32_t>(v.size() / 2), nullptr});
    }
    for (size_t i = 0; i < strokes.size(); ++i) strokes[i].points = pts[i].data();
  }
  if (pp_studio_hair_edited(out, zones, style, recolor ? 1 : 0, c(7), c(8), c(9), argc > 10 ? std::strtof(argv[10], nullptr) : 1.0f, face, &ed,
                            strokes.data(), static_cast<int32_t>(strokes.size()), 5, nullptr) != PP_OK) {
    std::fprintf(stderr, "hair failed: %s\n", pp_last_error_message());
    return 1;
  }
  const std::string prefix = argv[5];
  write(prefix + "_fg.ppm", out, -1);
  write(prefix + "_alpha.pgm", out, 3);
  std::printf("{\"ms\":%.1f,\"faces\":%d}\n", ms(t0), r.faces);
  return 0;
}

int styles() {
  for (int32_t i = 0; i < pp_studio_hair_style_count(); ++i) std::printf("%s %d\n", pp_studio_hair_style_id(i), pp_studio_hair_style_women(i));
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
  pp_studio_placement pl{arg(6, 0.5f), arg(7, 0.95f), arg(8, 0.8f), arg(9, 0.0f), arg(10, 0.0f), static_cast<int32_t>(arg(11, 0.0f)),
                         static_cast<int32_t>(arg(22, 0.0f)), arg(23, 0.0f), arg(24, 0.0f), arg(25, 1.0f), arg(26, 1.0f)};
  pp_studio_look lk{arg(12, 0.7f), arg(13, 0.35f), arg(14, 0.6f), arg(15, 0.0f), arg(16, 0.0f), arg(17, 0.45f), arg(18, 0.0f), arg(19, 1.0f), static_cast<uint32_t>(arg(32, 11.0f)), arg(20, 0.0f), arg(21, 0.0f),
                   static_cast<int32_t>(arg(27, 0.0f)), arg(28, 1.0f), arg(29, 1.0f), {arg(30, 1.0f), arg(31, 1.0f), arg(33, 1.0f)}, arg(34, 0.0f)};
  pp_studio_compose_report r{};
  const auto t0 = std::chrono::steady_clock::now();
  const pp_status st = pp_studio_compose(bg, fg, &pl, &lk, nullptr, &r);
  const double t = ms(t0);
  if (st != PP_OK) {
    std::fprintf(stderr, "compose failed: %s\n", pp_last_error_message());
    return 1;
  }
  write(argv[5], bg, -1);
  std::printf("{\"ms\":%.1f,\"box\":[%.4f,%.4f,%.4f,%.4f],\"exposure\":%.3f,\"balance\":[%.3f,%.3f,%.3f],\"grounded\":%d,\"contrast\":%.4f}\n", t, r.left,
              r.top, r.right, r.bottom, r.exposure, r.balance[0], r.balance[1], r.balance[2], r.grounded, r.contrast);
  return 0;
}

// backdrop OUT.ppm W H R G B [LIGHT=1] [CX=0.5] [CY=0.42]: a studio paper backdrop.
int backdrop(int argc, char** argv) {
  if (argc < 8) return 2;
  pp_image* img = nullptr;
  if (pp_image_create(std::atoi(argv[3]), std::atoi(argv[4]), PP_FORMAT_RGBA8, &img) != PP_OK) return 1;
  auto arg = [&](int i, float d) { return argc > i ? std::strtof(argv[i], nullptr) : d; };
  const pp_status st = pp_studio_backdrop(img, static_cast<uint8_t>(std::atoi(argv[5])), static_cast<uint8_t>(std::atoi(argv[6])),
                                          static_cast<uint8_t>(std::atoi(argv[7])), arg(8, 1.0f), arg(9, 0.5f), arg(10, 0.42f), 7, nullptr);
  if (st != PP_OK) return 1;
  write(argv[2], img, -1);
  pp_image_destroy(img);
  return 0;
}

// studio IN.ppm MASK.pgm ZONES.pgm OUT.ppm [key=value ...]: the whole studio:
// cut-out, studio light, a gradient backdrop, compose, finish. Keys: light
// (strength) angle height soft key fill keyc fillc rimL rimR rimc (r,g,b
// gains); from to (backdrop hex colours) radial (0/1) gangle; vig vigsize
// warm tint contrast sat fade sh hi tone grain gsize mono; size (person's
// height in the frame) fw fh (frame size).
int studio(int argc, char** argv) {
  if (argc < 6) return 2;
  std::map<std::string, std::string> kv;
  for (int i = 6; i < argc; ++i) {
    const char* eq = std::strchr(argv[i], '=');
    if (eq != nullptr) kv[std::string(argv[i], static_cast<size_t>(eq - argv[i]))] = eq + 1;
  }
  auto num = [&](const char* k, float d) { return kv.count(k) ? std::strtof(kv[k].c_str(), nullptr) : d; };
  auto tri = [&](const char* k, float d, float out[3]) {
    out[0] = out[1] = out[2] = d;
    if (kv.count(k)) std::sscanf(kv[k].c_str(), "%f,%f,%f", &out[0], &out[1], &out[2]);
  };
  auto hex = [&](const char* k, const char* d, uint8_t out[3]) {
    const std::string v = kv.count(k) ? kv[k] : d;
    unsigned r = 0, g = 0, b = 0;
    std::sscanf(v.c_str(), "%02x%02x%02x", &r, &g, &b);
    out[0] = static_cast<uint8_t>(r), out[1] = static_cast<uint8_t>(g), out[2] = static_cast<uint8_t>(b);
  };
  pp_image* img = readPpm(argv[2]);
  pp_image* mask = readPgm(argv[3]);
  pp_image* zones = readPgm(argv[4]);
  if (img == nullptr || mask == nullptr || zones == nullptr) return 1;
  pp_image_info info{};
  pp_image_get_info(img, &info);
  pp_image* cut = rgba(info.width, info.height);
  const pp_studio_cutout_params cp{0.0f, 0.0f, 0.5f, 1};
  pp_studio_cutout_report r{};
  if (pp_studio_cutout(img, mask, zones, nullptr, 0, &cp, cut, nullptr, &r) != PP_OK) return 1;
  const auto t0 = std::chrono::steady_clock::now();
  pp_studio_light L{};
  L.strength = num("light", 1.0f), L.key_angle = num("angle", -45.0f), L.key_height = num("height", 35.0f), L.key_softness = num("soft", 0.5f);
  L.key = num("key", 1.0f), L.fill = num("fill", 0.35f), L.rim_left = num("rimL", 0.0f), L.rim_right = num("rimR", 0.0f);
  tri("keyc", 1.0f, L.key_colour), tri("fillc", 1.0f, L.fill_colour), tri("rimc", 1.0f, L.rim_colour);
  L.face[0] = r.face_left, L.face[1] = r.face_top, L.face[2] = r.face_right, L.face[3] = r.face_bottom;
  if (pp_studio_relight(cut, &L, nullptr) != PP_OK) {
    std::fprintf(stderr, "light failed: %s\n", pp_last_error_message());
    return 1;
  }
  const double tLight = ms(t0);
  const auto fw = static_cast<int32_t>(num("fw", static_cast<float>(info.width))), fh = static_cast<int32_t>(num("fh", static_cast<float>(info.height)));
  pp_image* frame = rgba(fw, fh);
  uint8_t from[3], to[3];
  hex("from", "d9d4cc", from), hex("to", "8a8580", to);
  if (pp_studio_gradient(frame, from, to, static_cast<int32_t>(num("radial", 1.0f)), num("gangle", 90.0f), 0.5f, 0.38f, num("gradius", 0.85f)) != PP_OK) return 1;
  // The person as in the photo (their box's share of it), standing on its bottom edge.
  const float size = num("size", r.bottom - r.top);
  pp_studio_placement pl{0.5f * (r.left + r.right), r.bottom > 0.97f ? 1.0f : r.bottom, size, 0, 0, 0, 0, 0, 0, 1, 1};
  pp_studio_look lk{0.25f, 0.3f, 0.0f, 0.0f, 0.0f, 0.45f, num("blur", 0.0f), 0.6f, 11, 0.0f, 0.0f, 0, 1, 1, {1, 1, 1}, num("bounce", 0.4f)};
  if (pp_studio_compose(frame, cut, &pl, &lk, nullptr, nullptr) != PP_OK) {
    std::fprintf(stderr, "compose failed: %s\n", pp_last_error_message());
    return 1;
  }
  pp_studio_finish F{};
  F.vignette = num("vig", 0.0f), F.vignette_size = num("vigsize", 0.5f), F.warmth = num("warm", 0.0f), F.tint = num("tint", 0.0f);
  F.contrast = num("contrast", 0.0f), F.saturation = num("sat", 0.0f), F.fade = num("fade", 0.0f), F.tone_amount = num("tone", 0.0f);
  F.grain = num("grain", 0.0f), F.grain_size = num("gsize", 0.5f), F.mono = static_cast<int32_t>(num("mono", 0.0f)), F.seed = 3;
  tri("sh", 0.0f, F.shadow_tone), tri("hi", 0.0f, F.highlight_tone);
  if (pp_studio_finish_frame(frame, &F, nullptr) != PP_OK) return 1;
  write(argv[5], frame, -1);
  std::printf("{\"ms\":%.1f,\"light_ms\":%.1f}\n", ms(t0), tLight);
  return 0;
}

}  // namespace

// lips IN.ppm OUT.ppm LANDMARKS.txt R G B [STRENGTH]: pink lips. LANDMARKS.txt
// holds 262 numbers per face (131 x,y pairs normalised to the photo).
int lips(int argc, char** argv) {
  if (argc < 7) return 2;
  pp_image* img = readPpm(argv[2]);
  if (!img) return 1;
  std::ifstream in(argv[4]);
  std::vector<float> marks;
  for (float v; in >> v;) marks.push_back(v);
  const auto faces = static_cast<int32_t>(marks.size() / 262);
  float rep[2] = {};
  const int rc = pp_studio_lips(img, marks.data(), faces, std::atoi(argv[5]), std::atoi(argv[6]), std::atoi(argv[7]),
                                argc > 8 ? std::strtof(argv[8], nullptr) : 0.85f, nullptr, rep);
  if (rc != PP_OK) return 1;
  std::printf("{\"lips\":%.0f,\"lip_luma\":%.3f}\n", rep[0], rep[1]);
  write(argv[3], img, -1);
  pp_image_destroy(img);
  return 0;
}

int main(int argc, char** argv) {
  int rc = 2;
  if (argc > 1 && std::strcmp(argv[1], "lips") == 0) rc = lips(argc, argv);
  if (argc > 1 && std::strcmp(argv[1], "cutout") == 0) rc = cutout(argc, argv);
  else if (argc > 1 && std::strcmp(argv[1], "compose") == 0) rc = compose(argc, argv);
  else if (argc > 1 && std::strcmp(argv[1], "backdrop") == 0) rc = backdrop(argc, argv);
  else if (argc > 1 && std::strcmp(argv[1], "hair") == 0) rc = hair(argc, argv);
  else if (argc > 1 && std::strcmp(argv[1], "styles") == 0) rc = styles();
  else if (argc > 1 && std::strcmp(argv[1], "studio") == 0) rc = studio(argc, argv);
  if (rc == 2) {
    std::fprintf(stderr,
                 "usage: studio_demo backdrop OUT.ppm W H R G B [light cx cy]\n"
                 "       studio_demo hair IN.ppm MASK.pgm ZONES.pgm OUT_PREFIX STYLE|- [R G B [strength]]\n"
                 "       studio_demo styles\n"
                 "       studio_demo lips IN.ppm OUT.ppm LANDMARKS.txt R G B [strength]\n"
                 "       studio_demo studio IN.ppm MASK.pgm ZONES.pgm OUT.ppm [key=value ...] (light, backdrop, finish)\n"
                 "       studio_demo cutout IN.ppm MASK.pgm ZONES.pgm|- OUT_PREFIX [softness shift hair clean] [STROKES.txt]\n"
                 "       studio_demo compose BG.ppm FG.ppm ALPHA.pgm OUT.ppm x y height [tilt angle flip colorMatch wrap "
                 "shadow cast lightX horizon blur grain rim spotlight [fixedBox l t r b [fixedColour gain contrast balR balG seed balB]]]\n");
  }
  return rc;
}
