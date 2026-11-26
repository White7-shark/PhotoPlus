package com.photoplus.entitlement

import java.time.LocalDate
import java.time.ZoneId
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

class ArtAllowanceTest {
  @Test
  fun oneFreePaintingADay() {
    assertEquals(1, ArtAllowance.FREE_PER_DAY)
    assertEquals(1, ArtAllowance.left(0))
    assertEquals(0, ArtAllowance.left(1))
    assertEquals(0, ArtAllowance.left(5))
  }

  @Test
  fun yesterdaysCountDoesNotCountToday() {
    assertEquals(1, ArtAllowance.usedOn("2026-11-26", "2026-11-26", 1))
    assertEquals(0, ArtAllowance.usedOn("2026-11-27", "2026-11-26", 1))
    assertEquals(0, ArtAllowance.usedOn("2026-11-26", null, 3))
    assertEquals(0, ArtAllowance.usedOn("2026-11-26", "2026-11-26", -2))
  }

  @Test
  fun theDayIsTheLocalCalendarDay() {
    val zone = ZoneId.systemDefault()
    val noon = LocalDate.of(2026, 11, 26).atTime(12, 0).atZone(zone).toInstant().toEpochMilli()
    val lateNight = LocalDate.of(2026, 11, 26).atTime(23, 59).atZone(zone).toInstant().toEpochMilli()
    val nextMorning = LocalDate.of(2026, 11, 27).atTime(0, 1).atZone(zone).toInstant().toEpochMilli()
    assertEquals("2026-11-26", ArtAllowance.dayOf(noon))
    assertEquals(ArtAllowance.dayOf(noon), ArtAllowance.dayOf(lateNight))
    assertEquals("2026-11-27", ArtAllowance.dayOf(nextMorning))
  }
}
