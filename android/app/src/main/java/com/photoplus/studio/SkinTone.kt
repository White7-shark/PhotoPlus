package com.photoplus.studio

/**
 * Skin Tone shades: the skin of the face and the visible body given a new
 * colour, as a change of the skin's own colour under the photo's light
 * ([gains] on linear red, green, blue; 1 = unchanged). [color] is only the
 * swatch shown in the app. Ids, swatches, gains and sheen are the app's
 * (src/services/skintones/skintones.json); the unit test checks they match.
 */
enum class SkinShade(val id: String, val color: Int, val gains: FloatArray, val glow: Float) {
  PORCELAIN_ROSE("porcelain_rose", 0xEABFC3, floatArrayOf(1.535f, 1.321f, 1.581f), 0.15f),
  IVORY("ivory", 0xF1DDC6, floatArrayOf(1.449f, 1.434f, 1.666f), 0.0f),
  PEACH("peach", 0xE9B796, floatArrayOf(1.457f, 1.254f, 1.192f), 0.1f),
  GOLDEN_BEIGE("golden_beige", 0xDCB680, floatArrayOf(1.346f, 1.359f, 1.102f), 0.0f),
  HONEY("honey", 0xD6A060, floatArrayOf(1.168f, 1.122f, 0.708f), 0.1f),
  OLIVE("olive", 0xAC9A6C, floatArrayOf(1.09f, 1.123f, 0.929f), 0.0f),
  SUN_KISSED("sun_kissed", 0xB7794E, floatArrayOf(1.23f, 0.997f, 0.746f), 0.4f),
  CARAMEL("caramel", 0xB8743C, floatArrayOf(1.088f, 0.985f, 0.701f), 0.1f),
  BRONZE("bronze", 0x855442, floatArrayOf(0.813f, 0.829f, 0.64f), 0.35f),
  CINNAMON("cinnamon", 0x8C4E36, floatArrayOf(1.105f, 0.843f, 0.961f), 0.1f),
  MOCHA("mocha", 0x6E5040, floatArrayOf(0.803f, 0.787f, 0.779f), 0.1f),
  ESPRESSO("espresso", 0x562C20, floatArrayOf(0.818f, 0.683f, 0.618f), 0.1f),
  EBONY_GLOW("ebony_glow", 0x33241E, floatArrayOf(0.641f, 0.622f, 0.722f), 0.6f);

  companion object {
    /** How much blotchy colour is evened while the shade is applied (0..1). */
    const val EVENNESS = 0.5f

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
