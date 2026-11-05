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

  /** Size of [nativePortrait]'s report. */
  const val PORTRAIT_REPORT_SIZE = 6

  /**
   * Portrait ("Spotlight") in place on [bitmap] (ARGB_8888). Masks are
   * ALPHA_8 (or ARGB_8888) bitmaps of any size: [subject] required, [faces],
   * [protect] and [depth] (0 far … 255 near) optional; without [depth] the
   * engine estimates a ground plane. [params] = blur, retouch, brighten, pop
   * (0..1). [reportOut] ([PORTRAIT_REPORT_SIZE]) = subject coverage, skin
   * coverage, spots, blur radius, focus depth, depth estimated (0/1).
   */
  external fun nativePortrait(
    bitmap: Bitmap,
    subject: Bitmap,
    faces: Bitmap?,
    protect: Bitmap?,
    depth: Bitmap?,
    params: FloatArray,
    token: Long,
    reportOut: FloatArray,
  )

  /** Size of [nativeLensAnalyze]'s report and of [nativeTransform]'s. */
  const val LENS_REPORT_SIZE = 7
  const val TRANSFORM_REPORT_SIZE = 7

  /**
   * Lens analysis of [bitmap] (ARGB_8888, full resolution for a fair
   * reading). [out] ([LENS_REPORT_SIZE]) = detail blur, corner blur, fringe
   * px, noise, grade (0 basic … 1 flagship-like), measured regions, reliable
   * (0/1).
   */
  external fun nativeLensAnalyze(bitmap: Bitmap, token: Long, out: FloatArray)

  /**
   * Transform: restores [src] into [dst] (ARGB_8888, src size × upscale) as
   * if a high-end lens had taken it. [params] = upscale, enrich, strength,
   * detail. [reportOut] ([TRANSFORM_REPORT_SIZE]) = fringe fixed px, deblur
   * centre, deblur corner, noise before, noise after, grade before, measured
   * regions.
   */
  external fun nativeTransform(src: Bitmap, dst: Bitmap, params: FloatArray, token: Long, reportOut: FloatArray)

  /** Tap-seeded cut-out; [points] = x0,y0,x1,y1… (0..1). Returns mask bytes, size in [sizeOut]. */
  external fun nativeSubjectMask(bitmap: Bitmap, points: FloatArray, sizeOut: IntArray): ByteArray

  /** A photo's look as [STYLE_FLOATS] numbers (see pp_style). */
  external fun nativeStyleAnalyze(bitmap: Bitmap): FloatArray
  external fun nativeStyleApply(bitmap: Bitmap, style: FloatArray, strength: Float, token: Long)

  const val STYLE_FLOATS = 20
}
