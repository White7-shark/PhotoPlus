package com.photoplus.camera

import android.view.Surface
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class CaptureMathTest {
  private val mb = 1024L * 1024L

  @Test fun tiersFollowTheEngineBudget() {
    assertEquals(DeviceTier.ENTRY, CapturePolicy.tierFor(192 * mb))
    assertEquals(DeviceTier.ENTRY, CapturePolicy.tierFor(320 * mb))
    assertEquals(DeviceTier.STANDARD, CapturePolicy.tierFor(512 * mb))
    assertEquals(DeviceTier.PERFORMANCE, CapturePolicy.tierFor(768 * mb))
  }

  @Test fun plansScaleWithTheTierAndBurstsWorkSmaller() {
    for (tier in DeviceTier.entries) {
      val enhance = CapturePolicy.plan(CaptureMode.ENHANCE, tier)
      val night = CapturePolicy.plan(CaptureMode.NIGHT, tier)
      val superRes = CapturePolicy.plan(CaptureMode.SUPER_RES, tier)
      assertEquals(1, enhance.frames)
      assertTrue(night.frames in 4..6)
      assertTrue(superRes.frames >= night.frames)
      assertTrue(enhance.decodeDimension > night.decodeDimension)
      assertTrue(night.decodeDimension > superRes.decodeDimension)
      // Super-resolution output (2x) never exceeds the single-frame size.
      assertTrue(superRes.decodeDimension * superRes.upscale <= enhance.decodeDimension)
    }
    assertTrue(
      CapturePolicy.plan(CaptureMode.NIGHT, DeviceTier.PERFORMANCE).decodeDimension >
        CapturePolicy.plan(CaptureMode.NIGHT, DeviceTier.ENTRY).decodeDimension,
    )
    assertEquals(1, CapturePolicy.plan(CaptureMode.STANDARD, DeviceTier.ENTRY).frames)
  }

  @Test fun budgetFallbackShrinksThenStops() {
    var plan: ModePlan? = CapturePolicy.plan(CaptureMode.NIGHT, DeviceTier.PERFORMANCE)
    var steps = 0
    while (plan != null) {
      val next = CapturePolicy.reduced(plan)
      if (next != null) assertTrue(next.decodeDimension < plan.decodeDimension)
      plan = next
      steps++
    }
    assertTrue(steps in 2..8)
    assertNull(CapturePolicy.reduced(ModePlan(6, 640, 1)))
  }

  @Test fun exifOrientationToDegrees() {
    assertEquals(90, CaptureGeometry.exifRotation(6))
    assertEquals(180, CaptureGeometry.exifRotation(3))
    assertEquals(270, CaptureGeometry.exifRotation(8))
    assertEquals(0, CaptureGeometry.exifRotation(1))
    assertEquals(0, CaptureGeometry.exifRotation(0))
  }

  @Test fun rectanglesRotateWithTheirImage() {
    val r = IntRect(10, 20, 110, 70) // in a 400x300 image
    assertEquals(IntRect(230, 10, 280, 110), CaptureGeometry.rotateRect(r, 400, 300, 90))
    assertEquals(IntRect(290, 230, 390, 280), CaptureGeometry.rotateRect(r, 400, 300, 180))
    assertEquals(IntRect(20, 290, 70, 390), CaptureGeometry.rotateRect(r, 400, 300, 270))
    // Four quarter turns are the identity.
    var x = r
    var w = 400
    var h = 300
    repeat(4) {
      x = CaptureGeometry.rotateRect(x, w, h, 90)
      w = h.also { h = w }
    }
    assertEquals(r, x)
  }

  @Test fun decodePlanWithoutExifKeepsBufferOrientationAndDefersRotation() {
    // 4000x3000 sensor buffer, 1:1 crop, phone held upright (rotation 90), no EXIF.
    val plan = CaptureGeometry.decodePlan(4000, 3000, IntRect(500, 0, 3500, 3000), 0, 90, 1500)
    assertEquals(2000, plan.targetWidth)
    assertEquals(1500, plan.targetHeight)
    assertEquals(IntRect(250, 0, 1750, 1500), plan.crop)
    assertEquals(90, plan.remainingRotation)
    assertEquals(1500 to 1500, plan.outputWidth to plan.outputHeight)
  }

  @Test fun decodePlanWithExifRotatesTheCropBecauseTheDecoderRotatesThePixels() {
    // 16:9 crop of a 4:3 buffer: crop rows 375..2625. EXIF says rotate 90.
    val plan = CaptureGeometry.decodePlan(4000, 3000, IntRect(0, 375, 4000, 2625), 6.let { CaptureGeometry.exifRotation(it) }, 90, 0)
    assertEquals(3000, plan.targetWidth)
    assertEquals(4000, plan.targetHeight)
    assertEquals(IntRect(375, 0, 2625, 4000), plan.crop)
    assertEquals(0, plan.remainingRotation) // the decoder already rotated
  }

  @Test fun fullFrameNeedsNoCropAndSmallImagesAreNotUpscaled() {
    val plan = CaptureGeometry.decodePlan(1600, 1200, IntRect(0, 0, 1600, 1200), 0, 0, 3200)
    assertNull(plan.crop)
    assertEquals(1600, plan.targetWidth)
    // Degenerate crop rectangles fall back to the whole image.
    assertNull(CaptureGeometry.decodePlan(1600, 1200, IntRect(5, 5, 5, 5), 0, 0, 800).crop)
  }

  @Test fun displaySizeSwapsForQuarterTurns() {
    assertEquals(3000 to 4000, CaptureGeometry.displaySize(4000, 3000, 90))
    assertEquals(4000 to 3000, CaptureGeometry.displaySize(4000, 3000, 180))
    assertEquals(3000 to 4000, CaptureGeometry.displaySize(4000, 3000, -90))
  }

  @Test fun orientationHasHysteresisAtBucketEdges() {
    assertEquals(Surface.ROTATION_0, OrientationMath.rotationForDegrees(0, Surface.ROTATION_0))
    assertEquals(Surface.ROTATION_270, OrientationMath.rotationForDegrees(90, Surface.ROTATION_0))
    assertEquals(Surface.ROTATION_180, OrientationMath.rotationForDegrees(180, Surface.ROTATION_0))
    assertEquals(Surface.ROTATION_90, OrientationMath.rotationForDegrees(270, Surface.ROTATION_0))
    // Just past the 45° edge: stay put until clearly inside the new bucket.
    assertEquals(Surface.ROTATION_0, OrientationMath.rotationForDegrees(50, Surface.ROTATION_0))
    assertEquals(Surface.ROTATION_270, OrientationMath.rotationForDegrees(60, Surface.ROTATION_0))
    assertEquals(Surface.ROTATION_270, OrientationMath.rotationForDegrees(40, Surface.ROTATION_270))
    assertEquals(Surface.ROTATION_0, OrientationMath.rotationForDegrees(355, Surface.ROTATION_0))
    assertEquals(90, OrientationMath.uiDegrees(Surface.ROTATION_90))
  }

  @Test fun overlayMapsSensorPixelsOntoTheUprightPreview() {
    // 640x480 analysis frame, 160x120 overlay, full crop.
    fun corners(rotation: Int, mirror: Boolean): List<Pair<Float, Float>> {
      val m = OverlayGeometry.transform(160, 640, 0, 0, 640, 480, rotation, mirror)
      return listOf(0f to 0f, 160f to 0f, 0f to 120f).map { (x, y) -> OverlayGeometry.apply(m, x, y) }
    }
    fun assertCorners(expected: List<Pair<Float, Float>>, actual: List<Pair<Float, Float>>) =
      expected.zip(actual).forEach { (e, a) -> assertClose(e, a) }
    assertCorners(listOf(0f to 0f, 1f to 0f, 0f to 1f), corners(0, false))
    // Rotation 90: the sensor's top-left lands at the preview's top-right.
    assertCorners(listOf(1f to 0f, 1f to 1f, 0f to 0f), corners(90, false))
    assertCorners(listOf(1f to 1f, 0f to 1f, 1f to 0f), corners(180, false))
    assertCorners(listOf(0f to 1f, 0f to 0f, 1f to 1f), corners(270, false))
    assertCorners(listOf(0f to 0f, 0f to 1f, 1f to 0f), corners(90, true))
  }

  @Test fun overlayRespectsTheViewportCrop() {
    // 1:1 crop (80..560) of a 640x480 frame, no rotation.
    val m = OverlayGeometry.transform(160, 640, 80, 0, 480, 480, 0, false)
    assertClose(0f to 0f, OverlayGeometry.apply(m, 20f, 0f))
    assertClose(1f to 1f, OverlayGeometry.apply(m, 140f, 120f))
  }

  private fun assertClose(expected: Pair<Float, Float>, actual: Pair<Float, Float>) {
    assertEquals(expected.first, actual.first, 1e-5f)
    assertEquals(expected.second, actual.second, 1e-5f)
  }

  @Test fun peakingColourIsPackedInMemoryOrder() {
    assertEquals(0xFF6B3BFF.toInt(), OverlayGeometry.toRgbaMemory(0xFFFF3B6B.toInt()))
    assertArrayEquals(intArrayOf(0), intArrayOf(OverlayGeometry.toRgbaMemory(0)))
  }
}
