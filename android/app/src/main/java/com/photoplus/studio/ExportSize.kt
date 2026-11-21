package com.photoplus.studio

import android.graphics.Bitmap
import androidx.core.graphics.scale
import com.photoplus.engine.EngineException
import com.photoplus.engine.ImageCodec
import com.photoplus.engine.OutputFormat
import java.io.File
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * The download size of a studio photo, as its short edge: 360p and 480p on
 * every plan; 720p, 1080p, HD (1440p) and 4K (2160p) with Premium.
 */
object ExportSize {
  val SIZES = listOf(360, 480, 720, 1080, 1440, 2160)
  /** The largest size on the free plan. */
  const val FREE_MAX = 480
  /** No side beyond this (a long film strip at 4K stays within memory and the decoder's limits). */
  const val MAX_EDGE = 8192

  /** The size asked for (short edge, px), or null for the photo as made. */
  fun parse(v: Any?): Int? {
    if (v == null) return null
    val px = (v as? Number)?.toInt() ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown download size")
    if (px !in SIZES) throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown download size")
    return px
  }

  fun needsPremium(px: Int?): Boolean = px != null && px > FREE_MAX

  /** The size [w] x [h] becomes: its short edge [px], its long edge at most [MAX_EDGE]. */
  fun target(w: Int, h: Int, px: Int): Pair<Int, Int> {
    var k = px.toFloat() / min(w, h)
    if (max(w, h) * k > MAX_EDGE) k = MAX_EDGE.toFloat() / max(w, h)
    return max(1, (w * k).roundToInt()) to max(1, (h * k).roundToInt())
  }

  /** [bitmap] at the asked size (a new bitmap; the same one when it already is that size). */
  fun scaled(bitmap: Bitmap, px: Int?): Bitmap {
    if (px == null) return bitmap
    val (w, h) = target(bitmap.width, bitmap.height, px)
    return if (w == bitmap.width && h == bitmap.height) bitmap else bitmap.scale(w, h, true)
  }

  /** Rewrites a saved photo at the asked size, keeping its format. */
  fun applyToFile(file: File, px: Int?) {
    if (px == null) return
    val decoded = ImageCodec.decode(file, MAX_EDGE).bitmap
    try {
      val out = scaled(decoded, px)
      try {
        if (out !== decoded) {
          val png = file.extension.equals("png", ignoreCase = true)
          ImageCodec.encode(out, file, if (png) OutputFormat.PNG else OutputFormat.JPEG, 95)
        }
      } finally {
        if (out !== decoded) out.recycle()
      }
    } finally {
      decoded.recycle()
    }
  }
}
