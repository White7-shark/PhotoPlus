// light_demo — the lighting tools on PPM photos (developer tool).
//
//   light_demo meter IN.ppm ISO EXPOSURE_S F_NUMBER FRONT HAS_FLASH [FACE_LUMA] [FOCUS_DIOPTERS] → JSON
//   light_demo tone IN.ppm OUT.ppm [night] [shadows] [highlights] [exposure] [white_balance] [capture_bias]
//   light_demo burst DIR COUNT OUT_PREFIX [night] [capture_bias]
//        DIR/frame_%04d.ppm; writes OUT_PREFIXmerged.ppm (the burst merge alone,
//        as Moonlight did before) and OUT_PREFIXtoned.ppm (merge + tone)
//   light_demo fuse AMBIENT.ppm FLASH.ppm OUT.ppm [fill]
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

namespace {
double msSince(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
pp_image* blankLike(pp_image* src) {
  pp_image_info info{};
  pp_image_get_info(src, &info);
  pp_image* img = nullptr;
  pp_image_create(info.width, info.height, PP_FORMAT_RGBA8, &img);
  return img;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: see the header of tools/light_demo.cpp\n");
    return 2;
  }
  const std::string cmd = argv[1];
  auto arg = [&](int i, float d) { return argc > i ? std::strtof(argv[i], nullptr) : d; };
  if (cmd == "meter" && argc >= 8) {
    pp_image* img = readPpm(argv[2]);
    if (!img) return 1;
    pp_light_meter_input in{};
    in.iso = arg(3, 0), in.exposure_s = arg(4, 0), in.f_number = arg(5, 0);
    in.front = static_cast<int32_t>(arg(6, 0)), in.has_flash = static_cast<int32_t>(arg(7, 0));
    in.face_luma = arg(8, -1);
    in.focus_diopters = arg(9, -1);
    pp_light_meter_result m{};
    if (pp_light_meter(img, &in, &m) != PP_OK) return 1;
    std::printf("{\"ev100\":%.2f,\"lux\":%.2f,\"key\":%.4f,\"highlights\":%.4f,\"shadows\":%.4f,\"range\":%.2f,\"backlit\":%d,"
                "\"plan\":%d,\"frames\":%d,\"bias\":%.2f,\"night\":%.2f,\"screen\":[%d,%d,%d]}\n",
                m.ev100, m.lux, m.key, m.highlights, m.shadows, m.range_stops, m.backlit, m.plan, m.frames, m.exposure_bias,
                m.night, m.screen_flash[0], m.screen_flash[1], m.screen_flash[2]);
    pp_image_destroy(img);
    return 0;
  }
  if (cmd == "tone" && argc >= 4) {
    pp_image* img = readPpm(argv[2]);
    if (!img) return 1;
    pp_light_tone_params p;
    pp_light_tone_default_params(&p);
    p.night = arg(4, 0), p.shadows = arg(5, -1), p.highlights = arg(6, -1), p.exposure = arg(7, 0);
    p.white_balance = arg(8, 0.6f * p.night);
    p.capture_bias = arg(9, 0);
    float rep[6] = {};
    const auto t0 = std::chrono::steady_clock::now();
    if (pp_light_tone(img, &p, nullptr, rep) != PP_OK) return 1;
    std::printf("{\"ms\":%.0f,\"key_before\":%.4f,\"key_after\":%.4f,\"range\":%.2f,\"shadows\":%.2f,\"highlights\":%.2f,\"clipped\":%.4f}\n",
                msSince(t0), rep[0], rep[1], rep[2], rep[3], rep[4], rep[5]);
    writePpm(argv[3], img);
    pp_image_destroy(img);
    return 0;
  }
  if (cmd == "burst" && argc >= 5) {
    const std::string dir = argv[2], prefix = argv[4];
    const int count = std::atoi(argv[3]);
    std::vector<pp_image*> frames;
    for (int k = 0; k < count; ++k) {
      char name[64];
      std::snprintf(name, sizeof name, "/frame_%04d.ppm", k);
      if (pp_image* f = readPpm(dir + name)) frames.push_back(f);
    }
    if (frames.empty()) return 1;
    pp_enhance_params ep;
    pp_enhance_default_params(&ep);
    pp_image* merged = nullptr;
    pp_enhance_report er{};
    const auto t0 = std::chrono::steady_clock::now();
    if (pp_enhance(frames.data(), frames.size(), &ep, nullptr, &merged, &er) != PP_OK) return 1;
    const double mergeMs = msSince(t0);
    writePpm(prefix + "merged.ppm", merged);
    pp_light_tone_params p;
    pp_light_tone_default_params(&p);
    p.night = arg(5, 0);
    p.white_balance = 0.6f * p.night;
    p.capture_bias = arg(6, 0);
    float rep[6] = {};
    const auto t1 = std::chrono::steady_clock::now();
    if (pp_light_tone(merged, &p, nullptr, rep) != PP_OK) return 1;
    std::printf("{\"frames\":%d,\"used\":%d,\"noise_before\":%.4f,\"noise_after\":%.4f,\"merge_ms\":%.0f,\"tone_ms\":%.0f,"
                "\"key_before\":%.4f,\"key_after\":%.4f,\"range\":%.2f,\"shadows\":%.2f,\"highlights\":%.2f,\"clipped\":%.4f}\n",
                er.frames_input, er.frames_used, er.noise_before, er.noise_after, mergeMs, msSince(t1), rep[0], rep[1], rep[2], rep[3],
                rep[4], rep[5]);
    writePpm(prefix + "toned.ppm", merged);
    pp_image_destroy(merged);
    for (pp_image* f : frames) pp_image_destroy(f);
    return 0;
  }
  if (cmd == "fuse" && argc >= 5) {
    pp_image* a = readPpm(argv[2]);
    pp_image* f = readPpm(argv[3]);
    if (!a || !f) return 1;
    pp_image* out = blankLike(a);
    pp_flash_fuse_params p{};
    p.fill = arg(5, 0.3f), p.strength = 1.0f;
    float rep[4] = {};
    const auto t0 = std::chrono::steady_clock::now();
    const pp_status st = pp_flash_fuse(a, f, out, &p, nullptr, rep);
    if (st != PP_OK) {
      std::fprintf(stderr, "fuse failed: %s\n", pp_last_error_message());
      return 1;
    }
    std::printf("{\"ms\":%.0f,\"aligned\":%d,\"flash_gain\":%.2f,\"shadow_share\":%.3f,\"hot_share\":%.3f}\n", msSince(t0),
                static_cast<int>(rep[0]), rep[1], rep[2], rep[3]);
    writePpm(argv[4], out);
    for (pp_image* i : {a, f, out}) pp_image_destroy(i);
    return 0;
  }
  std::fprintf(stderr, "unknown command or missing arguments\n");
  return 2;
}
