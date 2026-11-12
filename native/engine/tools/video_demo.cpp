// video_demo — the Glide pipeline on a folder of frames (developer tool, not
// shipped; the app runs the same steps natively, frame by frame):
//
//   video_demo IN_DIR OUT_DIR COUNT [options]
//     IN_DIR/frame_%04d.ppm (0-based); optional per-frame masks in IN_DIR:
//     subject_%04d.pgm, faces_%04d.pgm, protect_%04d.pgm, landmarks_%04d.txt
//   --strength v      stabilisation 0..1 (default 1)       --max-zoom v  (default 1.12)
//   --window n        frames used per output frame (1, or 3 for temporal denoise; default 3)
//   --no-stabilize    --no-enhance
//   --retouch v --brighten v --blur v --pop v --bg-color v --edge v   (Spotlight / Glow)
//   --eyes v --under-eye v --shine v --face-light v --teeth v --lips v (face touches)
//   --backdrop n --backdrop-strength v                                  (Backdrops)
//   --skin-tone r g b glow --skin-strength v                            (Skin Tone, sRGB 0..1)
//
// Writes OUT_DIR/frame_%04d.ppm and prints a JSON report.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
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

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: video_demo IN_DIR OUT_DIR COUNT [options]\n");
    return 2;
  }
  const std::string in = argv[1], out = argv[2];
  const int n = std::atoi(argv[3]);
  float strength = 1.0f, maxZoom = 1.12f;
  int window = 3;
  bool stabilize = true, enhance = true;
  pp_portrait_params pp{};
  pp_portrait_default_params(&pp);
  pp.blur = pp.retouch = pp.brighten = pp.pop = 0.0f;
  pp_face_params fp{};
  pp_face_default_params(&fp);
  int backdrop = -1;  // pp_backdrop_design, applied after stabilising so painted backgrounds stay still
  float backdropStrength = 1.0f;
  bool skinTone = false;
  pp_skin_tone_params sp{};
  sp.strength = 1.0f;
  for (int i = 4; i < argc; ++i) {
    const std::string k = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::strtof(argv[++i], nullptr) : 0.0f; };
    if (k == "--strength") strength = next();
    else if (k == "--max-zoom") maxZoom = next();
    else if (k == "--window") window = static_cast<int>(next());
    else if (k == "--no-stabilize") stabilize = false;
    else if (k == "--no-enhance") enhance = false;
    else if (k == "--retouch") pp.retouch = next();
    else if (k == "--brighten") pp.brighten = next();
    else if (k == "--blur") pp.blur = next();
    else if (k == "--pop") pp.pop = next();
    else if (k == "--bg-color") pp.bg_color = next();
    else if (k == "--edge") pp.edge = next();
    else if (k == "--eyes") fp.eyes = next();
    else if (k == "--under-eye") fp.under_eye = next();
    else if (k == "--shine") fp.shine = next();
    else if (k == "--face-light") fp.face_light = next();
    else if (k == "--teeth") fp.teeth = next();
    else if (k == "--lips") fp.lips = next();
    else if (k == "--backdrop") backdrop = static_cast<int>(next());
    else if (k == "--backdrop-strength") backdropStrength = next();
    else if (k == "--skin-tone") {
      skinTone = true;
      sp.shade_r = next(), sp.shade_g = next(), sp.shade_b = next(), sp.glow = next();
    } else if (k == "--skin-strength") sp.strength = next();
    else {
      std::fprintf(stderr, "unknown option %s\n", k.c_str());
      return 2;
    }
  }
  if (n < 2 || (window != 1 && window != 3)) {
    std::fprintf(stderr, "need at least 2 frames; window 1 or 3\n");
    return 2;
  }
  const auto t0 = std::chrono::steady_clock::now();
  auto load = [&](int k) { return readPpm(name(in, "frame", k, "ppm")); };

  // ---- Pass 1: camera motion ----
  std::vector<pp_video_motion> motions(static_cast<size_t>(n));
  motions[0] = {1, 0, 0, 0, 1, {0, 0, 0}};
  pp_image* prev = load(0);
  if (prev == nullptr) {
    std::fprintf(stderr, "cannot read frame 0\n");
    return 1;
  }
  pp_image_info info{};
  pp_image_get_info(prev, &info);
  for (int k = 1; k < n && stabilize; ++k) {
    pp_image* cur = load(k);
    if (cur == nullptr || pp_video_estimate_motion(prev, cur, &motions[static_cast<size_t>(k)]) != PP_OK) {
      std::fprintf(stderr, "motion failed at %d: %s\n", k, pp_last_error_message());
      return 1;
    }
    pp_image_destroy(prev);
    prev = cur;
  }
  pp_image_destroy(prev);
  const auto t1 = std::chrono::steady_clock::now();

  // ---- Clip-wide measurements: noise, blur, tone (from the middle frame) ----
  pp_enhance_params ep{};
  pp_enhance_default_params(&ep);
  ep.fixed_reference = 1;
  uint8_t curves[768];
  for (int c = 0; c < 3; ++c)
    for (int v = 0; v < 256; ++v) curves[c * 256 + v] = static_cast<uint8_t>(v);
  if (enhance) {
    pp_image* mid = load(n / 2);
    pp_quality_report q{};
    pp_analyze_quality(mid, &q);
    ep.noise_sigma = q.noise_sigma;
    ep.measured_blur = q.blur_sigma > 0 ? q.blur_sigma : 0.6f;
    pp_enhance_params noTone = ep, withTone = ep;
    noTone.auto_tone = 0;
    withTone.auto_tone = 1;
    pp_image* a = nullptr;
    pp_image* b = nullptr;
    pp_enhance(&mid, 1, &noTone, nullptr, &a, nullptr);
    pp_enhance(&mid, 1, &withTone, nullptr, &b, nullptr);
    if (a != nullptr && b != nullptr) pp_video_fit_curves(a, b, curves);
    pp_image_destroy(a);
    pp_image_destroy(b);
    pp_image_destroy(mid);
  }
  pp_video_frame_params fparams{};
  fparams.noise_sigma = ep.noise_sigma;
  fparams.blur_sigma = ep.measured_blur;
  fparams.denoise = 1.0f;
  fparams.sharpen = 1.0f;

  // ---- Plan ----
  std::vector<float> warps(static_cast<size_t>(n) * 6, 0.0f);
  float report[3] = {1, 0, 0};
  if (stabilize) {
    if (pp_video_stabilize(motions.data(), static_cast<size_t>(n), info.width, info.height, strength, maxZoom, warps.data(),
                           report) != PP_OK) {
      std::fprintf(stderr, "stabilize failed: %s\n", pp_last_error_message());
      return 1;
    }
  }

  // ---- Pass 2 ----
  const bool portrait = pp.blur > 0 || pp.retouch > 0 || pp.brighten > 0 || pp.pop > 0 || pp.bg_color != 0;
  const bool faces = fp.eyes > 0 || fp.under_eye > 0 || fp.shine > 0 || fp.face_light > 0 || fp.teeth > 0 || fp.lips > 0;
  std::deque<std::pair<int, pp_image*>> cache;  // a few decoded frames around k
  auto frame = [&](int k) -> pp_image* {
    for (auto& [i, img] : cache)
      if (i == k) return img;
    pp_image* img = load(k);
    cache.emplace_back(k, img);
    while (cache.size() > 4) {
      pp_image_destroy(cache.front().second);
      cache.pop_front();
    }
    return img;
  };
  pp_image* steady = nullptr;  // the steadied subject mask, carried frame to frame
  double enhanceMs = 0, portraitMs = 0;
  for (int k = 0; k < n; ++k) {
    pp_image* work = nullptr;
    auto e0 = std::chrono::steady_clock::now();
    pp_image_create(info.width, info.height, PP_FORMAT_RGBA8, &work);
    if (enhance) {
      pp_image* p = window == 3 && k > 0 ? frame(k - 1) : nullptr;
      pp_image* nx = window == 3 && k + 1 < n ? frame(k + 1) : nullptr;
      pp_image* c = frame(k);
      const pp_video_motion* to = k + 1 < n ? &motions[static_cast<size_t>(k + 1)] : nullptr;
      if (pp_video_enhance_frame(c, p, nx, &motions[static_cast<size_t>(k)], to, &fparams, curves, work, nullptr) != PP_OK) {
        std::fprintf(stderr, "enhance failed at %d: %s\n", k, pp_last_error_message());
        return 1;
      }
    } else {
      pp_image_copy_pixels(frame(k), work);
    }
    enhanceMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - e0).count();
    auto p0 = std::chrono::steady_clock::now();
    if (portrait || backdrop >= 0 || skinTone) {
      pp_image* subj = readPgm(name(in, "subject", k, "pgm"));
      if (subj != nullptr) {
        if (steady == nullptr) {
          steady = subj;
          subj = nullptr;
        } else {
          pp_video_steady_mask(steady, subj, 0.6f);
          pp_image_destroy(subj);
        }
        pp_image* fm = readPgm(name(in, "faces", k, "pgm"));
        pp_image* pm = readPgm(name(in, "protect", k, "pgm"));
        if (portrait && pp_portrait(work, steady, fm, pm, nullptr, &pp, nullptr, nullptr) != PP_OK) {
          std::fprintf(stderr, "portrait failed at %d: %s\n", k, pp_last_error_message());
        }
        if (skinTone && fm != nullptr && pp_skin_tone(work, steady, fm, pm, &sp, nullptr, nullptr, nullptr) != PP_OK) {
          std::fprintf(stderr, "skin tone failed at %d: %s\n", k, pp_last_error_message());
        }
        pp_image_destroy(fm);
        pp_image_destroy(pm);
      }
    }
    if (faces) {
      const auto list = readFaces(name(in, "landmarks", k, "txt"));
      if (!list.empty()) pp_face_adjust(work, list.data(), list.size(), &fp, nullptr, nullptr);
    }
    portraitMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p0).count();
    if (stabilize) {
      pp_image* outImg = nullptr;
      pp_image_create(info.width, info.height, PP_FORMAT_RGBA8, &outImg);
      pp_video_warp(work, &warps[static_cast<size_t>(k) * 6], outImg, nullptr);
      pp_image_destroy(work);
      work = outImg;
    }
    if (backdrop >= 0 && steady != nullptr) {
      // The mask follows the stabilised frame (same warp, at the mask's scale).
      pp_image_info mi{};
      pp_image_get_info(steady, &mi);
      pp_image* mask = steady;
      pp_image* warped = nullptr;
      if (stabilize) {
        const float sx = static_cast<float>(mi.width) / static_cast<float>(info.width);
        const float sy = static_cast<float>(mi.height) / static_cast<float>(info.height);
        const float* w = &warps[static_cast<size_t>(k) * 6];
        const float mw[6] = {w[0], w[1] * sx / sy, w[2] * sx, w[3] * sy / sx, w[4], w[5] * sy};
        pp_image_create(mi.width, mi.height, PP_FORMAT_GRAY8, &warped);
        pp_video_warp(steady, mw, warped, nullptr);
        mask = warped;
      }
      pp_backdrop_params bp{};
      bp.design = backdrop;
      bp.strength = backdropStrength;
      bp.seed = 7;
      if (pp_backdrop(work, mask, nullptr, &bp, nullptr, nullptr) != PP_OK) {
        std::fprintf(stderr, "backdrop failed at %d: %s\n", k, pp_last_error_message());
      }
      if (warped != nullptr) pp_image_destroy(warped);
    }
    writePpm(name(out, "frame", k, "ppm"), work);
    pp_image_destroy(work);
  }
  for (auto& [i, img] : cache) pp_image_destroy(img);
  pp_image_destroy(steady);
  const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf(
      "{\"frames\":%d,\"zoom\":%.3f,\"shakeBefore\":%.2f,\"shakeAfter\":%.2f,\"motionMs\":%.0f,\"enhanceMsPerFrame\":%.0f,"
      "\"portraitMsPerFrame\":%.0f,\"noise\":%.4f,\"blur\":%.2f,\"totalMs\":%.0f}\n",
      n, static_cast<double>(report[0]), static_cast<double>(report[1]), static_cast<double>(report[2]),
      std::chrono::duration<double, std::milli>(t1 - t0).count(), enhanceMs / n, portraitMs / n,
      static_cast<double>(ep.noise_sigma), static_cast<double>(ep.measured_blur), total);
  return 0;
}
