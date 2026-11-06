package com.photoplus.ml

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows

/** The common model interface: specs, accelerator choice, warm-up checks, statistics. */
class ModelInterfaceTest {
  private val mean = floatArrayOf(0.5f, 0.5f, 0.5f)
  private val std = floatArrayOf(0.5f, 0.5f, 0.5f)

  private fun spec(accelerators: List<Accel>) = ModelRegistry.DEPTH.copy(id = "t", accelerators = accelerators)

  @Test fun acceleratorsAreTriedInOrderAndAFailedGpuIsSkipped() {
    assertEquals(listOf(Accel.GPU, Accel.CPU), AcceleratorPlan.order(spec(listOf(Accel.GPU, Accel.CPU)), gpuBlocked = false))
    assertEquals(listOf(Accel.CPU), AcceleratorPlan.order(spec(listOf(Accel.GPU, Accel.CPU)), gpuBlocked = true))
    assertEquals(listOf(Accel.CPU), AcceleratorPlan.order(spec(listOf(Accel.CPU)), gpuBlocked = false))
    // A GPU-only model on a device whose GPU failed still gets the CPU.
    assertEquals(listOf(Accel.CPU), AcceleratorPlan.order(spec(listOf(Accel.GPU)), gpuBlocked = true))
  }

  @Test fun warmupOutputsThatLookBrokenAreRejected() {
    assertTrue(AcceleratorPlan.plausible(floatArrayOf(0f, 0.3f, -2f)))
    assertFalse(AcceleratorPlan.plausible(floatArrayOf()))
    assertFalse(AcceleratorPlan.plausible(FloatArray(16)))
    assertFalse(AcceleratorPlan.plausible(floatArrayOf(1f, Float.NaN)))
    assertFalse(AcceleratorPlan.plausible(floatArrayOf(1f, Float.POSITIVE_INFINITY)))
  }

  @Test fun warmupInputIsAStructuredRampInTheModelsLayout() {
    for (layout in TensorInput.Layout.entries) {
      val input = TensorInput(4, 3, layout, TensorInput.Fit.STRETCH, mean, std)
      val ramp = AcceleratorPlan.warmupInput(input)
      assertEquals(4 * 3 * 3, ramp.size)
      assertTrue(ramp.all { it.isFinite() && it in -1f..1f })
      assertTrue(ramp.toSet().size > 1) // not constant (a constant input can hide broken paths)
      // The same value in every channel of a pixel, wherever the layout puts them.
      val n = 12
      for (i in 0 until n) {
        val c = if (layout == TensorInput.Layout.NCHW) List(3) { ramp[it * n + i] } else List(3) { ramp[i * 3 + it] }
        assertEquals(1, c.toSet().size)
      }
    }
  }

  @Test fun tensorInputsAreValidated() {
    assertThrows<IllegalArgumentException> { TensorInput(0, 10, TensorInput.Layout.NHWC, TensorInput.Fit.STRETCH, mean, std) }
    assertThrows<IllegalArgumentException> { TensorInput(10, 5000, TensorInput.Layout.NHWC, TensorInput.Fit.STRETCH, mean, std) }
    assertThrows<IllegalArgumentException> {
      TensorInput(10, 10, TensorInput.Layout.NHWC, TensorInput.Fit.STRETCH, mean, floatArrayOf(0.5f, 0f, 0.5f))
    }
    assertThrows<IllegalArgumentException> {
      TensorInput(10, 10, TensorInput.Layout.NHWC, TensorInput.Fit.STRETCH, floatArrayOf(Float.NaN, 0f, 0f), std)
    }
    assertThrows<IllegalArgumentException> { TensorInput(10, 10, TensorInput.Layout.NHWC, TensorInput.Fit.STRETCH, floatArrayOf(0f), std) }
    // Ids match the engine's pp_tensor_layout / pp_tensor_fit.
    assertEquals(listOf(0, 1), TensorInput.Layout.entries.map { it.id })
    assertEquals(listOf(0, 1, 2), TensorInput.Fit.entries.map { it.id })
  }

  @Test fun everyShippedModelIsCommerciallyUsableAndDescribed() {
    val allowed = setOf("Apache-2.0", "MIT", "BSD-3-Clause")
    assertEquals(ModelRegistry.ALL.size, ModelRegistry.ALL.map { it.id }.toSet().size)
    for (m in ModelRegistry.ALL) {
      assertTrue(m.license in allowed, "${m.id}: ${m.license}")
      assertTrue(m.asset.endsWith(".tflite"))
      assertTrue(m.memoryBytes > 0)
      assertTrue(m.accelerators.contains(Accel.CPU), "${m.id} needs a CPU fallback")
      assertEquals(m, ModelRegistry.byId(m.id))
    }
    // The depth model's input is what DepthMath expects.
    assertEquals(com.photoplus.studio.DepthMath.IN_W, ModelRegistry.DEPTH.input.width)
    assertEquals(com.photoplus.studio.DepthMath.IN_H, ModelRegistry.DEPTH.input.height)
  }

  @Test fun statisticsSummariseRuns() {
    val s = InferenceStats()
    assertEquals(0.0, s.snapshot()["averageMs"])
    s.accelerator = Accel.GPU
    s.record(10.0)
    s.record(30.0)
    s.fail()
    val snap = s.snapshot()
    assertEquals("gpu", snap["accelerator"])
    assertEquals(2.0, snap["runs"])
    assertEquals(20.0, snap["averageMs"])
    assertEquals(30.0, snap["worstMs"])
    assertEquals(1.0, snap["failures"])
  }
}
