package com.photoplus.entitlement

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class MotionAllowanceTest {
  private val day = "2026-11-27"

  @Test
  fun twoVideosADayEachUnlockedByAnAd() {
    var t = MotionAllowance.today(day, day, 0, 0)
    assertEquals(2, MotionAllowance.left(t))
    assertFalse(MotionAllowance.canMake(t)) // an ad first
    t = MotionAllowance.withAd(t)
    assertTrue(MotionAllowance.canMake(t))
    t = MotionAllowance.afterVideo(t)
    assertEquals(1, MotionAllowance.left(t))
    assertFalse(MotionAllowance.canMake(t)) // the next one needs its own ad
    t = MotionAllowance.afterVideo(MotionAllowance.withAd(t))
    assertEquals(0, MotionAllowance.left(t))
    // More ads don't make more than two a day.
    t = MotionAllowance.withAd(t)
    assertFalse(MotionAllowance.canMake(t))
  }

  @Test
  fun adsNeverUnlockMoreThanAreLeft() {
    var t = MotionAllowance.today(day, day, 0, 0)
    repeat(5) { t = MotionAllowance.withAd(t) }
    assertEquals(2, t.credits)
  }

  @Test
  fun anotherDayStartsAfresh() {
    val t = MotionAllowance.today("2026-11-28", day, 2, 1)
    assertEquals(0, t.used)
    assertEquals(0, t.credits)
    assertEquals(2, MotionAllowance.left(t))
  }
}

class PencilAllowanceTest {
  @Test
  fun pencilHasItsOwnTwoADayAndToolsAreFoundByName() {
    var t = PencilAllowance.today("2026-11-27", "2026-11-27", 0, 0)
    assertEquals(2, PencilAllowance.left(t))
    assertFalse(PencilAllowance.canMake(t))
    t = PencilAllowance.withAd(t)
    assertTrue(PencilAllowance.canMake(t))
    assertEquals(PencilAllowance, AdAllowance.of("pencil"))
    assertEquals(MotionAllowance, AdAllowance.of("motion"))
    assertEquals(null, AdAllowance.of("masters"))
  }
}
