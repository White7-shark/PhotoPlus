package com.photoplus.studio

import com.photoplus.engine.EngineException
import com.photoplus.studio.toon.CloudToonEngine
import com.photoplus.studio.toon.NoCloudToonEngine
import com.photoplus.studio.toon.ModelToonEngine
import com.photoplus.studio.toon.ToonCond
import com.photoplus.studio.toon.ToonContours
import com.photoplus.studio.toon.ToonEngine
import com.photoplus.studio.toon.ToonEngines
import com.photoplus.studio.toon.ToonInput
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertSame
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class ToonEngineTest {
  private class Fake(override val id: String, private val available: Boolean, private val fails: String? = null) : ToonEngine {
    var drawn = 0
    override fun isAvailable() = available
    override fun render(input: ToonInput, token: Long) = draw()
    fun draw() {
      drawn++
      fails?.let { throw EngineException(it, "no") }
    }
  }

  @Test
  fun contoursAreEncodedInMlKitsOrderNormalised() {
    assertEquals(ToonContours.POINTS, ToonContours.COUNTS.sum())
    assertEquals(131, ToonContours.POINTS)
    val parts = ToonContours.COUNTS.mapIndexed { p, n -> List(n) { i -> (p * 10f + i) to (i * 2f) } }
    val out = ToonContours.encode(parts, 200f, 100f)
    assertEquals(262, out.size)
    // The first point of the left eye (part 5) sits after the oval and the brows: 36 + 4 * 5 = 56.
    assertEquals(50f / 200f, out[56 * 2], 1e-6f)
    assertEquals(0f, out[56 * 2 + 1], 1e-6f)
    // The last value: the nose bottom's third point (part 12, i = 2), y = 4 / 100.
    assertEquals(0.04f, out.last(), 1e-6f)
  }

  @Test
  fun aMissingOrOddPartLeavesTheFaceUnknown() {
    val parts: List<List<Pair<Float, Float>>?> = ToonContours.COUNTS.map { n -> List(n) { 1f to 1f } }
    val unknown = FloatArray(ToonContours.FLOATS) { -1f }
    assertArrayEquals(unknown, ToonContours.encode(parts.toMutableList().also { it[5] = null }, 100f, 100f))
    assertArrayEquals(unknown, ToonContours.encode(parts.toMutableList().also { it[7] = List(10) { 1f to 1f } }, 100f, 100f))
    assertArrayEquals(unknown, ToonContours.encode(parts.dropLast(1), 100f, 100f))
    assertArrayEquals(unknown, ToonContours.encode(parts, 0f, 100f))
  }

  @Test
  fun theModelDrawsOnlyWhenItsFlagIsOnAndItIsThere() {
    val classic = Fake("classic", true)
    assertSame(classic, ToonEngines.pick(false, Fake("model", true), classic))
    assertSame(classic, ToonEngines.pick(true, Fake("model", false), classic))
    val model = Fake("model", true)
    assertSame(model, ToonEngines.pick(true, model, classic))
    // Off in this build: no model is bundled yet.
    assertFalse(ToonEngines.modelFlag)
  }

  @Test
  fun aFailingModelFallsBackToTheClassicEngineButACancelStops() {
    val classic = Fake("classic", true)
    val broken = Fake("model", true, EngineException.INTERNAL)
    assertSame(classic, ToonEngines.withFallback(broken, classic) { (it as Fake).draw() })
    assertEquals(1, broken.drawn)
    assertEquals(1, classic.drawn)
    val cancelled = Fake("model", true, EngineException.CANCELLED)
    assertThrows(EngineException::class.java) { ToonEngines.withFallback(cancelled, classic) { (it as Fake).draw() } }
    assertEquals(1, classic.drawn)
    val fine = Fake("model", true)
    assertSame(fine, ToonEngines.withFallback(fine, classic) { (it as Fake).draw() })
  }

  @Test
  fun thereIsNoCloudEngineYet() {
    val cloud: CloudToonEngine = NoCloudToonEngine
    assertFalse(cloud.isAvailable())
    assertEquals("cloud", cloud.id)
  }

  // The same example as tools/toon_model/cond.py (EXAMPLE_PARAMS, EXAMPLE_COND): the app and the training agree.
  @Test
  fun theModelReadsTheSettingsExactlyAsItWasTrained() {
    val params = floatArrayOf(2f, 3f, 0.75f, 4f, 0.5f, 0.45f, 0.15f, 0.5f, 0f, 0.8f, 0.25f, 1f, -0.5f, 0.5f, 0.5f, 0.6f, 0.4f, 1f)
    val want = floatArrayOf(
      0f, 0f, 1f, 0f, 0f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f, 0f, 0f,
      0.75f, 0.5f, 0.75f, 0.45f, 0.15f, 0.5f, 0f, 0.25f, 1f, 0.25f, 0.75f, 0.5f, 0.6f, 0.4f, 1f,
    )
    assertEquals(31, ToonCond.SIZE)
    assertArrayEquals(want, ToonCond.encode(params, hasMask = true), 1e-6f)
    // Out of range is clamped; no mask is said so.
    val wild = ToonCond.encode(FloatArray(ToonInput.PARAMS) { 99f }, hasMask = false)
    assertEquals(1f, wild[5])
    assertEquals(1f, wild[11])
    assertEquals(1f, wild[15])
    assertEquals(0f, wild.last())
    assertTrue(wild.all { it in 0f..1f })
    assertThrows(IllegalArgumentException::class.java) { ToonCond.encode(FloatArray(3), true) }
  }

  @Test
  fun theModelIsPhotoPlusOwnAndNotShippedYet() {
    assertEquals("toon_photoplus.tflite", ModelToonEngine.SPEC.asset)
    assertFalse(ModelToonEngine(null).isAvailable())
    assertFalse(com.photoplus.BuildConfig.TOON_MODEL_ENGINE)
  }
}
