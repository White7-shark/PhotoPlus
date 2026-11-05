package com.photoplus.edit

import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Frame designs (ids match src/services/edit/catalog.ts). Each is a card of
 * [aspect] (width / height) with the photo in [window] (canvas fractions)
 * cut to a shape, on a designed backdrop, with borders, outline, shadow,
 * glow or film holes. Rendered by the engine (pp_frame).
 */
data class FrameStyle(
  val id: String,
  val aspect: Float,
  val window: FloatArray,
  val shape: Int,
  val shapeParam: Float = 0f,
  val rotation: Float = 0f,
  val backdrop: Int = SOLID,
  val colorA: Int = 0xFFFFFFFF.toInt(),
  val colorB: Int = 0xFFFFFFFF.toInt(),
  val border: Float = 0f,
  val borderColor: Int = 0xFFFFFFFF.toInt(),
  val metallic: Boolean = false,
  val outlineGap: Float = 0f,
  val outlineWidth: Float = 0f,
  val outlineColor: Int = 0xFFFFFFFF.toInt(),
  val shadow: Float = 0f,
  val shadowOffset: Float = 0.02f,
  val shadowBlur: Float = 0.04f,
  val glow: Float = 0f,
  val glowColor: Int = 0xFFFFFFFF.toInt(),
  val decoration: Int = 0,
) {
  /** Window aspect (width / height) in pixels. */
  val windowAspect: Float get() = window[2] * aspect / window[3]

  /**
   * Canvas size so the window shows the photo at (about) its own resolution,
   * never larger than [maxEdge].
   */
  fun canvasSize(photoW: Int, photoH: Int, maxEdge: Int): Pair<Int, Int> {
    val windowW = minOf(photoW.toFloat(), photoH * windowAspect)
    var cw = windowW / window[2]
    var ch = cw / aspect
    val scale = minOf(1f, maxEdge / max(cw, ch))
    cw *= scale
    ch *= scale
    return max(16, cw.roundToInt()) to max(16, ch.roundToInt())
  }

  fun floats(zoom: Float, x: Float, y: Float) = floatArrayOf(
    shapeParam, rotation, window[0], window[1], window[2], window[3], border, outlineGap, outlineWidth,
    shadow, shadowOffset, shadowBlur, glow, zoom, x, y,
  )

  fun ints() = intArrayOf(shape, backdrop, colorA, colorB, borderColor, if (metallic) 1 else 0, outlineColor, glowColor, decoration)

  override fun equals(other: Any?) = other is FrameStyle && other.id == id
  override fun hashCode() = id.hashCode()

  companion object {
    const val RECT = 0
    const val ELLIPSE = 1
    const val ARCH = 2
    const val STAR = 3
    const val HEART = 4
    const val HEXAGON = 5
    const val DIAMOND = 6
    const val SOLID = 0
    const val GRADIENT = 1
    const val BLURRED_PHOTO = 2
    const val PAPER = 3
  }
}

object FrameStyles {
  private fun c(hex: Long) = hex.toInt()
  private const val GOLD = 0xFFDDB15EL

  val ALL: List<FrameStyle> = listOf(
    // Free.
    FrameStyle("gallery", 0.8f, floatArrayOf(0.09f, 0.08f, 0.82f, 0.74f), FrameStyle.RECT, backdrop = FrameStyle.PAPER,
      colorA = c(0xFFF4F1EA), border = 0.004f, borderColor = c(0xFF222222), shadow = 0.25f, shadowOffset = 0.01f, shadowBlur = 0.02f),
    FrameStyle("polaroid", 0.82f, floatArrayOf(0.06f, 0.05f, 0.88f, 0.735f), FrameStyle.RECT, backdrop = FrameStyle.PAPER,
      colorA = c(0xFFFBFAF6)),
    FrameStyle("arch", 0.8f, floatArrayOf(0.17f, 0.12f, 0.66f, 0.7f), FrameStyle.ARCH, backdrop = FrameStyle.PAPER,
      colorA = c(0xFFEDE3D6), outlineGap = 0.035f, outlineWidth = 0.004f, outlineColor = c(0xFFB89A78),
      shadow = 0.35f, shadowOffset = 0.02f, shadowBlur = 0.05f),
    FrameStyle("circle", 0.8f, floatArrayOf(0.1f, 0.16f, 0.8f, 0.64f), FrameStyle.ELLIPSE, backdrop = FrameStyle.BLURRED_PHOTO,
      border = 0.012f, borderColor = c(0xFFFFFFFF), shadow = 0.4f, shadowOffset = 0.02f, shadowBlur = 0.05f),
    FrameStyle("soft", 0.8f, floatArrayOf(0.1f, 0.14f, 0.8f, 0.64f), FrameStyle.RECT, shapeParam = 0.16f,
      backdrop = FrameStyle.BLURRED_PHOTO, shadow = 0.45f, shadowOffset = 0.025f, shadowBlur = 0.06f),
    // Premium.
    FrameStyle("star", 0.8f, floatArrayOf(0.06f, 0.15f, 0.88f, 0.704f), FrameStyle.STAR, shapeParam = 0.48f,
      backdrop = FrameStyle.GRADIENT, colorA = c(0xFF141B3D), colorB = c(0xFF4B2A6B), border = 0.012f,
      borderColor = c(GOLD), metallic = true, glow = 0.45f, glowColor = c(0xFFF6D27A)),
    FrameStyle("heart", 0.8f, floatArrayOf(0.08f, 0.15f, 0.84f, 0.672f), FrameStyle.HEART, backdrop = FrameStyle.GRADIENT,
      colorA = c(0xFFF9DDE0), colorB = c(0xFFF0B3BF), border = 0.014f, borderColor = c(0xFFFFFFFF),
      shadow = 0.3f, shadowOffset = 0.02f, shadowBlur = 0.05f),
    FrameStyle("hexagon", 0.8f, floatArrayOf(0.1f, 0.15f, 0.8f, 0.64f), FrameStyle.HEXAGON, backdrop = FrameStyle.SOLID,
      colorA = c(0xFF1C1C1E), border = 0.012f, borderColor = c(GOLD), metallic = true, glow = 0.15f, glowColor = c(0xFFF6D27A)),
    FrameStyle("film", 0.8f, floatArrayOf(0.06f, 0.14f, 0.88f, 0.72f), FrameStyle.RECT, backdrop = FrameStyle.SOLID,
      colorA = c(0xFF111111), decoration = 1),
    FrameStyle("diamond", 0.8f, floatArrayOf(0.1f, 0.15f, 0.8f, 0.64f), FrameStyle.DIAMOND, backdrop = FrameStyle.GRADIENT,
      colorA = c(0xFFEDE8E0), colorB = c(0xFFCFC6B8), border = 0.01f, borderColor = c(GOLD), metallic = true,
      shadow = 0.35f, shadowOffset = 0.02f, shadowBlur = 0.05f),
    FrameStyle("window", 0.8f, floatArrayOf(0.14f, 0.1f, 0.72f, 0.78f), FrameStyle.ARCH, backdrop = FrameStyle.BLURRED_PHOTO,
      border = 0.014f, borderColor = c(0xFFD9B26A), metallic = true, shadow = 0.4f, shadowOffset = 0.02f, shadowBlur = 0.05f),
  )

  fun byId(id: String): FrameStyle? = ALL.firstOrNull { it.id == id }
}
