package com.photoplus.studio

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Matrix
import android.os.SystemClock
import android.util.Log
import com.photoplus.ml.ModelRegistry
import com.photoplus.ml.ModelRuntime

/**
 * Spotlight's sense of depth: how far every part of the photo is from the
 * camera, estimated on-device from a single photo (no depth sensor needed).
 * With it the background blurs progressively with distance like a real lens,
 * while the ground the subject stands on stays sharp.
 *
 * The model runs through the common [ModelRuntime] (GPU when it is verified,
 * else CPU; input prepared natively; released under memory pressure). If it
 * is missing or fails, [estimate] returns null and the engine falls back to
 * its own ground-plane estimate, so Spotlight always works.
 */
object DepthEstimator {
  private const val TAG = "PhotoPlusDepth"
  const val ASSET = "spotlight_depth.tflite"

  /** Called once from the Application. */
  fun install(context: Context) = ModelRuntime.install(context)

  /** A depth map, 0 far … 255 near, row-major [width] x [height]. */
  class DepthMap(val bytes: ByteArray, val width: Int, val height: Int)

  /** Depth of [upright] (the photo the right way up), in the same orientation, or null. */
  fun estimate(upright: Bitmap): DepthMap? = ModelRuntime.use(ModelRegistry.DEPTH) { session ->
    val started = SystemClock.elapsedRealtime()
    // The model expects landscape: turn portrait photos so nothing is squeezed.
    val portrait = upright.height > upright.width
    val turned = if (portrait) {
      Bitmap.createBitmap(upright, 0, 0, upright.width, upright.height, Matrix().apply { postRotate(-90f) }, true)
    } else {
      upright
    }
    val raw = try {
      session.run(turned)
    } finally {
      if (turned !== upright) turned.recycle()
    }
    val n = DepthMath.IN_W * DepthMath.IN_H
    if (raw.size < n) error("unexpected depth output size ${raw.size}")
    val bytes = DepthMath.toBytes(raw, n) ?: return@use null
    // Back to upright (undo the landscape turn). The engine samples masks
    // at any size, so the map stays at the model's resolution.
    val (b, w, h) = if (portrait) {
      MaskGeometry.rotateMask(bytes, DepthMath.IN_W, DepthMath.IN_H, 90)
    } else {
      Triple(bytes, DepthMath.IN_W, DepthMath.IN_H)
    }
    Log.i(TAG, "depth in ${SystemClock.elapsedRealtime() - started} ms on ${session.accelerator}")
    DepthMap(b, w, h)
  }

  /** Frees the model (memory pressure); it loads again on next use. */
  fun release() = ModelRuntime.releaseAll()
}
