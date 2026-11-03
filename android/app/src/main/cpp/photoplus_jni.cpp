// JNI bridge between com.photoplus.engine.NativeEngine (Kotlin) and the
// engine's C ABI. This file contains no image logic: it only marshals
// Bitmaps/arrays, converts errors to EngineException and guarantees that every
// native resource acquired here is released on every path.

#include <android/bitmap.h>
#include <android/log.h>
#include <jni.h>

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
  BitmapLock(JNIEnv* env, jobject bitmap) : env_(env), bitmap_(bitmap) {
    if (AndroidBitmap_getInfo(env, bitmap, &info_) != ANDROID_BITMAP_RESULT_SUCCESS) {
      error_ = "Unable to read bitmap info";
      return;
    }
    if (info_.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
      error_ = "Bitmap must be ARGB_8888";
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
  if (env->GetArrayLength(params) != count * 4) {
    throwEngineException(env, "INVALID_ARGUMENT", "params must contain 4 floats per operation");
    return 0;
  }

  std::vector<pp_op> ops(static_cast<size_t>(count));
  if (count > 0) {
    std::vector<jint> t(static_cast<size_t>(count));
    std::vector<jfloat> p(static_cast<size_t>(count) * 4);
    env->GetIntArrayRegion(types, 0, count, t.data());
    env->GetFloatArrayRegion(params, 0, count * 4, p.data());
    if (env->ExceptionCheck()) return 0;
    for (jsize i = 0; i < count; ++i) {
      ops[i].type = t[i];
      for (int k = 0; k < 4; ++k) ops[i].p[k] = p[i * 4 + k];
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
  const jlong values[6] = {static_cast<jlong>(s.current_bytes),     static_cast<jlong>(s.peak_bytes),
                           static_cast<jlong>(s.budget_bytes),      static_cast<jlong>(s.live_allocations),
                           static_cast<jlong>(s.total_allocations), static_cast<jlong>(s.failed_allocations)};
  jlongArray out = env->NewLongArray(6);
  if (out != nullptr) env->SetLongArrayRegion(out, 0, 6, values);
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

}  // extern "C"
