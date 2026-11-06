// edit_demo — runs the photo editor's engine steps on a PPM photo (developer
// tool, not shipped). Props and stamps are PAM (P7, RGB_ALPHA) files.
//
//   edit_demo IN.ppm OUT.ppm [--op type,p0,p1,...]...
//             [--prop FILE.pam cx cy width rotation flip blend shadow]...
//             [--frame canvasW canvasH shape param rot wx wy ww wh backdrop colorA colorB border borderColor
//                      metallic gap outlineW outlineColor shadow shadowOff shadowBlur glow glowColor zoom ox oy deco]
//             [--stamp FILE.pam x y opacity]
// Colours are hex 0xAARRGGBB. Order: ops, props, frame, stamp. Prints JSON.

#include <cstdio>
#include <cstdlib>
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
  if (!in || magic != "P6" || maxv != 255) return nullptr;
  std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
  in.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    px[i * 4] = rgb[i * 3];
    px[i * 4 + 1] = rgb[i * 3 + 1];
    px[i * 4 + 2] = rgb[i * 3 + 2];
    px[i * 4 + 3] = 255;
  }
  return img;
}

pp_image* readPam(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::string line, magic;
  std::getline(in, magic);
  if (magic != "P7") return nullptr;
  int w = 0, h = 0, depth = 0;
  while (std::getline(in, line) && line != "ENDHDR") {
    std::istringstream ss(line);
    std::string key;
    ss >> key;
    if (key == "WIDTH") ss >> w;
    else if (key == "HEIGHT") ss >> h;
    else if (key == "DEPTH") ss >> depth;
  }
  if (w <= 0 || h <= 0 || depth != 4) return nullptr;
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
  in.read(static_cast<char*>(pp_image_pixels(img)), static_cast<std::streamsize>(w) * h * 4);
  return img;
}

void writePpm(const std::string& path, pp_image* img) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << info.width << " " << info.height << "\n255\n";
  const auto* px = static_cast<const unsigned char*>(pp_image_pixels(img));
  for (int y = 0; y < info.height; ++y)
    for (int x = 0; x < info.width; ++x) out.write(reinterpret_cast<const char*>(px + static_cast<size_t>(y) * info.stride + x * 4), 3);
}

std::vector<double> nums(const std::string& s) {
  std::vector<double> v;
  std::stringstream ss(s);
  std::string tok;
  while (std::getline(ss, tok, ',')) v.push_back(std::strtod(tok.c_str(), nullptr));
  return v;
}

uint32_t hex(const char* s) { return static_cast<uint32_t>(std::strtoul(s, nullptr, 16)); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: edit_demo IN.ppm OUT.ppm [options]\n");
    return 2;
  }
  pp_image* img = readPpm(argv[1]);
  if (!img) return 1;
  std::vector<pp_op> ops;
  long long covered = 0, visible = -1;
  struct Prop {
    std::string file;
    pp_prop_params p;
  };
  std::vector<Prop> props;
  bool hasFrame = false;
  int canvasW = 0, canvasH = 0;
  pp_frame_params f{};
  std::string stampFile;
  int sx = 0, sy = 0;
  float sop = 1.0f;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--op" && i + 1 < argc) {
      const auto v = nums(argv[++i]);
      pp_op o{};
      o.type = static_cast<int32_t>(v.at(0));
      for (size_t k = 1; k < v.size() && k <= PP_OP_PARAMS; ++k) o.p[k - 1] = static_cast<float>(v[k]);
      ops.push_back(o);
    } else if (a == "--prop" && i + 8 < argc) {
      Prop pr;
      pr.file = argv[++i];
      pr.p.cx = std::strtof(argv[++i], nullptr);
      pr.p.cy = std::strtof(argv[++i], nullptr);
      pr.p.width = std::strtof(argv[++i], nullptr);
      pr.p.rotation_deg = std::strtof(argv[++i], nullptr);
      pr.p.flip = std::atoi(argv[++i]);
      pr.p.blend = std::strtof(argv[++i], nullptr);
      pr.p.shadow = std::strtof(argv[++i], nullptr);
      pr.p.opacity = 1.0f;
      props.push_back(pr);
    } else if (a == "--frame" && i + 27 < argc) {
      hasFrame = true;
      canvasW = std::atoi(argv[++i]);
      canvasH = std::atoi(argv[++i]);
      f.shape = std::atoi(argv[++i]);
      f.shape_param = std::strtof(argv[++i], nullptr);
      f.rotation_deg = std::strtof(argv[++i], nullptr);
      for (float& w : f.window) w = std::strtof(argv[++i], nullptr);
      f.backdrop = std::atoi(argv[++i]);
      f.color_a = hex(argv[++i]);
      f.color_b = hex(argv[++i]);
      f.border = std::strtof(argv[++i], nullptr);
      f.border_color = hex(argv[++i]);
      f.metallic_border = std::atoi(argv[++i]);
      f.outline_gap = std::strtof(argv[++i], nullptr);
      f.outline_width = std::strtof(argv[++i], nullptr);
      f.outline_color = hex(argv[++i]);
      f.shadow = std::strtof(argv[++i], nullptr);
      f.shadow_offset = std::strtof(argv[++i], nullptr);
      f.shadow_blur = std::strtof(argv[++i], nullptr);
      f.glow = std::strtof(argv[++i], nullptr);
      f.glow_color = hex(argv[++i]);
      f.zoom = std::strtof(argv[++i], nullptr);
      f.offset_x = std::strtof(argv[++i], nullptr);
      f.offset_y = std::strtof(argv[++i], nullptr);
      f.decoration = std::atoi(argv[++i]);
    } else if (a == "--stamp" && i + 4 < argc) {
      stampFile = argv[++i];
      sx = std::atoi(argv[++i]);
      sy = std::atoi(argv[++i]);
      sop = std::strtof(argv[++i], nullptr);
    } else {
      std::fprintf(stderr, "unknown or incomplete argument: %s\n", a.c_str());
      return 2;
    }
  }
  if (!ops.empty()) {
    pp_image* out = nullptr;
    if (pp_process(img, ops.data(), ops.size(), nullptr, &out) != PP_OK) {
      std::fprintf(stderr, "ops failed: %s\n", pp_last_error_message());
      return 1;
    }
    pp_image_destroy(img);
    img = out;
  }
  for (auto& pr : props) {
    pp_image* prop = readPam(pr.file);
    int64_t c = 0;
    if (!prop || pp_place_prop(img, prop, &pr.p, nullptr, &c) != PP_OK) {
      std::fprintf(stderr, "prop failed: %s\n", pp_last_error_message());
      return 1;
    }
    covered += c;
    pp_image_destroy(prop);
  }
  if (hasFrame) {
    pp_image* canvas = nullptr;
    pp_image_create(canvasW, canvasH, PP_FORMAT_RGBA8, &canvas);
    if (pp_frame(img, canvas, &f, nullptr) != PP_OK) {
      std::fprintf(stderr, "frame failed: %s\n", pp_last_error_message());
      return 1;
    }
    pp_image_destroy(img);
    img = canvas;
  }
  if (!stampFile.empty()) {
    pp_image* stamp = readPam(stampFile);
    int64_t v = 0;
    if (!stamp || pp_overlay(img, stamp, sx, sy, sop, &v) != PP_OK) return 1;
    visible = v;
    pp_image_destroy(stamp);
  }
  writePpm(argv[2], img);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  std::printf("{\"width\":%d,\"height\":%d,\"propPixels\":%lld,\"stampVisible\":%lld}\n", info.width, info.height, covered, visible);
  pp_image_destroy(img);
  return 0;
}
