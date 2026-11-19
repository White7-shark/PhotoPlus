// Glide in a new scene, frame by frame, exactly as the phone runs it
// (GlideProcessor + SceneComposer): camera motion, three frames' consensus,
// the room memory, the steadied mask, the studio cut-out and the compose at
// a fixed box with the colour match kept from the first frame. Writes the
// steadied masks and the composed frames.
//
//   scene_video_demo IN_DIR OUT_DIR COUNT BG.ppm x y height boxL boxT boxR boxB [options]
//   IN_DIR: frame_%04d.ppm (the frames the cut-out sees) and subject_%04d.pgm
//   options: --no-room  --no-vote  --shape  --steady S  --look colorMatch wrap shadow cast lightX horizon blur grain rim spotlight
#include <algorithm>
#include <chrono>
#include <cmath>
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
  if (!in) return nullptr;
  pp_image* img = nullptr;
  if (pp_image_create(w, h, PP_FORMAT_RGBA8, &img) != PP_OK) return nullptr;
  auto* px = static_cast<unsigned char*>(pp_image_pixels(img));
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    px[i * 4] = rgb[i * 3], px[i * 4 + 1] = rgb[i * 3 + 1], px[i * 4 + 2] = rgb[i * 3 + 2], px[i * 4 + 3] = 255;
  }
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

pp_image* readPgm(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return nullptr;
  std::string magic;
  int w = 0, h = 0, maxv = 0;
  in >> magic >> w >> h >> maxv;
  in.get();
  if (!in || magic != "P5" || maxv != 255) return nullptr;
  pp_image* img = nullptr;
  pp_image_create(w, h, PP_FORMAT_GRAY8, &img);
  pp_image_info info{};
  pp_image_get_info(img, &info);
  auto* px = static_cast<char*>(pp_image_pixels(img));
  for (int y = 0; y < h; ++y) in.read(px + static_cast<size_t>(y) * info.stride, w);
  return img;
}

std::vector<pp_face> readFaces(const std::string& path) {
  std::vector<pp_face> out;
  std::ifstream in(path);
  for (std::string line; std::getline(in, line);) {
    std::stringstream ss(line);
    pp_face f{};
    int i = 0;
    for (std::string tok; std::getline(ss, tok, ',') && i < PP_FACE_POINTS * 2;) f.points[i++] = std::strtof(tok.c_str(), nullptr);
    if (i == PP_FACE_POINTS * 2) {
      f.detailed = 1;
      out.push_back(f);
    }
  }
  return out;
}

std::string name(const std::string& dir, const char* stem, int k, const char* ext) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "/%s_%04d.%s", stem, k, ext);
  return dir + buf;
}

}  // namespace

void writePgm(const std::string& path, pp_image* img) {
  pp_image_info info{};
  pp_image_get_info(img, &info);
  std::ofstream out(path, std::ios::binary);
  out << "P5\n" << info.width << " " << info.height << "\n255\n";
  const auto* px = static_cast<const char*>(pp_image_pixels(img));
  for (int y = 0; y < info.height; ++y) out.write(px + static_cast<size_t>(y) * info.stride, info.width);
}

pp_image* copyOf(pp_image* src) {
  pp_image_info info{};
  pp_image_get_info(src, &info);
  pp_image* out = nullptr;
  pp_image_create(info.width, info.height, info.format, &out);
  pp_image_info o{};
  pp_image_get_info(out, &o);
  const int bpp = info.format == PP_FORMAT_GRAY8 ? 1 : 4;
  for (int y = 0; y < info.height; ++y)
    std::memcpy(static_cast<char*>(pp_image_pixels(out)) + static_cast<size_t>(y) * o.stride,
                static_cast<const char*>(pp_image_pixels(src)) + static_cast<size_t>(y) * info.stride, static_cast<size_t>(info.width) * bpp);
  return out;
}

int main(int argc, char** argv) {
  if (argc < 12) {
    std::fprintf(stderr, "usage: scene_video_demo IN_DIR OUT_DIR COUNT BG.ppm x y height boxL boxT boxR boxB [options]\n");
    return 2;
  }
  const std::string in = argv[1], out = argv[2];
  const int n = std::atoi(argv[3]);
  pp_image* bg = readPpm(argv[4]);
  if (bg == nullptr) return 1;
  pp_studio_placement pl{std::strtof(argv[5], nullptr), std::strtof(argv[6], nullptr), std::strtof(argv[7], nullptr), 0, 0, 0, 1,
                         std::strtof(argv[8], nullptr), std::strtof(argv[9], nullptr), std::strtof(argv[10], nullptr), std::strtof(argv[11], nullptr)};
  pp_studio_look lk{0.7f, 0.35f, 0.6f, 0, 0, 0.45f, 0.3f, 1, 11u, 0, 0, 0, 1, 1, {1, 1, 1}};
  bool room = true, vote = true, compose = true, shapeMem = false;
  float steadyK = 0.6f;
  for (int i = 12; i < argc; ++i) {
    const std::string k = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::strtof(argv[++i], nullptr) : 0.0f; };
    if (k == "--no-room") room = false;
    else if (k == "--no-vote") vote = false;
    else if (k == "--shape") shapeMem = true;
    else if (k == "--masks-only") compose = false;
    else if (k == "--steady") steadyK = next();
    else if (k == "--look") {
      lk.color_match = next(), lk.light_wrap = next(), lk.shadow = next(), lk.cast_shadow = next(), lk.light_x = next();
      lk.horizon = next(), lk.bg_blur = next(), lk.grain = next(), lk.rim = next(), lk.spotlight = next();
    } else {
      std::fprintf(stderr, "unknown option %s\n", k.c_str());
      return 2;
    }
  }
  pp_video_room* memory = nullptr;
  if (room) pp_video_room_create(&memory);
  pp_video_shape* shape = nullptr;
  if (shapeMem) pp_video_shape_create(&shape);
  pp_image* steady = nullptr;
  pp_image* prevFrame = nullptr;
  std::vector<pp_image*> masks(static_cast<size_t>(n), nullptr);
  auto maskOf = [&](int k) {
    if (k < 0 || k >= n) return static_cast<pp_image*>(nullptr);
    if (masks[static_cast<size_t>(k)] == nullptr) masks[static_cast<size_t>(k)] = readPgm(name(in, "subject", k, "pgm"));
    return masks[static_cast<size_t>(k)];
  };
  bool harmony = false;
  double ms = 0;
  for (int k = 0; k < n; ++k) {
    pp_image* frame = readPpm(name(in, "frame", k, "ppm"));
    if (frame == nullptr) return 1;
    const auto t0 = std::chrono::steady_clock::now();
    pp_video_motion motion{1, 0, 0, 0, 1, {0, 0, 0}};
    if (prevFrame != nullptr) pp_video_estimate_motion(prevFrame, frame, &motion);
    pp_image* cur = maskOf(k);
    pp_image* subj = copyOf(cur);
    if (vote) pp_video_consensus_mask(maskOf(k - 1), cur, maskOf(k + 1), subj);
    if (memory != nullptr) pp_video_room_check(memory, frame, subj, prevFrame != nullptr ? &motion : nullptr, nullptr, nullptr);
    if (shape != nullptr) pp_video_shape_check(shape, frame, subj, nullptr, nullptr);
    if (steady == nullptr) {
      steady = subj;
    } else {
      pp_video_steady_mask(steady, subj, steadyK);
      pp_image_destroy(subj);
    }
    if (k >= 2 && masks[static_cast<size_t>(k - 2)] != nullptr) {
      pp_image_destroy(masks[static_cast<size_t>(k - 2)]);
      masks[static_cast<size_t>(k - 2)] = nullptr;
    }
    writePgm(name(out, "steady", k, "pgm"), steady);
    if (compose) {
      pp_image_info fi{};
      pp_image_get_info(frame, &fi);
      pp_image* cut = nullptr;
      pp_image_create(fi.width, fi.height, PP_FORMAT_RGBA8, &cut);
      const pp_studio_cutout_params cp{0, 0, 0.5f, 1};
      if (pp_studio_cutout(frame, steady, nullptr, nullptr, 0, &cp, cut, nullptr, nullptr) != PP_OK) {
        std::fprintf(stderr, "cutout %d: %s\n", k, pp_last_error_message());
        return 1;
      }
      pp_image* o = copyOf(bg);
      lk.seed = static_cast<uint32_t>(11 + k);
      pp_studio_compose_report rep{};
      if (pp_studio_compose(o, cut, &pl, &lk, nullptr, &rep) != PP_OK) {
        std::fprintf(stderr, "compose %d: %s\n", k, pp_last_error_message());
        return 1;
      }
      if (!harmony && lk.color_match > 0) {
        lk.fixed_harmony = 1, lk.gain = rep.exposure, lk.contrast = rep.contrast;
        lk.balance[0] = rep.balance[0], lk.balance[1] = rep.balance[1], lk.balance[2] = rep.balance[2];
        harmony = true;
      }
      writePpm(name(out, "out", k, "ppm"), o);
      writePpm(name(out, "cut", k, "ppm"), cut);
      pp_image_destroy(o);
      pp_image_destroy(cut);
    }
    ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (prevFrame != nullptr) pp_image_destroy(prevFrame);
    prevFrame = frame;
  }
  std::printf("{\"frames\":%d,\"msPerFrame\":%.1f}\n", n, ms / n);
  for (pp_image* m : masks)
    if (m != nullptr) pp_image_destroy(m);
  if (steady != nullptr) pp_image_destroy(steady);
  if (prevFrame != nullptr) pp_image_destroy(prevFrame);
  if (memory != nullptr) pp_video_room_destroy(memory);
  if (shape != nullptr) pp_video_shape_destroy(shape);
  pp_image_destroy(bg);
  return 0;
}
