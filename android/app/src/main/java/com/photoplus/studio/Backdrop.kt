package com.photoplus.studio

/**
 * Backdrops: background designs behind the person. The order is the
 * engine's (pp_backdrop_design) and the ids are the app's
 * (src/services/backdrops/backdrops.json); the unit test checks both.
 */
enum class Backdrop(val id: String) {
  SIGNATURE("signature"),
  SOFT_STUDIO("soft_studio"),
  STAGE("stage"),
  NOIR("noir"),
  PORCELAIN("porcelain"),
  CANVAS("canvas"),
  GOLDEN_HOUR("golden_hour"),
  CITY_LIGHTS("city_lights"),
  NEON("neon"),
  COLOR_POP("color_pop"),
  VELOCITY("velocity"),
  SWIRL("swirl"),
  AURORA("aurora"),
  DREAM("dream");

  val engineId: Int get() = ordinal

  /** Only Signature uses depth (its lens blur follows distance). */
  val usesDepth: Boolean get() = this == SIGNATURE

  companion object {
    fun byId(id: String?): Backdrop? = entries.firstOrNull { it.id == id }
  }
}

/** A backdrop as chosen: the design and how strongly it is applied (0..1). */
data class BackdropChoice(val design: Backdrop, val strength: Float = 1f) {
  val active: Boolean get() = strength > 0f

  companion object {
    /** From the bridge ({ id, strength }); null when absent or unknown. */
    fun fromMap(map: Map<*, *>?): BackdropChoice? {
      val design = Backdrop.byId(map?.get("id") as? String) ?: return null
      val strength = ((map?.get("strength") as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: 1f).coerceIn(0f, 1f)
      return BackdropChoice(design, strength)
    }
  }
}
