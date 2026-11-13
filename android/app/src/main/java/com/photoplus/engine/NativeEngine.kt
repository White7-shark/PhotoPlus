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

  /**
   * pp_process_ex: [maxEdge] > 0 marks a preview (reduced if larger, with
   * pixel-measured settings scaled; may run on the verified GPU); [backend] is
   * [BACKEND_AUTO], [BACKEND_CPU] or [BACKEND_GPU].
   */
  external fun nativeProcessEx(bitmap: Bitmap, types: IntArray, params: FloatArray, maxEdge: Int, backend: Int, token: Long): Long

  /** How far the job using [handle] has got, 0..1. */
  external fun nativeTokenProgress(handle: Long): Float

  /** [current, peak, budget, live, total, failed, cached, reused] */
  external fun nativeMemoryStats(): LongArray

  /** Frees buffers kept for reuse (memory pressure). */
  external fun nativeMemoryTrim()

  /** Initialises and verifies the GPU: [compiled, available, maxDifference, renderer, reason]. */
  external fun nativeGpuInit(): Array<String>
  external fun nativeGpuShutdown()

  /** Suggested vertical perspective correction (-1..1), 0 if unclear. */
  external fun nativeDetectKeystone(bitmap: Bitmap): Float

  /**
   * Model input for [bitmap]: [out] gets width*height*3 floats ([layout] 0
   * NHWC, 1 NCHW; [fit] 0 stretch, 1 letterbox, 2 centre crop; value =
   * (pixel/255 - mean) / std). [placement] (4 floats, optional) receives where
   * the photo landed: scaleX, scaleY, offsetX, offsetY.
   */
  external fun nativeImageToTensor(
    bitmap: Bitmap, width: Int, height: Int, layout: Int, fit: Int, mean: FloatArray, std: FloatArray,
    out: FloatArray, placement: FloatArray?,
  )

  const val BACKEND_AUTO = 0
  const val BACKEND_CPU = 1
  const val BACKEND_GPU = 2
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
  const val PORTRAIT_REPORT_SIZE = 7

  /**
   * Portrait ("Spotlight") in place on [bitmap] (ARGB_8888). Masks are
   * ALPHA_8 (or ARGB_8888) bitmaps of any size: [subject] required, [faces],
   * [protect] and [depth] (0 far … 255 near) optional; without [depth] the
   * engine estimates a ground plane. [params] = blur, retouch, brighten, pop
   * (0..1), optionally followed by smooth, blemish, even (0..1, negative =
   * follow retouch), background light, background colour (-1..1) and edge
   * refinement (0..1): 4 or 10 values. [reportOut] ([PORTRAIT_REPORT_SIZE]) =
   * subject coverage, skin coverage, spots, blur radius, focus depth, depth
   * estimated (0/1), share of the frame whose edge colour was cleaned.
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

  /** Floats per face in [nativeFaceAdjust] (131 landmark x, y pairs). */
  const val FACE_FLOATS = 262

  /** Size of [nativeFaceAdjust]'s params and report. */
  const val FACE_PARAMS = 9
  const val FACE_REPORT_SIZE = 3

  /** pp_beauty_grade's float count, the beauty recipe (6 skin + 13 face + grade + auto light) and its report. */
  const val BEAUTY_GRADE_FLOATS = 58
  const val BEAUTY_RECIPE_FLOATS = 6 + 13 + BEAUTY_GRADE_FLOATS + 1
  const val BEAUTY_REPORT_SIZE = 8
  const val BEAUTY_SCENE_SIZE = 9

  /**
   * Face-precise adjustments in place on [bitmap]: [faces] holds
   * [FACE_FLOATS] per face (landmarks normalised to the bitmap, the
   * com.photoplus.studio.FaceLandmarks layout), [detailed] one 0/1 per face.
   * [params] ([FACE_PARAMS]) = eyes, teeth, lips, under-eye, shine, slim, face
   * light, studio light (0..1) and light angle (-1..1). [reportOut] = faces,
   * adjusted, light gain.
   */
  /**
   * The beauty compositor (skin → face → lighting / background / colour) in
   * place. Masks are ALPHA_8 (any may be null). [faces] = count x 262
   * normalised landmark floats, [detailed] one flag per face. [recipe] =
   * [BEAUTY_RECIPE_FLOATS] (see com.photoplus.beauty.BeautyRecipe). [reportOut]
   * (8) = light angle, subject, skin, clipped before/after, spots, faces
   * adjusted, layers.
   */
  external fun nativeBeauty(
    bitmap: Bitmap,
    subject: Bitmap?,
    faceMask: Bitmap?,
    protect: Bitmap?,
    depth: Bitmap?,
    faces: FloatArray,
    detailed: IntArray,
    recipe: FloatArray,
    token: Long,
    reportOut: FloatArray,
  )

  /**
   * Backdrops: restyles or replaces the background behind the subject, in
   * place. [subject] is an ALPHA_8 mask; [depth] (optional) refines the
   * Signature blur. [design] is com.photoplus.studio.Backdrop.engineId.
   */
  external fun nativeBackdrop(bitmap: Bitmap, subject: Bitmap, depth: Bitmap?, design: Int, strength: Float, seed: Int, token: Long)

  /**
   * Skin Tone: gives the skin of the face and the visible body a new shade,
   * in place, as gains on its own colour in linear light (1 = unchanged), so
   * the photo's light stays on it. [faces] (ALPHA_8 face area) is needed to
   * measure the skin; [subject] and [protect] are optional. Returns false
   * when no face could be measured (nothing changed).
   */
  external fun nativeSkinTone(
    bitmap: Bitmap,
    subject: Bitmap?,
    faces: Bitmap?,
    protect: Bitmap?,
    gainR: Float,
    gainG: Float,
    gainB: Float,
    glow: Float,
    evenness: Float,
    strength: Float,
    token: Long,
  ): Boolean

  /**
   * What the beauty grade measures about a frame, for the live preview:
   * [out] (9) = has skin (0/1), skin Y, Cb, Cr, spread, subject x, subject
   * width, light angle, skin luma spread.
   */
  external fun nativeBeautyMeasure(
    bitmap: Bitmap,
    subject: Bitmap?,
    faceMask: Bitmap?,
    protect: Bitmap?,
    faces: FloatArray,
    detailed: IntArray,
    out: FloatArray,
  )

  external fun nativeFaceAdjust(
    bitmap: Bitmap,
    faces: FloatArray,
    detailed: IntArray,
    params: FloatArray,
    token: Long,
    reportOut: FloatArray,
  )

  // ---- Glide video (Phase 6) ----

  /** Camera motion from [previous] to [current]; [out] (5) = a, b, tx, ty, confidence. */
  external fun nativeVideoMotion(previous: Bitmap, current: Bitmap, out: FloatArray)

  /**
   * The steadying plan: [motions] holds 5 floats per frame (frame 0's are
   * ignored); [warpsOut] receives 6 per frame (output → source); [reportOut]
   * (3) = crop zoom, shake before, shake after (px per frame).
   */
  external fun nativeVideoStabilize(
    motions: FloatArray, width: Int, height: Int, strength: Float, maxZoom: Float, warpsOut: FloatArray, reportOut: FloatArray,
  )

  /**
   * One frame cleaned into [dst]: temporal denoise with [previous]/[next]
   * (null at the ends; [fromPrevious] is this frame's motion, [toNext] the
   * next one's), lens deblur and the clip's tone [curves] (768 bytes or null).
   * [params] = noise, blur px, denoise, sharpen.
   */
  external fun nativeVideoEnhanceFrame(
    frame: Bitmap, previous: Bitmap?, next: Bitmap?, fromPrevious: FloatArray?, toNext: FloatArray?,
    params: FloatArray, curves: ByteArray?, dst: Bitmap, token: Long,
  )

  external fun nativeVideoWarp(src: Bitmap, warp: FloatArray, dst: Bitmap, token: Long)
  external fun nativeVideoFitCurves(before: Bitmap, after: Bitmap, out: ByteArray)
  /** [previous] (ALPHA_8) becomes the steadied version of [current] (same size). */
  external fun nativeVideoSteadyMask(previous: Bitmap, current: Bitmap, smoothing: Float)
  /** ARGB_8888 frame → encoder YUV 4:2:0 planes (direct buffers, with their strides). */
  external fun nativeRgbaToYuv420(
    bitmap: Bitmap, y: java.nio.ByteBuffer, u: java.nio.ByteBuffer, v: java.nio.ByteBuffer,
    yRowStride: Int, uvRowStride: Int, uvPixelStride: Int,
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

  /** Places a prop on [image]; [params] = cx, cy, width, rotation, flip, opacity, blend, shadow. Returns covered px. */
  external fun nativePlaceProp(image: Bitmap, prop: Bitmap, params: FloatArray, token: Long): Long

  /** Renders [photo] framed into [canvas] (see FrameStyle.floats() / ints()). */
  external fun nativeFrame(photo: Bitmap, canvas: Bitmap, floats: FloatArray, ints: IntArray, token: Long)

  /** Composites [stamp] at (x, y); returns the stamp pixels left clearly visible. */
  external fun nativeOverlay(image: Bitmap, stamp: Bitmap, x: Int, y: Int, opacity: Float): Long

  /** Tap-seeded cut-out; [points] = x0,y0,x1,y1… (0..1). Returns mask bytes, size in [sizeOut]. */
  external fun nativeSubjectMask(bitmap: Bitmap, points: FloatArray, sizeOut: IntArray): ByteArray

  /** A photo's look as [STYLE_FLOATS] numbers (see pp_style). */
  external fun nativeStyleAnalyze(bitmap: Bitmap): FloatArray
  external fun nativeStyleApply(bitmap: Bitmap, style: FloatArray, strength: Float, token: Long)

  const val STYLE_FLOATS = 20
}
