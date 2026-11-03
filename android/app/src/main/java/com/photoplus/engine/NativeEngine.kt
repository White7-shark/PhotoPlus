package com.photoplus.engine

import android.graphics.Bitmap

/**
 * Raw JNI surface of libphotoplus_jni. Callers must pair every handle
 * returned here with its matching release call; [EngineRunner] does this.
 */
object NativeEngine {
  @Volatile private var loaded = false
  @Volatile var loadError: Throwable? = null
    private set

  /** Loads the native library once. Returns false if the ABI is unsupported. */
  @Synchronized
  fun ensureLoaded(): Boolean {
    if (loaded) return true
    return try {
      System.loadLibrary("photoplus_jni")
      loaded = true
      true
    } catch (t: UnsatisfiedLinkError) {
      loadError = t
      false
    }
  }

  external fun nativeVersion(): String

  external fun nativeCreateToken(): Long
  external fun nativeCancelToken(handle: Long)
  external fun nativeDestroyToken(handle: Long)

  /** Returns a result handle. Throws [EngineException] on failure. */
  external fun nativeProcess(bitmap: Bitmap, types: IntArray, params: FloatArray, token: Long): Long
  external fun nativeResultWidth(handle: Long): Int
  external fun nativeResultHeight(handle: Long): Int
  external fun nativeCopyResultToBitmap(handle: Long, bitmap: Bitmap)
  external fun nativeReleaseResult(handle: Long)

  /** [current, peak, budget, live, total, failed] */
  external fun nativeMemoryStats(): LongArray
  external fun nativeSetMemoryBudget(bytes: Long)
  external fun nativeProfileReport(): String
  external fun nativeResetProfiler()
}
