package com.photoplus.video

import android.graphics.Bitmap
import com.photoplus.ml.ModelRegistry
import com.photoplus.ml.ModelRuntime
import kotlin.math.roundToInt

/**
 * The person's soft matte (outline and hair) from the portrait matting model
 * ([ModelRegistry.MATTE]), for Glide. The frame is letterboxed into the
 * model's square input (never stretched: a squeezed person confuses it) and
 * the matte cropped back to the frame's shape. Null when the model is not
 * bundled, cannot load, or fails: Glide then uses the person segmenter alone.
 */
object PersonMatte {
  class Matte(val bytes: ByteArray, val width: Int, val height: Int)

  fun estimate(frame: Bitmap): Matte? = ModelRuntime.use(ModelRegistry.MATTE) { session ->
    val side = ModelRegistry.MATTE.input.width
    val raw = session.run(frame)
    if (raw.size < side * side) error("unexpected matte output size ${raw.size}")
    crop(raw, side, session.lastPlacement, frame.width, frame.height)
  }

  /**
   * The part of a [side] x [side] matte where a [width] x [height] frame landed
   * ([placement] = scaleX, scaleY, offsetX, offsetY), as bytes. Pure (unit-tested).
   */
  fun crop(raw: FloatArray, side: Int, placement: FloatArray, width: Int, height: Int): Matte {
    val x0 = placement[2].roundToInt().coerceIn(0, side - 1)
    val y0 = placement[3].roundToInt().coerceIn(0, side - 1)
    val w = (width * placement[0]).roundToInt().coerceIn(1, side - x0)
    val h = (height * placement[1]).roundToInt().coerceIn(1, side - y0)
    val bytes = ByteArray(w * h)
    for (y in 0 until h) {
      val row = (y0 + y) * side + x0
      for (x in 0 until w) {
        val v = raw[row + x]
        bytes[y * w + x] = (if (v.isFinite()) (v.coerceIn(0f, 1f) * 255f).roundToInt() else 0).toByte()
      }
    }
    return Matte(bytes, w, h)
  }
}
