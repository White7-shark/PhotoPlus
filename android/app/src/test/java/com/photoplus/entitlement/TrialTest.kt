package com.photoplus.entitlement

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

/** The simulated 7-day free trial window, on the trusted clock. */
class TrialTest {
  private val day = 24L * 60 * 60 * 1000
  private val start = 1_800_000_000_000L

  @Test
  fun lastsExactlySevenDays() {
    assertEquals(start + 7 * day, Entitlements.trialEnds(start))
    assertTrue(Entitlements.trialOn(start, start))
    assertTrue(Entitlements.trialOn(start, start + 7 * day - 1))
    assertFalse(Entitlements.trialOn(start, start + 7 * day))
  }

  @Test
  fun neverStartedOrClockWoundBackIsNotATrial() {
    assertEquals(0L, Entitlements.trialEnds(0))
    assertFalse(Entitlements.trialOn(0, start))
    assertFalse(Entitlements.trialOn(start, start - 1))
  }
}
