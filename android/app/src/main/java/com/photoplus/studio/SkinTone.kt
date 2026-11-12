package com.photoplus.studio

/**
 * Skin Tone shades: the skin of the face and the visible body moved toward
 * a shade. The ids, colours and sheen are the app's
 * (src/services/skintones/skintones.json); the unit test checks they match.
 */
enum class SkinShade(val id: String, val color: Int, val glow: Float) {
  PORCELAIN_ROSE("porcelain_rose", 0xEABFC3, 0.15f),
  IVORY("ivory", 0xF1DDC6, 0f),
  PEACH("peach", 0xE9B796, 0.1f),
  GOLDEN_BEIGE("golden_beige", 0xDCB680, 0f),
  HONEY("honey", 0xD6A060, 0.1f),
  OLIVE("olive", 0xAC9A6C, 0f),
  SUN_KISSED("sun_kissed", 0xB7794E, 0.4f),
  CARAMEL("caramel", 0xB8743C, 0.1f),
  BRONZE("bronze", 0x855442, 0.35f),
  CINNAMON("cinnamon", 0x8C4E36, 0.1f),
  MOCHA("mocha", 0x6E5040, 0.1f),
  ESPRESSO("espresso", 0x562C20, 0.1f),
  EBONY_GLOW("ebony_glow", 0x33241E, 0.7f);

  /** sRGB 0..1, as the engine takes it. */
  val r: Float get() = ((color shr 16) and 0xFF) / 255f
  val g: Float get() = ((color shr 8) and 0xFF) / 255f
  val b: Float get() = (color and 0xFF) / 255f

  companion object {
    fun byId(id: String?): SkinShade? = entries.firstOrNull { it.id == id }
  }
}

/** A shade as chosen: which one and how strongly it is applied (0..1). */
data class SkinToneChoice(val shade: SkinShade, val strength: Float = 1f) {
  val active: Boolean get() = strength > 0f

  companion object {
    /** From the bridge ({ id, strength }); null when absent or unknown. */
    fun fromMap(map: Map<*, *>?): SkinToneChoice? {
      val shade = SkinShade.byId(map?.get("id") as? String) ?: return null
      val strength = ((map?.get("strength") as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: 1f).coerceIn(0f, 1f)
      return SkinToneChoice(shade, strength)
    }
  }
}
