package com.photoplus.pose

import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * The pose assistant's plain data (no Android types, JVM-tested): the guide
 * the app asks for, the engine's per-frame answer, and the level.
 *
 * Landmarks are MediaPipe's 33 (x, y, z, visibility, presence each), x and y
 * as shares of the upright image, the person's own left/right.
 */
object PoseData {
  const val POINTS = 33
  const val FLOATS = POINTS * 5

  /** Landmarks from the app: a flat list of 165 numbers, or 33 lists of 5. */
  fun landmarks(value: Any?): FloatArray? {
    val list = value as? List<*> ?: return null
    val flat = if (list.size == POINTS && list.all { it is List<*> }) list.flatMap { it as List<*> } else list
    if (flat.size != FLOATS) return null
    val out = FloatArray(FLOATS)
    for (i in 0 until FLOATS) {
      val v = (flat[i] as? Number)?.toFloat() ?: return null
      if (!v.isFinite()) return null
      out[i] = v
    }
    return out
  }
}

/** What the camera's pose assistant should do (from setPoseGuide). */
data class PoseGuide(
  /** The pose to match (null: just follow the person, for face position and simple guides). */
  val reference: FloatArray?,
  val referenceAspect: Float,
  /** Match the reference's mirror image (the other hand). */
  val mirrorReference: Boolean,
  /** Compare where the person is in the frame and how big. */
  val framing: Boolean,
  /** 0..1: tolerances shrink as it grows. */
  val strictness: Float,
) {
  companion object {
    fun fromMap(map: Map<String, Any?>?): PoseGuide? {
      if (map == null) return null
      val ref = map["reference"] as? Map<*, *>
      val points = ref?.let { PoseData.landmarks(it["landmarks"]) }
      val aspect = (ref?.get("aspect") as? Number)?.toFloat()?.takeIf { it.isFinite() && it > 0.05f && it < 20f }
      return PoseGuide(
        reference = if (points != null && aspect != null) points else null,
        referenceAspect = aspect ?: 1f,
        mirrorReference = map["mirrorReference"] as? Boolean ?: false,
        framing = map["framing"] as? Boolean ?: true,
        strictness = ((map["strictness"] as? Number)?.toFloat() ?: 0.5f).takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0.5f,
      )
    }
  }

  override fun equals(other: Any?): Boolean = other is PoseGuide && reference.contentEqualsNullable(other.reference) &&
    referenceAspect == other.referenceAspect && mirrorReference == other.mirrorReference &&
    framing == other.framing && strictness == other.strictness

  override fun hashCode(): Int = (reference?.contentHashCode() ?: 0) * 31 + referenceAspect.hashCode()
}

private fun FloatArray?.contentEqualsNullable(other: FloatArray?) =
  if (this == null || other == null) this === other else contentEquals(other)

/** The engine's answer for one frame (nativePoseStep's layout). */
object PoseStep {
  const val OUT = 45 + PoseData.POINTS * 3
  private const val POINTS_AT = 45

  /** Words for the app: names it can phrase (the engine's codes). */
  val PARTS = listOf("body", "head", "leftHand", "rightHand", "leftElbow", "rightElbow", "leftFoot", "rightFoot", "leftKnee", "rightKnee", "shoulders", "camera")
  val ACTIONS = listOf(
    "noPerson", "show", "closer", "back", "moveLeft", "moveRight", "cameraUp", "cameraDown", "turnLeft", "turnRight",
    "up", "down", "in", "out", "tiltLeft", "tiltRight", "lookLeft", "lookRight", "leanLeft", "leanRight",
    "straightenUp", "holdStill", "perfect",
  )
  val SHOTS = listOf("face", "headAndShoulders", "halfBody", "threeQuarter", "fullBody")

  /** The event payload for the app (preview fractions; sides as shown on screen). */
  fun toMap(o: FloatArray, mirrored: Boolean): Map<String, Any?> {
    require(o.size >= OUT)
    val person = o[0] > 0.5f
    val matched = o[10] > 0.5f
    val advice = (0 until o[11].roundToInt().coerceIn(0, 4)).mapNotNull { i ->
      val part = PARTS.getOrNull(o[12 + i * 3].roundToInt())
      val action = ACTIONS.getOrNull(o[13 + i * 3].roundToInt())
      if (part == null || action == null) null else mapOf("part" to part, "action" to action, "amount" to o[14 + i * 3].toDouble())
    }
    val points = if (!person) emptyList() else (0 until PoseData.POINTS).map { i ->
      listOf(o[POINTS_AT + i * 3].toDouble(), o[POINTS_AT + i * 3 + 1].toDouble(), o[POINTS_AT + i * 3 + 2].toDouble())
    }
    return mapOf(
      "person" to person,
      "mirrored" to mirrored,
      "stillness" to o[1].toDouble(),
      "shot" to SHOTS.getOrNull(o[9].roundToInt()),
      "anchor" to listOf(o[38].toDouble(), o[39].toDouble()),
      "unit" to o[40].toDouble(),
      "headTilt" to o[41].toDouble(),
      "face" to if (o[44] >= 0.5f) listOf(o[42].toDouble(), o[43].toDouble()) else null,
      "points" to points,
      "match" to if (!matched) null else mapOf(
        "score" to o[2].toDouble(),
        "coverage" to o[3].toDouble(),
        "pose" to o[4].toDouble(),
        "framing" to o[5].toDouble(),
        "offset" to listOf(o[6].toDouble(), o[7].toDouble()),
        "size" to o[8].toDouble(),
        "bones" to (0 until 12).map { o[26 + it].toDouble() },
        "hands" to listOf(o[24].toDouble(), o[25].toDouble()),
        "advice" to advice,
      ),
    )
  }
}

/**
 * The level: the phone's tilt from gravity (sensor axes: x right, y up the
 * screen, z out of it). Roll is measured from the nearest upright way of
 * holding the phone (portrait or landscape), pitch from vertical.
 */
object LevelMath {
  data class Level(val roll: Double, val pitch: Double, val orientation: Int)

  fun fromGravity(gx: Float, gy: Float, gz: Float): Level? {
    val g = sqrt(gx * gx + gy * gy + gz * gz)
    if (!g.isFinite() || g < 1f) return null
    // Around the screen's axis: 0 when held upright in portrait.
    val angle = Math.toDegrees(atan2(gx.toDouble(), gy.toDouble()))
    val orientation = (((angle / 90.0).roundToInt() * 90) % 360 + 360) % 360
    var roll = angle - (angle / 90.0).roundToInt() * 90.0
    if (roll > 45) roll -= 90 else if (roll < -45) roll += 90
    // Forward or back: 0 with the camera aimed at the horizon.
    val pitch = Math.toDegrees(atan2(gz.toDouble(), sqrt((gx * gx + gy * gy).toDouble())))
    return Level(roll, pitch, orientation)
  }

  /** Level enough to call it level (degrees). */
  fun isLevel(level: Level): Boolean = abs(level.roll) < 1.0
}
