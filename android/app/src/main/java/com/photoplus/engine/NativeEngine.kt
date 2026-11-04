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
  external fun nativeResetMemoryPeak()

  /**
   * Enhances one frame, or fuses a burst, writing into [dst] (ARGB_8888,
   * unpremultiplied, size = frame size × [upscale]). Frames and [dst] are
   * locked and processed in place. [params] = denoise, sharpen, detail,
   * vibrance. [reportOut] (7) receives the engine report.
   */
  external fun nativeEnhance(
    frames: Array<Bitmap>,
    upscale: Int,
    params: FloatArray,
    autoTone: Boolean,
    dst: Bitmap,
    token: Long,
    reportOut: FloatArray,
  )

  /** [noise, blur px, sharpness, mean luma, highlights clipped, shadows clipped] */
  external fun nativeAnalyzeQuality(bitmap: Bitmap): FloatArray

  /**
   * Focus peaking from a camera Y plane read in place (direct buffer) into the
   * [overlay] bitmap. [rgbaColor] is packed in memory order (R in the low
   * byte). Returns a 0..1 centre sharpness score.
   */
  external fun nativeFocusPeaking(
    luma: java.nio.ByteBuffer,
    width: Int,
    height: Int,
    rowStride: Int,
    pixelStride: Int,
    threshold: Float,
    rgbaColor: Int,
    overlay: Bitmap,
  ): Float
}
