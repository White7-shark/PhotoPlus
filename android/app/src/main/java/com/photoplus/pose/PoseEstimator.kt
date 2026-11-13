package com.photoplus.pose

import android.content.Context
import android.graphics.Bitmap
import android.graphics.ImageDecoder
import androidx.core.net.toUri
import android.util.Log
import com.google.mediapipe.framework.image.BitmapImageBuilder
import com.google.mediapipe.framework.image.MPImage
import com.google.mediapipe.tasks.components.containers.NormalizedLandmark
import com.google.mediapipe.tasks.core.BaseOptions
import com.google.mediapipe.tasks.core.Delegate
import com.google.mediapipe.tasks.vision.core.RunningMode
import com.google.mediapipe.tasks.vision.poselandmarker.PoseLandmarker
import com.google.mediapipe.tasks.vision.poselandmarker.PoseLandmarkerResult
import com.photoplus.engine.NativeEngine
import java.io.File
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * MediaPipe's pose landmarker, on-device: the "lite" model follows the person
 * on the live camera; the "full" one reads the pose in the user's photos. Both
 * try the GPU first and fall back to the CPU.
 */
object PoseEstimator {
  private const val TAG = "PhotoPlusPose"
  const val LIVE_MODEL = "pose_landmarker_lite.task"
  const val PHOTO_MODEL = "pose_landmarker_full.task"
  private const val PHOTO_EDGE = 1280

  /** Whether the models were packaged (a build may leave them out). */
  fun available(context: Context): Boolean = try {
    context.assets.list("")?.let { LIVE_MODEL in it && PHOTO_MODEL in it } ?: false
  } catch (_: Exception) {
    false
  }

  private fun options(model: String, delegate: Delegate) =
    BaseOptions.builder().setModelAssetPath(model).setDelegate(delegate).build()

  /** The live landmarker; results arrive on MediaPipe's thread. */
  fun live(
    context: Context,
    gpu: Boolean,
    onResult: (PoseLandmarkerResult, MPImage) -> Unit,
    onError: (RuntimeException) -> Unit,
  ): PoseLandmarker {
    fun build(delegate: Delegate) = PoseLandmarker.createFromOptions(
      context,
      PoseLandmarker.PoseLandmarkerOptions.builder()
        .setBaseOptions(options(LIVE_MODEL, delegate))
        .setRunningMode(RunningMode.LIVE_STREAM)
        .setNumPoses(1)
        .setMinPoseDetectionConfidence(0.5f)
        .setMinPosePresenceConfidence(0.5f)
        .setMinTrackingConfidence(0.5f)
        .setResultListener { result, input -> onResult(result, input) }
        .setErrorListener { e -> onError(e) }
        .build(),
    )
    if (gpu) {
      try {
        return build(Delegate.GPU)
      } catch (e: Exception) {
        Log.w(TAG, "pose on the GPU unavailable, using the CPU", e)
      }
    }
    return build(Delegate.CPU)
  }

  /** The 33 landmarks as PoseData's flat layout. */
  fun flatten(points: List<NormalizedLandmark>): FloatArray? {
    if (points.size < PoseData.POINTS) return null
    val out = FloatArray(PoseData.FLOATS)
    for (i in 0 until PoseData.POINTS) {
      val p = points[i]
      out[i * 5] = p.x()
      out[i * 5 + 1] = p.y()
      out[i * 5 + 2] = p.z()
      out[i * 5 + 3] = p.visibility().orElse(0f)
      out[i * 5 + 4] = p.presence().orElse(0f)
    }
    return out
  }

  /**
   * The pose in a photo (content:// or file path): landmarks of the upright
   * image, its aspect, the shot and the body's scale. Blocking: call off the
   * main thread.
   */
  fun analyzePhoto(context: Context, uri: String): Map<String, Any?> {
    val bitmap = decode(context, uri)
    try {
      val aspect = bitmap.width.toFloat() / bitmap.height
      val points = detect(context, bitmap) ?: return mapOf("valid" to false, "aspect" to aspect.toDouble())
      val d = FloatArray(5)
      NativeEngine.nativePoseDescribe(points, aspect, d)
      return mapOf(
        "valid" to (d[0] > 0.5f),
        "aspect" to aspect.toDouble(),
        "landmarks" to points.map { it.toDouble() },
        "shot" to PoseStep.SHOTS.getOrNull(d[1].roundToInt()),
        "unit" to d[2].toDouble(),
      )
    } finally {
      bitmap.recycle()
    }
  }

  private fun detect(context: Context, bitmap: Bitmap): FloatArray? {
    fun run(delegate: Delegate): FloatArray? {
      val landmarker = PoseLandmarker.createFromOptions(
        context,
        PoseLandmarker.PoseLandmarkerOptions.builder()
          .setBaseOptions(options(PHOTO_MODEL, delegate))
          .setRunningMode(RunningMode.IMAGE)
          .setNumPoses(1)
          .setMinPoseDetectionConfidence(0.4f)
          .setMinPosePresenceConfidence(0.4f)
          .build(),
      )
      return landmarker.use { it.detect(BitmapImageBuilder(bitmap).build()).landmarks().firstOrNull()?.let(::flatten) }
    }
    return try {
      run(Delegate.GPU)
    } catch (e: Exception) {
      Log.w(TAG, "photo pose on the GPU unavailable, using the CPU", e)
      run(Delegate.CPU)
    }
  }

  private fun decode(context: Context, uri: String): Bitmap {
    val source = if (uri.startsWith("content:") || uri.startsWith("file:")) {
      ImageDecoder.createSource(context.contentResolver, uri.toUri())
    } else {
      ImageDecoder.createSource(File(uri))
    }
    // Upright (EXIF applied by the decoder), software ARGB, at most 1280 px.
    return ImageDecoder.decodeBitmap(source) { decoder, info, _ ->
      decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
      val longest = max(info.size.width, info.size.height)
      if (longest > PHOTO_EDGE) {
        val k = PHOTO_EDGE.toFloat() / longest
        decoder.setTargetSize((info.size.width * k).roundToInt(), (info.size.height * k).roundToInt())
      }
    }.let { if (it.config == Bitmap.Config.ARGB_8888) it else it.copy(Bitmap.Config.ARGB_8888, false).also { _ -> it.recycle() } }
  }
}
