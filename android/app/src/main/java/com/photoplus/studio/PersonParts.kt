package com.photoplus.studio

import android.content.Context
import android.graphics.Bitmap
import android.util.Log
import androidx.core.graphics.scale
import com.google.mediapipe.framework.image.BitmapImageBuilder
import com.google.mediapipe.framework.image.ByteBufferExtractor
import com.google.mediapipe.tasks.core.BaseOptions
import com.google.mediapipe.tasks.core.Delegate
import com.google.mediapipe.tasks.vision.core.RunningMode
import com.google.mediapipe.tasks.vision.imagesegmenter.ImageSegmenter

/**
 * A map of a person's parts (MediaPipe Selfie Multiclass, on-device): 0
 * background, 1 hair, 2 body skin, 3 face skin, 4 clothes, 5 accessories.
 * The studio cut-out lets colour decide where hair is fine and stays firm
 * on clothes. The model runs at 256 px whatever the photo, so it is given a
 * small copy.
 */
object PersonParts {
  private const val TAG = "PhotoPlusStudio"
  const val MODEL = "selfie_multiclass_256x256.tflite"
  private const val EDGE = 768

  data class Map(val bytes: ByteArray, val width: Int, val height: Int)

  fun available(context: Context): Boolean = try {
    context.assets.list("")?.contains(MODEL) == true
  } catch (_: Exception) {
    false
  }

  /** The parts map of an upright photo, or null (no model packaged, or it failed). */
  fun segment(context: Context, photo: Bitmap): Map? {
    if (!available(context)) return null
    val k = minOf(1f, EDGE.toFloat() / maxOf(photo.width, photo.height))
    val small = if (k < 1f) photo.scale(maxOf(1, (photo.width * k).toInt()), maxOf(1, (photo.height * k).toInt())) else photo
    var segmenter: ImageSegmenter? = null
    return try {
      segmenter = ImageSegmenter.createFromOptions(
        context,
        ImageSegmenter.ImageSegmenterOptions.builder()
          .setBaseOptions(BaseOptions.builder().setModelAssetPath(MODEL).setDelegate(Delegate.CPU).build())
          .setRunningMode(RunningMode.IMAGE)
          .setOutputCategoryMask(true)
          .setOutputConfidenceMasks(false)
          .build(),
      )
      val result = segmenter.segment(BitmapImageBuilder(small).build())
      val mask = result.categoryMask().orElse(null) ?: return null
      val buffer = ByteBufferExtractor.extract(mask)
      val bytes = ByteArray(mask.width * mask.height)
      buffer.rewind()
      buffer.get(bytes, 0, minOf(bytes.size, buffer.remaining()))
      Map(bytes, mask.width, mask.height)
    } catch (e: Exception) {
      Log.w(TAG, "person parts unavailable", e)
      null
    } finally {
      segmenter?.close()
      if (small !== photo) small.recycle()
    }
  }
}
