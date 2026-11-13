package com.photoplus.video

import android.graphics.Bitmap
import android.graphics.Matrix
import android.media.MediaMetadataRetriever
import android.os.SystemClock
import android.util.Log
import androidx.core.graphics.createBitmap
import androidx.core.graphics.scale
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.face.FaceDetection
import com.google.mlkit.vision.face.FaceDetectorOptions
import com.google.mlkit.vision.segmentation.Segmentation
import com.google.mlkit.vision.segmentation.selfie.SelfieSegmenterOptions
import com.photoplus.engine.EngineException
import com.photoplus.engine.NativeEngine
import com.photoplus.studio.FaceLandmarks
import com.photoplus.studio.PortraitProcessor
import java.io.File
import java.nio.ByteOrder
import java.util.concurrent.TimeUnit
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Glide: turns a short clip into a steady, clean, pro-looking video.
 *
 *   pass 1  every frame: camera motion (on a small copy) and, when Spotlight
 *           or Glow is on, the subject and face landmarks (on-device models)
 *   once    the clip's noise and blur (middle frame), its tone (the photo
 *           pipeline's automatic white balance and levels, fitted as curves),
 *           and the steadying plan
 *   pass 2  every frame: temporal denoise + lens deblur + tone, Spotlight and
 *           Glow with a steadied subject mask and smoothed landmarks, the
 *           steadying warp, then H.264 with the original sound
 *
 * Memory stays bounded: at most three full frames are held at a time.
 */
class GlideProcessor {
  fun interface Progress {
    fun onProgress(stage: String, fraction: Double)
  }

  data class Result(
    val width: Int,
    val height: Int,
    val frames: Int,
    val fps: Float,
    val durationMs: Long,
    val zoom: Float,
    val shakeBefore: Float,
    val shakeAfter: Float,
    val processMs: Long,
  )

  private val segmenter by lazy {
    Segmentation.getClient(SelfieSegmenterOptions.Builder().setDetectorMode(SelfieSegmenterOptions.STREAM_MODE).build())
  }
  private val faceDetector by lazy {
    FaceDetection.getClient(
      FaceDetectorOptions.Builder()
        .setPerformanceMode(FaceDetectorOptions.PERFORMANCE_MODE_FAST)
        .setContourMode(FaceDetectorOptions.CONTOUR_MODE_ALL)
        .setLandmarkMode(FaceDetectorOptions.LANDMARK_MODE_ALL)
        .setMinFaceSize(0.1f)
        .build(),
    )
  }

  /** [decorate] runs on every finished frame just before encoding (e.g. the free plan's mark). */
  fun process(
    input: File,
    output: File,
    options: GlideOptions,
    token: Long,
    progress: Progress,
    decorate: ((Bitmap) -> Unit)? = null,
  ): Result {
    val started = SystemClock.elapsedRealtime()
    val source = FrameSource(input)
    try {
      val n = source.count
      if (n < 2) throw EngineException(EngineException.INVALID_ARGUMENT, "The clip is too short")
      val w = source.width
      val h = source.height
      val analysisScale = ANALYSIS_EDGE.toFloat() / max(w, h)
      val aw = (w * analysisScale).roundToInt().coerceAtLeast(16)
      val ah = (h * analysisScale).roundToInt().coerceAtLeast(16)

      // ---- Pass 1: motion, subject, faces ----
      val motions = FloatArray(n * 5).also { it[0] = 1f; it[4] = 1f }
      val masks = arrayOfNulls<ByteArray>(n)
      val faces = arrayOfNulls<List<FaceLandmarks>>(n)
      val smoother = LandmarkSmoother()
      var prevSmall: Bitmap? = null
      val needSubject = options.portrait
      val needFaces = options.portrait || options.faces
      val motion = FloatArray(5)
      for (i in 0 until n) {
        val frame = source.frame(i)
        val small = frame.scale(aw, ah)
        source.release(i - 1)
        prevSmall?.let {
          NativeEngine.nativeVideoMotion(it, small, motion)
          // The small copy's shift, in full-size pixels.
          motions[i * 5] = motion[0]
          motions[i * 5 + 1] = motion[1]
          motions[i * 5 + 2] = motion[2] / analysisScale
          motions[i * 5 + 3] = motion[3] / analysisScale
          motions[i * 5 + 4] = motion[4]
          it.recycle()
        }
        if (needSubject) masks[i] = subjectMask(small)
        if (needFaces) {
          val found = facesIn(small)
          // Steady the main face's landmarks over time (the others pass through).
          val main = smoother.next(found.firstOrNull()?.points)
          faces[i] = if (main != null) listOf(FaceLandmarks(main, found.first().detailed)) + found.drop(1) else emptyList()
        }
        prevSmall = small
        progress.onProgress("analysing", 0.25 * (i + 1) / n)
      }
      prevSmall?.recycle()
      source.releaseAll()

      // ---- The clip's measurements and tone, once ----
      progress.onProgress("measuring", 0.27)
      val mid = source.frame(n / 2)
      val quality = NativeEngine.nativeAnalyzeQuality(mid)
      val noise = quality[0]
      val blur = if (quality[1] > 0f) quality[1] else 0.6f
      val curves = ByteArray(768)
      run {
        val a = createBitmap(w, h).apply { isPremultiplied = false }
        val b = createBitmap(w, h).apply { isPremultiplied = false }
        val report = FloatArray(7)
        val params = floatArrayOf(0f, 0f, 0f, 0f) // tone only
        NativeEngine.nativeEnhance(arrayOf(mid), 1, params, false, a, token, report)
        NativeEngine.nativeEnhance(arrayOf(mid), 1, params, true, b, token, report)
        NativeEngine.nativeVideoFitCurves(a, b, curves)
        a.recycle()
        b.recycle()
      }
      source.releaseAll()
      val warps = FloatArray(n * 6)
      val plan = FloatArray(3)
      NativeEngine.nativeVideoStabilize(motions, w, h, options.steady, options.maxZoom, warps, plan)

      // ---- Pass 2: every frame, encoded as it is finished ----
      val fps = source.fps
      val encoder = GlideEncoder(output, w, h, fps, input)
      try {
        val frameParams = floatArrayOf(noise, blur, options.denoise, options.sharpen)
        val portraitParams = floatArrayOf(
          options.blur, options.retouch, options.brighten, if (options.spotlight) options.pop else 0f,
          -1f, -1f, -1f, 0f, 0f, 0f,
        )
        val work = createBitmap(w, h).apply { isPremultiplied = false }
        val out = createBitmap(w, h).apply { isPremultiplied = false }
        var steady: Bitmap? = null
        // The room remembered behind the person: pieces of it the model
        // swallows for many frames are given back (no shadow growing from the
        // person and shrinking back).
        val room = if (options.portrait) NativeEngine.nativeVideoRoomCreate() else 0L
        val fromPrev = FloatArray(5)
        val toNext = FloatArray(5)
        val report = FloatArray(NativeEngine.PORTRAIT_REPORT_SIZE)
        val faceReport = FloatArray(NativeEngine.FACE_REPORT_SIZE)
        val beautyReport = FloatArray(NativeEngine.BEAUTY_REPORT_SIZE)
        try {
          for (i in 0 until n) {
            val prev = if (i > 0) source.frame(i - 1) else null
            val cur = source.frame(i)
            val next = if (i + 1 < n) source.frame(i + 1) else null
            source.release(i - 2)
            motions.copyInto(fromPrev, 0, i * 5, i * 5 + 5)
            if (next != null) motions.copyInto(toNext, 0, (i + 1) * 5, (i + 1) * 5 + 5)
            NativeEngine.nativeVideoEnhanceFrame(
              cur, prev, next, if (prev != null) fromPrev else null, if (next != null) toNext else null,
              frameParams, curves, work, token,
            )
            val landmarks = faces[i].orEmpty()
            val mask = masks[i]
            if (options.portrait && mask != null) {
              // Three frames' vote first (a one-frame mistake never shows),
              // then the room memory, then steadying.
              val voted = MaskVote.median(masks.getOrNull(i - 1), mask, masks.getOrNull(i + 1))
              val subject = PortraitProcessor.alphaBitmap(voted, aw, ah)
              if (room != 0L) NativeEngine.nativeVideoRoomCheck(room, cur, subject, if (prev != null) fromPrev else null, token)
              val held = steady
              if (held == null) {
                steady = subject
              } else {
                NativeEngine.nativeVideoSteadyMask(held, subject, STEADY)
                subject.recycle()
              }
              val (faceMask, protect) = if (landmarks.isEmpty()) null to null else FaceMasks.draw(landmarks, aw, ah)
              try {
                val recipe = options.recipe
                if (recipe != null && options.beautyLook) {
                  // The beauty preset: skin, face, light, background and colour in one pass.
                  val (pts, detailed) = FaceLandmarks.pack(landmarks.take(4))
                  NativeEngine.nativeBeauty(work, steady!!, faceMask, protect, null, pts, detailed, recipe.toArray(), token, beautyReport)
                } else {
                  NativeEngine.nativePortrait(work, steady!!, faceMask, protect, null, portraitParams, token, report)
                }
              } finally {
                faceMask?.recycle()
                protect?.recycle()
              }
            }
            if (options.faces && !options.beautyLook && landmarks.isNotEmpty()) {
              val (pts, detailed) = FaceLandmarks.pack(landmarks.take(4))
              NativeEngine.nativeFaceAdjust(work, pts, detailed, options.beauty.toArray(), token, faceReport)
            }
            NativeEngine.nativeVideoWarp(work, warps.copyOfRange(i * 6, i * 6 + 6), out, token)
            decorate?.invoke(out)
            encoder.encode(out, GlideOptions.frameTimeUs(i, fps))
            progress.onProgress("crafting", 0.3 + 0.68 * (i + 1) / n)
          }
        } finally {
          work.recycle()
          out.recycle()
          steady?.recycle()
          if (room != 0L) NativeEngine.nativeVideoRoomDestroy(room)
          source.releaseAll()
        }
        encoder.finish(GlideOptions.frameTimeUs(n, fps))
      } catch (e: Throwable) {
        encoder.close()
        output.delete()
        throw e
      }
      progress.onProgress("done", 1.0)
      return Result(
        w, h, n, fps, (n * 1000 / fps).toLong(), plan[0], plan[1], plan[2],
        SystemClock.elapsedRealtime() - started,
      )
    } finally {
      source.close()
    }
  }

  private fun subjectMask(small: Bitmap): ByteArray? {
    val result = runCatching { Tasks.await(segmenter.process(InputImage.fromBitmap(small, 0)), ML_TIMEOUT_S, TimeUnit.SECONDS) }
      .onFailure { Log.w(TAG, "person segmentation unavailable", it) }.getOrNull() ?: return null
    val floats = result.buffer.order(ByteOrder.nativeOrder()).asFloatBuffer()
    val bytes = PortraitProcessor.floatsToBytes(floats, result.width * result.height)
    return if (result.width == small.width && result.height == small.height) bytes
    else PortraitProcessor.resizeNearest(bytes, result.width, result.height, small.width, small.height)
  }

  private fun facesIn(small: Bitmap): List<FaceLandmarks> {
    val found = runCatching { Tasks.await(faceDetector.process(InputImage.fromBitmap(small, 0)), ML_TIMEOUT_S, TimeUnit.SECONDS) }
      .getOrNull().orEmpty()
    return found.sortedByDescending { it.boundingBox.width() }
      .mapNotNull { PortraitProcessor.landmarksOf(it, small.width.toFloat(), small.height.toFloat()) }
  }

  /**
   * Frames of the clip, upright, decoded a few at a time and kept only while
   * needed (each 1080p frame is ~8 MB).
   */
  private class FrameSource(file: File) : AutoCloseable {
    private val retriever = MediaMetadataRetriever().apply { setDataSource(file.absolutePath) }
    private val rotation = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION)?.toIntOrNull() ?: 0
    private val codedW = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)?.toIntOrNull() ?: 0
    private val codedH = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)?.toIntOrNull() ?: 0
    private val total = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_FRAME_COUNT)?.toIntOrNull() ?: 0
    private val durationMs = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
    private val cache = HashMap<Int, Bitmap>()

    val count = total.coerceAtMost(GlideOptions.MAX_FRAMES)
    val fps: Float = if (durationMs > 0 && total > 0) (total * 1000f / durationMs).coerceIn(10f, 60f) else 30f
    private val turned = rotation == 90 || rotation == 270
    val width = if (turned) codedH else codedW
    val height = if (turned) codedW else codedH

    fun frame(i: Int): Bitmap {
      cache[i]?.let { return it }
      val batch = minOf(BATCH, count - i)
      val params = MediaMetadataRetriever.BitmapParams().apply { preferredConfig = Bitmap.Config.ARGB_8888 }
      val decoded = retriever.getFramesAtIndex(i, batch, params)
      decoded.forEachIndexed { k, raw ->
        if (cache.containsKey(i + k)) raw.recycle() else cache[i + k] = upright(raw)
      }
      return cache[i] ?: throw EngineException(EngineException.IO_ERROR, "Frame $i could not be read")
    }

    /** Engine-ready: upright, mutable, straight alpha (video is opaque). */
    private fun upright(raw: Bitmap): Bitmap {
      // Some decoders apply the rotation themselves; turn only frames still in the coded orientation.
      val needsTurn = turned && raw.width == codedW && raw.height == codedH && codedW != codedH
      val out = if (needsTurn) {
        Bitmap.createBitmap(raw, 0, 0, raw.width, raw.height, Matrix().apply { postRotate(rotation.toFloat()) }, false)
      } else {
        raw.copy(Bitmap.Config.ARGB_8888, true)
      }
      if (out !== raw) raw.recycle()
      out.isPremultiplied = false
      check(out.width == width && out.height == height) { "unexpected frame size ${out.width}x${out.height}" }
      return out
    }

    fun release(i: Int) {
      val keys = cache.keys.filter { it <= i }
      for (k in keys) cache.remove(k)?.recycle()
    }

    fun releaseAll() {
      cache.values.forEach { it.recycle() }
      cache.clear()
    }

    override fun close() {
      releaseAll()
      retriever.release()
    }
  }

  private companion object {
    const val TAG = "PhotoPlusGlide"
    const val ANALYSIS_EDGE = 640
    const val ML_TIMEOUT_S = 5L
    const val BATCH = 6
    /** Subject mask steadiness (0..0.9). */
    const val STEADY = 0.6f
  }
}
