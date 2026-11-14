package com.photoplus.video

import android.graphics.Bitmap
import android.graphics.Matrix
import android.media.MediaMetadataRetriever
import android.util.Log
import androidx.core.graphics.scale
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.segmentation.Segmentation
import com.google.mlkit.vision.segmentation.selfie.SelfieSegmenterOptions
import com.photoplus.studio.PortraitProcessor
import java.io.File
import java.nio.ByteOrder
import java.util.concurrent.TimeUnit
import kotlin.math.max
import kotlin.math.roundToInt

/** Finds where the person usually is in a clip: a dozen frames, the on-device person model, [GlideSubject]. */
object GlideSubjectFinder {
  private const val TAG = "PhotoPlusGlide"
  private const val SAMPLES = 12
  private const val EDGE = 512

  /** The summary (or null when nobody was found) and the clip's upright width / height. */
  fun find(file: File): Pair<GlideSubject.Summary?, Float> {
    val retriever = MediaMetadataRetriever()
    val segmenter = Segmentation.getClient(SelfieSegmenterOptions.Builder().setDetectorMode(SelfieSegmenterOptions.SINGLE_IMAGE_MODE).build())
    try {
      retriever.setDataSource(file.absolutePath)
      val rotation = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION)?.toIntOrNull() ?: 0
      val codedW = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)?.toIntOrNull() ?: 0
      val codedH = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)?.toIntOrNull() ?: 0
      val total = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_FRAME_COUNT)?.toIntOrNull() ?: 0
      val turned = rotation == 90 || rotation == 270
      val aspect = if (codedW > 0 && codedH > 0) (if (turned) codedH.toFloat() / codedW else codedW.toFloat() / codedH) else 9f / 16f
      if (total <= 0) return null to aspect
      val boxes = ArrayList<Pair<FloatArray, BooleanArray>>()
      val count = minOf(SAMPLES, total)
      for (k in 0 until count) {
        val index = ((k + 0.5f) * total / count).toInt().coerceIn(0, total - 1)
        val raw = retriever.getFrameAtIndex(index) ?: continue
        val upright = if (turned && raw.width == codedW && raw.height == codedH && codedW != codedH) {
          Bitmap.createBitmap(raw, 0, 0, raw.width, raw.height, Matrix().apply { postRotate(rotation.toFloat()) }, false).also { raw.recycle() }
        } else {
          raw
        }
        val s = EDGE.toFloat() / max(upright.width, upright.height)
        val small = upright.scale(max(1, (upright.width * s).roundToInt()), max(1, (upright.height * s).roundToInt()))
        if (small !== upright) upright.recycle()
        try {
          val result = Tasks.await(segmenter.process(InputImage.fromBitmap(small, 0)), 5, TimeUnit.SECONDS)
          val floats = result.buffer.order(ByteOrder.nativeOrder()).asFloatBuffer()
          val bytes = PortraitProcessor.floatsToBytes(floats, result.width * result.height)
          GlideSubject.frameBox(bytes, result.width, result.height)?.let { boxes.add(it) }
        } catch (e: Exception) {
          Log.w(TAG, "person segmentation unavailable", e)
        } finally {
          small.recycle()
        }
      }
      return GlideSubject.summarise(boxes) to aspect
    } finally {
      segmenter.close()
      retriever.release()
    }
  }
}
