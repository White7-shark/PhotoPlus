package com.photoplus.studio

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * The artist's signature (Premium): the person's name — and, if they like,
 * a second line for their brand or studio and the year — written into the
 * corner of the drawing or painting, as a master signed their work: in a
 * flowing script, an engraved serif or neat print capitals, in the medium's
 * own ink (graphite, charcoal, red chalk, or paint), light on a dark corner
 * and dark on a light one.
 */
object ArtSignature {
  data class Spec(
    val text: String,
    val line2: String?,
    val style: String,   // "script" | "serif" | "print"
    val position: String, // "right" | "left" | "center"
    val size: Float,     // 0..1
    val year: Boolean,
  )

  private val STYLES = setOf("script", "serif", "print")
  private val POSITIONS = setOf("right", "left", "center")

  /** The person's signature from { text, line2, style, position, size, year }; null when there's nothing to sign. */
  fun parse(raw: Any?): Spec? {
    val m = raw as? Map<*, *> ?: return null
    val text = clean(m["text"]) ?: return null
    return Spec(
      text = text,
      line2 = clean(m["line2"]),
      style = (m["style"] as? String)?.takeIf { it in STYLES } ?: "script",
      position = (m["position"] as? String)?.takeIf { it in POSITIONS } ?: "right",
      size = ((m["size"] as? Number)?.toFloat() ?: 0.5f).let { if (it.isFinite()) it.coerceIn(0f, 1f) else 0.5f },
      year = m["year"] as? Boolean ?: false,
    )
  }

  /** Letters, numbers, spaces and common marks only, at most 40 of them. */
  private fun clean(v: Any?): String? =
    (v as? String)?.replace(Regex("[\\p{Cntrl}]"), "")?.replace(Regex("\\s+"), " ")?.trim()?.take(40)?.takeIf { it.isNotEmpty() }

  /**
   * Writes the signature into [bmp] (in place). [medium]: the pencil's
   * (0 graphite, 1 charcoal, 2 red chalk), or -1 for a painting.
   */
  fun apply(bmp: Bitmap, spec: Spec, medium: Int, year: Int) {
    val short = min(bmp.width, bmp.height).toFloat()
    val size = short * (0.032f + 0.03f * spec.size)
    val face = when (spec.style) {
      "serif" -> Typeface.create(Typeface.SERIF, Typeface.ITALIC)
      "print" -> Typeface.create("sans-serif-condensed", Typeface.BOLD)
      else -> Typeface.create("cursive", Typeface.NORMAL)
    }
    val main = Paint(Paint.ANTI_ALIAS_FLAG or Paint.SUBPIXEL_TEXT_FLAG).apply {
      typeface = face
      textSize = size
      if (spec.style == "print") letterSpacing = 0.12f
    }
    val second = Paint(main).apply {
      typeface = Typeface.create(Typeface.SERIF, Typeface.NORMAL)
      textSize = size * 0.46f
      letterSpacing = 0.18f
    }
    val first = if (spec.year) "${spec.text}  $year" else spec.text
    val title = if (spec.style == "print") first.uppercase() else first
    val sub = spec.line2?.uppercase()
    val w1 = main.measureText(title)
    val w2 = sub?.let { second.measureText(it) } ?: 0f
    val width = max(w1, w2)
    val margin = short * 0.05f
    val lineGap = size * 0.25f
    val height = size + (if (sub != null) lineGap + second.textSize else 0f)
    val x = when (spec.position) {
      "left" -> margin
      "center" -> (bmp.width - width) / 2f
      else -> bmp.width - margin - width
    }.coerceIn(2f, max(2f, bmp.width - width - 2f))
    val top = (bmp.height - margin - height).coerceAtLeast(2f)
    // Light ink on a dark corner, dark on a light one.
    val dark = meanLuma(bmp, x.roundToInt(), top.roundToInt(), (x + width).roundToInt(), (top + height).roundToInt()) > 0.42f
    val ink = when {
      !dark -> Color.argb(225, 242, 234, 214)
      medium == 1 -> Color.argb(235, 22, 20, 18)
      medium == 2 -> Color.argb(230, 140, 58, 34)
      medium == 0 -> Color.argb(225, 46, 46, 52)
      else -> Color.argb(230, 42, 29, 20)
    }
    val canvas = Canvas(bmp)
    canvas.save()
    // A hand-written signature leans a little.
    if (spec.style == "script") canvas.rotate(-4f, x + width / 2, top + height / 2)
    // Written twice, a hair apart and fainter: the softness of a real pencil or brush mark.
    for ((dx, alpha) in listOf(0.6f to 0.35f, 0f to 1f)) {
      main.color = ink
      main.alpha = (Color.alpha(ink) * alpha).roundToInt()
      val tx = when (spec.position) {
        "left" -> x
        "center" -> x + (width - w1) / 2
        else -> x + width - w1
      }
      canvas.drawText(title, tx + dx, top + size * 0.82f + dx * 0.5f, main)
      if (sub != null) {
        second.color = ink
        second.alpha = (Color.alpha(ink) * alpha * 0.9f).roundToInt()
        val sx = when (spec.position) {
          "left" -> x
          "center" -> x + (width - w2) / 2
          else -> x + width - w2
        }
        canvas.drawText(sub, sx + dx, top + size + lineGap + second.textSize * 0.85f, second)
      }
    }
    canvas.restore()
  }

  private fun meanLuma(bmp: Bitmap, x0: Int, y0: Int, x1: Int, y1: Int): Float {
    val ax = x0.coerceIn(0, bmp.width - 1)
    val ay = y0.coerceIn(0, bmp.height - 1)
    val bx = x1.coerceIn(ax + 1, bmp.width)
    val by = y1.coerceIn(ay + 1, bmp.height)
    var sum = 0.0
    var n = 0
    val step = max(1, (bx - ax) / 24)
    var y = ay
    while (y < by) {
      var xx = ax
      while (xx < bx) {
        val c = bmp.getPixel(xx, y)
        sum += (0.299 * Color.red(c) + 0.587 * Color.green(c) + 0.114 * Color.blue(c)) / 255.0
        n++
        xx += step
      }
      y += step
    }
    return if (n == 0) 1f else (sum / n).toFloat()
  }
}
