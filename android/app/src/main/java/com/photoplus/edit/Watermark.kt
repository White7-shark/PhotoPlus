package com.photoplus.edit

import com.photoplus.engine.EngineException
import kotlin.math.max
import kotlin.math.roundToInt

enum class Plan { FREE, PREMIUM }

enum class WatermarkStyle { CLASSIC, PILL, SIGNATURE, MINIMAL, BADGE }
enum class WatermarkFont(val asset: String) {
  MODERN("fonts/Poppins-SemiBold.ttf"),
  ELEGANT("fonts/DMSerifDisplay-Regular.ttf"),
  SIGNATURE("fonts/GreatVibes-Regular.ttf"),
  BOLD("fonts/BebasNeue-Regular.ttf"),
  TYPE("fonts/SpaceMono-Regular.ttf"),
}
enum class WatermarkLayout { INLINE, STACKED }
enum class Align { LEFT, CENTER, RIGHT }

/** Nine places around the photo; bottom-right is the default (like a signature). */
enum class WatermarkPosition(val fx: Float, val fy: Float) {
  TL(0f, 0f), TC(0.5f, 0f), TR(1f, 0f),
  ML(0f, 0.5f), MC(0.5f, 0.5f), MR(1f, 0.5f),
  BL(0f, 1f), BC(0.5f, 1f), BR(1f, 1f);

  companion object {
    val DEFAULT = BR
  }
}

/** Premium branding: own text, colour, logo and alignment. */
data class CustomBranding(val text: String, val color: Int, val logoPath: String?, val align: Align)

/** What the editor asks for (src/services/edit/watermark.ts). */
data class WatermarkSpec(
  val style: WatermarkStyle = WatermarkStyle.SIGNATURE,
  val font: WatermarkFont = WatermarkFont.MODERN,
  val layout: WatermarkLayout = WatermarkLayout.INLINE,
  /** 0..1 slider, mapped into the plan's size range. */
  val size: Float = 0.5f,
  /** 0..1 slider, mapped into the plan's opacity range. */
  val opacity: Float = 0.85f,
  val position: WatermarkPosition = WatermarkPosition.DEFAULT,
  /** false = export without a watermark (Premium only). */
  val enabled: Boolean = true,
  val custom: CustomBranding? = null,
) {
  companion object {
    fun fromMap(map: Map<String, Any?>?): WatermarkSpec {
      if (map == null) return WatermarkSpec()
      fun <T : Enum<T>> enum(key: String, values: Array<T>, default: T): T {
        val raw = map[key] as? String ?: return default
        return values.firstOrNull { it.name.equals(raw, ignoreCase = true) }
          ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown watermark $key '$raw'")
      }
      fun unit(key: String, default: Float): Float {
        val v = (map[key] as? Number)?.toFloat() ?: return default
        if (!v.isFinite()) throw EngineException(EngineException.INVALID_ARGUMENT, "'$key' must be finite")
        return v.coerceIn(0f, 1f)
      }
      val custom = (map["custom"] as? Map<*, *>)?.let { c ->
        val text = (c["text"] as? String ?: "").take(MAX_TEXT)
        val color = parseColor(c["color"] as? String) ?: WHITE
        val align = (c["align"] as? String)?.let { a -> Align.entries.firstOrNull { it.name.equals(a, true) } } ?: Align.CENTER
        CustomBranding(text, color, (c["logoPath"] as? String)?.takeIf { it.isNotBlank() }, align)
      }
      return WatermarkSpec(
        style = enum("style", WatermarkStyle.entries.toTypedArray(), WatermarkStyle.SIGNATURE),
        font = enum("font", WatermarkFont.entries.toTypedArray(), WatermarkFont.MODERN),
        layout = enum("layout", WatermarkLayout.entries.toTypedArray(), WatermarkLayout.INLINE),
        size = unit("size", 0.5f),
        opacity = unit("opacity", 0.85f),
        position = enum("position", WatermarkPosition.entries.toTypedArray(), WatermarkPosition.DEFAULT),
        enabled = map["enabled"] as? Boolean ?: true,
        custom = custom,
      )
    }

    const val MAX_TEXT = 40
    const val WHITE = 0xFFFFFFFF.toInt()

    fun parseColor(raw: String?): Int? {
      val s = raw?.removePrefix("#") ?: return null
      if (s.length != 6 || !s.all { it.isDigit() || it.lowercaseChar() in 'a'..'f' }) return null
      return (0xFF000000 or s.toLong(16)).toInt()
    }
  }
}

/** The watermark that will actually be drawn. */
data class ResolvedWatermark(
  val style: WatermarkStyle,
  val font: WatermarkFont,
  val layout: WatermarkLayout,
  /** Height of the mark as a fraction of the photo's shorter side. */
  val height: Float,
  val opacity: Float,
  val position: WatermarkPosition,
  val text: String,
  val color: Int,
  /** The Photo Plus emblem is drawn unless a custom logo replaces it (or the style has none). */
  val logoPath: String?,
  val showEmblem: Boolean,
  val align: Align,
  /** True when the plan changed what was asked for (e.g. free plan asked for none). */
  val enforced: Boolean,
  val brand: Boolean,
)

/**
 * The watermark rules, enforced natively for everything that leaves the app:
 *  - Free: the Photo Plus mark is always rendered, in one of the predefined
 *    styles and fonts, at a clearly visible size and opacity, anywhere around
 *    the photo. Custom text, logos, colours and "no watermark" are ignored.
 *  - Premium: custom branding, any size/opacity, or no watermark at all.
 * After rendering, [verify] checks the mark is really there, so a free export
 * can never leave without it by accident.
 */
object ExportPolicy {
  const val BRAND = "Photo Plus"
  val FREE_HEIGHT = 0.035f..0.08f
  val PREMIUM_HEIGHT = 0.02f..0.2f
  val FREE_OPACITY = 0.6f..1f
  val PREMIUM_OPACITY = 0.15f..1f

  private fun map(v: Float, range: ClosedFloatingPointRange<Float>) = range.start + (range.endInclusive - range.start) * v.coerceIn(0f, 1f)

  /** Null only when a Premium user turned the watermark off. */
  fun resolve(spec: WatermarkSpec?, plan: Plan): ResolvedWatermark? {
    val s = spec ?: WatermarkSpec()
    val premium = plan == Plan.PREMIUM
    if (premium && !s.enabled) return null
    val custom = if (premium) s.custom?.takeIf { it.text.isNotBlank() || it.logoPath != null } else null
    val enforced = !premium && (!s.enabled || s.custom != null)
    return ResolvedWatermark(
      style = s.style,
      font = s.font,
      layout = s.layout,
      height = map(s.size, if (premium) PREMIUM_HEIGHT else FREE_HEIGHT),
      opacity = map(s.opacity, if (premium) PREMIUM_OPACITY else FREE_OPACITY),
      position = s.position,
      text = custom?.text?.trim() ?: if (s.style == WatermarkStyle.MINIMAL) BRAND.uppercase() else BRAND,
      color = custom?.color ?: WatermarkSpec.WHITE,
      logoPath = custom?.logoPath,
      showEmblem = custom == null && s.style != WatermarkStyle.MINIMAL,
      align = custom?.align ?: Align.CENTER,
      enforced = enforced,
      brand = custom == null,
    )
  }

  /** The least visible area a rendered mark of [heightPx] must cover. */
  fun minimumVisiblePixels(heightPx: Int): Long = max(40L, (0.15 * heightPx.toDouble() * heightPx).roundToInt().toLong())

  /**
   * Free plan: throws unless the Photo Plus mark really landed in the photo
   * (not off the edge, not invisible). Premium marks are the owner's choice
   * (a faint signature is fine), so they are not checked.
   */
  fun verify(resolved: ResolvedWatermark?, plan: Plan, visiblePixels: Long, imageWidth: Int, imageHeight: Int) {
    if (plan == Plan.PREMIUM) return
    if (resolved == null) throw EngineException(WATERMARK_MISSING, "A free export must carry the Photo Plus watermark")
    val heightPx = (resolved.height * minOf(imageWidth, imageHeight)).roundToInt()
    if (visiblePixels < minimumVisiblePixels(heightPx)) {
      throw EngineException(WATERMARK_MISSING, "The watermark could not be drawn on this photo")
    }
  }

  const val WATERMARK_MISSING = "WATERMARK_MISSING"
}

/** Where the mark goes: [position] with a margin, kept fully inside the photo. */
object WatermarkPlacement {
  fun origin(position: WatermarkPosition, stampW: Int, stampH: Int, imageW: Int, imageH: Int): Pair<Int, Int> {
    val margin = (0.035f * minOf(imageW, imageH)).roundToInt()
    val freeW = imageW - stampW - 2 * margin
    val freeH = imageH - stampH - 2 * margin
    val x = margin + (maxOf(0, freeW) * position.fx).roundToInt()
    val y = margin + (maxOf(0, freeH) * position.fy).roundToInt()
    return x.coerceIn(0, maxOf(0, imageW - stampW)) to y.coerceIn(0, maxOf(0, imageH - stampH))
  }
}
