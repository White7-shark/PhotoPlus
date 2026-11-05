package com.photoplus.edit

import android.content.Context
import android.content.res.AssetManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import androidx.core.graphics.createBitmap
import androidx.core.graphics.get
import com.photoplus.engine.NativeEngine
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Draws the watermark into a photo's pixels (never a screen overlay).
 *
 * The default "Signature" mark is a small lockup at the bottom of the photo:
 * the Photo Plus emblem, "Created with" in a handwritten script above a
 * bold "PhotoPlus" wordmark, with a gold swoosh beneath. Its ink adapts to
 * what is behind it (white with a soft shadow on darker areas, deep charcoal
 * on bright ones) so it stays readable without spoiling the photo.
 *
 * The mark is drawn with Canvas into a stamp, then composited by the engine,
 * which reports how much of it is visible (checked by [ExportPolicy.verify]).
 */
object WatermarkRenderer {
  @Volatile private var assets: AssetManager? = null
  private val typefaces = ConcurrentHashMap<WatermarkFont, Typeface>()
  @Volatile private var emblem: Bitmap? = null

  fun install(context: Context) {
    if (assets == null) assets = context.applicationContext.assets
  }

  private fun typeface(font: WatermarkFont): Typeface = typefaces.getOrPut(font) {
    assets?.let { runCatching { Typeface.createFromAsset(it, font.asset) }.getOrNull() } ?: Typeface.DEFAULT_BOLD
  }

  private fun brandEmblem(): Bitmap? = emblem ?: assets?.let { am ->
    runCatching { am.open("brand/emblem.png").use { BitmapFactory.decodeStream(it) } }.getOrNull()?.also { emblem = it }
  }

  /** Light or dark ink, chosen from the brightness behind the mark. */
  data class Ink(val text: Int, val shadow: Int, val dark: Boolean)

  fun inkFor(backgroundLuma: Float): Ink = if (backgroundLuma > 0.62f) {
    Ink(0xFF1F1F24.toInt(), 0x80FFFFFF.toInt(), dark = true)
  } else {
    Ink(0xFFFFFFFF.toInt(), 0x8C000000.toInt(), dark = false)
  }

  /**
   * Draws [mark] onto [target] (mutable ARGB_8888) and returns the visible
   * pixel count from the engine.
   */
  fun apply(target: Bitmap, mark: ResolvedWatermark): Long {
    val shortSide = minOf(target.width, target.height)
    val h = max(12f, mark.height * shortSide)
    val logo = mark.logoPath?.let { path -> runCatching { BitmapFactory.decodeFile(File(path).path) }.getOrNull() }
    val layout = measure(mark, h, logo)
    val (x, y) = WatermarkPlacement.origin(mark.position, layout.width, layout.height, target.width, target.height)
    val luma = meanLuma(target, x, y, layout.width, layout.height)
    val ink = if (mark.brand) inkFor(luma) else Ink(mark.color, if (luma > 0.62f) 0x66FFFFFF else 0x8C000000.toInt(), luma > 0.62f)
    val stamp = createBitmap(layout.width, layout.height, Bitmap.Config.ARGB_8888)
    try {
      draw(Canvas(stamp), mark, h, ink, layout, logo)
      return NativeEngine.nativeOverlay(target, stamp, x, y, mark.opacity)
    } finally {
      stamp.recycle()
      logo?.recycle()
    }
  }

  /** Mean brightness (0..1) of a region, from a sparse sample. */
  fun meanLuma(bitmap: Bitmap, x: Int, y: Int, w: Int, h: Int): Float {
    val step = max(1, minOf(w, h) / 24)
    var sum = 0.0
    var n = 0
    var yy = y.coerceAtLeast(0)
    while (yy < minOf(bitmap.height, y + h)) {
      var xx = x.coerceAtLeast(0)
      while (xx < minOf(bitmap.width, x + w)) {
        val c = bitmap[xx, yy]
        sum += (0.299 * ((c shr 16) and 0xFF) + 0.587 * ((c shr 8) and 0xFF) + 0.114 * (c and 0xFF)) / 255.0
        n++
        xx += step
      }
      yy += step
    }
    return if (n == 0) 0.5f else (sum / n).toFloat()
  }

  data class Layout(val width: Int, val height: Int, val textW: Float, val pad: Float)

  private fun textPaint(font: WatermarkFont, size: Float, ink: Int = 0xFFFFFFFF.toInt()) = Paint(Paint.ANTI_ALIAS_FLAG).apply {
    typeface = typeface(font)
    textSize = size
    color = ink
  }

  private fun measure(m: ResolvedWatermark, h: Float, logo: Bitmap?): Layout {
    val pad = 0.18f * h // room for the soft shadow
    val hasMark = m.showEmblem || logo != null
    return when (m.style) {
      WatermarkStyle.SIGNATURE -> {
        val line1 = textPaint(WatermarkFont.SIGNATURE, 0.42f * h).measureText(SIGNATURE_LINE)
        val line2 = textPaint(m.font, 0.5f * h).measureText(m.text.replace(" ", ""))
        val textW = max(line1, line2)
        val w = if (m.layout == WatermarkLayout.STACKED) max(textW, h) else (if (hasMark) h * 1.12f else 0f) + textW
        val hh = if (m.layout == WatermarkLayout.STACKED && hasMark) h * 1.75f else h
        Layout(ceil(w + 2 * pad).toInt(), ceil(hh + 2 * pad).toInt(), textW, pad)
      }
      WatermarkStyle.BADGE -> {
        val textW = textPaint(m.font, 0.3f * h).apply { letterSpacing = 0.15f }.measureText(m.text.uppercase())
        Layout(ceil(max(textW, 0.62f * h) + 2 * pad).toInt(), ceil(h + 2 * pad).toInt(), textW, pad)
      }
      WatermarkStyle.MINIMAL -> {
        val textW = textPaint(m.font, 0.55f * h).apply { letterSpacing = 0.25f }.measureText(m.text)
        Layout(ceil(textW + 2 * pad).toInt(), ceil(h + 2 * pad).toInt(), textW, pad)
      }
      else -> { // CLASSIC, PILL
        val textW = textPaint(m.font, 0.6f * h).measureText(m.text)
        val inner = (if (hasMark) h * 1.12f else 0f) + textW
        val extra = if (m.style == WatermarkStyle.PILL) 0.9f * h else 0f
        val w = if (m.layout == WatermarkLayout.STACKED) max(textW, h) + extra else inner + extra
        val hh = if (m.layout == WatermarkLayout.STACKED && hasMark) h * 1.8f else h
        Layout(ceil(w + 2 * pad).toInt(), ceil(hh + 2 * pad).toInt(), textW, pad)
      }
    }
  }

  private fun draw(c: Canvas, m: ResolvedWatermark, h: Float, ink: Ink, l: Layout, logo: Bitmap?) {
    val pad = l.pad
    val shadowR = 0.1f * h
    val mark = logo ?: if (m.showEmblem) brandEmblem() else null
    fun drawMark(left: Float, top: Float, size: Float) {
      val bmp = mark ?: return
      val scale = size / max(bmp.width, bmp.height)
      val dst = RectF(left, top, left + bmp.width * scale, top + bmp.height * scale)
      // A soft halo keeps the emblem readable on busy backgrounds.
      val halo = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0
        setShadowLayer(shadowR, 0f, 0.03f * h, ink.shadow)
      }
      c.drawRoundRect(dst, size * 0.22f, size * 0.22f, halo)
      c.drawBitmap(bmp, null, dst, Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG))
    }
    fun text(font: WatermarkFont, size: Float, spacing: Float = 0f) = textPaint(font, size, ink.text).apply {
      letterSpacing = spacing
      setShadowLayer(shadowR, 0f, 0.03f * h, ink.shadow)
    }
    val stacked = m.layout == WatermarkLayout.STACKED && mark != null
    when (m.style) {
      WatermarkStyle.SIGNATURE -> {
        val wordmark = m.text.replace(" ", "")
        val p1 = text(WatermarkFont.SIGNATURE, 0.42f * h)
        val p2 = text(m.font, 0.5f * h)
        val blockW = l.textW
        val (textLeft, top) = if (stacked) {
          drawMark((l.width - h * 0.7f) / 2f, pad, h * 0.7f)
          (l.width - blockW) / 2f to pad + h * 0.75f
        } else {
          if (mark != null) drawMark(pad, pad + 0.04f * h, h * 0.92f)
          (pad + if (mark != null) h * 1.12f else 0f) to pad
        }
        val w1 = p1.measureText(SIGNATURE_LINE)
        val w2 = p2.measureText(wordmark)
        c.drawText(SIGNATURE_LINE, textLeft + (blockW - w1) * 0.65f, top + 0.4f * h, p1)
        c.drawText(wordmark, textLeft + (blockW - w2), top + 0.86f * h, p2)
        // The gold swoosh under the wordmark.
        val sx = textLeft + (blockW - w2) + 0.12f * w2
        val ex = textLeft + blockW + 0.02f * w2
        val sy = top + 0.97f * h
        val swoosh = Path().apply {
          moveTo(sx, sy)
          quadTo((sx + ex) / 2f, sy - 0.1f * h, ex, sy - 0.17f * h)
          quadTo((sx + ex) / 2f, sy - 0.02f * h, sx, sy + 0.035f * h)
          close()
        }
        val gold = Paint(Paint.ANTI_ALIAS_FLAG).apply {
          shader = LinearGradient(sx, sy, ex, sy, 0xFFF6C453.toInt(), 0xFFE08A1E.toInt(), Shader.TileMode.CLAMP)
          setShadowLayer(shadowR * 0.6f, 0f, 0.02f * h, ink.shadow)
        }
        c.drawPath(swoosh, gold)
      }
      WatermarkStyle.BADGE -> {
        val size = 0.62f * h
        drawMark((l.width - size) / 2f, pad, size)
        val p = text(m.font, 0.3f * h, 0.15f)
        val t = m.text.uppercase()
        c.drawText(t, (l.width - p.measureText(t)) / 2f, pad + 0.95f * h, p)
      }
      WatermarkStyle.MINIMAL -> {
        val p = text(m.font, 0.55f * h, 0.25f)
        c.drawText(m.text, pad, pad + 0.72f * h, p)
      }
      else -> { // CLASSIC, PILL
        val pill = m.style == WatermarkStyle.PILL
        if (pill) {
          val bg = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = if (ink.dark) 0xB3FFFFFF.toInt() else 0x73000000 }
          c.drawRoundRect(RectF(pad, pad, l.width - pad, l.height - pad), h, h, bg)
        }
        val inset = if (pill) 0.45f * h else 0f
        val p = text(m.font, 0.6f * h).apply { if (pill) clearShadowLayer() }
        if (stacked) {
          drawMark((l.width - h * 0.75f) / 2f, pad + 0.05f * h, h * 0.75f)
          c.drawText(m.text, (l.width - p.measureText(m.text)) / 2f, pad + 1.6f * h, p)
        } else {
          var left = pad + inset
          if (mark != null) {
            drawMark(left, pad + 0.1f * h, h * 0.8f)
            left += h * 1.12f
          }
          c.drawText(m.text, left, pad + 0.72f * h, p)
        }
      }
    }
  }

  const val SIGNATURE_LINE = "Created with"

  /** Pixel height a mark will have on a photo (for tests and previews). */
  fun heightPx(mark: ResolvedWatermark, width: Int, height: Int) = (mark.height * minOf(width, height)).roundToInt()
}
