package com.photoplus.beauty

import com.photoplus.engine.NativeEngine

/**
 * A resolved beauty recipe, as JS sends it (src/services/beauty/recipe.ts
 * resolveRecipe): skin/portrait, face and the grade, flattened in the
 * engine's order for the bridge ([toArray], NativeEngine.nativeBeauty) and
 * the live shader (uG = [grade]).
 *
 * Every value is range-checked here: non-finite numbers are dropped, ranges
 * clamped, so nothing a caller sends can push the engine out of bounds.
 */
class BeautyRecipe private constructor(
  /** blur, smooth, blemish, even, brighten, edge */
  val portrait: FloatArray,
  /** eyes, teeth, lips, underEye, shine, faceLight, studio, blush, blush R, G, B, contour, highlight */
  val face: FloatArray,
  /** pp_beauty_grade, [NativeEngine.BEAUTY_GRADE_FLOATS] floats */
  val grade: FloatArray,
  val autoLight: Boolean,
) {
  val blur: Float get() = portrait[0]

  /** Skin layer values for the live shader: smooth, blemish, even, brighten. */
  val skin: FloatArray get() = portrait.copyOfRange(1, 5)

  /** True when the recipe changes anything. */
  val active: Boolean
    get() = portrait.take(5).any { it != 0f } ||
      FACE_AMOUNTS.any { face[it] != 0f } ||
      GRADE_AMOUNTS.any { grade[it] != 0f } ||
      (grade[BAND_WIDTH] > 0f && (grade[BAND_SHIFT] != 0f || grade[BAND_SATURATION] != 0f))

  /** Whether the background is separated from the person (a subject mask is needed). */
  val needsSubject: Boolean get() = blur > 0f || GRADE_AMOUNTS.any { grade[it] != 0f }

  /** A stable cache key (the editor's slow-stage cache). */
  fun key(): String = toArray().joinToString(",")

  fun toArray(): FloatArray {
    val out = FloatArray(NativeEngine.BEAUTY_RECIPE_FLOATS)
    portrait.copyInto(out, 0)
    face.copyInto(out, 6)
    grade.copyInto(out, 19)
    out[out.size - 1] = if (autoLight) 1f else 0f
    return out
  }

  companion object {
    private enum class Kind { UNIT, SIGNED, COLOR, DEGREES, SHIFT, POSITION, SIZE }

    /** pp_beauty_grade's fields in order, with their ranges (colours take three floats). */
    private val GRADE: List<Pair<String, Kind>> = listOf(
      "temperature" to Kind.SIGNED, "tint" to Kind.SIGNED, "contrast" to Kind.SIGNED, "blacks" to Kind.SIGNED,
      "highlights" to Kind.SIGNED, "saturation" to Kind.SIGNED, "vibrance" to Kind.SIGNED,
      "shadowColor" to Kind.COLOR, "shadowAmount" to Kind.UNIT, "highlightColor" to Kind.COLOR, "highlightAmount" to Kind.UNIT,
      "bandColor" to Kind.COLOR, "bandWidth" to Kind.DEGREES, "bandHueShift" to Kind.SHIFT, "bandSaturation" to Kind.SIGNED,
      "skinProtect" to Kind.UNIT, "skinWarmth" to Kind.SIGNED, "skinRosy" to Kind.UNIT, "skinLuminance" to Kind.SIGNED,
      "subjectLight" to Kind.SIGNED, "lightWarmth" to Kind.SIGNED, "shadowLift" to Kind.UNIT, "rim" to Kind.UNIT,
      "rimColor" to Kind.COLOR, "bloom" to Kind.UNIT, "softFocus" to Kind.UNIT, "skinSheen" to Kind.UNIT,
      "bgExposure" to Kind.SIGNED, "bgSaturation" to Kind.SIGNED, "bgTint" to Kind.COLOR, "bgTintAmount" to Kind.UNIT,
      "bgGlow" to Kind.UNIT, "bgGlowX" to Kind.POSITION, "bgGlowY" to Kind.POSITION, "bgGlowSize" to Kind.SIZE,
      "bgGlowColor" to Kind.COLOR, "bgVignette" to Kind.UNIT, "bgHaze" to Kind.UNIT, "bgWarmth" to Kind.SIGNED,
    )
    private val FACE = listOf("eyes", "teeth", "lips", "underEye", "shine", "faceLight", "studio", "blush")
    private val FACE_AMOUNTS = intArrayOf(0, 1, 2, 3, 4, 5, 6, 7, 11, 12)

    /** Offset of each grade field in the flattened array. */
    private val OFFSETS: Map<String, Int> = buildMap {
      var i = 0
      for ((name, kind) in GRADE) {
        put(name, i)
        i += if (kind == Kind.COLOR) 3 else 1
      }
    }
    private val BAND_WIDTH = OFFSETS.getValue("bandWidth")
    private val BAND_SHIFT = OFFSETS.getValue("bandHueShift")
    private val BAND_SATURATION = OFFSETS.getValue("bandSaturation")
    /** Grade fields that change the photo when non-zero (not colours, positions or protection). */
    private val GRADE_AMOUNTS: IntArray = GRADE
      .filter { (_, kind) -> kind == Kind.UNIT || kind == Kind.SIGNED }
      .filter { (name, _) -> name != "skinProtect" && name != "bandSaturation" }
      .map { OFFSETS.getValue(it.first) }.toIntArray()

    /** Floats the named fields use (51); the rest of pp_beauty_grade is reserved. */
    val GRADE_USED: Int = GRADE.sumOf { if (it.second == Kind.COLOR) 3 else 1 }

    private fun num(v: Any?): Float? = (v as? Number)?.toFloat()?.takeIf { it.isFinite() }

    private fun range(kind: Kind, v: Float): Float = when (kind) {
      Kind.UNIT, Kind.COLOR, Kind.POSITION -> v.coerceIn(0f, 1f)
      Kind.SIGNED -> v.coerceIn(-1f, 1f)
      Kind.DEGREES -> v.coerceIn(0f, 90f)
      Kind.SHIFT -> v.coerceIn(-60f, 60f)
      Kind.SIZE -> v.coerceIn(0.02f, 1.5f)
    }

    private fun defaultOf(name: String): Float = when (name) {
      "skinProtect" -> 0.75f
      "bgGlowX" -> 0.8f
      "bgGlowY" -> 0.2f
      "bgGlowSize" -> 0.35f
      else -> 0f
    }

    private fun color(v: Any?, fallback: FloatArray): FloatArray {
      val list = (v as? List<*>)?.map { num(it) }
      return if (list != null && list.size == 3 && list.all { it != null }) {
        FloatArray(3) { list[it]!!.coerceIn(0f, 1f) }
      } else {
        fallback.copyOf()
      }
    }

    private val GREY = floatArrayOf(0.5f, 0.5f, 0.5f)
    private val COLOR_DEFAULTS = mapOf(
      "rimColor" to floatArrayOf(1f, 0.92f, 0.8f),
      "bgGlowColor" to floatArrayOf(1f, 0.8f, 0.5f),
    )

    /** From the bridge map (resolveRecipe's shape); null when it is not a recipe. */
    fun fromMap(map: Map<*, *>?): BeautyRecipe? {
      if (map == null) return null
      val p = map["portrait"] as? Map<*, *> ?: return null
      val f = map["face"] as? Map<*, *> ?: return null
      val g = map["grade"] as? Map<*, *> ?: return null
      val portrait = floatArrayOf(
        num(p["blur"]) ?: 0f, num(p["smooth"]) ?: 0f, num(p["blemish"]) ?: 0f, num(p["even"]) ?: 0f,
        num(p["brighten"]) ?: 0f, num(p["edge"]) ?: 0f,
      ).map { it.coerceIn(0f, 1f) }.toFloatArray()
      val face = FloatArray(13)
      FACE.forEachIndexed { i, name -> face[i] = (num(f[name]) ?: 0f).coerceIn(0f, 1f) }
      color(f["blushColor"], floatArrayOf(0.86f, 0.42f, 0.47f)).copyInto(face, 8)
      face[11] = (num(f["contour"]) ?: 0f).coerceIn(0f, 1f)
      face[12] = (num(f["highlight"]) ?: 0f).coerceIn(0f, 1f)
      val grade = FloatArray(NativeEngine.BEAUTY_GRADE_FLOATS)
      for ((name, kind) in GRADE) {
        val at = OFFSETS.getValue(name)
        if (kind == Kind.COLOR) {
          color(g[name], COLOR_DEFAULTS[name] ?: GREY).copyInto(grade, at)
        } else {
          grade[at] = range(kind, num(g[name]) ?: defaultOf(name))
        }
      }
      return BeautyRecipe(portrait, face, grade, map["autoLight"] as? Boolean ?: true)
    }

    /** No beauty at all. */
    fun none(): BeautyRecipe = fromMap(mapOf("portrait" to emptyMap<String, Any>(), "face" to emptyMap<String, Any>(), "grade" to emptyMap<String, Any>()))!!
  }
}
