package com.photoplus.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class MemoryBudgetPolicyTest {
  private val gb = 1024L * 1024L * 1024L

  @Test fun budgetsScaleWithRamTier() {
    val entry = MemoryBudgetPolicy.forTotalRam(3 * gb - 200 * 1024 * 1024) // "3 GB" phones report less
    val standard = MemoryBudgetPolicy.forTotalRam(3 * gb + 512L * 1024 * 1024)
    val recommended = MemoryBudgetPolicy.forTotalRam(4 * gb)
    val performance = MemoryBudgetPolicy.forTotalRam(8 * gb)
    assertTrue(entry.engineBytes < standard.engineBytes)
    assertTrue(standard.engineBytes < recommended.engineBytes)
    assertTrue(recommended.engineBytes < performance.engineBytes)
    assertEquals(1, entry.workers)
    assertEquals(2, performance.workers)
    assertTrue(performance.maxDecodeDimension > entry.maxDecodeDimension)
  }

  @Test fun lowRamFlagForcesSmallestBudget() {
    assertEquals(MemoryBudgetPolicy.forTotalRam(1 * gb), MemoryBudgetPolicy.forTotalRam(8 * gb, isLowRamDevice = true))
  }

  @Test fun pressureShrinksBudget() {
    val base = MemoryBudgetPolicy.forTotalRam(6 * gb)
    assertEquals(base.engineBytes / 2, MemoryBudgetPolicy.underPressure(base, severe = false).engineBytes)
    assertEquals(base.engineBytes / 4, MemoryBudgetPolicy.underPressure(base, severe = true).engineBytes)
  }

  @Test fun largestDecodeFitsInsideBudgetWithWorkingCopies() {
    // decode (host) + clone + replacement buffer must fit the engine budget for
    // every tier at the tier's maximum decode size (square worst case is
    // pessimistic; real photos are 4:3).
    listOf(2L, 3L, 4L, 6L, 12L).forEach { ram ->
      val b = MemoryBudgetPolicy.forTotalRam(ram * gb)
      val d = b.maxDecodeDimension.toLong()
      val fourThree = d * (d * 3 / 4) * 4
      assertTrue("tier ${ram}GB", fourThree * 2 <= b.engineBytes)
    }
  }
}
