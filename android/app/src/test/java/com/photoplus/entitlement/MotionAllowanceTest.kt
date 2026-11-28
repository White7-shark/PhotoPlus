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

class ArtsAndToonAllowanceTest {
  @Test
  fun photoArtsAndToonHaveFiveADayEachAfterAnAdAndToolsAreFoundByName() {
    var t = ArtsAllowance.today("2026-11-27", "2026-11-27", 0, 0)
    assertEquals(5, ArtsAllowance.left(t))
    assertFalse(ArtsAllowance.canMake(t))
    t = ArtsAllowance.withAd(t)
    assertTrue(ArtsAllowance.canMake(t))
    assertEquals(5, ToonAllowance.perDay)
    // Paintings and drawings share one allowance.
    assertEquals(ArtsAllowance, AdAllowance.of("pencil"))
    assertEquals(ArtsAllowance, AdAllowance.of("masters"))
    assertEquals(ArtsAllowance, AdAllowance.of("arts"))
    assertEquals(ToonAllowance, AdAllowance.of("toon"))
    assertEquals(MotionAllowance, AdAllowance.of("motion"))
    assertEquals(null, AdAllowance.of("glide"))
  }
}

class OwedAdsTest {
  private val a = ToonAllowance
  private fun day(used: Int = 0, credits: Int = 0) = a.today("2026-11-27", "2026-11-27", used, credits)

  @Test
  fun offlineUnlocksOnCreditUpToThreeOwedThenFreezes() {
    var t = day()
    var owed = 0
    repeat(3) {
      val r = a.offlineUnlock(t, owed)!!
      assertTrue(a.canMake(r.first))
      owed = r.second
      t = a.afterVideo(r.first) // the picture is made
    }
    assertEquals(3, owed)
    assertEquals(AdAllowance.MAX_OWED, owed)
    // A fourth offline unlock: frozen.
    assertEquals(null, a.offlineUnlock(t, owed))
    assertFalse(a.canMake(t))
  }

  @Test
  fun onlineEachAdPaysOffAnOwedOneBeforeItUnlocksAUse() {
    var t = day(used = 3)
    var owed = 3
    repeat(3) {
      val r = a.watched(t, owed)
      assertFalse(a.canMake(r.first)) // paying off unlocks nothing
      t = r.first
      owed = r.second
    }
    assertEquals(0, owed)
    val r = a.watched(t, owed)
    assertTrue(a.canMake(r.first))
    assertEquals(0, r.second)
  }

  @Test
  fun noOfflineCreditBeyondTodaysUsesAndOwedIsCapped() {
    // Every use made today: offline gives nothing more.
    assertEquals(null, a.offlineUnlock(day(used = 5), 0))
    // A use already unlocked and waiting: no second credit for it.
    assertEquals(null, a.offlineUnlock(day(used = 4, credits = 1), 0))
  }
}
