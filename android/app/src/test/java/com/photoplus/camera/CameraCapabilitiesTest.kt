package com.photoplus.camera

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class CameraCapabilitiesTest {
  private val full = CameraCapabilities.Inputs(
    lensFacing = "back",
    hardwareLevel = "full",
    zoomMin = 0.6f,
    zoomMax = 10f,
    exposureSupported = true,
    exposureMinIndex = -12,
    exposureMaxIndex = 12,
    exposureStep = 1f / 6,
    hasFlash = true,
    focusMeteringSupported = true,
    manualSensor = true,
    manualPostProcessing = true,
    isoRange = 50 to 3200,
    exposureRangeNs = 100_000L to 500_000_000L,
    minFocusDiopters = 10f,
    awbModes = listOf("auto", "incandescent", "daylight"),
    aeLockAvailable = true,
    awbLockAvailable = true,
    rawSupported = true,
    jpegSizes = listOf(PixelSize(4032, 3024), PixelSize(4032, 2268), PixelSize(1600, 1200)),
  )

  @Suppress("UNCHECKED_CAST")
  private fun manual(caps: Map<String, Any?>) = caps["manual"] as Map<String, Any?>

  @Test fun capableHardwareExposesEveryControl() {
    val caps = CameraCapabilities.build(full)
    val m = manual(caps)
    assertEquals(true, m["exposure"])
    assertEquals(true, m["focus"])
    assertEquals(true, m["kelvin"])
    assertEquals(listOf(50, 3200), m["iso"])
    assertEquals(listOf("auto", "incandescent", "daylight"), m["whiteBalanceModes"])
    assertEquals(true, caps["raw"])
    assertTrue((caps["captureModes"] as List<*>).contains("raw"))
    assertNotNull(caps["exposureCompensation"])
  }

  @Test fun legacyHardwareNeverOffersManualSensorControls() {
    val m = manual(CameraCapabilities.build(full.copy(hardwareLevel = "legacy")))
    assertEquals(false, m["exposure"])
    assertEquals(false, m["focus"])
    assertEquals(false, m["kelvin"])
    assertNull(m["iso"])
    assertEquals(emptyList<Int>(), m["isoStops"])
  }

  @Test fun missingFeaturesAreHidden() {
    val caps = CameraCapabilities.build(
      full.copy(
        manualSensor = false, rawSupported = false, hasFlash = false, exposureMinIndex = 0, exposureMaxIndex = 0,
        minFocusDiopters = 0f, awbModes = emptyList(),
      ),
    )
    val m = manual(caps)
    assertEquals(false, m["exposure"])
    assertEquals(false, m["focus"]) // fixed-focus lens
    assertEquals(listOf("auto"), m["whiteBalanceModes"])
    assertFalse(caps["raw"] as Boolean)
    assertFalse((caps["captureModes"] as List<*>).contains("raw"))
    assertFalse(caps["flash"] as Boolean)
    assertNull(caps["exposureCompensation"])
  }

  @Test fun squareResolutionsAreReportedAsTheCroppedSize() {
    @Suppress("UNCHECKED_CAST")
    val square = (CameraCapabilities.build(full)["resolutions"] as Map<String, List<Map<String, Any?>>>)["1:1"]!!
    assertEquals(3024, square.first()["width"])
    assertEquals(3024, square.first()["height"])
  }

  @Test fun manualRangesNeedBothIsoAndExposure() {
    assertNull(CameraCapabilities.manualRanges(full.copy(isoRange = null)))
    assertEquals(50, CameraCapabilities.manualRanges(full)?.isoMin)
    assertEquals("cloudy", CameraCapabilities.awbModeName(6))
    assertNull(CameraCapabilities.awbModeName(0))
  }
}
