package com.photoplus.edit

import com.photoplus.engine.EngineException
import com.photoplus.engine.EngineOp

/**
 * A photo's edits, as sent by the editor (src/services/edit/document.ts).
 * Non-destructive: it is replayed on the untouched photo every time, in a
 * fixed order (geometry → portrait → tone and look → focus → sharpen →
 * props → frame), so undo/redo is just a different document.
 *
 * Every value is validated and clamped here: the document comes from
 * JavaScript and is never trusted. Pure Kotlin, unit-tested on the JVM.
 */
data class EditDocument(
  val quarterTurns: Int = 0,
  val flipH: Boolean = false,
  val straighten: Float = 0f,
  /** Perspective (keystone) correction, -1..1: vertical > 0 widens the top, horizontal > 0 the right. */
  val perspectiveV: Float = 0f,
  val perspectiveH: Float = 0f,
  /** Crop in fractions of the turned and straightened photo, or null. */
  val crop: CropRect? = null,
  val exposure: Float = 0f,
  val brightness: Float = 0f,
  val contrast: Float = 0f,
  val saturation: Float = 0f,
  val warmth: Float = 0f,
  val sharpen: Float = 0f,
  val look: LookChoice? = null,
  /** Glow skin retouch strength (0 off). */
  val retouch: Float = 0f,
  /** Spotlight: what should shine (taps, upright photo) and how much the rest melts. */
  val spotlight: SpotlightChoice? = null,
  /** Transform: the high-end lens look, applied to the untouched photo first. */
  val transform: Boolean = false,
  val focus: FocusChoice? = null,
  /** Advanced portrait controls (Premium), or null for the presets only. */
  val portrait: PortraitChoice? = null,
  val props: List<PropPlacement> = emptyList(),
  val frame: FrameChoice? = null,
  /** A beauty preset applied in the editor (its resolved recipe), or null. */
  val beauty: BeautyChoice? = null,
) {
  data class BeautyChoice(val preset: String, val recipe: com.photoplus.beauty.BeautyRecipe) {
    override fun toString() = "$preset:${recipe.key()}"
  }
  data class CropRect(val x: Float, val y: Float, val w: Float, val h: Float)
  data class SpotlightChoice(val points: List<Pair<Float, Float>>, val blur: Float)
  data class LookChoice(val id: String, val engineId: Int, val strength: Float)
  data class FocusChoice(val kind: Int, val x: Float, val y: Float, val size: Float, val angle: Float, val strength: Float)
  data class PropPlacement(val id: String, val cx: Float, val cy: Float, val width: Float, val rotation: Float, val flip: Boolean)
  data class FrameChoice(val id: String, val zoom: Float, val x: Float, val y: Float)

  /**
   * Fine portrait controls. Skin: smooth, blemish, even (0..1, they replace
   * the Glow preset's single strength). Face: eyes, teeth, lips, under-eye,
   * shine, slim, face light, studio light (0..1) and its side (-1..1).
   * Background: light and colour (-1..1). Edges: refinement (0..1).
   */
  data class PortraitChoice(
    val smooth: Float = 0f,
    val blemish: Float = 0f,
    val even: Float = 0f,
    val eyes: Float = 0f,
    val teeth: Float = 0f,
    val lips: Float = 0f,
    val underEye: Float = 0f,
    val shine: Float = 0f,
    val slim: Float = 0f,
    val faceLight: Float = 0f,
    val studio: Float = 0f,
    val lightAngle: Float = 0f,
    val bgLight: Float = 0f,
    val bgColor: Float = 0f,
    val edge: Float = 0f,
  ) {
    val skin: Boolean get() = smooth > 0f || blemish > 0f || even > 0f
    val background: Boolean get() = bgLight != 0f || bgColor != 0f
    fun tuning() = com.photoplus.studio.FaceTuning(eyes, teeth, lips, underEye, shine, slim, faceLight, studio, lightAngle)
    val active: Boolean get() = skin || background || tuning().any
  }

  val hasPortraitWork: Boolean get() = retouch > 0f || spotlight != null || portrait?.active == true

  /** Turn, mirror, perspective and straighten (crop needs the resulting size: [cropOp]). */
  fun geometryOps(): List<EngineOp> = buildList {
    if (quarterTurns != 0) add(EngineOp.Rotate(quarterTurns))
    if (flipH) add(EngineOp.Flip(horizontal = true, vertical = false))
    if (perspectiveV != 0f || perspectiveH != 0f) add(EngineOp.Perspective(perspectiveV, perspectiveH))
    if (straighten != 0f) add(EngineOp.Straighten(straighten))
  }

  /** Only the geometry (turn, mirror, perspective, straighten, crop): what faces and props are measured on. */
  fun geometryOnly() = EditDocument(
    quarterTurns = quarterTurns, flipH = flipH, perspectiveV = perspectiveV, perspectiveH = perspectiveH,
    straighten = straighten, crop = crop,
  )

  /** The crop in pixels of a [width] x [height] image, or null for none. */
  fun cropOp(width: Int, height: Int): EngineOp.Crop? {
    val c = crop ?: return null
    val x = (c.x * width).toInt().coerceIn(0, width - 1)
    val y = (c.y * height).toInt().coerceIn(0, height - 1)
    val w = (c.w * width).toInt().coerceIn(1, width - x)
    val h = (c.h * height).toInt().coerceIn(1, height - y)
    if (x == 0 && y == 0 && w == width && h == height) return null
    return EngineOp.Crop(x, y, w, h)
  }

  /**
   * Light, colour, look, focus and sharpening, in that order, for an image
   * whose longest edge is [longEdge] pixels. Sharpening's radius follows the
   * size, so a small preview and the full-size save look alike.
   */
  fun toneOps(longEdge: Int = 0): List<EngineOp> = buildList {
    if (exposure != 0f || brightness != 0f || contrast != 0f) add(EngineOp.Tone(exposure, brightness, contrast))
    if (warmth != 0f) add(EngineOp.Warmth(warmth, 0f))
    if (saturation != 0f) add(EngineOp.Saturation(saturation))
    look?.let { if (it.strength > 0f) add(EngineOp.Filter(it.engineId, it.strength)) }
    focus?.let { if (it.strength > 0f) add(EngineOp.Focus(it.kind, it.x, it.y, it.size, it.angle, it.strength, 0.5f)) }
    if (sharpen > 0f) add(EngineOp.Sharpen(sharpen * 1.2f, sharpenRadius(longEdge), SHARPEN_THRESHOLD))
  }

  companion object {
    /** Look ids → engine look presets (pp_filter_id). */
    val LOOKS = mapOf(
      "natural" to 1, "vivid" to 2, "warm" to 3, "cool" to 4, "mono" to 5, "fade" to 6,
      "soft" to 7, "noir" to 8, "cinema" to 9, "golden" to 10, "film" to 11, "lush" to 12,
    )

    /** Bundled props (android/app/src/main/assets/props/<id>.png). */
    val PROPS = setOf(
      "fedora", "beanie", "cap", "sunhat", "beret", "crown", "laurel", "tophat", "bucket", "cowboy", "flowers",
      "aviators", "round", "cateye", "tortoise", "hearts", "shield", "rimless",
      "bowtie", "chain", "necktie", "pearls", "scarf", "choker", "medal",
    )

    /** Props the user made (CustomProps), stored as files: "custom-" + a random id. */
    val CUSTOM_PROP = Regex("^custom-[a-z0-9]{8,32}$")

    fun isKnownProp(id: String) = id in PROPS || CUSTOM_PROP.matches(id)

    const val MAX_PROPS = 12

    /** Leaves flat areas and noise alone when sharpening (0..0.2). */
    const val SHARPEN_THRESHOLD = 0.01f

    /** One pixel per 2000 of the longest edge (0: the engine's 1 px). */
    fun sharpenRadius(longEdge: Int): Float = if (longEdge <= 0) 0f else (longEdge / 2000f).coerceIn(0.6f, 4f)

    fun fromMap(map: Map<String, Any?>?): EditDocument {
      if (map == null) return EditDocument()
      fun num(key: String, min: Float, max: Float, src: Map<*, *> = map): Float {
        val v = (src[key] as? Number)?.toFloat() ?: return 0f.coerceIn(min, max)
        if (!v.isFinite()) invalid("'$key' must be finite")
        return v.coerceIn(min, max)
      }
      val crop = (map["crop"] as? Map<*, *>)?.let { c ->
        val x = num("x", 0f, 0.99f, c)
        val y = num("y", 0f, 0.99f, c)
        CropRect(x, y, num("w", 0.01f, 1f - x, c), num("h", 0.01f, 1f - y, c))
      }
      val look = (map["look"] as? Map<*, *>)?.let { l ->
        val id = l["id"] as? String ?: invalid("look id is required")
        val engineId = LOOKS[id] ?: invalid("Unknown look '$id'")
        LookChoice(id, engineId, num("strength", 0f, 1f, l))
      }
      val focus = (map["focus"] as? Map<*, *>)?.let { f ->
        val kind = when (f["kind"]) {
          "radial" -> 1
          "tilt" -> 2
          else -> invalid("Unknown focus kind")
        }
        FocusChoice(kind, num("x", 0f, 1f, f), num("y", 0f, 1f, f), num("size", 0.02f, 1f, f), num("angle", -90f, 90f, f), num("strength", 0f, 1f, f))
      }
      val rawProps = map["props"] as? List<*> ?: emptyList<Any?>()
      if (rawProps.size > MAX_PROPS) invalid("Too many props (max $MAX_PROPS)")
      val props = rawProps.map { raw ->
        val p = raw as? Map<*, *> ?: invalid("A prop must be an object")
        val id = p["id"] as? String ?: invalid("prop id is required")
        if (!isKnownProp(id)) invalid("Unknown prop '$id'")
        PropPlacement(id, num("cx", -0.5f, 1.5f, p), num("cy", -0.5f, 1.5f, p), num("width", 0.01f, 3f, p), num("rotation", -180f, 180f, p), p["flip"] == true)
      }
      val portrait = (map["portrait"] as? Map<*, *>)?.let { q ->
        PortraitChoice(
          smooth = num("smooth", 0f, 1f, q), blemish = num("blemish", 0f, 1f, q), even = num("even", 0f, 1f, q),
          eyes = num("eyes", 0f, 1f, q), teeth = num("teeth", 0f, 1f, q), lips = num("lips", 0f, 1f, q),
          underEye = num("underEye", 0f, 1f, q), shine = num("shine", 0f, 1f, q), slim = num("slim", 0f, 1f, q),
          faceLight = num("faceLight", 0f, 1f, q), studio = num("studio", 0f, 1f, q), lightAngle = num("lightAngle", -1f, 1f, q),
          bgLight = num("bgLight", -1f, 1f, q), bgColor = num("bgColor", -1f, 1f, q), edge = num("edge", 0f, 1f, q),
        )
      }
      val beauty = (map["beauty"] as? Map<*, *>)?.let { b ->
        val preset = (b["preset"] as? String)?.takeIf { it.length in 1..64 } ?: invalid("beauty preset is required")
        val recipe = com.photoplus.beauty.BeautyRecipe.fromMap(b["recipe"] as? Map<*, *>) ?: invalid("beauty recipe is invalid")
        BeautyChoice(preset, recipe)
      }
      val frame = (map["frame"] as? Map<*, *>)?.let { f ->
        val id = f["id"] as? String ?: invalid("frame id is required")
        if (FrameStyles.byId(id) == null) invalid("Unknown frame '$id'")
        FrameChoice(id, num("zoom", 1f, 3f, f).coerceAtLeast(1f), num("x", -1f, 1f, f), num("y", -1f, 1f, f))
      }
      return EditDocument(
        quarterTurns = (((map["quarterTurns"] as? Number)?.toInt() ?: 0) % 4 + 4) % 4,
        flipH = map["flipH"] == true,
        straighten = num("straighten", -45f, 45f),
        perspectiveV = num("perspectiveV", -1f, 1f),
        perspectiveH = num("perspectiveH", -1f, 1f),
        crop = crop,
        exposure = num("exposure", -2f, 2f),
        brightness = num("brightness", -1f, 1f),
        contrast = num("contrast", -1f, 1f),
        saturation = num("saturation", -1f, 1f),
        warmth = num("warmth", -1f, 1f),
        sharpen = num("sharpen", 0f, 1f),
        look = look,
        retouch = num("retouch", 0f, 1f),
        spotlight = (map["spotlight"] as? Map<*, *>)?.let { s ->
          val pts = (s["points"] as? List<*>)?.mapNotNull { (it as? Number)?.toFloat() } ?: emptyList()
          if (pts.size % 2 != 0 || pts.size > 16 || pts.any { !it.isFinite() }) invalid("Spotlight points are invalid")
          SpotlightChoice(pts.chunked(2).map { it[0].coerceIn(0f, 1f) to it[1].coerceIn(0f, 1f) }, num("blur", 0f, 1f, s))
        },
        transform = map["transform"] == true,
        focus = focus,
        portrait = portrait,
        props = props,
        frame = frame,
        beauty = beauty,
      )
    }

    private fun invalid(message: String): Nothing = throw EngineException(EngineException.INVALID_ARGUMENT, message)
  }
}
