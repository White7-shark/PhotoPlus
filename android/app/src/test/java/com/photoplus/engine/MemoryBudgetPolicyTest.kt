package com.photoplus.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class MemoryBudgetPolicyTest {
  private val gb = 1024L * 1024L * 1024L

  @Test fun budgetsScaleWithRamTier() {
    val entry = MemoryBudgetPolicy.forTotalRam(2 * gb)
    val standard = MemoryBudgetPolicy.forTotalRam(3 * gb * 93 / 100) // a "3 GB" phone
    val recommended = MemoryBudgetPolicy.forTotalRam(4 * gb * 93 / 100) // a "4 GB" phone
    val performance = MemoryBudgetPolicy.forTotalRam(8 * gb * 93 / 100)
    assertTrue(entry.engineBytes < standard.engineBytes)
    assertTrue(standard.engineBytes < recommended.engineBytes)
    assertTrue(recommended.engineBytes < performance.engineBytes)
    assertEquals(1, entry.workers)
    assertEquals(2, performance.workers)
    assertTrue(performance.maxDecodeDimension > entry.maxDecodeDimension)
  }

  @Test fun boundariesMatchTypeScriptTiering() {
    // Same fixtures as __tests__/capabilities.test.ts ("3/4/6 GB product targets").
    assertEquals(320L * 1024 * 1024, MemoryBudgetPolicy.forTotalRam(3 * gb * 93 / 100).engineBytes) // entry
    assertEquals(512L * 1024 * 1024, MemoryBudgetPolicy.forTotalRam(4 * gb * 93 / 100).engineBytes) // standard
    assertEquals(768L * 1024 * 1024, MemoryBudgetPolicy.forTotalRam(6 * gb * 93 / 100).engineBytes) // performance
    assertEquals(192L * 1024 * 1024, MemoryBudgetPolicy.forTotalRam(2 * gb * 93 / 100).engineBytes) // unsupported
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
