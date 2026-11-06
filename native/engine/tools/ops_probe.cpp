// Runs one engine operation on an image file and writes the result as PNG,
// through the public C API only. Used by scripts/verify-engine-against-opencv.py
// to compare the engine with OpenCV's reference implementations.
//
//   ops_probe decode  in out.png
//   ops_probe resize  in out.png width height filter      (0 auto, 1 bilinear, 2 area, 3 Lanczos)
//   ops_probe blur    in out.png sigma
//   ops_probe warp    in out.png width height x0 y0 x1 y1 x2 y2 x3 y3
//   ops_probe blend   base layer out.png mode opacity     (pp_blend_mode)
//   ops_probe keystone in                                  (prints the suggested vertical amount)
//   ops_probe perspective in out.png vertical horizontal  (PP_OP_PERSPECTIVE, -1..1)
//   ops_probe enhance in out.png                           (Clarity: single-frame pp_enhance, defaults)
//
// With PP_PROBE_PROFILE=1 in the environment, the engine's own timings and
// peak memory are printed to stderr (decoding and PNG writing excluded).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "photoplus/pp_engine.h"

namespace {

int fail(const char* what, pp_status s) {
  std::fprintf(stderr, "%s failed: %s (%s)\n", what, pp_status_name(s), pp_last_error_message());
  return 1;
}

pp_image* load(const char* path) {
  pp_image* img = nullptr;
  const pp_status s = pp_decode_file(path, nullptr, nullptr, &img, nullptr);
  if (s != PP_OK) {
    fail("decode", s);
    return nullptr;
  }
  return img;
}

void report() {
  if (std::getenv("PP_PROBE_PROFILE") == nullptr) return;
  char json[8192];
  const size_t n = pp_profiler_report_json(json, sizeof json);
  pp_memory_stats m{};
  pp_memory_get_stats(&m);
  std::fprintf(stderr, "profile %.*s\npeak_bytes %llu\n", static_cast<int>(n < sizeof json ? n : sizeof json - 1), json,
               static_cast<unsigned long long>(m.peak_bytes));
}

int save(pp_image* img, const char* path) {
  report();
  const pp_status s = pp_encode_file(img, path, PP_CODEC_PNG, 100);
  pp_image_destroy(img);
  return s == PP_OK ? 0 : fail("encode", s);
}

int process(pp_image* in, const char* out, const pp_op& op) {
  pp_image* result = nullptr;
  pp_render_options options{};
  options.backend = PP_BACKEND_CPU;
  const pp_status s = pp_process_ex(in, &op, 1, &options, nullptr, &result);
  pp_image_destroy(in);
  return s == PP_OK ? save(result, out) : fail("process", s);
}

float num(const char* s) { return static_cast<float>(std::strtod(s, nullptr)); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: ops_probe decode|resize|blur|warp|blend|keystone|perspective|enhance ...\n");
    return 2;
  }
  const std::string cmd = argv[1];
  if (cmd == "decode" && argc == 4) {
    pp_image* in = load(argv[2]);
    return in != nullptr ? save(in, argv[3]) : 1;
  }
  if (cmd == "resize" && argc == 7) {
    pp_image* in = load(argv[2]);
    if (in == nullptr) return 1;
    pp_op op{};
    op.type = PP_OP_RESIZE;
    op.p[0] = num(argv[4]);
    op.p[1] = num(argv[5]);
    op.p[2] = num(argv[6]);
    return process(in, argv[3], op);
  }
  if (cmd == "blur" && argc == 5) {
    pp_image* in = load(argv[2]);
    if (in == nullptr) return 1;
    pp_op op{};
    op.type = PP_OP_BLUR;
    op.p[0] = num(argv[4]);
    return process(in, argv[3], op);
  }
  if (cmd == "warp" && argc == 14) {
    pp_image* in = load(argv[2]);
    if (in == nullptr) return 1;
    float quad[8];
    for (int i = 0; i < 8; ++i) quad[i] = num(argv[6 + i]);
    pp_image* out = nullptr;
    const pp_status s = pp_warp_perspective(in, quad, std::atoi(argv[4]), std::atoi(argv[5]), nullptr, &out);
    pp_image_destroy(in);
    return s == PP_OK ? save(out, argv[3]) : fail("warp", s);
  }
  if (cmd == "blend" && argc == 7) {
    pp_image* base = load(argv[2]);
    pp_image* layer = load(argv[3]);
    if (base == nullptr || layer == nullptr) return 1;
    const pp_status s = pp_blend(base, layer, nullptr, 0, 0, std::atoi(argv[5]), num(argv[6]), nullptr);
    pp_image_destroy(layer);
    if (s != PP_OK) {
      pp_image_destroy(base);
      return fail("blend", s);
    }
    return save(base, argv[4]);
  }
  if (cmd == "perspective" && argc == 6) {
    pp_image* in = load(argv[2]);
    if (in == nullptr) return 1;
    pp_op op{};
    op.type = PP_OP_PERSPECTIVE;
    op.p[0] = num(argv[4]);
    op.p[1] = num(argv[5]);
    return process(in, argv[3], op);
  }
  if (cmd == "enhance" && argc == 4) {
    pp_image* in = load(argv[2]);
    if (in == nullptr) return 1;
    pp_enhance_params ep{};
    pp_enhance_default_params(&ep);
    pp_image* out = nullptr;
    pp_enhance_report rep{};
    const pp_status s = pp_enhance(&in, 1, &ep, nullptr, &out, &rep);
    pp_image_destroy(in);
    if (s != PP_OK) return fail("enhance", s);
    std::printf("{\"noiseBefore\":%.4f,\"noiseAfter\":%.4f,\"blurBefore\":%.2f,\"deblurSigma\":%.2f}\n",
                static_cast<double>(rep.noise_before), static_cast<double>(rep.noise_after),
                static_cast<double>(rep.blur_before), static_cast<double>(rep.deblur_sigma));
    return save(out, argv[3]);
  }
  if (cmd == "keystone" && argc == 3) {
    pp_image* in = load(argv[2]);
    if (in == nullptr) return 1;
    float v = 0.0f;
    const pp_status s = pp_detect_keystone(in, nullptr, &v);
    pp_image_destroy(in);
    if (s != PP_OK) return fail("keystone", s);
    std::printf("%.4f\n", static_cast<double>(v));
    return 0;
  }
  std::fprintf(stderr, "bad arguments for '%s'\n", cmd.c_str());
  return 2;
}
