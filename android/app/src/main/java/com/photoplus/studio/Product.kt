package com.photoplus.studio

import com.photoplus.engine.EngineException

/**
 * Product photography (Phase 12): how a cut-out product becomes a catalogue
 * shot (src/services/product/setup.ts). Every value is checked and clamped
 * here; [premiumReason] says why a save needs Premium (null: free).
 */
enum class ProductBackdropKind(val engine: Int, val premium: Boolean) {
  TRANSPARENT(0, true),
  SOLID(1, false),
  SWEEP(2, false),
  SURFACE(3, true),
  RADIAL(4, true),
  SCENE(5, true),
}

data class ProductBackdrop(
  val kind: ProductBackdropKind = ProductBackdropKind.SWEEP,
  val wall: Int = 0xF0F0F0,
  val floor: Int = 0xFCFCFC,
  val horizon: Float = 0.65f,
  val softness: Float = 0.5f,
  val light: Float = 0.5f,
  /** A scene photo from the studio's plates (SCENE only). */
  val plate: String? = null,
)

data class ProductShadow(
  val contact: Float = 0.6f,
  val soft: Float = 0.35f,
  val cast: Float = 0f,
  val castAngle: Float = 25f,
  val castLength: Float = 0.45f,
  val castSoftness: Float = 0.5f,
  val reflection: Float = 0f,
)

data class ProductCorrection(
  val autoWhite: Boolean = true,
  val autoExposure: Boolean = false,
  val exposure: Float = 0f,
  val contrast: Float = 0f,
  val saturation: Float = 0f,
  val warmth: Float = 0f,
  val tint: Float = 0f,
)

data class ProductLayout(
  val fill: Float = 0.8f,
  val x: Float = 0.5f,
  val y: Float = 0.5f,
  val ground: Boolean = false,
  val flip: Boolean = false,
)

data class ProductSetup(
  val backdrop: ProductBackdrop = ProductBackdrop(),
  val shadow: ProductShadow = ProductShadow(),
  val correction: ProductCorrection = ProductCorrection(),
  val layout: ProductLayout = ProductLayout(),
) {
  /** Why saving this needs Premium, or null. */
  fun premiumReason(): String? = when {
    backdrop.kind.premium -> "This backdrop is part of Premium"
    shadow.cast > 0f || shadow.reflection > 0f -> "Cast shadows and reflections are part of Premium"
    else -> null
  }

  fun ints(width: Int, height: Int, seed: Int = 7): IntArray = intArrayOf(
    backdrop.kind.engine,
    (backdrop.wall shr 16) and 0xFF, (backdrop.wall shr 8) and 0xFF, backdrop.wall and 0xFF,
    (backdrop.floor shr 16) and 0xFF, (backdrop.floor shr 8) and 0xFF, backdrop.floor and 0xFF,
    if (correction.autoWhite) 1 else 0, if (correction.autoExposure) 1 else 0,
    width, height, if (layout.ground) 1 else 0, if (layout.flip) 1 else 0, seed,
  )

  /** [reference]: the photographed backdrop (sRGB 0..255), or null when it was not plain. */
  fun floats(reference: FloatArray?): FloatArray = floatArrayOf(
    backdrop.horizon, backdrop.softness, backdrop.light,
    shadow.contact, shadow.soft, shadow.cast, shadow.castAngle, shadow.castLength, shadow.castSoftness, shadow.reflection,
    reference?.getOrNull(0) ?: -1f, reference?.getOrNull(1) ?: -1f, reference?.getOrNull(2) ?: -1f,
    correction.exposure, correction.contrast, correction.saturation, correction.warmth, correction.tint,
    layout.fill, layout.x, layout.y,
  )

  companion object {
    private val PLATE = Regex("^[a-z0-9-]{1,48}$")

    @Suppress("UNCHECKED_CAST")
    fun fromMap(map: Map<String, Any?>?): ProductSetup {
      if (map == null) return ProductSetup()
      val b = map["backdrop"] as? Map<String, Any?> ?: emptyMap()
      val s = map["shadow"] as? Map<String, Any?> ?: emptyMap()
      val c = map["correction"] as? Map<String, Any?> ?: emptyMap()
      val l = map["layout"] as? Map<String, Any?> ?: emptyMap()
      val kindName = b["kind"] as? String ?: "sweep"
      val kind = ProductBackdropKind.entries.firstOrNull { it.name.equals(kindName, ignoreCase = true) }
        ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown backdrop '$kindName'")
      val plate = (b["plate"] as? String)?.takeIf { it.isNotBlank() }
      if (plate != null && !PLATE.matches(plate)) throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown scene")
      if (kind == ProductBackdropKind.SCENE && plate == null) throw EngineException(EngineException.INVALID_ARGUMENT, "A scene backdrop needs a scene")
      val d = ProductSetup()
      return ProductSetup(
        backdrop = ProductBackdrop(
          kind = kind,
          wall = colour(b["wall"], d.backdrop.wall),
          floor = colour(b["floor"], d.backdrop.floor),
          horizon = num(b, "horizon", d.backdrop.horizon, 0.05f, 0.95f),
          softness = num(b, "softness", d.backdrop.softness, 0f, 1f),
          light = num(b, "light", d.backdrop.light, 0f, 1f),
          plate = plate,
        ),
        shadow = ProductShadow(
          contact = num(s, "contact", d.shadow.contact, 0f, 1f),
          soft = num(s, "soft", d.shadow.soft, 0f, 1f),
          cast = num(s, "cast", d.shadow.cast, 0f, 1f),
          castAngle = num(s, "castAngle", d.shadow.castAngle, -70f, 70f),
          castLength = num(s, "castLength", d.shadow.castLength, -1.5f, 1.5f),
          castSoftness = num(s, "castSoftness", d.shadow.castSoftness, 0f, 1f),
          reflection = num(s, "reflection", d.shadow.reflection, 0f, 1f),
        ),
        correction = ProductCorrection(
          autoWhite = c["autoWhite"] as? Boolean ?: d.correction.autoWhite,
          autoExposure = c["autoExposure"] as? Boolean ?: d.correction.autoExposure,
          exposure = num(c, "exposure", 0f, -1f, 1f),
          contrast = num(c, "contrast", 0f, -1f, 1f),
          saturation = num(c, "saturation", 0f, -1f, 1f),
          warmth = num(c, "warmth", 0f, -1f, 1f),
          tint = num(c, "tint", 0f, -1f, 1f),
        ),
        layout = ProductLayout(
          fill = num(l, "fill", d.layout.fill, 0.1f, 1f),
          x = num(l, "x", d.layout.x, 0f, 1f),
          y = num(l, "y", d.layout.y, 0f, 1f),
          ground = l["ground"] as? Boolean ?: false,
          flip = l["flip"] as? Boolean ?: false,
        ),
      )
    }

    internal fun num(m: Map<String, Any?>, key: String, default: Float, lo: Float, hi: Float): Float {
      val v = (m[key] as? Number)?.toFloat() ?: return default
      if (!v.isFinite()) throw EngineException(EngineException.INVALID_ARGUMENT, "'$key' must be a number")
      return v.coerceIn(lo, hi)
    }

    internal fun colour(v: Any?, default: Int): Int {
      val raw = v as? String ?: return default
      if (!Regex("^#[0-9a-fA-F]{6}$").matches(raw)) throw EngineException(EngineException.INVALID_ARGUMENT, "Colours are #RRGGBB")
      return raw.substring(1).toInt(16)
    }
  }
}

/**
 * How a product photo is delivered: its frame's shape, size (short edge, as
 * the download sizes), format and quality. A transparent backdrop is a PNG.
 */
data class ExportProfile(
  val aspectW: Int = 1,
  val aspectH: Int = 1,
  /** Short edge, px (ExportSize.SIZES). */
  val resolution: Int = 1080,
  val png: Boolean = false,
  val quality: Int = 92,
) {
  /** The frame, px: the short edge [resolution], the long edge within ExportSize.MAX_EDGE. */
  fun frame(): Pair<Int, Int> = ExportSize.target(aspectW * 1000, aspectH * 1000, resolution)

  /** A preview of the same shape, its long edge [edge] px. */
  fun previewFrame(edge: Int): Pair<Int, Int> {
    val k = edge.toFloat() / maxOf(aspectW, aspectH)
    return maxOf(16, (aspectW * k).toInt()) to maxOf(16, (aspectH * k).toInt())
  }

  fun premiumReason(): String? = when {
    ExportSize.needsPremium(resolution) -> "720p, 1080p, HD and 4K downloads are part of Premium"
    png -> "Transparent PNG downloads are part of Premium"
    else -> null
  }

  companion object {
    fun fromMap(map: Map<String, Any?>?, transparent: Boolean): ExportProfile {
      if (map == null) return ExportProfile(png = transparent)
      val w = (map["aspectW"] as? Number)?.toInt() ?: 1
      val h = (map["aspectH"] as? Number)?.toInt() ?: 1
      if (w !in 1..32 || h !in 1..32 || maxOf(w, h) > 4 * minOf(w, h)) {
        throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown photo shape")
      }
      val px = ExportSize.parse(map["resolution"] ?: 1080) ?: 1080
      val format = map["format"] as? String ?: "jpeg"
      if (format != "jpeg" && format != "png") throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown format '$format'")
      val quality = ((map["quality"] as? Number)?.toInt() ?: 92).coerceIn(70, 100)
      return ExportProfile(w, h, px, png = transparent || format == "png", quality = quality)
    }
  }
}

/** The cut-out's own settings (the edge). */
data class ProductMatte(val tolerance: Float = 0.5f, val softness: Float = 0.2f, val shift: Float = 0f) {
  fun toArray() = floatArrayOf(tolerance, softness, shift, 1f)

  companion object {
    fun fromMap(map: Map<String, Any?>): ProductMatte = ProductMatte(
      tolerance = ProductSetup.num(map, "tolerance", 0.5f, 0f, 1f),
      softness = ProductSetup.num(map, "softness", 0.2f, 0f, 1f),
      shift = ProductSetup.num(map, "shift", 0f, -1f, 1f),
    )
  }
}
