// JNI bridge between com.photoplus.engine.NativeEngine (Kotlin) and the
// engine's C ABI. This file contains no image logic: it only marshals
// Bitmaps/arrays, converts errors to EngineException and guarantees that every
// native resource acquired here is released on every path.

#include <android/bitmap.h>
#include <android/log.h>
#include <jni.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "photoplus/pp_engine.h"

#define PP_LOG_TAG "PhotoPlusEngine"
#define PP_LOGW(...) __android_log_print(ANDROID_LOG_WARN, PP_LOG_TAG, __VA_ARGS__)

namespace {

constexpr const char* kExceptionClass = "com/photoplus/engine/EngineException";

void throwEngineException(JNIEnv* env, const char* code, const std::string& message) {
  if (env->ExceptionCheck()) return;  // never stack exceptions
  jclass cls = env->FindClass(kExceptionClass);
  if (cls == nullptr) return;  // FindClass already threw NoClassDefFoundError
  jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V");
  if (ctor == nullptr) return;
  jstring jcode = env->NewStringUTF(code);
  jstring jmsg = env->NewStringUTF(message.c_str());
  auto ex = static_cast<jthrowable>(env->NewObject(cls, ctor, jcode, jmsg));
  if (ex != nullptr) env->Throw(ex);
  env->DeleteLocalRef(jcode);
  env->DeleteLocalRef(jmsg);
  env->DeleteLocalRef(cls);
}

void throwStatus(JNIEnv* env, pp_status status) {
  throwEngineException(env, pp_status_name(status), pp_last_error_message());
}

// Locks a Bitmap's pixels for the lifetime of the object.
class BitmapLock {
 public:
  // Masks may be ALPHA_8 bitmaps (allowMask), wrapped as single-channel images.
  BitmapLock(JNIEnv* env, jobject bitmap, bool allowMask = false) : env_(env), bitmap_(bitmap) {
    if (AndroidBitmap_getInfo(env, bitmap, &info_) != ANDROID_BITMAP_RESULT_SUCCESS) {
      error_ = "Unable to read bitmap info";
      return;
    }
    const bool mask = allowMask && info_.format == ANDROID_BITMAP_FORMAT_A_8;
    if (info_.format != ANDROID_BITMAP_FORMAT_RGBA_8888 && !mask) {
      error_ = allowMask ? "Mask must be ALPHA_8 or ARGB_8888" : "Bitmap must be ARGB_8888";
      return;
    }
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels_) != ANDROID_BITMAP_RESULT_SUCCESS) {
      error_ = "Unable to lock bitmap pixels (hardware bitmaps are not supported)";
      pixels_ = nullptr;
    }
  }
  ~BitmapLock() {
    if (pixels_ != nullptr) AndroidBitmap_unlockPixels(env_, bitmap_);
  }
  BitmapLock(const BitmapLock&) = delete;
  BitmapLock& operator=(const BitmapLock&) = delete;

  bool ok() const { return pixels_ != nullptr; }
  const std::string& error() const { return error_; }
  void* pixels() const { return pixels_; }
  const AndroidBitmapInfo& info() const { return info_; }
  int32_t ppFormat() const { return info_.format == ANDROID_BITMAP_FORMAT_A_8 ? PP_FORMAT_GRAY8 : PP_FORMAT_RGBA8; }

  // Wraps the locked pixels (no copy). Caller destroys the wrapper.
  pp_status wrap(pp_image** out) const {
    return pp_image_wrap(pixels_, static_cast<int32_t>(info_.width), static_cast<int32_t>(info_.height),
                         static_cast<int32_t>(info_.stride), ppFormat(), out);
  }

 private:
  JNIEnv* env_;
  jobject bitmap_;
  AndroidBitmapInfo info_{};
  void* pixels_ = nullptr;
  std::string error_;
};

pp_image* fromHandle(jlong handle) { return reinterpret_cast<pp_image*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL Java_com_photoplus_engine_NativeEngine_nativeVersion(JNIEnv* env,
                                                                                  jobject) {
  return env->NewStringUTF(pp_version());
}

JNIEXPORT jlong JNICALL Java_com_photoplus_engine_NativeEngine_nativeCreateToken(JNIEnv* env,
                                                                                   jobject) {
  pp_cancel_token* token = pp_cancel_token_create();
  if (token == nullptr) throwEngineException(env, "OUT_OF_MEMORY", "Unable to allocate token");
  return reinterpret_cast<jlong>(token);
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeCancelToken(JNIEnv*, jobject,
                                                                                  jlong handle) {
  pp_cancel_token_cancel(reinterpret_cast<pp_cancel_token*>(handle));
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeDestroyToken(JNIEnv*, jobject,
                                                                                   jlong handle) {
  pp_cancel_token_destroy(reinterpret_cast<pp_cancel_token*>(handle));
}

JNIEXPORT jlong JNICALL Java_com_photoplus_engine_NativeEngine_nativeProcess(
    JNIEnv* env, jobject, jobject bitmap, jintArray types, jfloatArray params, jlong token) {
  const jsize count = env->GetArrayLength(types);
  constexpr jsize kParams = PP_OP_PARAMS;
  if (env->GetArrayLength(params) != count * kParams) {
    throwEngineException(env, "INVALID_ARGUMENT", "params must contain 8 floats per operation");
    return 0;
  }

  std::vector<pp_op> ops(static_cast<size_t>(count));
  if (count > 0) {
    std::vector<jint> t(static_cast<size_t>(count));
    std::vector<jfloat> p(static_cast<size_t>(count) * kParams);
    env->GetIntArrayRegion(types, 0, count, t.data());
    env->GetFloatArrayRegion(params, 0, count * kParams, p.data());
    if (env->ExceptionCheck()) return 0;
    for (jsize i = 0; i < count; ++i) {
      ops[i].type = t[i];
      for (int k = 0; k < kParams; ++k) ops[i].p[k] = p[i * kParams + k];
    }
  }

  BitmapLock lock(env, bitmap);
  if (!lock.ok()) {
    throwEngineException(env, "UNSUPPORTED_FORMAT", lock.error());
    return 0;
  }

  pp_image* input = nullptr;
  pp_status status = pp_image_wrap(lock.pixels(), static_cast<int32_t>(lock.info().width),
                                   static_cast<int32_t>(lock.info().height),
                                   static_cast<int32_t>(lock.info().stride), PP_FORMAT_RGBA8,
                                   &input);
  if (status != PP_OK) {
    throwStatus(env, status);
    return 0;
  }

  pp_image* output = nullptr;
  status = pp_process(input, ops.data(), ops.size(),
                      reinterpret_cast<const pp_cancel_token*>(token), &output);
  pp_image_destroy(input);  // wrapper only; Bitmap memory is untouched
  if (status != PP_OK) {
    throwStatus(env, status);
    return 0;
  }
  return reinterpret_cast<jlong>(output);
}

JNIEXPORT jint JNICALL Java_com_photoplus_engine_NativeEngine_nativeResultWidth(JNIEnv*, jobject,
                                                                                  jlong handle) {
  pp_image_info info{};
  return pp_image_get_info(fromHandle(handle), &info) == PP_OK ? info.width : 0;
}

JNIEXPORT jint JNICALL Java_com_photoplus_engine_NativeEngine_nativeResultHeight(JNIEnv*, jobject,
                                                                                   jlong handle) {
  pp_image_info info{};
  return pp_image_get_info(fromHandle(handle), &info) == PP_OK ? info.height : 0;
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeCopyResultToBitmap(
    JNIEnv* env, jobject, jlong handle, jobject bitmap) {
  BitmapLock lock(env, bitmap);
  if (!lock.ok()) {
    throwEngineException(env, "UNSUPPORTED_FORMAT", lock.error());
    return;
  }
  pp_image* target = nullptr;
  pp_status status = pp_image_wrap(lock.pixels(), static_cast<int32_t>(lock.info().width),
                                   static_cast<int32_t>(lock.info().height),
                                   static_cast<int32_t>(lock.info().stride), PP_FORMAT_RGBA8,
                                   &target);
  if (status == PP_OK) status = pp_image_copy_pixels(fromHandle(handle), target);
  pp_image_destroy(target);
  if (status != PP_OK) throwStatus(env, status);
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeReleaseResult(JNIEnv*, jobject,
                                                                                    jlong handle) {
  pp_image_destroy(fromHandle(handle));
}

JNIEXPORT jlongArray JNICALL Java_com_photoplus_engine_NativeEngine_nativeMemoryStats(JNIEnv* env,
                                                                                        jobject) {
  pp_memory_stats s{};
  pp_memory_get_stats(&s);
  const jlong values[8] = {static_cast<jlong>(s.current_bytes),     static_cast<jlong>(s.peak_bytes),
                           static_cast<jlong>(s.budget_bytes),      static_cast<jlong>(s.live_allocations),
                           static_cast<jlong>(s.total_allocations), static_cast<jlong>(s.failed_allocations),
                           static_cast<jlong>(s.cached_bytes),      static_cast<jlong>(s.reused_allocations)};
  jlongArray out = env->NewLongArray(8);
  if (out != nullptr) env->SetLongArrayRegion(out, 0, 8, values);
  return out;
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeSetMemoryBudget(JNIEnv*, jobject,
                                                                                      jlong bytes) {
  pp_memory_set_budget(bytes > 0 ? static_cast<uint64_t>(bytes) : 0);
}

JNIEXPORT jstring JNICALL Java_com_photoplus_engine_NativeEngine_nativeProfileReport(JNIEnv* env,
                                                                                       jobject) {
  // Concurrent jobs can grow the report between sizing and filling, so retry
  // with headroom until the whole report fits.
  std::string buffer(pp_profiler_report_json(nullptr, 0) + 512, '\0');
  for (int attempt = 0; attempt < 4; ++attempt) {
    const size_t needed = pp_profiler_report_json(buffer.data(), buffer.size());
    if (needed < buffer.size()) {
      buffer.resize(needed);
      return env->NewStringUTF(buffer.c_str());
    }
    buffer.assign(needed + 512, '\0');
  }
  return env->NewStringUTF("{}");
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeResetProfiler(JNIEnv*, jobject) {
  pp_profiler_reset();
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeResetMemoryPeak(JNIEnv*, jobject) {
  pp_memory_reset_peak();
}

// ---- Computational photography -------------------------------------------

namespace {
// Locks every frame of a burst and wraps it for the engine without copying.
struct LockedBurst {
  std::vector<std::unique_ptr<BitmapLock>> locks;
  std::vector<pp_image*> images;
  ~LockedBurst() {
    for (pp_image* i : images) pp_image_destroy(i);
  }
};
}  // namespace

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeEnhance(
    JNIEnv* env, jobject, jobjectArray frames, jint upscale, jfloatArray params, jboolean autoTone, jobject dst,
    jlong token, jfloatArray reportOut) {
  const jsize count = env->GetArrayLength(frames);
  if (count <= 0 || env->GetArrayLength(params) < 4 || env->GetArrayLength(reportOut) < 7) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad enhance arguments");
    return;
  }
  jfloat p[4];
  env->GetFloatArrayRegion(params, 0, 4, p);
  pp_enhance_params ep{};
  pp_enhance_default_params(&ep);
  ep.upscale = upscale;
  ep.denoise = p[0];
  ep.sharpen = p[1];
  ep.detail = p[2];
  ep.vibrance = p[3];
  ep.auto_tone = autoTone ? 1 : 0;

  LockedBurst burst;
  for (jsize i = 0; i < count; ++i) {
    jobject bmp = env->GetObjectArrayElement(frames, i);
    auto lock = std::make_unique<BitmapLock>(env, bmp);
    if (!lock->ok()) {
      throwEngineException(env, "UNSUPPORTED_FORMAT", lock->error());
      return;
    }
    pp_image* img = nullptr;
    const pp_status st = pp_image_wrap(lock->pixels(), static_cast<int32_t>(lock->info().width),
                                       static_cast<int32_t>(lock->info().height),
                                       static_cast<int32_t>(lock->info().stride), PP_FORMAT_RGBA8, &img);
    if (st != PP_OK) {
      throwStatus(env, st);
      return;
    }
    burst.locks.push_back(std::move(lock));
    burst.images.push_back(img);
  }
  BitmapLock out(env, dst);
  if (!out.ok()) {
    throwEngineException(env, "UNSUPPORTED_FORMAT", out.error());
    return;
  }
  pp_image* dstImage = nullptr;
  pp_status st = pp_image_wrap(out.pixels(), static_cast<int32_t>(out.info().width),
                               static_cast<int32_t>(out.info().height), static_cast<int32_t>(out.info().stride),
                               PP_FORMAT_RGBA8, &dstImage);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  pp_enhance_report report{};
  st = pp_enhance_into(burst.images.data(), burst.images.size(), &ep,
                       reinterpret_cast<const pp_cancel_token*>(token), dstImage, &report);
  pp_image_destroy(dstImage);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  const jfloat values[7] = {static_cast<jfloat>(report.frames_input), static_cast<jfloat>(report.frames_used),
                            static_cast<jfloat>(report.reference_index), report.noise_before,
                            report.noise_after, report.blur_before, report.deblur_sigma};
  env->SetFloatArrayRegion(reportOut, 0, 7, values);
}

JNIEXPORT jfloatArray JNICALL Java_com_photoplus_engine_NativeEngine_nativeAnalyzeQuality(JNIEnv* env, jobject,
                                                                                           jobject bitmap) {
  BitmapLock lock(env, bitmap);
  if (!lock.ok()) {
    throwEngineException(env, "UNSUPPORTED_FORMAT", lock.error());
    return nullptr;
  }
  pp_image* img = nullptr;
  pp_status st = pp_image_wrap(lock.pixels(), static_cast<int32_t>(lock.info().width),
                               static_cast<int32_t>(lock.info().height), static_cast<int32_t>(lock.info().stride),
                               PP_FORMAT_RGBA8, &img);
  pp_quality_report q{};
  if (st == PP_OK) st = pp_analyze_quality(img, &q);
  pp_image_destroy(img);
  if (st != PP_OK) {
    throwStatus(env, st);
    return nullptr;
  }
  const jfloat values[6] = {q.noise_sigma, q.blur_sigma, q.sharpness, q.mean_luma, q.highlights_clipped,
                            q.shadows_clipped};
  jfloatArray out = env->NewFloatArray(6);
  if (out != nullptr) env->SetFloatArrayRegion(out, 0, 6, values);
  return out;
}

// Reads the camera's Y plane in place (direct ByteBuffer) and writes the
// peaking mask straight into the locked overlay Bitmap: zero frame copies.
JNIEXPORT jfloat JNICALL Java_com_photoplus_engine_NativeEngine_nativeFocusPeaking(
    JNIEnv* env, jobject, jobject lumaBuffer, jint width, jint height, jint rowStride, jint pixelStride,
    jfloat threshold, jint rgbaColor, jobject overlay) {
  auto* luma = static_cast<const uint8_t*>(env->GetDirectBufferAddress(lumaBuffer));
  const jlong capacity = env->GetDirectBufferCapacity(lumaBuffer);
  if (luma == nullptr || capacity < static_cast<jlong>(rowStride) * (height - 1) + static_cast<jlong>(width) * pixelStride) {
    throwEngineException(env, "INVALID_ARGUMENT", "Luma buffer must be a direct buffer covering the frame");
    return 0.0f;
  }
  BitmapLock lock(env, overlay);
  if (!lock.ok()) {
    throwEngineException(env, "UNSUPPORTED_FORMAT", lock.error());
    return 0.0f;
  }
  if (lock.info().stride != lock.info().width * 4) {
    throwEngineException(env, "INVALID_ARGUMENT", "Overlay bitmap must be tightly packed");
    return 0.0f;
  }
  float sharpness = 0.0f;
  const pp_status st = pp_focus_peaking(luma, width, height, rowStride, pixelStride, threshold,
                                        static_cast<uint32_t>(rgbaColor), static_cast<uint32_t*>(lock.pixels()),
                                        static_cast<int32_t>(lock.info().width),
                                        static_cast<int32_t>(lock.info().height), &sharpness);
  if (st != PP_OK) throwStatus(env, st);
  return sharpness;
}

// ---- Portrait ("Spotlight") and look ("Vibe Match") -----------------------

namespace {
// Owns a pp_image wrapper of a locked bitmap (nullptr bitmap = no image).
struct WrappedBitmap {
  std::unique_ptr<BitmapLock> lock;
  pp_image* image = nullptr;
  ~WrappedBitmap() { pp_image_destroy(image); }
  // Returns false (with a Java exception pending) on failure.
  bool open(JNIEnv* env, jobject bitmap, bool mask) {
    if (bitmap == nullptr) return true;
    lock = std::make_unique<BitmapLock>(env, bitmap, mask);
    if (!lock->ok()) {
      throwEngineException(env, "UNSUPPORTED_FORMAT", lock->error());
      return false;
    }
    const pp_status st = lock->wrap(&image);
    if (st != PP_OK) {
      throwStatus(env, st);
      return false;
    }
    return true;
  }
};
}  // namespace

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativePortrait(
    JNIEnv* env, jobject, jobject bitmap, jobject subject, jobject faces, jobject protect, jobject depth,
    jfloatArray params, jlong token, jfloatArray reportOut) {
  constexpr jsize kReport = 6;
  if (env->GetArrayLength(params) < 4 || env->GetArrayLength(reportOut) < kReport || subject == nullptr) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad portrait arguments");
    return;
  }
  jfloat p[4];
  env->GetFloatArrayRegion(params, 0, 4, p);
  WrappedBitmap img, subj, fac, prot, dep;
  if (!img.open(env, bitmap, false) || !subj.open(env, subject, true) || !fac.open(env, faces, true) ||
      !prot.open(env, protect, true) || !dep.open(env, depth, true)) {
    return;
  }
  const pp_portrait_params pp{p[0], p[1], p[2], p[3]};
  pp_portrait_report report{};
  const pp_status st = pp_portrait(img.image, subj.image, fac.image, prot.image, dep.image, &pp,
                                   reinterpret_cast<const pp_cancel_token*>(token), &report);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  const jfloat values[kReport] = {report.subject_coverage,
                                  report.skin_coverage,
                                  static_cast<jfloat>(report.spots_removed),
                                  report.blur_radius,
                                  report.focus_depth,
                                  static_cast<jfloat>(report.depth_estimated)};
  env->SetFloatArrayRegion(reportOut, 0, kReport, values);
}

// Lens analysis ("Transform"): fills `out` (7) with detail blur, corner blur,
// fringe px, noise, grade, measured regions, reliable (0/1).
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeLensAnalyze(JNIEnv* env, jobject, jobject bitmap,
                                                                               jlong token, jfloatArray out) {
  constexpr jsize kOut = 7;
  if (env->GetArrayLength(out) < kOut) {
    throwEngineException(env, "INVALID_ARGUMENT", "Lens report needs 7 slots");
    return;
  }
  WrappedBitmap img;
  if (!img.open(env, bitmap, false)) return;
  pp_lens_report r{};
  const pp_status st = pp_lens_analyze(img.image, reinterpret_cast<const pp_cancel_token*>(token), &r);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  const jfloat values[kOut] = {r.detail_blur, r.corner_blur, r.fringe_px, r.noise, r.grade,
                               static_cast<jfloat>(r.measured_tiles), static_cast<jfloat>(r.reliable)};
  env->SetFloatArrayRegion(out, 0, kOut, values);
}

// Transform: `src` restored into `dst` (src size x upscale). params (4) =
// upscale, enrich (0/1), strength, detail. reportOut (7) = fringe fixed px,
// deblur centre, deblur corner, noise before, noise after, grade before,
// measured regions.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeTransform(JNIEnv* env, jobject, jobject src,
                                                                             jobject dst, jfloatArray params,
                                                                             jlong token, jfloatArray reportOut) {
  constexpr jsize kReport = 7;
  if (env->GetArrayLength(params) < 4 || env->GetArrayLength(reportOut) < kReport || src == nullptr || dst == nullptr) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad transform arguments");
    return;
  }
  jfloat p[4];
  env->GetFloatArrayRegion(params, 0, 4, p);
  WrappedBitmap in, out;
  if (!in.open(env, src, false) || !out.open(env, dst, false)) return;
  const pp_transform_params tp{static_cast<int32_t>(std::lround(p[0])), p[1] > 0.5f ? 1 : 0, p[2], p[3]};
  pp_transform_report r{};
  const pp_status st = pp_transform(in.image, out.image, &tp, reinterpret_cast<const pp_cancel_token*>(token), &r);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  const jfloat values[kReport] = {r.fringe_fixed_px, r.deblur_centre, r.deblur_corner, r.noise_before,
                                  r.noise_after,     r.before.grade,  static_cast<jfloat>(r.before.measured_tiles)};
  env->SetFloatArrayRegion(reportOut, 0, kReport, values);
}

// ---- Editor: props, frames, watermark ----------------------------------------

// Places a prop (straight-alpha ARGB_8888) on `image`. params (8) = cx, cy,
// width, rotation, flip, opacity, blend, shadow. Returns covered pixels.
JNIEXPORT jlong JNICALL Java_com_photoplus_engine_NativeEngine_nativePlaceProp(JNIEnv* env, jobject, jobject image,
                                                                              jobject prop, jfloatArray params,
                                                                              jlong token) {
  if (env->GetArrayLength(params) < 8) {
    throwEngineException(env, "INVALID_ARGUMENT", "Prop needs 8 parameters");
    return 0;
  }
  jfloat p[8];
  env->GetFloatArrayRegion(params, 0, 8, p);
  WrappedBitmap img, pr;
  if (!img.open(env, image, false) || !pr.open(env, prop, false)) return 0;
  const pp_prop_params pp{p[0], p[1], p[2], p[3], p[4] > 0.5f ? 1 : 0, p[5], p[6], p[7]};
  int64_t covered = 0;
  const pp_status st = pp_place_prop(img.image, pr.image, &pp, reinterpret_cast<const pp_cancel_token*>(token), &covered);
  if (st != PP_OK) {
    throwStatus(env, st);
    return 0;
  }
  return static_cast<jlong>(covered);
}

// Renders `photo` framed into `canvas`. f (19) = shape param, rotation,
// window x, y, w, h, border, outline gap, outline width, shadow, shadow
// offset, shadow blur, glow, zoom, offset x, offset y, and three spare.
// ints (9) = shape, backdrop, colour A, colour B, border colour, metallic,
// outline colour, glow colour, decoration.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeFrame(JNIEnv* env, jobject, jobject photo,
                                                                         jobject canvas, jfloatArray floats,
                                                                         jintArray ints, jlong token) {
  if (env->GetArrayLength(floats) < 16 || env->GetArrayLength(ints) < 9) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad frame arguments");
    return;
  }
  jfloat f[16];
  jint n[9];
  env->GetFloatArrayRegion(floats, 0, 16, f);
  env->GetIntArrayRegion(ints, 0, 9, n);
  WrappedBitmap ph, cv;
  if (!ph.open(env, photo, false) || !cv.open(env, canvas, false)) return;
  pp_frame_params fp{};
  fp.shape = n[0];
  fp.shape_param = f[0];
  fp.rotation_deg = f[1];
  for (int i = 0; i < 4; ++i) fp.window[i] = f[2 + i];
  fp.backdrop = n[1];
  fp.color_a = static_cast<uint32_t>(n[2]);
  fp.color_b = static_cast<uint32_t>(n[3]);
  fp.border = f[6];
  fp.border_color = static_cast<uint32_t>(n[4]);
  fp.metallic_border = n[5];
  fp.outline_gap = f[7];
  fp.outline_width = f[8];
  fp.outline_color = static_cast<uint32_t>(n[6]);
  fp.shadow = f[9];
  fp.shadow_offset = f[10];
  fp.shadow_blur = f[11];
  fp.glow = f[12];
  fp.glow_color = static_cast<uint32_t>(n[7]);
  fp.zoom = f[13];
  fp.offset_x = f[14];
  fp.offset_y = f[15];
  fp.decoration = n[8];
  const pp_status st = pp_frame(ph.image, cv.image, &fp, reinterpret_cast<const pp_cancel_token*>(token));
  if (st != PP_OK) throwStatus(env, st);
}

// Composites `stamp` (e.g. the watermark, drawn with Canvas) onto `image`
// at (x, y). Canvas output is premultiplied: it is converted to straight
// alpha first. Returns the visible stamp pixels (the export check).
JNIEXPORT jlong JNICALL Java_com_photoplus_engine_NativeEngine_nativeOverlay(JNIEnv* env, jobject, jobject image,
                                                                            jobject stamp, jint x, jint y,
                                                                            jfloat opacity) {
  WrappedBitmap img;
  if (!img.open(env, image, false)) return 0;
  BitmapLock lock(env, stamp);
  if (!lock.ok()) {
    throwEngineException(env, "UNSUPPORTED_FORMAT", lock.error());
    return 0;
  }
  const auto w = static_cast<int32_t>(lock.info().width), h = static_cast<int32_t>(lock.info().height);
  pp_image* straight = nullptr;
  pp_status st = pp_image_create(w, h, PP_FORMAT_RGBA8, &straight);
  if (st != PP_OK) {
    throwStatus(env, st);
    return 0;
  }
  const bool premul = (lock.info().flags & ANDROID_BITMAP_FLAGS_ALPHA_MASK) == ANDROID_BITMAP_FLAGS_ALPHA_PREMUL;
  auto* dst = static_cast<uint8_t*>(pp_image_pixels(straight));
  for (int32_t row = 0; row < h; ++row) {
    const auto* src = static_cast<const uint8_t*>(lock.pixels()) + static_cast<size_t>(row) * lock.info().stride;
    uint8_t* d = dst + static_cast<size_t>(row) * static_cast<size_t>(w) * 4;
    for (int32_t col = 0; col < w; ++col, src += 4, d += 4) {
      const uint8_t a = src[3];
      for (int c = 0; c < 3; ++c) {
        d[c] = premul && a > 0 && a < 255 ? static_cast<uint8_t>(std::min(255, (src[c] * 255 + a / 2) / a)) : src[c];
      }
      d[3] = a;
    }
  }
  int64_t visible = 0;
  st = pp_overlay(img.image, straight, x, y, opacity, &visible);
  pp_image_destroy(straight);
  if (st != PP_OK) {
    throwStatus(env, st);
    return 0;
  }
  return static_cast<jlong>(visible);
}

// Tap-seeded cut-out. Returns the mask bytes; dims are written to sizeOut.
JNIEXPORT jbyteArray JNICALL Java_com_photoplus_engine_NativeEngine_nativeSubjectMask(JNIEnv* env, jobject,
                                                                                     jobject bitmap,
                                                                                     jfloatArray points,
                                                                                     jintArray sizeOut) {
  if (env->GetArrayLength(sizeOut) < 2) {
    throwEngineException(env, "INVALID_ARGUMENT", "sizeOut needs 2 slots");
    return nullptr;
  }
  WrappedBitmap img;
  if (!img.open(env, bitmap, false)) return nullptr;
  const jsize n = points ? env->GetArrayLength(points) : 0;
  std::vector<float> pts(static_cast<size_t>(n));
  if (n > 0) env->GetFloatArrayRegion(points, 0, n, pts.data());
  pp_image* mask = nullptr;
  const pp_status st = pp_subject_mask(img.image, pts.data(), static_cast<size_t>(n / 2), nullptr, &mask);
  if (st != PP_OK) {
    throwStatus(env, st);
    return nullptr;
  }
  pp_image_info info{};
  pp_image_get_info(mask, &info);
  jbyteArray out = env->NewByteArray(info.width * info.height);
  if (out != nullptr) {
    const auto* px = static_cast<const jbyte*>(pp_image_pixels(mask));
    for (int32_t y = 0; y < info.height; ++y) {
      env->SetByteArrayRegion(out, y * info.width, info.width, px + static_cast<size_t>(y) * info.stride);
    }
    const jint dims[2] = {info.width, info.height};
    env->SetIntArrayRegion(sizeOut, 0, 2, dims);
  }
  pp_image_destroy(mask);
  return out;
}

namespace {
constexpr jsize kStyleFloats = PP_STYLE_QUANTILES + 3 + 3 + 2 + 3;
}

JNIEXPORT jfloatArray JNICALL Java_com_photoplus_engine_NativeEngine_nativeStyleAnalyze(JNIEnv* env, jobject,
                                                                                       jobject bitmap) {
  WrappedBitmap img;
  if (!img.open(env, bitmap, false)) return nullptr;
  pp_style s{};
  const pp_status st = pp_style_analyze(img.image, &s);
  if (st != PP_OK) {
    throwStatus(env, st);
    return nullptr;
  }
  jfloat v[kStyleFloats];
  jsize k = 0;
  for (float q : s.luma) v[k++] = q;
  for (float c : s.zone_cb) v[k++] = c;
  for (float c : s.zone_cr) v[k++] = c;
  v[k++] = s.chroma_std[0];
  v[k++] = s.chroma_std[1];
  v[k++] = s.vignette;
  v[k++] = s.grain;
  v[k++] = s.mean_luma;
  jfloatArray out = env->NewFloatArray(kStyleFloats);
  if (out != nullptr) env->SetFloatArrayRegion(out, 0, kStyleFloats, v);
  return out;
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeStyleApply(JNIEnv* env, jobject, jobject bitmap,
                                                                             jfloatArray style, jfloat strength,
                                                                             jlong token) {
  if (env->GetArrayLength(style) != kStyleFloats) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad look");
    return;
  }
  jfloat v[kStyleFloats];
  env->GetFloatArrayRegion(style, 0, kStyleFloats, v);
  pp_style s{};
  jsize k = 0;
  for (float& q : s.luma) q = v[k++];
  for (float& c : s.zone_cb) c = v[k++];
  for (float& c : s.zone_cr) c = v[k++];
  s.chroma_std[0] = v[k++];
  s.chroma_std[1] = v[k++];
  s.vignette = v[k++];
  s.grain = v[k++];
  s.mean_luma = v[k++];
  WrappedBitmap img;
  if (!img.open(env, bitmap, false)) return;
  const pp_status st = pp_style_apply(img.image, &s, strength, reinterpret_cast<const pp_cancel_token*>(token));
  if (st != PP_OK) throwStatus(env, st);
}

// pp_process_ex: maxEdge > 0 marks a preview (reduced if larger; the engine
// may use the verified GPU); backend is a pp_backend value.
JNIEXPORT jlong JNICALL Java_com_photoplus_engine_NativeEngine_nativeProcessEx(
    JNIEnv* env, jobject, jobject bitmap, jintArray types, jfloatArray params, jint maxEdge, jint backend, jlong token) {
  const jsize count = env->GetArrayLength(types);
  constexpr jsize kParams = PP_OP_PARAMS;
  if (env->GetArrayLength(params) != count * kParams) {
    throwEngineException(env, "INVALID_ARGUMENT", "params must contain 8 floats per operation");
    return 0;
  }
  std::vector<pp_op> ops(static_cast<size_t>(count));
  if (count > 0) {
    std::vector<jint> t(static_cast<size_t>(count));
    std::vector<jfloat> p(static_cast<size_t>(count) * kParams);
    env->GetIntArrayRegion(types, 0, count, t.data());
    env->GetFloatArrayRegion(params, 0, count * kParams, p.data());
    if (env->ExceptionCheck()) return 0;
    for (jsize i = 0; i < count; ++i) {
      ops[i].type = t[i];
      for (int k = 0; k < kParams; ++k) ops[i].p[k] = p[i * kParams + k];
    }
  }
  WrappedBitmap img;
  if (!img.open(env, bitmap, false)) return 0;
  pp_render_options options{};
  options.max_edge = maxEdge;
  options.backend = backend;
  pp_image* output = nullptr;
  const pp_status status = pp_process_ex(img.image, ops.data(), ops.size(), &options,
                                         reinterpret_cast<const pp_cancel_token*>(token), &output);
  if (status != PP_OK) {
    throwStatus(env, status);
    return 0;
  }
  return reinterpret_cast<jlong>(output);
}

JNIEXPORT jfloat JNICALL Java_com_photoplus_engine_NativeEngine_nativeTokenProgress(JNIEnv*, jobject, jlong handle) {
  return pp_cancel_token_progress(reinterpret_cast<const pp_cancel_token*>(handle));
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeMemoryTrim(JNIEnv*, jobject) { pp_memory_trim(); }

// [compiled, available, maxDifference, renderer, reason]
JNIEXPORT jobjectArray JNICALL Java_com_photoplus_engine_NativeEngine_nativeGpuInit(JNIEnv* env, jobject) {
  pp_gpu_status s{};
  pp_gpu_init(&s);
  jobjectArray out = env->NewObjectArray(5, env->FindClass("java/lang/String"), nullptr);
  if (out == nullptr) return nullptr;
  const std::string values[5] = {std::to_string(s.compiled), std::to_string(s.available), std::to_string(s.max_difference),
                                 s.renderer, s.reason};
  for (int i = 0; i < 5; ++i) {
    jstring v = env->NewStringUTF(values[i].c_str());
    env->SetObjectArrayElement(out, i, v);
    env->DeleteLocalRef(v);
  }
  return out;
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeGpuShutdown(JNIEnv*, jobject) { pp_gpu_shutdown(); }

JNIEXPORT jfloat JNICALL Java_com_photoplus_engine_NativeEngine_nativeDetectKeystone(JNIEnv* env, jobject, jobject bitmap) {
  WrappedBitmap img;
  if (!img.open(env, bitmap, false)) return 0.0f;
  float v = 0.0f;
  const pp_status st = pp_detect_keystone(img.image, nullptr, &v);
  if (st != PP_OK) throwStatus(env, st);
  return v;
}

// Fills `out` (width*height*3 floats) with the model input for `bitmap`;
// `placement` (4 floats, may be null) receives scaleX, scaleY, offsetX, offsetY.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeImageToTensor(
    JNIEnv* env, jobject, jobject bitmap, jint width, jint height, jint layout, jint fit, jfloatArray mean,
    jfloatArray stdv, jfloatArray out, jfloatArray placement) {
  if (mean == nullptr || stdv == nullptr || out == nullptr || env->GetArrayLength(mean) < 3 || env->GetArrayLength(stdv) < 3) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad tensor arguments");
    return;
  }
  WrappedBitmap img;
  if (!img.open(env, bitmap, false)) return;
  pp_tensor_spec spec{};
  spec.width = width;
  spec.height = height;
  spec.layout = layout;
  spec.fit = fit;
  env->GetFloatArrayRegion(mean, 0, 3, spec.mean);
  env->GetFloatArrayRegion(stdv, 0, 3, spec.std);
  const jsize n = env->GetArrayLength(out);
  std::vector<float> buffer(static_cast<size_t>(n));
  pp_tensor_placement where{};
  const pp_status st = pp_image_to_tensor(img.image, &spec, buffer.data(), buffer.size(), nullptr, &where);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  env->SetFloatArrayRegion(out, 0, n, buffer.data());
  if (placement != nullptr && env->GetArrayLength(placement) >= 4) {
    const float p[4] = {where.scale_x, where.scale_y, where.offset_x, where.offset_y};
    env->SetFloatArrayRegion(placement, 0, 4, p);
  }
}

}  // extern "C"
