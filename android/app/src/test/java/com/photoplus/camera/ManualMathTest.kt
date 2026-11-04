package com.photoplus.camera

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class ManualMathTest {
  private val ranges = ManualRanges(isoMin = 50, isoMax = 3200, exposureMinNs = 100_000L, exposureMaxNs = 500_000_000L, minFocusDiopters = 10f)

  @Test fun parsesManualSettingsFromTheBridge() {
    val s = ManualSettings.fromMap(mapOf("iso" to 400.0, "whiteBalance" to "kelvin", "kelvin" to 15000.0, "aeLock" to true))
    assertEquals(400, s.iso)
    assertEquals(WhiteBalance.Kelvin(10000), s.whiteBalance) // clamped
    assertTrue(s.manualExposure)
    assertFalse(s.manualFocus)
    assertTrue(s.aeLock)
    assertEquals(WhiteBalance.Auto, WhiteBalance.parse("sodium", null))
    assertEquals(WhiteBalance.Kelvin(5500), WhiteBalance.parse("kelvin", null))
  }

  @Test fun exposureNeedsBothHalvesSoTheMissingOneComesFromLiveValues() {
    assertNull(ManualMath.resolveExposure(ManualSettings(), ranges, 200, 10_000_000L))
    assertEquals(800 to 10_000_000L, ManualMath.resolveExposure(ManualSettings(iso = 800), ranges, 200, 10_000_000L))
    assertEquals(200 to 4_000_000L, ManualMath.resolveExposure(ManualSettings(exposureTimeNs = 4_000_000L), ranges, 200, 10_000_000L))
    // No live values yet: sane defaults.
    assertEquals(100 to 16_666_667L, ManualMath.resolveExposure(ManualSettings(iso = null, exposureTimeNs = null, focusDiopters = null).copy(iso = 100), ranges, null, null))
  }

  @Test fun clampsToTheSensor() {
    assertEquals(3200 to 500_000_000L, ManualMath.resolveExposure(ManualSettings(iso = 25600, exposureTimeNs = 30_000_000_000L), ranges, null, null))
    assertEquals(50 to 100_000L, ManualMath.resolveExposure(ManualSettings(iso = 1, exposureTimeNs = 1L), ranges, null, null))
    assertEquals(10f, ManualMath.clampFocus(25f, ranges))
    assertEquals(0f, ManualMath.clampFocus(-1f, ranges))
  }

  @Test fun dialStopsStayInsideTheSensorRange() {
    val shutter = ManualMath.shutterStops(ranges)
    assertTrue(shutter.all { it in ranges.exposureMinNs..ranges.exposureMaxNs })
    assertEquals(125_000L, shutter.first()) // 1/8000 s is below this sensor's minimum
    assertEquals(500_000_000L, shutter.last()) // 1/2 s, exactly the sensor maximum
    assertEquals(shutter.sorted(), shutter)
    assertEquals(listOf(50, 64, 100, 125, 200, 250, 400, 640, 800, 1250, 1600, 3200), ManualMath.isoStops(ranges))
  }

  @Test fun kelvinGainsWarmLowTemperaturesAndCoolHighOnes() {
    val tungsten = ManualMath.kelvinToRggbGains(3000)
    val daylight = ManualMath.kelvinToRggbGains(6500)
    val shade = ManualMath.kelvinToRggbGains(9000)
    // Under warm light the blue channel must be boosted, and vice versa.
    assertTrue(tungsten[3] > daylight[3] && daylight[3] > shade[3])
    assertTrue(tungsten[0] < daylight[0] && daylight[0] <= shade[0])
    assertEquals(1f, tungsten[1])
    assertEquals(1f, tungsten[2])
    assertEquals(1f, daylight[0], 0.1f)
    assertEquals(1f, daylight[3], 0.1f)
    assertTrue(ManualMath.kelvinToRggbGains(500).all { it in 0.25f..4f })
  }

  @Test fun shutterLabelsReadLikeACamera() {
    assertEquals("1/250", ManualMath.shutterLabel(4_000_000L))
    assertEquals("1/8000", ManualMath.shutterLabel(125_000L))
    assertEquals("2\"", ManualMath.shutterLabel(2_000_000_000L))
    assertEquals("1.5\"", ManualMath.shutterLabel(1_500_000_000L))
  }
}
