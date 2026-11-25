package com.photoplus.integrity

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class IntegrityLogicTest {
  private class MapStore : TrustedTime.Store {
    val m = HashMap<String, Long>()
    override fun getLong(key: String, default: Long) = m[key] ?: default
    override fun putLong(key: String, value: Long) {
      m[key] = value
    }
  }

  private val day = 86_400_000L
  private val min = 60_000L
  private var wall = 1_800_000_000_000L
  private var up = 50_000L
  private var bootNo = 3L
  private val store = MapStore()
  private val time = TrustedTime(store, { wall }, { up }, { bootNo })

  private fun pass(ms: Long) {
    wall += ms
    up += ms
  }

  @Test fun theTrueTimeFollowsTheBootTimerNotTheClock() {
    time.firstRun()
    assertNull(time.skewMs()) // never checked online: nothing to compare with
    time.synced(wall)
    pass(3 * 3_600_000L)
    assertEquals(wall, time.now())
    assertFalse(time.clockWrong())
    // The clock is set a day ahead (or back): the true time doesn't move, the clock is wrong.
    wall += day
    assertEquals(wall - day, time.now())
    assertTrue(time.clockWrong())
    wall -= 2 * day
    assertTrue(time.clockWrong())
    wall += day + 2 * min // within the tolerance
    assertFalse(time.clockWrong())
  }

  @Test fun afterARestartTheClockMayNotGoBack() {
    time.synced(wall)
    pass(day)
    time.now() // a true time seen
    bootNo++
    up = 10_000 // the boot timer starts again
    assertFalse(time.anchored())
    assertFalse(time.clockWrong()) // the clock moved on with real time
    wall -= 3 * day // set back after the restart
    assertTrue(time.clockWrong())
    assertTrue(time.now() > wall)
  }

  @Test fun aWeekOfflineIsCountedEvenWithTheClockHeldBack() {
    time.synced(wall)
    pass(6 * day)
    assertTrue(time.offlineMs() < 7 * day)
    // The clock held back, but the phone keeps running: running time still counts.
    up += 2 * day
    assertTrue(time.offlineMs() >= 8 * day)
    // Online again: it starts over.
    time.synced(wall)
    assertTrue(time.offlineMs() < min)
    // Restarts add the time each boot ran.
    pass(3 * day)
    time.offlineMs()
    bootNo++
    up = 5 * day
    assertTrue(time.offlineMs() >= 8 * day)
  }

  @Test fun anUpdateHasSevenDaysWithRemindersEveryTwo() {
    val p = UpdatePolicy(store)
    var now = wall
    val installed = 41L
    p.observed(null, installed, now)
    assertFalse(p.state(now, installed).pending)
    p.observed(42, installed, now)
    var s = p.state(now, installed)
    assertTrue(s.pending && s.banner && !s.required && !s.reminderDue)
    assertEquals(now + 7 * day, s.deadline)
    // Later hides the banner until the next reminder.
    p.later(now, installed)
    assertFalse(p.state(now + day, installed).banner)
    now += 2 * day
    s = p.state(now, installed)
    assertTrue(s.reminderDue && s.banner)
    p.reminded(now)
    assertFalse(p.state(now, installed).reminderDue)
    // A newer version keeps the first deadline.
    p.observed(43, installed, now)
    assertEquals(wall + 7 * day, p.state(now, installed).deadline)
    // On the last day the banner shows even after Later.
    now = wall + 6 * day + 1
    p.later(now, installed)
    assertTrue(p.state(now, installed).banner)
    // Past the deadline: required (the app blocks).
    now = wall + 7 * day
    assertTrue(p.state(now, installed).required)
    // Updated: cleared.
    p.observed(null, 43, now)
    assertFalse(p.state(now, 43).pending)
    assertFalse(p.state(now, 43).required)
  }
}
