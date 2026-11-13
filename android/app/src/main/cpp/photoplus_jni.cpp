// JNI bridge between com.photoplus.engine.NativeEngine (Kotlin) and the
// engine's C ABI. This file contains no image logic: it only marshals
// Bitmaps/arrays, converts errors to EngineException and guarantees that every
// native resource acquired here is released on every path.

#include <android/bitmap.h>
#include <android/log.h>
#include <jni.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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
  constexpr jsize kReport = 7;
  const jsize n = env->GetArrayLength(params);
  if ((n != 4 && n != 10) || env->GetArrayLength(reportOut) < kReport || subject == nullptr) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad portrait arguments");
    return;
  }
  // 4 values: blur, retouch, brighten, pop (the advanced controls follow the
  // engine defaults); 10 adds smooth, blemish, even, bg light, bg colour, edge.
  jfloat p[10];
  env->GetFloatArrayRegion(params, 0, n, p);
  WrappedBitmap img, subj, fac, prot, dep;
  if (!img.open(env, bitmap, false) || !subj.open(env, subject, true) || !fac.open(env, faces, true) ||
      !prot.open(env, protect, true) || !dep.open(env, depth, true)) {
    return;
  }
  pp_portrait_params pp{};
  pp_portrait_default_params(&pp);
  pp.blur = p[0];
  pp.retouch = p[1];
  pp.brighten = p[2];
  pp.pop = p[3];
  if (n == 10) {
    pp.smooth = p[4];
    pp.blemish = p[5];
    pp.even = p[6];
    pp.bg_light = p[7];
    pp.bg_color = p[8];
    pp.edge = p[9];
  }
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
                                  static_cast<jfloat>(report.depth_estimated),
                                  report.edge_cleaned};
  env->SetFloatArrayRegion(reportOut, 0, kReport, values);
}

// Face-precise adjustments. `faces` holds count * 262 floats (PP_FACE_POINTS
// x, y pairs, normalised to the bitmap), `detailed` one flag per face,
// `params` 9 floats (eyes, teeth, lips, under-eye, shine, slim, face light,
// studio light, light angle). `reportOut` (3) = faces, adjusted, light gain.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeFaceAdjust(
    JNIEnv* env, jobject, jobject bitmap, jfloatArray faces, jintArray detailed, jfloatArray params, jlong token,
    jfloatArray reportOut) {
  constexpr jsize kPer = PP_FACE_POINTS * 2;
  if (faces == nullptr || detailed == nullptr || params == nullptr || reportOut == nullptr) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad face arguments");
    return;
  }
  const jsize total = env->GetArrayLength(faces);
  const jsize count = env->GetArrayLength(detailed);
  if (total != count * kPer || count > 32 || env->GetArrayLength(params) != 9 || env->GetArrayLength(reportOut) < 3) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad face arguments");
    return;
  }
  std::vector<pp_face> list(static_cast<size_t>(count));
  std::vector<jfloat> pts(static_cast<size_t>(total));
  std::vector<jint> det(static_cast<size_t>(count));
  if (total > 0) env->GetFloatArrayRegion(faces, 0, total, pts.data());
  if (count > 0) env->GetIntArrayRegion(detailed, 0, count, det.data());
  jfloat p[9];
  env->GetFloatArrayRegion(params, 0, 9, p);
  if (env->ExceptionCheck()) return;
  for (jsize i = 0; i < count; ++i) {
    std::copy_n(pts.data() + static_cast<size_t>(i) * kPer, kPer, list[static_cast<size_t>(i)].points);
    list[static_cast<size_t>(i)].detailed = det[static_cast<size_t>(i)];
  }
  WrappedBitmap img;
  if (!img.open(env, bitmap, false)) return;
  pp_face_params fp{};
  pp_face_default_params(&fp);
  fp.eyes = p[0];
  fp.teeth = p[1];
  fp.lips = p[2];
  fp.under_eye = p[3];
  fp.shine = p[4];
  fp.slim = p[5];
  fp.face_light = p[6];
  fp.studio_light = p[7];
  fp.light_angle = p[8];
  pp_face_report report{};
  const pp_status st = pp_face_adjust(img.image, list.data(), list.size(), &fp,
                                      reinterpret_cast<const pp_cancel_token*>(token), &report);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  const jfloat values[3] = {static_cast<jfloat>(report.faces), static_cast<jfloat>(report.adjusted), report.light_gain};
  env->SetFloatArrayRegion(reportOut, 0, 3, values);
}

// Packs `count` faces (count * 262 floats, normalised) into pp_face records.
static bool readFaces(JNIEnv* env, jfloatArray faces, jintArray detailed, std::vector<pp_face>& out) {
  constexpr jsize kPer = PP_FACE_POINTS * 2;
  if (faces == nullptr || detailed == nullptr) return false;
  const jsize total = env->GetArrayLength(faces);
  const jsize count = env->GetArrayLength(detailed);
  if (total != count * kPer || count > 32) return false;
  std::vector<jfloat> pts(static_cast<size_t>(total));
  std::vector<jint> det(static_cast<size_t>(count));
  if (total > 0) env->GetFloatArrayRegion(faces, 0, total, pts.data());
  if (count > 0) env->GetIntArrayRegion(detailed, 0, count, det.data());
  if (env->ExceptionCheck()) return false;
  out.assign(static_cast<size_t>(count), pp_face{});
  for (jsize i = 0; i < count; ++i) {
    std::copy_n(pts.data() + static_cast<size_t>(i) * kPer, kPer, out[static_cast<size_t>(i)].points);
    out[static_cast<size_t>(i)].detailed = det[static_cast<size_t>(i)];
  }
  return true;
}

// The beauty compositor (skin → face → lighting / background / colour).
// `recipe` holds 78 floats: 6 skin/portrait (blur, smooth, blemish, even,
// brighten, edge), 13 face (eyes, teeth, lips, under-eye, shine, face light,
// studio, blush, blush R G B, contour, highlight), PP_BEAUTY_GRADE_FLOATS
// grade (pp_beauty_grade order) and auto light (0/1). Masks may be null.
// `reportOut` (8) = light angle, subject, skin, clipped before, clipped after,
// spots, faces adjusted, layers.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeBeauty(
    JNIEnv* env, jobject, jobject bitmap, jobject subject, jobject faceMask, jobject protect, jobject depth,
    jfloatArray faces, jintArray detailed, jfloatArray recipe, jlong token, jfloatArray reportOut) {
  constexpr jsize kRecipe = 6 + 13 + PP_BEAUTY_GRADE_FLOATS + 1;
  constexpr jsize kReport = 8;
  std::vector<pp_face> list;
  if (recipe == nullptr || reportOut == nullptr || env->GetArrayLength(recipe) != kRecipe ||
      env->GetArrayLength(reportOut) < kReport || !readFaces(env, faces, detailed, list)) {
    if (!env->ExceptionCheck()) throwEngineException(env, "INVALID_ARGUMENT", "Bad beauty arguments");
    return;
  }
  jfloat r[kRecipe];
  env->GetFloatArrayRegion(recipe, 0, kRecipe, r);
  if (env->ExceptionCheck()) return;
  WrappedBitmap img, subj, fac, prot, dep;
  if (!img.open(env, bitmap, false) || !subj.open(env, subject, true) || !fac.open(env, faceMask, true) ||
      !prot.open(env, protect, true) || !dep.open(env, depth, true)) {
    return;
  }
  pp_beauty_params p{};
  pp_beauty_default_params(&p);
  p.portrait.blur = r[0];
  p.portrait.retouch = 0.0f;
  p.portrait.brighten = r[4];
  p.portrait.pop = 0.0f;
  p.portrait.smooth = r[1];
  p.portrait.blemish = r[2];
  p.portrait.even = r[3];
  p.portrait.edge = r[5];
  const jfloat* f = r + 6;
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
  std::memcpy(&p.grade, r + 19, sizeof(pp_beauty_grade));
  p.auto_light = r[kRecipe - 1] > 0.5f ? 1 : 0;
  pp_beauty_report report{};
  const pp_status st = pp_beauty(img.image, subj.image, fac.image, prot.image, dep.image, list.empty() ? nullptr : list.data(),
                                 list.size(), &p, reinterpret_cast<const pp_cancel_token*>(token), &report);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  const jfloat values[kReport] = {report.light_angle, report.subject_coverage, report.skin_coverage,
                                  report.clipped_before, report.clipped_after, static_cast<jfloat>(report.spots_removed),
                                  static_cast<jfloat>(report.faces_adjusted), static_cast<jfloat>(report.layers)};
  env->SetFloatArrayRegion(reportOut, 0, kReport, values);
}

// Backdrops: restyles or replaces the background behind the subject, in
// place. `subject` is required (ALPHA_8), `depth` optional. `design` is a
// pp_backdrop_design, `strength` 0..1.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeBackdrop(
    JNIEnv* env, jobject, jobject bitmap, jobject subject, jobject depth, jint design, jfloat strength, jint seed, jlong token) {
  if (subject == nullptr) {
    throwEngineException(env, "INVALID_ARGUMENT", "Backdrops need a subject mask");
    return;
  }
  WrappedBitmap img, subj, dep;
  if (!img.open(env, bitmap, false) || !subj.open(env, subject, true) || !dep.open(env, depth, true)) return;
  pp_backdrop_params p{};
  p.design = design;
  p.strength = strength;
  p.seed = static_cast<uint32_t>(seed);
  const pp_status st = pp_backdrop(img.image, subj.image, dep.image, &p, reinterpret_cast<const pp_cancel_token*>(token), nullptr);
  if (st != PP_OK) throwStatus(env, st);
}

// Skin Tone: gives the skin of the face and visible body a new shade, in
// place, as gains on its own colour (linear light). `faces` (ALPHA_8 face
// area) is needed to measure the skin; `subject` and `protect` are optional.
// Returns false when no face could be measured (nothing changed).
JNIEXPORT jboolean JNICALL Java_com_photoplus_engine_NativeEngine_nativeSkinTone(
    JNIEnv* env, jobject, jobject bitmap, jobject subject, jobject faces, jobject protect, jfloat gainR, jfloat gainG,
    jfloat gainB, jfloat glow, jfloat evenness, jfloat strength, jlong token) {
  WrappedBitmap img, subj, fm, pm;
  if (!img.open(env, bitmap, false) || !subj.open(env, subject, true) || !fm.open(env, faces, true) || !pm.open(env, protect, true)) {
    return JNI_FALSE;
  }
  pp_skin_tone_params p{};
  p.gain_r = gainR;
  p.gain_g = gainG;
  p.gain_b = gainB;
  p.glow = glow;
  p.evenness = evenness;
  p.strength = strength;
  int32_t hasSkin = 0;
  const pp_status st = pp_skin_tone(img.image, subj.image, fm.image, pm.image, &p, reinterpret_cast<const pp_cancel_token*>(token),
                                    &hasSkin, nullptr);
  if (st != PP_OK) {
    throwStatus(env, st);
    return JNI_FALSE;
  }
  return hasSkin != 0 ? JNI_TRUE : JNI_FALSE;
}

// Smart lighting: reads the scene from a small preview frame and the
// camera's exposure. `input` (7) = ISO, exposure seconds, f-number, front
// (0/1), has flash (0/1), main face luma (-1 = none), focus diopters
// (-1 = unknown). `out` (15) = EV100, lux, exposure known, key, highlights,
// shadows, range (stops), backlit, plan, frames, exposure bias, night,
// screen-flash R, G, B (0..255).
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeLightMeter(JNIEnv* env, jobject, jobject preview,
                                                                               jfloatArray input, jfloatArray out) {
  constexpr jsize kIn = 7, kOut = 15;
  if (input == nullptr || out == nullptr || env->GetArrayLength(input) < kIn || env->GetArrayLength(out) < kOut) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad light meter arguments");
    return;
  }
  WrappedBitmap img;
  if (!img.open(env, preview, false)) return;
  jfloat v[kIn];
  env->GetFloatArrayRegion(input, 0, kIn, v);
  pp_light_meter_input in{};
  in.iso = v[0];
  in.exposure_s = v[1];
  in.f_number = v[2];
  in.front = v[3] > 0.5f ? 1 : 0;
  in.has_flash = v[4] > 0.5f ? 1 : 0;
  in.face_luma = v[5];
  in.focus_diopters = v[6];
  pp_light_meter_result m{};
  const pp_status st = pp_light_meter(img.image, &in, &m);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  const jfloat values[kOut] = {m.ev100, m.lux, static_cast<jfloat>(m.exposure_known), m.key, m.highlights, m.shadows,
                               m.range_stops, static_cast<jfloat>(m.backlit), static_cast<jfloat>(m.plan),
                               static_cast<jfloat>(m.frames), m.exposure_bias, m.night,
                               static_cast<jfloat>(m.screen_flash[0]), static_cast<jfloat>(m.screen_flash[1]),
                               static_cast<jfloat>(m.screen_flash[2])};
  env->SetFloatArrayRegion(out, 0, kOut, values);
}

// Local tone after a burst merge, in place. `params` (8) = night, shadows
// (-1 auto), highlights (-1 auto), exposure (stops), detail, white balance,
// capture bias (EV the frames were held back by, <= 0), strength.
// `reportOut` (6, optional) = key before, key after, range, shadows,
// highlights, share clipped.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeLightTone(JNIEnv* env, jobject, jobject bitmap,
                                                                              jfloatArray params, jlong token,
                                                                              jfloatArray reportOut) {
  constexpr jsize kParams = 8, kReport = 6;
  if (params == nullptr || env->GetArrayLength(params) < kParams ||
      (reportOut != nullptr && env->GetArrayLength(reportOut) < kReport)) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad light tone arguments");
    return;
  }
  WrappedBitmap img;
  if (!img.open(env, bitmap, false)) return;
  jfloat v[kParams];
  env->GetFloatArrayRegion(params, 0, kParams, v);
  pp_light_tone_params p;
  pp_light_tone_default_params(&p);
  p.night = v[0];
  p.shadows = v[1];
  p.highlights = v[2];
  p.exposure = v[3];
  p.detail = v[4];
  p.white_balance = v[5];
  p.capture_bias = v[6];
  p.strength = v[7];
  jfloat report[kReport] = {};
  const pp_status st = pp_light_tone(img.image, &p, reinterpret_cast<const pp_cancel_token*>(token), report);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  if (reportOut != nullptr) env->SetFloatArrayRegion(reportOut, 0, kReport, report);
}

// Natural Flash: `ambient` (no flash, merged) and `flash` fused into `dst`
// (all the same size; `dst` may be `ambient`). `reportOut` (4, optional) =
// aligned (0/1), flash gain, flash-shadow share, hot-spot share.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeFlashFuse(JNIEnv* env, jobject, jobject ambient,
                                                                              jobject flash, jobject dst, jfloat fill,
                                                                              jfloat strength, jlong token,
                                                                              jfloatArray reportOut) {
  constexpr jsize kReport = 4;
  if (reportOut != nullptr && env->GetArrayLength(reportOut) < kReport) {
    throwEngineException(env, "INVALID_ARGUMENT", "Flash report needs 4 slots");
    return;
  }
  const bool inPlace = env->IsSameObject(ambient, dst);
  WrappedBitmap amb, fl, out;
  if (!amb.open(env, ambient, false) || !fl.open(env, flash, false) || (!inPlace && !out.open(env, dst, false))) return;
  pp_flash_fuse_params p{};
  p.fill = fill;
  p.strength = strength;
  jfloat report[kReport] = {};
  const pp_status st = pp_flash_fuse(amb.image, fl.image, inPlace ? amb.image : out.image, &p,
                                     reinterpret_cast<const pp_cancel_token*>(token), report);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  if (reportOut != nullptr) env->SetFloatArrayRegion(reportOut, 0, kReport, report);
}

// What the beauty grade measures about a frame (the live preview's scene):
// `out` (9) = has skin, skin Y, Cb, Cr, spread, subject x, subject width,
// light angle, skin luma spread.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeBeautyMeasure(
    JNIEnv* env, jobject, jobject bitmap, jobject subject, jobject faceMask, jobject protect, jfloatArray faces,
    jintArray detailed, jfloatArray out) {
  std::vector<pp_face> list;
  if (out == nullptr || env->GetArrayLength(out) < 9 || !readFaces(env, faces, detailed, list)) {
    if (!env->ExceptionCheck()) throwEngineException(env, "INVALID_ARGUMENT", "Bad measure arguments");
    return;
  }
  WrappedBitmap img, subj, fac, prot;
  if (!img.open(env, bitmap, false) || !subj.open(env, subject, true) || !fac.open(env, faceMask, true) ||
      !prot.open(env, protect, true)) {
    return;
  }
  pp_beauty_scene sc{};
  const pp_status st = pp_beauty_measure(img.image, subj.image, fac.image, prot.image, list.empty() ? nullptr : list.data(),
                                         list.size(), &sc);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  const jfloat values[9] = {static_cast<jfloat>(sc.has_skin), sc.skin_y, sc.skin_cb, sc.skin_cr, sc.skin_spread,
                            sc.subject_x, sc.subject_w, sc.light_angle, sc.skin_y_spread};
  env->SetFloatArrayRegion(out, 0, 9, values);
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


// ---- Glide video (Phase 6) ---------------------------------------------------

namespace {
bool readMotion(JNIEnv* env, jfloatArray arr, pp_video_motion& m) {
  if (arr == nullptr) return false;
  if (env->GetArrayLength(arr) < 4) return false;
  jfloat v[5] = {1, 0, 0, 0, 1};
  env->GetFloatArrayRegion(arr, 0, std::min<jsize>(5, env->GetArrayLength(arr)), v);
  m = pp_video_motion{};
  m.a = v[0], m.b = v[1], m.tx = v[2], m.ty = v[3], m.confidence = v[4];
  return true;
}
}  // namespace

// out (5): a, b, tx, ty, confidence (see pp_video_motion).
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeVideoMotion(JNIEnv* env, jobject, jobject previous,
                                                                               jobject current, jfloatArray out) {
  if (out == nullptr || env->GetArrayLength(out) < 5) {
    throwEngineException(env, "INVALID_ARGUMENT", "Motion needs 5 slots");
    return;
  }
  WrappedBitmap a, b;
  if (!a.open(env, previous, false) || !b.open(env, current, false)) return;
  pp_video_motion m{};
  const pp_status st = pp_video_estimate_motion(a.image, b.image, &m);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  const jfloat v[5] = {m.a, m.b, m.tx, m.ty, m.confidence};
  env->SetFloatArrayRegion(out, 0, 5, v);
}

// motions: count * 5 (a, b, tx, ty, confidence); warpsOut: count * 6; reportOut (3): zoom, shake before/after.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeVideoStabilize(
    JNIEnv* env, jobject, jfloatArray motions, jint width, jint height, jfloat strength, jfloat maxZoom,
    jfloatArray warpsOut, jfloatArray reportOut) {
  const jsize total = env->GetArrayLength(motions);
  const jsize count = total / 5;
  if (count < 1 || total != count * 5 || env->GetArrayLength(warpsOut) < count * 6 || env->GetArrayLength(reportOut) < 3) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad stabilisation arguments");
    return;
  }
  std::vector<jfloat> raw(static_cast<size_t>(total));
  env->GetFloatArrayRegion(motions, 0, total, raw.data());
  std::vector<pp_video_motion> ms(static_cast<size_t>(count));
  for (jsize i = 0; i < count; ++i) {
    const jfloat* v = raw.data() + static_cast<size_t>(i) * 5;
    ms[static_cast<size_t>(i)] = pp_video_motion{};
    ms[static_cast<size_t>(i)].a = v[0], ms[static_cast<size_t>(i)].b = v[1], ms[static_cast<size_t>(i)].tx = v[2];
    ms[static_cast<size_t>(i)].ty = v[3], ms[static_cast<size_t>(i)].confidence = v[4];
  }
  std::vector<float> warps(static_cast<size_t>(count) * 6);
  float report[3] = {1, 0, 0};
  const pp_status st = pp_video_stabilize(ms.data(), ms.size(), width, height, strength, maxZoom, warps.data(), report);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  env->SetFloatArrayRegion(warpsOut, 0, count * 6, warps.data());
  env->SetFloatArrayRegion(reportOut, 0, 3, report);
}

// One frame of the clip into dst. fromPrevious / toNext: 5 floats or null; params (4): noise, blur, denoise, sharpen;
// curves: 768 bytes or null.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeVideoEnhanceFrame(
    JNIEnv* env, jobject, jobject frame, jobject previous, jobject next, jfloatArray fromPrevious, jfloatArray toNext,
    jfloatArray params, jbyteArray curves, jobject dst, jlong token) {
  if (params == nullptr || env->GetArrayLength(params) < 4 || (curves != nullptr && env->GetArrayLength(curves) < 768)) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad frame arguments");
    return;
  }
  jfloat p[4];
  env->GetFloatArrayRegion(params, 0, 4, p);
  pp_video_frame_params fp{};
  fp.noise_sigma = p[0], fp.blur_sigma = p[1], fp.denoise = p[2], fp.sharpen = p[3];
  pp_video_motion from{}, to{};
  const bool hasFrom = readMotion(env, fromPrevious, from), hasTo = readMotion(env, toNext, to);
  uint8_t c[768];
  if (curves != nullptr) env->GetByteArrayRegion(curves, 0, 768, reinterpret_cast<jbyte*>(c));
  WrappedBitmap cur, prev, nxt, out;
  if (!cur.open(env, frame, false) || !prev.open(env, previous, false) || !nxt.open(env, next, false) ||
      !out.open(env, dst, false)) {
    return;
  }
  const pp_status st = pp_video_enhance_frame(cur.image, prev.image, nxt.image, hasFrom ? &from : nullptr,
                                              hasTo ? &to : nullptr, &fp, curves ? c : nullptr, out.image,
                                              reinterpret_cast<const pp_cancel_token*>(token));
  if (st != PP_OK) throwStatus(env, st);
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeVideoWarp(JNIEnv* env, jobject, jobject src,
                                                                             jfloatArray warp, jobject dst, jlong token) {
  if (warp == nullptr || env->GetArrayLength(warp) < 6) {
    throwEngineException(env, "INVALID_ARGUMENT", "A warp needs 6 values");
    return;
  }
  jfloat w[6];
  env->GetFloatArrayRegion(warp, 0, 6, w);
  WrappedBitmap a, b;
  if (!a.open(env, src, false) || !b.open(env, dst, false)) return;
  const pp_status st = pp_video_warp(a.image, w, b.image, reinterpret_cast<const pp_cancel_token*>(token));
  if (st != PP_OK) throwStatus(env, st);
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeVideoFitCurves(JNIEnv* env, jobject, jobject before,
                                                                                  jobject after, jbyteArray out) {
  if (out == nullptr || env->GetArrayLength(out) < 768) {
    throwEngineException(env, "INVALID_ARGUMENT", "Curves need 768 bytes");
    return;
  }
  WrappedBitmap a, b;
  if (!a.open(env, before, false) || !b.open(env, after, false)) return;
  uint8_t c[768];
  const pp_status st = pp_video_fit_curves(a.image, b.image, c);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  env->SetByteArrayRegion(out, 0, 768, reinterpret_cast<const jbyte*>(c));
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeVideoSteadyMask(JNIEnv* env, jobject, jobject previous,
                                                                                   jobject current, jfloat smoothing) {
  WrappedBitmap a, b;
  if (!a.open(env, previous, true) || !b.open(env, current, true)) return;
  if (a.image == nullptr || b.image == nullptr) {
    throwEngineException(env, "INVALID_ARGUMENT", "Two masks are required");
    return;
  }
  const pp_status st = pp_video_steady_mask(a.image, b.image, smoothing);
  if (st != PP_OK) throwStatus(env, st);
}

// ---- Pose assistant ------------------------------------------------------

namespace {
constexpr jsize kPoseFloats = PP_POSE_LANDMARKS * 5;
// nativePoseStep output layout (PoseStep.kt reads the same indices).
constexpr jsize kPoseOut = 45 + PP_POSE_LANDMARKS * 3;

bool readPoseArray(JNIEnv* env, jfloatArray arr, jfloat aspect, pp_pose& out) {
  out = pp_pose{};
  if (arr == nullptr) return true;  // nobody
  if (env->GetArrayLength(arr) < kPoseFloats || !std::isfinite(aspect) || aspect <= 0.05f || aspect >= 20.0f) return false;
  jfloat v[kPoseFloats];
  env->GetFloatArrayRegion(arr, 0, kPoseFloats, v);
  for (int i = 0; i < PP_POSE_LANDMARKS; ++i) {
    out.points[i] = pp_pose_point{v[i * 5], v[i * 5 + 1], v[i * 5 + 2], v[i * 5 + 3], v[i * 5 + 4]};
  }
  out.aspect = aspect;
  out.valid = 1;
  return true;
}
}  // namespace

JNIEXPORT jlong JNICALL Java_com_photoplus_engine_NativeEngine_nativePoseTrackerCreate(JNIEnv* env, jobject) {
  pp_pose_tracker* t = nullptr;
  const pp_status st = pp_pose_tracker_create(&t);
  if (st != PP_OK) {
    throwStatus(env, st);
    return 0;
  }
  return reinterpret_cast<jlong>(t);
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativePoseTrackerDestroy(JNIEnv*, jobject, jlong tracker) {
  pp_pose_tracker_destroy(reinterpret_cast<pp_pose_tracker*>(tracker));
}

// One live frame: the detected pose (null = nobody), mirrored for a selfie
// preview, steadied by the tracker, compared with the reference (optional,
// mirrored on request) and described. Writes the layout PoseStep.kt reads.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativePoseStep(
    JNIEnv* env, jobject, jlong tracker, jfloatArray live, jfloat liveAspect, jlong timeMs, jboolean mirrorLive,
    jfloatArray reference, jfloat referenceAspect, jboolean mirrorReference, jboolean framing, jfloat strictness,
    jfloatArray out) {
  if (tracker == 0 || out == nullptr || env->GetArrayLength(out) < kPoseOut) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad pose arguments");
    return;
  }
  pp_pose now{}, ref{};
  if (!readPoseArray(env, live, liveAspect, now) || !readPoseArray(env, reference, referenceAspect, ref)) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad pose landmarks");
    return;
  }
  if (now.valid && mirrorLive) {
    pp_pose m{};
    pp_pose_mirror(&now, &m);
    now = m;
  }
  if (ref.valid && mirrorReference) {
    pp_pose m{};
    pp_pose_mirror(&ref, &m);
    ref = m;
  }
  pp_pose steady{};
  float still = 0;
  pp_status st = pp_pose_tracker_update(reinterpret_cast<pp_pose_tracker*>(tracker), now.valid ? &now : nullptr,
                                        static_cast<int64_t>(timeMs), &steady, &still);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  pp_pose_description d{};
  if (steady.valid) pp_pose_describe(&steady, &d);
  pp_pose_match_result m{};
  if (ref.valid) {
    pp_pose_match_params mp{};
    mp.framing = framing ? 1 : 0;
    mp.strictness = strictness;
    st = pp_pose_match(&ref, &steady, &mp, &m);
    if (st != PP_OK) {
      throwStatus(env, st);
      return;
    }
  }
  std::vector<jfloat> o(static_cast<size_t>(kPoseOut), 0.0f);
  o[0] = d.valid ? 1.0f : 0.0f;
  o[1] = still;
  o[2] = m.score, o[3] = m.coverage, o[4] = m.pose_score, o[5] = m.framing_score;
  o[6] = m.offset_x, o[7] = m.offset_y, o[8] = m.size_ratio;
  o[9] = static_cast<jfloat>(d.shot);
  o[10] = ref.valid ? 1.0f : 0.0f;
  o[11] = static_cast<jfloat>(m.advice_count);
  for (int i = 0; i < 4; ++i) {
    o[static_cast<size_t>(12 + i * 3)] = static_cast<jfloat>(m.advice[i].part);
    o[static_cast<size_t>(13 + i * 3)] = static_cast<jfloat>(m.advice[i].action);
    o[static_cast<size_t>(14 + i * 3)] = m.advice[i].amount;
  }
  o[24] = m.left_hand_score, o[25] = m.right_hand_score;
  for (int b = 0; b < PP_POSE_BONES; ++b) o[static_cast<size_t>(26 + b)] = ref.valid ? m.bone_score[b] : -1.0f;
  o[38] = d.anchor_x, o[39] = d.anchor_y, o[40] = d.scale, o[41] = d.head_roll;
  o[42] = steady.points[0].x, o[43] = steady.points[0].y, o[44] = d.reliability[0];
  for (int i = 0; i < PP_POSE_LANDMARKS; ++i) {
    o[static_cast<size_t>(45 + i * 3)] = steady.points[i].x;
    o[static_cast<size_t>(46 + i * 3)] = steady.points[i].y;
    o[static_cast<size_t>(47 + i * 3)] = d.valid ? d.reliability[i] : 0.0f;
  }
  env->SetFloatArrayRegion(out, 0, kPoseOut, o.data());
}

// A pose from a photo: `out` (5) = valid, shot, scale (shoulder width /
// image height), anchor x, anchor y.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativePoseDescribe(JNIEnv* env, jobject, jfloatArray points,
                                                                                jfloat aspect, jfloatArray out) {
  if (points == nullptr || out == nullptr || env->GetArrayLength(out) < 5) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad pose arguments");
    return;
  }
  pp_pose p{};
  if (!readPoseArray(env, points, aspect, p)) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad pose landmarks");
    return;
  }
  pp_pose_description d{};
  const pp_status st = pp_pose_describe(&p, &d);
  if (st != PP_OK) {
    throwStatus(env, st);
    return;
  }
  const jfloat v[5] = {d.valid ? 1.0f : 0.0f, static_cast<jfloat>(d.shot), d.scale, d.anchor_x, d.anchor_y};
  env->SetFloatArrayRegion(out, 0, 5, v);
}

// The room remembered behind the person, for one clip (see pp_video_room).
JNIEXPORT jlong JNICALL Java_com_photoplus_engine_NativeEngine_nativeVideoRoomCreate(JNIEnv* env, jobject) {
  pp_video_room* room = nullptr;
  const pp_status st = pp_video_room_create(&room);
  if (st != PP_OK) {
    throwStatus(env, st);
    return 0;
  }
  return reinterpret_cast<jlong>(room);
}

JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeVideoRoomDestroy(JNIEnv*, jobject, jlong room) {
  pp_video_room_destroy(reinterpret_cast<pp_video_room*>(room));
}

// Corrects `mask` (ALPHA_8) for `frame` (ARGB_8888) in place: pieces of the
// room the person model swallowed are given back. `motion` (5, optional) =
// a, b, tx, ty, confidence of the frame's motion from the previous one, in
// the frame's pixels. Returns the share of the person area given back.
JNIEXPORT jfloat JNICALL Java_com_photoplus_engine_NativeEngine_nativeVideoRoomCheck(JNIEnv* env, jobject, jlong room,
                                                                                   jobject frame, jobject mask,
                                                                                   jfloatArray motion, jlong token) {
  if (room == 0 || (motion != nullptr && env->GetArrayLength(motion) < 5)) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad room arguments");
    return 0.0f;
  }
  WrappedBitmap img, m;
  if (!img.open(env, frame, false) || !m.open(env, mask, true)) return 0.0f;
  pp_video_motion mo{1, 0, 0, 0, 1, {0, 0, 0}};
  if (motion != nullptr) {
    jfloat v[5];
    env->GetFloatArrayRegion(motion, 0, 5, v);
    mo.a = v[0], mo.b = v[1], mo.tx = v[2], mo.ty = v[3], mo.confidence = v[4];
  }
  float reclaimed = 0.0f;
  const pp_status st = pp_video_room_check(reinterpret_cast<pp_video_room*>(room), img.image, m.image,
                                           motion != nullptr ? &mo : nullptr,
                                           reinterpret_cast<const pp_cancel_token*>(token), &reclaimed);
  if (st != PP_OK) {
    throwStatus(env, st);
    return 0.0f;
  }
  return reclaimed;
}

// RGBA bitmap → the encoder's YUV 4:2:0 planes (BT.601 limited range, as
// video players expect), written through direct buffers with the plane strides.
JNIEXPORT void JNICALL Java_com_photoplus_engine_NativeEngine_nativeRgbaToYuv420(
    JNIEnv* env, jobject, jobject bitmap, jobject yBuf, jobject uBuf, jobject vBuf, jint yRowStride, jint uvRowStride,
    jint uvPixelStride) {
  auto* yp = static_cast<uint8_t*>(env->GetDirectBufferAddress(yBuf));
  auto* up = static_cast<uint8_t*>(env->GetDirectBufferAddress(uBuf));
  auto* vp = static_cast<uint8_t*>(env->GetDirectBufferAddress(vBuf));
  const jlong ycap = env->GetDirectBufferCapacity(yBuf), ucap = env->GetDirectBufferCapacity(uBuf),
              vcap = env->GetDirectBufferCapacity(vBuf);
  BitmapLock lock(env, bitmap);
  if (!lock.ok() || yp == nullptr || up == nullptr || vp == nullptr || uvPixelStride < 1) {
    throwEngineException(env, "INVALID_ARGUMENT", "Bad YUV conversion arguments");
    return;
  }
  const auto w = static_cast<int32_t>(lock.info().width), h = static_cast<int32_t>(lock.info().height);
  const auto stride = static_cast<int32_t>(lock.info().stride);
  const auto* px = static_cast<const uint8_t*>(lock.pixels());
  if (static_cast<jlong>(yRowStride) * (h - 1) + w > ycap ||
      static_cast<jlong>(uvRowStride) * (h / 2 - 1) + static_cast<jlong>(uvPixelStride) * (w / 2 - 1) + 1 > std::min(ucap, vcap)) {
    throwEngineException(env, "INVALID_ARGUMENT", "YUV planes are too small for the frame");
    return;
  }
  for (int32_t y = 0; y < h; ++y) {
    const uint8_t* r = px + static_cast<size_t>(y) * stride;
    uint8_t* yo = yp + static_cast<size_t>(y) * yRowStride;
    for (int32_t x = 0; x < w; ++x) {
      const int R = r[x * 4], G = r[x * 4 + 1], B = r[x * 4 + 2];
      yo[x] = static_cast<uint8_t>(std::clamp((66 * R + 129 * G + 25 * B + 128) / 256 + 16, 0, 255));
    }
  }
  for (int32_t y = 0; y < h / 2; ++y) {
    for (int32_t x = 0; x < w / 2; ++x) {
      int R = 0, G = 0, B = 0;
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
          const uint8_t* q = px + static_cast<size_t>(2 * y + dy) * stride + (2 * x + dx) * 4;
          R += q[0], G += q[1], B += q[2];
        }
      R /= 4, G /= 4, B /= 4;
      const size_t o = static_cast<size_t>(y) * uvRowStride + static_cast<size_t>(x) * uvPixelStride;
      up[o] = static_cast<uint8_t>(std::clamp((-38 * R - 74 * G + 112 * B + 128) / 256 + 128, 0, 255));
      vp[o] = static_cast<uint8_t>(std::clamp((112 * R - 94 * G - 18 * B + 128) / 256 + 128, 0, 255));
    }
  }
}

}  // extern "C"
