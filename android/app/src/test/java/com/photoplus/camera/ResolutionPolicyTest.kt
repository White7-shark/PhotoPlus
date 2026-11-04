package com.photoplus.camera

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class ResolutionPolicyTest {
  // A typical 12 MP phone sensor's JPEG sizes.
  private val sensor = listOf(
    PixelSize(4032, 3024), PixelSize(4032, 2268), PixelSize(3264, 2448), PixelSize(3024, 3024),
    PixelSize(2560, 1920), PixelSize(1920, 1080), PixelSize(1600, 1200), PixelSize(1280, 720), PixelSize(640, 480),
  )

  @Test fun ratioMatchingIgnoresOrientationAndTinyDifferences() {
    assertTrue(ResolutionPolicy.matchesRatio(PixelSize(3024, 4032), AspectRatioMode.RATIO_4_3))
    assertTrue(ResolutionPolicy.matchesRatio(PixelSize(4032, 2268), AspectRatioMode.RATIO_16_9))
    assertTrue(ResolutionPolicy.matchesRatio(PixelSize(1920, 1088), AspectRatioMode.RATIO_16_9))
    assertFalse(ResolutionPolicy.matchesRatio(PixelSize(1920, 1080), AspectRatioMode.RATIO_4_3))
    // 1:1 is cropped from 4:3.
    assertTrue(ResolutionPolicy.matchesRatio(PixelSize(4032, 3024), AspectRatioMode.RATIO_1_1))
  }

  @Test fun choosesTheLargestSizeWithinThePreset() {
    assertEquals(PixelSize(4032, 3024), ResolutionPolicy.choose(sensor, AspectRatioMode.RATIO_4_3, ResolutionPreset.MAX))
    assertEquals(PixelSize(4032, 3024), ResolutionPolicy.choose(sensor, AspectRatioMode.RATIO_4_3, ResolutionPreset.MP12))
    assertEquals(PixelSize(3264, 2448), ResolutionPolicy.choose(sensor, AspectRatioMode.RATIO_4_3, ResolutionPreset.MP8))
    assertEquals(PixelSize(2560, 1920), ResolutionPolicy.choose(sensor, AspectRatioMode.RATIO_4_3, ResolutionPreset.MP5))
    assertEquals(PixelSize(1600, 1200), ResolutionPolicy.choose(sensor, AspectRatioMode.RATIO_4_3, ResolutionPreset.MP2))
    assertEquals(PixelSize(4032, 2268), ResolutionPolicy.choose(sensor, AspectRatioMode.RATIO_16_9, ResolutionPreset.MAX))
  }

  @Test fun fallsBackWhenNothingMatchesTheRatio() {
    val odd = listOf(PixelSize(1000, 1000), PixelSize(500, 500))
    assertEquals(PixelSize(1000, 1000), ResolutionPolicy.choose(odd, AspectRatioMode.RATIO_16_9, ResolutionPreset.MP2))
    assertNull(ResolutionPolicy.choose(emptyList(), AspectRatioMode.RATIO_4_3, ResolutionPreset.MAX))
  }

  @Test fun onlyOffersPresetsThatExistOnThisSensor() {
    val ids = ResolutionPolicy.availablePresets(sensor, AspectRatioMode.RATIO_4_3).map { it.first }
    // MAX and 12 MP resolve to the same 4032x3024, so only MAX is listed.
    assertEquals(listOf(ResolutionPreset.MAX, ResolutionPreset.MP8, ResolutionPreset.MP5, ResolutionPreset.MP2), ids)
    val small = listOf(PixelSize(1600, 1200), PixelSize(640, 480))
    // A 2 MP sensor has nothing near 12/8/5 MP.
    assertEquals(listOf(ResolutionPreset.MAX), ResolutionPolicy.availablePresets(small, AspectRatioMode.RATIO_4_3).map { it.first })
  }
}
