package com.photoplus.pose

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class PoseDataTest {
  private fun flat(v: Double = 0.5) = List(PoseData.FLOATS) { v }

  @Test fun landmarksAcceptFlatOrNestedAndRejectJunk() {
    assertEquals(PoseData.FLOATS, PoseData.landmarks(flat())!!.size)
    val nested = List(PoseData.POINTS) { listOf(0.1, 0.2, 0.0, 0.9, 0.95) }
    val n = PoseData.landmarks(nested)!!
    assertEquals(0.2f, n[1])
    assertEquals(0.95f, n[PoseData.FLOATS - 1])
    assertNull(PoseData.landmarks(List(10) { 0.0 }))
    assertNull(PoseData.landmarks(flat().toMutableList().also { it[3] = Double.NaN }))
    assertNull(PoseData.landmarks(flat().toMutableList<Any?>().also { it[0] = "x" }))
    assertNull(PoseData.landmarks("nope"))
  }

  @Test fun guideParsesAndClamps() {
    assertNull(PoseGuide.fromMap(null))
    val follow = PoseGuide.fromMap(emptyMap())!!
    assertNull(follow.reference)
    assertTrue(follow.framing)
    assertEquals(0.5f, follow.strictness)
    val g = PoseGuide.fromMap(mapOf(
      "reference" to mapOf("landmarks" to flat(), "aspect" to 0.75),
      "mirrorReference" to true, "framing" to false, "strictness" to 4.0,
    ))!!
    assertNotNull(g.reference)
    assertEquals(0.75f, g.referenceAspect)
    assertTrue(g.mirrorReference)
    assertFalse(g.framing)
    assertEquals(1f, g.strictness)
    // A bad aspect means no reference (never a broken comparison).
    assertNull(PoseGuide.fromMap(mapOf("reference" to mapOf("landmarks" to flat(), "aspect" to 0.0)))!!.reference)
    assertEquals(g, g.copy())
  }

  @Test fun stepReadsTheEngineLayout() {
    val o = FloatArray(PoseStep.OUT)
    o[0] = 1f; o[1] = 0.9f; o[2] = 82f; o[3] = 0.8f; o[4] = 88f; o[5] = 70f; o[6] = 0.1f; o[7] = -0.05f; o[8] = 1.2f
    o[9] = 2f; o[10] = 1f; o[11] = 2f
    o[12] = 0f; o[13] = 3f; o[14] = 0.2f   // body, back
    o[15] = 3f; o[16] = 10f; o[17] = 0.6f  // right hand, up
    o[38] = 0.5f; o[39] = 0.4f; o[40] = 0.12f; o[42] = 0.5f; o[43] = 0.2f; o[44] = 0.9f
    for (i in 0 until PoseData.POINTS) { o[45 + i * 3] = 0.3f; o[46 + i * 3] = 0.6f; o[47 + i * 3] = 0.8f }
    val m = PoseStep.toMap(o, mirrored = true)
    assertEquals(true, m["person"])
    assertEquals(true, m["mirrored"])
    assertEquals("halfBody", m["shot"])
    assertEquals(listOf(0.5, 0.20000000298023224), m["face"])
    assertEquals(33, (m["points"] as List<*>).size)
    @Suppress("UNCHECKED_CAST") val match = m["match"] as Map<String, Any?>
    assertEquals(82.0, match["score"])
    @Suppress("UNCHECKED_CAST") val advice = match["advice"] as List<Map<String, Any?>>
    assertEquals("back", advice[0]["action"])
    assertEquals("rightHand", advice[1]["part"])
    assertEquals("up", advice[1]["action"])
    // No person, no reference: no points, no match.
    val empty = PoseStep.toMap(FloatArray(PoseStep.OUT), mirrored = false)
    assertEquals(false, empty["person"])
    assertNull(empty["match"])
    assertNull(empty["face"])
    assertTrue((empty["points"] as List<*>).isEmpty())
    // Codes line up with the engine's enums.
    assertEquals(23, PoseStep.ACTIONS.size)
    assertEquals("perfect", PoseStep.ACTIONS[22])
    assertEquals(12, PoseStep.PARTS.size)
  }

  @Test fun levelFromGravity() {
    val upright = LevelMath.fromGravity(0f, 9.81f, 0f)!!
    assertEquals(0.0, upright.roll, 1e-6)
    assertEquals(0, upright.orientation)
    assertTrue(LevelMath.isLevel(upright))
    // Tilted 5° clockwise in portrait.
    val tilt = LevelMath.fromGravity(9.81f * Math.sin(Math.toRadians(5.0)).toFloat(), 9.81f * Math.cos(Math.toRadians(5.0)).toFloat(), 0f)!!
    assertEquals(5.0, tilt.roll, 0.01)
    assertFalse(LevelMath.isLevel(tilt))
    // Landscape, 3° off: measured from landscape, not portrait.
    val land = LevelMath.fromGravity(9.81f * Math.cos(Math.toRadians(3.0)).toFloat(), -9.81f * Math.sin(Math.toRadians(3.0)).toFloat(), 0f)!!
    assertEquals(90, land.orientation)
    assertEquals(3.0, land.roll, 0.01)
    // Camera aimed down at a table: pitch near 90.
    assertEquals(90.0, LevelMath.fromGravity(0f, 0f, 9.81f)!!.pitch, 0.01)
    assertNull(LevelMath.fromGravity(0f, 0f, 0f))
    assertNull(LevelMath.fromGravity(Float.NaN, 1f, 1f))
  }
}
