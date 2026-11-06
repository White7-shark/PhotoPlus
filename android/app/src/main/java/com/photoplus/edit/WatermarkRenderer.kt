package com.photoplus.edit

import android.content.Context
import android.content.res.AssetManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.BlurMaskFilter
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PointF
import android.graphics.PorterDuff
import android.graphics.PorterDuffColorFilter
import android.graphics.Rect
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
 * The default "Signature" mark is the brand lockup: a hand-drawn camera with
 * a glossy lens and a gold swoosh across it, "Created with" in a thin script,
 * and a bold italic "PhotoPlus" wordmark ("Photo" in ink, "Plus" in gold, the
 * first "o" drawn as an aperture) over a tapered gold swoosh. Its ink adapts
 * to what is behind it (white with a soft shadow on darker areas, deep
 * charcoal on bright ones) so it stays readable without spoiling the photo.
 *
 * The mark is drawn with Canvas into a stamp, then composited by the engine,
 * which reports how much of it is visible (checked by [ExportPolicy.verify]).
 */
object WatermarkRenderer {
  @Volatile private var assets: AssetManager? = null
  private val typefaces = ConcurrentHashMap<String, Typeface>()
  private val art = ConcurrentHashMap<String, Bitmap>()

  fun install(context: Context) {
    if (assets == null) assets = context.applicationContext.assets
  }

  private fun typeface(asset: String): Typeface = typefaces.getOrPut(asset) {
    assets?.let { runCatching { Typeface.createFromAsset(it, asset) }.getOrNull() } ?: Typeface.DEFAULT_BOLD
  }

  private fun typeface(font: WatermarkFont): Typeface = typeface(font.asset)

  private fun bitmap(asset: String): Bitmap? = art[asset] ?: assets?.let { am ->
    runCatching { am.open(asset).use { BitmapFactory.decodeStream(it) } }.getOrNull()?.also { art[asset] = it }
  }

  private fun brandEmblem(): Bitmap? = bitmap("brand/emblem.png")

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

  // ---- the Signature lockup ----------------------------------------------------
  //
  // Proportions are measured from the brand reference, in units of the
  // wordmark's cap height C (C = 0.46 h, h being the mark's nominal height).
  // Origin: the top of the script line; x = 0 is the left of the camera.

  private const val C_PER_H = 0.46f
  private const val CAMERA_W = 2.28f // camera width, in C (it spans the script top to the wordmark baseline)
  private const val CAMERA_ASPECT = 300f / 320f // height / width of the camera art
  private const val TEXT_LEFT = 2.4f // where both text lines start
  private const val SCRIPT_BASELINE = 0.77f
  private const val WORDMARK_BASELINE = 2.2f
  private const val SCRIPT_WIDTH = 1.05f // "Created with" width / wordmark width
  private const val PAD = 0.32f // room for the soft shadow
  private const val SCRIPT_ASSET = "fonts/PetitFormalScript-Regular.ttf"
  private const val WORDMARK_ASSET = "fonts/Poppins-ExtraBoldItalic.ttf"
  private val PLUS_GOLD = intArrayOf(0xFFFFCB2E.toInt(), 0xFFF5A015.toInt())
  private val PLUS_GOLD_ON_LIGHT = intArrayOf(0xFFF2A900.toInt(), 0xFFD9790B.toInt())
  private val SWOOSH_GOLD = intArrayOf(0xFFF7B733.toInt(), 0xFFFFC93C.toInt(), 0xFFF39A1E.toInt())

  /** Everything the signature needs, laid out for one mark size. */
  private class Signature(m: ResolvedWatermark, h: Float, val hasLogo: Boolean) {
    val c = C_PER_H * h
    val brandText = m.text.replace(" ", "")
    val isBrand = m.brand && brandText.equals(BRAND_WORDMARK, ignoreCase = true)
    val wordmark: Paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
      typeface = if (m.font == WatermarkFont.MODERN) typeface(WORDMARK_ASSET) else typeface(m.font)
      textSize = 100f
      val r = Rect()
      getTextBounds("H", 0, 1, r)
      textSize = 100f * c / r.height().coerceAtLeast(1)
    }
    val text = if (isBrand) BRAND_WORDMARK else m.text
    val wordW = wordmark.measureText(text)
    val script: Paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
      typeface = typeface(SCRIPT_ASSET)
      textSkewX = -0.1f
      style = Paint.Style.FILL_AND_STROKE
      strokeJoin = Paint.Join.ROUND
      // As wide as the reference line (1.05 × the wordmark).
      textSize = 100f
      textSize = 100f * SCRIPT_WIDTH * wordW / measureText(SIGNATURE_LINE).coerceAtLeast(1f)
      strokeWidth = 0.018f * c // the reference script is a little heavier than the font
    }
    val stacked = m.layout == WatermarkLayout.STACKED
    val showCamera = m.showEmblem || hasLogo

    // Inline: camera on the left. Stacked: camera centred above the text.
    val cameraW = if (stacked) 1.8f * c else CAMERA_W * c
    val cameraH = cameraW * CAMERA_ASPECT
    val textBlockW = max(wordW, SCRIPT_WIDTH * wordW)
    val textLeft = if (stacked || !showCamera) 0f else TEXT_LEFT * c
    val textTop = if (stacked && showCamera) cameraH + 0.15f * c else 0f
    val contentW = if (stacked) max(textBlockW, cameraW) else textLeft + textBlockW + 0.1f * c
    val contentH = textTop + WORDMARK_BASELINE * c + 0.95f * c
    val pad = PAD * c

    fun width() = ceil(contentW + 2 * pad).toInt()
    fun height() = ceil(contentH + 2 * pad).toInt()
  }

  private fun measure(m: ResolvedWatermark, h: Float, logo: Bitmap?): Layout {
    val pad = 0.18f * h // room for the soft shadow
    val hasMark = m.showEmblem || logo != null
    return when (m.style) {
      WatermarkStyle.SIGNATURE -> {
        val s = Signature(m, h, logo != null)
        Layout(s.width(), s.height(), s.wordW, s.pad)
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
    if (m.style == WatermarkStyle.SIGNATURE) {
      drawSignature(c, Signature(m, h, logo != null), m, ink, logo)
      return
    }
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

  /**
   * The signature lockup. It is drawn without shadows on a layer first; the
   * layer's own silhouette, blurred, then becomes the soft shadow beneath it.
   */
  private fun drawSignature(out: Canvas, s: Signature, m: ResolvedWatermark, ink: Ink, logo: Bitmap?) {
    val layer = createBitmap(out.width, out.height, Bitmap.Config.ARGB_8888)
    try {
      val c = Canvas(layer)
      val unit = s.c
      val ox = s.pad
      val oy = s.pad
      val textX = ox + if (s.stacked) (s.contentW - s.textBlockW) / 2f else s.textLeft
      val top = oy + s.textTop

      // The camera (or a Premium logo in its place).
      if (s.showCamera) {
        val left = ox + if (s.stacked) (s.contentW - s.cameraW) / 2f else 0f
        val camTop = oy
        val dst = RectF(left, camTop, left + s.cameraW, camTop + s.cameraH)
        val bitmapPaint = Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG)
        if (logo != null) {
          val scale = minOf(dst.width() / logo.width, dst.height() / logo.height)
          val w = logo.width * scale
          val hh = logo.height * scale
          c.drawBitmap(logo, null, RectF(dst.centerX() - w / 2, dst.centerY() - hh / 2, dst.centerX() + w / 2, dst.centerY() + hh / 2), bitmapPaint)
        } else {
          bitmap("brand/mark-camera-lines.png")?.let { lines ->
            c.drawBitmap(lines, null, dst, Paint(bitmapPaint).apply { colorFilter = PorterDuffColorFilter(ink.text, PorterDuff.Mode.SRC_IN) })
          }
          bitmap("brand/mark-camera-color.png")?.let { c.drawBitmap(it, null, dst, bitmapPaint) }
        }
      }

      // "Created with".
      val script = Paint(s.script).apply { color = ink.text }
      c.drawText(SIGNATURE_LINE, textX, top + SCRIPT_BASELINE * unit, script)

      // The wordmark.
      val baseline = top + WORDMARK_BASELINE * unit
      val word = Paint(s.wordmark).apply { color = ink.text } // custom colours arrive as the ink
      if (s.isBrand) {
        drawBrandWordmark(c, word, textX, baseline, ink)
      } else {
        c.drawText(s.text, textX, baseline, word)
      }

      // The gold swoosh under the wordmark.
      val l = PointF(textX + 0.43f * unit, baseline + 0.83f * unit)
      val r = PointF(textX + s.wordW - 0.74f * unit, baseline + 0.74f * unit)
      val peak = PointF(textX + 0.4f * s.wordW + 0.4f * unit, baseline + 0.31f * unit)
      val thick = 0.22f * unit
      val ctrl = PointF(2 * peak.x - (l.x + r.x) / 2f, 2 * peak.y - (l.y + r.y) / 2f)
      val swoosh = Path().apply {
        moveTo(l.x, l.y)
        quadTo(ctrl.x, ctrl.y, r.x, r.y)
        quadTo(ctrl.x, ctrl.y + 2 * thick, l.x, l.y)
        close()
      }
      val gold = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        shader = LinearGradient(l.x, 0f, r.x, 0f, SWOOSH_GOLD, floatArrayOf(0f, 0.45f, 1f), Shader.TileMode.CLAMP)
      }
      c.drawPath(swoosh, gold)

      // Soft shadow from the layer's silhouette, then the layer on top.
      val blur = Paint().apply { maskFilter = BlurMaskFilter(0.16f * unit, BlurMaskFilter.Blur.NORMAL) }
      val offset = IntArray(2)
      val silhouette = layer.extractAlpha(blur, offset)
      out.drawBitmap(silhouette, offset[0].toFloat(), offset[1] + 0.05f * unit, Paint().apply { color = ink.shadow })
      silhouette.recycle()
      out.drawBitmap(layer, 0f, 0f, null)
    } finally {
      layer.recycle()
    }
  }

  /** "Photo" in ink with the first "o" as an aperture, then "Plus" in gold. */
  private fun drawBrandWordmark(c: Canvas, paint: Paint, x: Float, baseline: Float, ink: Ink) {
    val ph = "Ph"
    c.drawText(ph, x, baseline, paint)
    val oLeft = x + paint.measureText(ph)
    val bounds = Rect()
    paint.getTextBounds("o", 0, 1, bounds)
    val aperture = bitmap("brand/mark-aperture.png")
    if (aperture != null) {
      val d = bounds.height().toFloat()
      val cx = oLeft + bounds.exactCenterX()
      val cy = baseline + bounds.exactCenterY()
      c.drawBitmap(
        aperture,
        null,
        RectF(cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2),
        Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG).apply { colorFilter = PorterDuffColorFilter(ink.text, PorterDuff.Mode.SRC_IN) },
      )
    } else {
      c.drawText("o", oLeft, baseline, paint)
    }
    val toLeft = x + paint.measureText("Pho")
    c.drawText("to", toLeft, baseline, paint)
    val plusLeft = x + paint.measureText("Photo")
    val capTop = baseline - paint.textSize * 0.72f
    val plus = Paint(paint).apply {
      shader = LinearGradient(0f, capTop, 0f, baseline, if (ink.dark) PLUS_GOLD_ON_LIGHT else PLUS_GOLD, null, Shader.TileMode.CLAMP)
    }
    c.drawText("Plus", plusLeft, baseline, plus)
  }

  const val SIGNATURE_LINE = "Created with"
  private const val BRAND_WORDMARK = "PhotoPlus"

  /** Pixel height a mark will have on a photo (for tests and previews). */
  fun heightPx(mark: ResolvedWatermark, width: Int, height: Int) = (mark.height * minOf(width, height)).roundToInt()
}
