package com.photoplus.camera

import android.graphics.Bitmap
import android.util.Log
import androidx.core.graphics.scale
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.segmentation.Segmentation
import com.google.mlkit.vision.segmentation.selfie.SelfieSegmenterOptions
import com.photoplus.studio.PortraitProcessor
import java.nio.ByteOrder
import java.util.concurrent.TimeUnit
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Where the person is, for Lively: the person gets the full finish, the
 * surroundings a gentle one (walls keep their own colour; the far background
 * is not deepened). A small mask (at most [EDGE] px) from the on-device
 * person segmenter; null when no person is found or the segmenter is
 * unavailable (Lively then treats the whole photo alike).
 */
object LivelyPerson {
  data class Mask(val bytes: ByteArray, val width: Int, val height: Int)

  private const val EDGE = 512
  private const val TIMEOUT_S = 5L
  /** Less than this share of the photo is no person (a stray patch). */
  private const val MIN_COVER = 0.01f

  private val segmenter by lazy {
    Segmentation.getClient(SelfieSegmenterOptions.Builder().setDetectorMode(SelfieSegmenterOptions.SINGLE_IMAGE_MODE).build())
  }

  fun find(photo: Bitmap): Mask? = try {
    val scale = EDGE.toFloat() / max(photo.width, photo.height)
    val small = if (scale < 1f) photo.scale(max(1, (photo.width * scale).roundToInt()), max(1, (photo.height * scale).roundToInt())) else photo
    try {
      val result = Tasks.await(segmenter.process(InputImage.fromBitmap(small, 0)), TIMEOUT_S, TimeUnit.SECONDS)
      val n = result.width * result.height
      val bytes = PortraitProcessor.floatsToBytes(result.buffer.order(ByteOrder.nativeOrder()).asFloatBuffer(), n)
      val cover = bytes.count { (it.toInt() and 0xFF) > 127 }.toFloat() / n.coerceAtLeast(1)
      if (cover < MIN_COVER) null else Mask(bytes, result.width, result.height)
    } finally {
      if (small !== photo) small.recycle()
    }
  } catch (e: Exception) {
    Log.w("PhotoPlusLively", "person not found; the whole photo gets one finish", e)
    null
  }
}
