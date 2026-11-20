package com.photoplus.camera

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class CameraConfigTest {
  @Test fun unknownValuesFallBackToSafeDefaults() {
    assertEquals(AspectRatioMode.RATIO_4_3, AspectRatioMode.parse("21:9"))
    assertEquals(ResolutionPreset.MAX, ResolutionPreset.parse(null))
    assertEquals(FlashSetting.OFF, FlashSetting.parse("strobe"))
    assertEquals(CaptureMode.STANDARD, CaptureMode.parse("hdr"))
  }

  @Test fun squareIsCroppedFromTheFourThreeStream() {
    assertEquals(AspectRatioMode.RATIO_4_3, AspectRatioMode.RATIO_1_1.streamRatio)
    assertEquals(AspectRatioMode.RATIO_16_9, AspectRatioMode.RATIO_16_9.streamRatio)
  }

  @Test fun sessionConfigMergesOnlyTheKeysSent() {
    val current = SessionConfig(AspectRatioMode.RATIO_16_9, ResolutionPreset.MP8, rawEnabled = true)
    val next = SessionConfig.fromMap(mapOf("resolution" to "5mp"), current)
    assertEquals(AspectRatioMode.RATIO_16_9, next.aspectRatio)
    assertEquals(ResolutionPreset.MP5, next.resolution)
    assertTrue(next.rawEnabled)
    assertFalse(SessionConfig.fromMap(mapOf("raw" to false), current).rawEnabled)
  }

  @Test fun captureOptionsClampStrengthsAndKeepIds() {
    val o = CaptureOptions.fromMap(mapOf("captureId" to "c1", "mode" to "night", "flash" to "on", "denoise" to 3.0, "detail" to -1))
    assertEquals("c1", o.captureId)
    assertEquals(CaptureMode.NIGHT, o.mode)
    assertEquals(1f, o.denoise)
    assertEquals(0f, o.detail)
    assertTrue(o.autoTone)
    assertTrue(o.mirrorFront)
    assertTrue(CaptureOptions.fromMap(emptyMap()).captureId.startsWith("capture-"))
  }

  @Test fun livelyIsOnByDefaultAndCanBeTurnedOff() {
    val plain = CaptureOptions.fromMap(mapOf("mode" to "standard"))
    assertEquals(1f, plain.lively)
    // A plain photo is still saved straight from the camera, then finished in place.
    assertFalse(plain.needsProcessing)
    assertEquals(0f, CaptureOptions.fromMap(mapOf("lively" to 0)).lively)
    assertEquals(0.5f, CaptureOptions.fromMap(mapOf("lively" to 0.5)).lively)
    assertEquals(1f, CaptureOptions.fromMap(mapOf("lively" to 7)).lively)
    assertEquals(0f, CaptureOptions.fromMap(mapOf("lively" to -2)).lively)
    // Big sensors are finished at a size each kind of phone can hold.
    assertTrue(CapturePolicy.livelyMaxDimension(DeviceTier.ENTRY) < CapturePolicy.livelyMaxDimension(DeviceTier.PERFORMANCE))
    assertTrue(CapturePolicy.livelyMaxDimension(DeviceTier.ENTRY) >= 4096)
  }

  @Test fun burstsNeverFireTheFlash() {
    assertEquals(FlashSetting.OFF, CaptureOptions("a", CaptureMode.NIGHT, FlashSetting.ON).effectiveFlash)
    assertEquals(FlashSetting.OFF, CaptureOptions("a", CaptureMode.SUPER_RES, FlashSetting.AUTO).effectiveFlash)
    assertEquals(FlashSetting.ON, CaptureOptions("a", CaptureMode.ENHANCE, FlashSetting.ON).effectiveFlash)
    assertEquals(FlashSetting.AUTO, CaptureOptions("a", CaptureMode.STANDARD, FlashSetting.AUTO).effectiveFlash)
  }

  @Test fun modesDescribeTheirPipeline() {
    assertFalse(CaptureMode.STANDARD.processed)
    assertFalse(CaptureMode.RAW.processed)
    assertTrue(CaptureMode.ENHANCE.processed && !CaptureMode.ENHANCE.burst)
    assertTrue(CaptureMode.NIGHT.burst)
    assertEquals(2, CaptureMode.SUPER_RES.upscale)
  }
}
