package com.photoplus.studio

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Shader
import androidx.core.graphics.createBitmap
import com.photoplus.engine.EngineException
import com.photoplus.engine.NativeEngine
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** One correction stroke: brush 0 keep, 1 remove, 2 fix hair; radius and points in photo fractions. */
data class StudioStroke(val brush: Int, val radius: Float, val points: FloatArray) {
  companion object {
    const val MAX_STROKES = 400
    const val MAX_POINTS = 4000

    @Suppress("UNCHECKED_CAST")
    fun listFrom(raw: Any?, brushes: IntRange = 0..2): List<StudioStroke> {
      val list = raw as? List<Any?> ?: return emptyList()
      if (list.size > MAX_STROKES) throw EngineException(EngineException.INVALID_ARGUMENT, "Too many strokes")
      return list.mapNotNull { item ->
        val m = item as? Map<String, Any?> ?: return@mapNotNull null
        val brush = (m["brush"] as? Number)?.toInt() ?: return@mapNotNull null
        if (brush !in brushes) throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown brush")
        val radius = ((m["radius"] as? Number)?.toFloat() ?: 0.02f).takeIf { it.isFinite() }?.coerceIn(0.002f, 0.3f) ?: 0.02f
        val pts = (m["points"] as? List<Any?>)?.mapNotNull { (it as? Number)?.toFloat()?.takeIf { v -> v.isFinite() } } ?: emptyList()
        if (pts.size > MAX_POINTS * 2) throw EngineException(EngineException.INVALID_ARGUMENT, "Stroke too long")
        if (pts.size < 2) return@mapNotNull null
        StudioStroke(brush, radius, FloatArray(pts.size / 2 * 2) { pts[it].coerceIn(-0.1f, 1.1f) })
      }
    }

    /** The engine's flat layout: count, then per stroke brush, radius, n, x0, y0, … */
    fun encode(strokes: List<StudioStroke>): FloatArray {
      val out = ArrayList<Float>(1 + strokes.sumOf { 3 + it.points.size })
      out.add(strokes.size.toFloat())
      for (s in strokes) {
        out.add(s.brush.toFloat())
        out.add(s.radius)
        out.add((s.points.size / 2).toFloat())
        s.points.forEach { out.add(it) }
      }
      return out.toFloatArray()
    }
  }

  override fun equals(other: Any?) =
    other is StudioStroke && other.brush == brush && other.radius == radius && other.points.contentEquals(points)

  override fun hashCode() = 31 * (31 * brush + radius.hashCode()) + points.contentHashCode()
}

data class CutoutSettings(val softness: Float = 0f, val shift: Float = 0f, val hairDetail: Float = 0.5f, val cleanColours: Boolean = true) {
  fun toArray() = floatArrayOf(softness, shift, hairDetail, if (cleanColours) 1f else 0f)

  companion object {
    fun fromMap(m: Map<String, Any?>): CutoutSettings {
      fun f(k: String, d: Float, lo: Float, hi: Float) = ((m[k] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: d).coerceIn(lo, hi)
      return CutoutSettings(f("softness", 0f, 0f, 1f), f("shift", 0f, -1f, 1f), f("hairDetail", 0.5f, 0f, 1f), m["cleanColours"] as? Boolean ?: true)
    }
  }
}

/** The part of the background used, as fractions of it. */
data class SceneCrop(val x: Float = 0f, val y: Float = 0f, val w: Float = 1f, val h: Float = 1f) {
  companion object {
    fun fromMap(m: Map<String, Any?>?): SceneCrop {
      if (m == null) return SceneCrop()
      fun f(k: String, d: Float) = (m[k] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: d
      val w = f("w", 1f).coerceIn(0.05f, 1f)
      val h = f("h", 1f).coerceIn(0.05f, 1f)
      return SceneCrop(f("x", 0f).coerceIn(0f, 1f - w), f("y", 0f).coerceIn(0f, 1f - h), w, h)
    }
  }
}

data class ScenePlacement(val x: Float, val y: Float, val height: Float, val tilt: Float, val angle: Float, val flip: Boolean) {
  fun toArray() = floatArrayOf(x, y, height, tilt, angle, if (flip) 1f else 0f)

  /** With a fixed box (left, top, right, bottom, normalised to the cut-out): video keeps one size and place. */
  fun toArray(box: FloatArray) = floatArrayOf(x, y, height, tilt, angle, if (flip) 1f else 0f, 1f, box[0], box[1], box[2], box[3])

  companion object {
    fun fromMap(m: Map<String, Any?>?): ScenePlacement {
      val p = m ?: emptyMap()
      fun f(k: String, d: Float, lo: Float, hi: Float) = ((p[k] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: d).coerceIn(lo, hi)
      return ScenePlacement(
        f("x", 0.5f, -0.5f, 1.5f), f("y", 0.95f, 0f, 1.5f), f("height", 0.8f, 0.05f, 3f),
        f("tilt", 0f, -1f, 1f), f("angle", 0f, -45f, 45f), p["flip"] as? Boolean ?: false,
      )
    }
  }
}

data class SceneLook(
  val colorMatch: Float = 0.7f,
  val lightWrap: Float = 0.35f,
  val shadow: Float = 0.6f,
  val castShadow: Float = 0f,
  val lightX: Float = 0f,
  val horizon: Float = 0.45f,
  val bgBlur: Float = 0f,
  val grain: Float = 1f,
  val rim: Float = 0f,
  val seed: Int = 11,
  /** Spotlight: light on the person, the scene dimmed and softened around them. */
  val spotlight: Float = 0f,
  /** Video: a colour match measured once and kept (gain, contrast, balance r g b), or null to measure each time. */
  val harmony: FloatArray? = null,
  /** A coloured backdrop's light bouncing back onto the person (their tint, not their brightness). */
  val bounce: Float = 0f,
) {
  fun toArray(): FloatArray {
    val h = harmony
    return floatArrayOf(
      colorMatch, lightWrap, shadow, castShadow, lightX, horizon, bgBlur, grain, rim, seed.toFloat(), spotlight,
      if (h != null) 1f else 0f, h?.getOrNull(0) ?: 1f, h?.getOrNull(1) ?: 1f, h?.getOrNull(2) ?: 1f, h?.getOrNull(3) ?: 1f, h?.getOrNull(4) ?: 1f,
      bounce,
    )
  }

  override fun equals(other: Any?) = other is SceneLook && toArray().contentEquals(other.toArray())

  override fun hashCode() = toArray().contentHashCode()

  companion object {
    fun fromMap(m: Map<String, Any?>?): SceneLook {
      val p = m ?: emptyMap()
      fun f(k: String, d: Float, lo: Float = 0f, hi: Float = 1f) = ((p[k] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: d).coerceIn(lo, hi)
      return SceneLook(
        f("colorMatch", 0.7f), f("lightWrap", 0.35f), f("shadow", 0.6f), f("castShadow", 0f), f("lightX", 0f, -1f, 1f),
        f("horizon", 0.45f), f("bgBlur", 0f), f("grain", 1f), f("rim", 0f), spotlight = f("spotlight", 0f),
        bounce = f("bounce", 0f),
      )
    }
  }
}

/**
 * A photographer's own edit of a hairstyle and its colour. Shape -1..1
 * (length, volume, height, fringe, waves, shine), the parting -1..1 when
 * [partSet]; placement: moved (face heights), sized, turned (degrees);
 * colour: depth, darker roots (0..1), lighter or darker ends.
 */
data class HairEdit(
  val length: Float = 0f,
  val volume: Float = 0f,
  val height: Float = 0f,
  val fringe: Float = 0f,
  val partSet: Boolean = false,
  val part: Float = 0f,
  val waves: Float = 0f,
  val shine: Float = 0f,
  val moveX: Float = 0f,
  val moveY: Float = 0f,
  val size: Float = 1f,
  val rotate: Float = 0f,
  val depth: Float = 0f,
  val roots: Float = 0f,
  val ends: Float = 0f,
) {
  /** The engine's layout (15 values). */
  fun toArray() = floatArrayOf(
    length, volume, height, fringe, if (partSet) 1f else 0f, part, waves, shine, moveX, moveY, size, rotate, depth, roots, ends,
  )

  companion object {
    fun fromMap(m: Map<String, Any?>?): HairEdit {
      if (m == null) return HairEdit()
      fun f(k: String, d: Float, lo: Float, hi: Float) = ((m[k] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: d).coerceIn(lo, hi)
      val part = (m["part"] as? Number)?.toFloat()?.takeIf { it.isFinite() }
      return HairEdit(
        f("length", 0f, -1f, 1f), f("volume", 0f, -1f, 1f), f("height", 0f, -1f, 1f), f("fringe", 0f, -1f, 1f),
        part != null, (part ?: 0f).coerceIn(-1f, 1f), f("waves", 0f, -1f, 1f), f("shine", 0f, -1f, 1f),
        f("moveX", 0f, -0.5f, 0.5f), f("moveY", 0f, -0.5f, 0.5f), f("size", 1f, 0.7f, 1.4f), f("rotate", 0f, -30f, 30f),
        f("depth", 0f, -1f, 1f), f("roots", 0f, 0f, 1f), f("ends", 0f, -1f, 1f),
      )
    }
  }
}

/**
 * Hair in the Photoshoot Studio (Premium): a new [style] (an engine style
 * id) and/or a new [color] (0xRRGGBB) for the person's hair, with a
 * photographer's [edit] and [strokes] (brushes 0 erase new hair, 1 restore
 * the photo, 2 colour here too, 3 no colour here).
 */
data class HairSpec(
  val style: String?,
  val color: Int?,
  val strength: Float = 1f,
  val edit: HairEdit = HairEdit(),
  val strokes: List<StudioStroke> = emptyList(),
) {
  val any get() = style != null || color != null

  companion object {
    private val ID = Regex("^[a-z0-9-]{1,40}$")
    private val HEX = Regex("^#[0-9a-fA-F]{6}$")

    /** Null when nothing is asked. */
    fun fromMap(m: Map<String, Any?>?): HairSpec? {
      if (m == null) return null
      val style = (m["style"] as? String)?.takeIf { it.isNotEmpty() }
      if (style != null && !ID.matches(style)) throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown hairstyle")
      val c = (m["color"] as? String)?.takeIf { it.isNotEmpty() }
      if (c != null && !HEX.matches(c)) throw EngineException(EngineException.INVALID_ARGUMENT, "Bad hair colour")
      val strength = ((m["strength"] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: 1f).coerceIn(0f, 1f)
      @Suppress("UNCHECKED_CAST")
      val edit = HairEdit.fromMap(m["edit"] as? Map<String, Any?>)
      val strokes = StudioStroke.listFrom(m["strokes"], 0..3)
      return HairSpec(style, c?.substring(1)?.toInt(16), strength, edit, strokes).takeIf { it.any }
    }
  }
}

/**
 * Lips in the Photoshoot Studio, in [color] (0xRRGGBB) at [strength] 0..1.
 * [natural] (pink lips): the strength adapts to the person, so naturally pink
 * lips and fair faces get very little and brown or greyish lips the most;
 * otherwise lipstick, applied as asked. Lips keep their own light and gloss.
 */
data class LipsSpec(val color: Int, val strength: Float = 0.85f, val natural: Boolean = false) {
  companion object {
    private val HEX = Regex("^#[0-9a-fA-F]{6}$")

    /** Null when nothing is asked (no map, or strength 0). */
    fun fromMap(m: Map<String, Any?>?): LipsSpec? {
      if (m == null) return null
      val c = m["color"] as? String ?: throw EngineException(EngineException.INVALID_ARGUMENT, "A lip colour is needed")
      if (!HEX.matches(c)) throw EngineException(EngineException.INVALID_ARGUMENT, "Bad lip colour")
      val strength = ((m["strength"] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: 0.85f).coerceIn(0f, 1f)
      return LipsSpec(c.substring(1).toInt(16), strength, m["natural"] == true).takeIf { strength > 0f }
    }
  }
}

/**
 * Studio light on the person (Phase 11): a key light from [angle] degrees
 * around them (0 from the camera, -90 the frame's left, 90 its right) at
 * [height] degrees, [softness] 0 hard .. 1 soft, a [fill] from the other
 * side, rim lights, and colours as linear gains (gels). [strength] eases it
 * back toward the photo's own light. [preset] names the look it came from.
 */
data class SceneLight(
  val preset: String,
  val strength: Float = 1f,
  val angle: Float = -45f,
  val height: Float = 35f,
  val softness: Float = 0.5f,
  val key: Float = 1f,
  val fill: Float = 0.35f,
  val keyColour: FloatArray = floatArrayOf(1f, 1f, 1f),
  val fillColour: FloatArray = floatArrayOf(1f, 1f, 1f),
  val rimLeft: Float = 0f,
  val rimRight: Float = 0f,
  val rimColour: FloatArray = floatArrayOf(1f, 1f, 1f),
) {
  /** The looks on the free plan; the rest (and any hand-made light) are Premium. */
  val premium get() = preset !in FREE_LIGHT

  fun toArray(face: FloatArray?): FloatArray =
    floatArrayOf(strength, angle, height, softness, key, fill) + keyColour + fillColour + floatArrayOf(rimLeft, rimRight) + rimColour +
      (face ?: FloatArray(4))

  override fun equals(other: Any?) = other is SceneLight && preset == other.preset && toArray(null).contentEquals(other.toArray(null))

  override fun hashCode() = 31 * preset.hashCode() + toArray(null).contentHashCode()

  companion object {
    val FREE_LIGHT = setOf("soft-studio", "high-key")
    private val ID = Regex("^[a-z0-9-]{1,40}$")

    /** Null when no light is asked (the photo's own). */
    fun fromMap(m: Map<String, Any?>?): SceneLight? {
      if (m == null) return null
      val preset = m["preset"] as? String ?: "custom"
      if (!ID.matches(preset)) throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown light")
      fun f(k: String, d: Float, lo: Float, hi: Float) = ((m[k] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: d).coerceIn(lo, hi)
      fun c(k: String): FloatArray {
        val l = m[k] as? List<*> ?: return floatArrayOf(1f, 1f, 1f)
        if (l.size != 3) throw EngineException(EngineException.INVALID_ARGUMENT, "Bad light colour")
        return FloatArray(3) { ((l[it] as? Number)?.toFloat()?.takeIf { v -> v.isFinite() } ?: 1f).coerceIn(0f, 4f) }
      }
      return SceneLight(
        preset, f("strength", 1f, 0f, 1f), f("angle", -45f, -180f, 180f), f("height", 35f, -60f, 90f), f("softness", 0.5f, 0f, 1f),
        f("key", 1f, 0f, 2f), f("fill", 0.35f, 0f, 1f), c("keyColour"), c("fillColour"), f("rimLeft", 0f, 0f, 1f), f("rimRight", 0f, 0f, 1f),
        c("rimColour"),
      )
    }
  }
}

/**
 * The finished photo's grade: vignette, warmth and tint, contrast and
 * saturation (free); film looks: faded blacks, split toning, grain, black
 * and white (Premium).
 */
data class SceneFinish(
  val vignette: Float = 0f,
  val vignetteSize: Float = 0.5f,
  val warmth: Float = 0f,
  val tint: Float = 0f,
  val contrast: Float = 0f,
  val saturation: Float = 0f,
  val fade: Float = 0f,
  val shadowTone: FloatArray = FloatArray(3),
  val highlightTone: FloatArray = FloatArray(3),
  val toneAmount: Float = 0f,
  val grain: Float = 0f,
  val grainSize: Float = 0.5f,
  val mono: Boolean = false,
) {
  /** Film looks (fade, split toning, grain, black and white) are Premium. */
  val premium get() = fade > 0f || toneAmount > 0f || grain > 0f || mono

  val none get() = vignette == 0f && warmth == 0f && tint == 0f && contrast == 0f && saturation == 0f && !premium

  fun toArray(): FloatArray =
    floatArrayOf(vignette, vignetteSize, warmth, tint, contrast, saturation, fade) + shadowTone + highlightTone +
      floatArrayOf(toneAmount, grain, grainSize, if (mono) 1f else 0f)

  override fun equals(other: Any?) = other is SceneFinish && toArray().contentEquals(other.toArray())

  override fun hashCode() = toArray().contentHashCode()

  companion object {
    fun fromMap(m: Map<String, Any?>?): SceneFinish? {
      if (m == null) return null
      fun f(k: String, d: Float, lo: Float, hi: Float) = ((m[k] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: d).coerceIn(lo, hi)
      fun t(k: String): FloatArray {
        val l = m[k] as? List<*> ?: return FloatArray(3)
        if (l.size != 3) throw EngineException(EngineException.INVALID_ARGUMENT, "Bad tone")
        return FloatArray(3) { ((l[it] as? Number)?.toFloat()?.takeIf { v -> v.isFinite() } ?: 0f).coerceIn(-1f, 1f) }
      }
      return SceneFinish(
        f("vignette", 0f, -1f, 1f), f("vignetteSize", 0.5f, 0f, 1f), f("warmth", 0f, -1f, 1f), f("tint", 0f, -1f, 1f),
        f("contrast", 0f, -1f, 1f), f("saturation", 0f, -1f, 1f), f("fade", 0f, 0f, 1f), t("shadowTone"), t("highlightTone"),
        f("toneAmount", 0f, 0f, 1f), f("grain", 0f, 0f, 1f), f("grainSize", 0.5f, 0f, 1f), m["mono"] == true,
      ).takeIf { !it.none }
    }
  }
}

/** What goes behind the person. */
sealed class SceneBackground {
  data class Plate(val assetId: String) : SceneBackground()
  data class Photo(val path: String) : SceneBackground()
  /** A colour: flat, darker toward the edges ([vignette]), or lit like studio paper ([light] 0..1). */
  data class Plain(val color: Int, val vignette: Boolean, val light: Float = 0f) : SceneBackground()
  /** A blend from one colour to another: down the frame along [angle] degrees, or [radial] from a pool behind the person. */
  data class Gradient(val from: Int, val to: Int, val radial: Boolean, val angle: Float = 90f) : SceneBackground()
  data object Transparent : SceneBackground()

  companion object {
    private val ID = Regex("^[a-z0-9-]{1,60}$")
    private val HEX = Regex("^#[0-9a-fA-F]{6}$")

    fun fromMap(m: Map<String, Any?>?): SceneBackground = when (m?.get("kind")) {
      "plate" -> {
        val id = m["id"] as? String ?: ""
        if (!ID.matches(id)) throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown background")
        Plate(id)
      }
      "photo" -> Photo(m["path"] as? String ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Background photo is missing"))
      "plain" -> {
        val c = m["color"] as? String ?: "#F4F4F2"
        if (!HEX.matches(c)) throw EngineException(EngineException.INVALID_ARGUMENT, "Bad colour")
        val light = (m["light"] as? Number)?.toFloat()?.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f
        Plain(0xFF000000.toInt() or c.substring(1).toInt(16), m["vignette"] as? Boolean ?: false, light)
      }
      "gradient" -> {
        val a = m["from"] as? String ?: ""
        val b = m["to"] as? String ?: ""
        if (!HEX.matches(a) || !HEX.matches(b)) throw EngineException(EngineException.INVALID_ARGUMENT, "Bad colour")
        val angle = (m["angle"] as? Number)?.toFloat()?.takeIf { it.isFinite() }?.coerceIn(-360f, 360f) ?: 90f
        Gradient(a.substring(1).toInt(16), b.substring(1).toInt(16), m["radial"] == true, angle)
      }
      "transparent" -> Transparent
      else -> throw EngineException(EngineException.INVALID_ARGUMENT, "Choose a background")
    }
  }
}

/**
 * The studio's bitmap work, without React Native: the frame (a background
 * cropped and scaled, or a plain colour), and the size an export should be
 * so the person keeps their own detail.
 */
object SceneStudio {
  /** Previews: quick to make and enough for a phone screen. */
  const val PREVIEW_EDGE = 1280

  /** Background crop in pixels of a background of size w × h. */
  fun cropRect(crop: SceneCrop, w: Int, h: Int): Rect {
    val l = (crop.x * w).roundToInt().coerceIn(0, w - 1)
    val t = (crop.y * h).roundToInt().coerceIn(0, h - 1)
    val r = ((crop.x + crop.w) * w).roundToInt().coerceIn(l + 1, w)
    val b = ((crop.y + crop.h) * h).roundToInt().coerceIn(t + 1, h)
    return Rect(l, t, r, b)
  }

  /**
   * Output size for an export: the person keeps their own resolution (a
   * person `personPx` tall, filling `height` of the frame) as far as the
   * long edge allows, and the frame is never smaller than the preview.
   */
  fun exportSize(frameAspect: Float, personPx: Int, height: Float, maxEdge: Int): Pair<Int, Int> {
    val wantH = personPx / height.coerceAtLeast(0.05f)
    var h = wantH
    var w = h * frameAspect
    val long = max(w, h)
    val k = when {
      long > maxEdge -> maxEdge / long
      long < PREVIEW_EDGE -> PREVIEW_EDGE / long
      else -> 1f
    }
    w *= k
    h *= k
    return max(16, w.roundToInt()) to max(16, h.roundToInt())
  }

  /** The background as an opaque frame of the given size (crop scaled to fill). */
  fun frame(source: Bitmap, crop: SceneCrop, width: Int, height: Int): Bitmap {
    val out = createBitmap(width, height, Bitmap.Config.ARGB_8888)
    val c = Canvas(out)
    c.drawBitmap(source, cropRect(crop, source.width, source.height), RectF(0f, 0f, width.toFloat(), height.toFloat()), Paint(Paint.FILTER_BITMAP_FLAG))
    out.setHasAlpha(false)
    return out
  }

  /** A gradient backdrop (the engine blends it in linear light, dithered: no bands). */
  fun gradient(g: SceneBackground.Gradient, width: Int, height: Int): Bitmap {
    val out = createBitmap(width, height, Bitmap.Config.ARGB_8888)
    out.isPremultiplied = false
    NativeEngine.nativeStudioGradient(out, g.from, g.to, if (g.radial) 1 else 0, g.angle, 0.5f, 0.38f, 0.85f)
    out.isPremultiplied = true
    out.setHasAlpha(false)
    return out
  }

  /** A plain colour, optionally darker toward the edges like a lit paper sweep. */
  fun plain(color: Int, vignette: Boolean, width: Int, height: Int, light: Float = 0f): Bitmap {
    val out = createBitmap(width, height, Bitmap.Config.ARGB_8888)
    if (light > 0f) {
      // Studio paper: the engine lights it (a pool of light behind the person, falling off, a faint texture).
      out.isPremultiplied = false
      NativeEngine.nativeStudioBackdrop(out, Color.red(color), Color.green(color), Color.blue(color), light, 0.5f, 0.42f, 7)
      out.isPremultiplied = true
      out.setHasAlpha(false)
      return out
    }
    val c = Canvas(out)
    c.drawColor(color or 0xFF000000.toInt())
    if (vignette) {
      val r = max(width, height) * 0.75f
      val edge = Color.argb(110, 0, 0, 0)
      val p = Paint().apply {
        shader = RadialGradient(width * 0.5f, height * 0.42f, r, intArrayOf(Color.TRANSPARENT, Color.TRANSPARENT, edge), floatArrayOf(0f, 0.45f, 1f), Shader.TileMode.CLAMP)
      }
      c.drawRect(0f, 0f, width.toFloat(), height.toFloat(), p)
    }
    out.setHasAlpha(false)
    return out
  }

  /** The outfit colours in a 26-value cut-out report: up to two, as "#rrggbb" with their share of the clothes. Pure. */
  fun outfit(r: FloatArray): List<Map<String, Any>> {
    if (r.size < 26) return emptyList()
    val n = r[17].toInt().coerceIn(0, 2)
    return (0 until n).map { i ->
      fun c(k: Int) = r[18 + i * 3 + k].roundToInt().coerceIn(0, 255)
      mapOf("color" to "#%02x%02x%02x".format(c(0), c(1), c(2)), "share" to r[24 + i].toDouble())
    }
  }

  /** A sample size that decodes a w × h image to at least `edge` on its long side. */
  fun sampleSize(w: Int, h: Int, edge: Int): Int {
    var s = 1
    while (max(w, h) / (s * 2) >= edge) s *= 2
    return s
  }

  fun decodeOptions(w: Int, h: Int, edge: Int) = BitmapFactory.Options().apply {
    inSampleSize = sampleSize(w, h, edge)
    inPreferredConfig = Bitmap.Config.ARGB_8888
    inMutable = false
  }

  /** The person's box in a straight-alpha cut-out (alpha > ~10%), or null if empty. */
  fun alphaBox(cutout: Bitmap): Rect? {
    val w = cutout.width
    val h = cutout.height
    val row = IntArray(w)
    var l = w
    var t = h
    var r = -1
    var b = -1
    for (y in 0 until h) {
      cutout.getPixels(row, 0, w, 0, y, w, 1)
      for (x in 0 until w) {
        if ((row[x] ushr 24) > 25) {
          if (x < l) l = x
          if (x > r) r = x
          if (y < t) t = y
          b = y
        }
      }
    }
    return if (r < 0) null else Rect(l, t, r + 1, b + 1)
  }

  /** A transparent PNG's worth: the cut-out trimmed to the person with a small margin. */
  fun trimmed(cutout: Bitmap): Bitmap {
    val box = alphaBox(cutout) ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Nobody was found in the photo")
    val m = (0.03f * max(box.width(), box.height())).roundToInt()
    val l = max(0, box.left - m)
    val t = max(0, box.top - m)
    val r = min(cutout.width, box.right + m)
    val b = min(cutout.height, box.bottom + m)
    // Copied row by row: straight alpha stays straight (a canvas would premultiply).
    val out = createBitmap(r - l, b - t, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
    val row = IntArray(r - l)
    for (y in t until b) {
      cutout.getPixels(row, 0, r - l, l, y, r - l, 1)
      out.setPixels(row, 0, r - l, 0, y - t, r - l, 1)
    }
    return out
  }
}
