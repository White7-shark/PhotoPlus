package com.photoplus.studio

import com.photoplus.camera.CaptureMode
import com.photoplus.camera.CaptureOptions
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class StudioTest {
  private fun close(expected: Pair<Float, Float>, actual: Pair<Float, Float>) {
    assertEquals(expected.first, actual.first, 1e-5f)
    assertEquals(expected.second, actual.second, 1e-5f)
  }

  @Test fun tapsFollowThePhoneIntoTheUprightPhoto() {
    // Phone upright: the preview is the photo.
    close(0.2f to 0.3f, MaskGeometry.previewToUpright(0.2f, 0.3f, 0, mirrored = false))
    // Phone turned clockwise (landscape, left edge up): the preview's top edge
    // is the photo's right edge.
    close(1f to 0.5f, MaskGeometry.previewToUpright(0.5f, 0f, 90, mirrored = false))
    close(0.5f to 1f, MaskGeometry.previewToUpright(1f, 0.5f, 90, mirrored = false))
    // Turned anticlockwise: the preview's top edge is the photo's left edge.
    close(0f to 0.5f, MaskGeometry.previewToUpright(0.5f, 0f, 270, mirrored = false))
    // Upside down.
    close(0.8f to 0.7f, MaskGeometry.previewToUpright(0.2f, 0.3f, 180, mirrored = false))
    // Front camera: the preview is a mirror image of the real scene.
    close(0.8f to 0.3f, MaskGeometry.previewToUpright(0.2f, 0.3f, 0, mirrored = true))
  }

  @Test fun uprightPointsMapIntoBitmapsThatStillNeedRotating() {
    // A bitmap that needs 90° clockwise to be upright: its top-left corner
    // shows up at the upright photo's top-right.
    close(0f to 0f, MaskGeometry.uprightToBitmap(1f, 0f, 90))
    for (deg in listOf(0, 90, 180, 270)) {
      val p = MaskGeometry.uprightToBitmap(0.3f, 0.6f, deg)
      close(0.3f to 0.6f, MaskGeometry.rotatePoint(p.first, p.second, deg)) // round trip
    }
  }

  @Test fun masksRotateLosslessly() {
    val src = byteArrayOf(1, 2, 3, 4, 5, 6) // 3 wide, 2 tall
    val (r90, w90, h90) = MaskGeometry.rotateMask(src, 3, 2, 90)
    assertEquals(2, w90)
    assertEquals(3, h90)
    assertArrayEquals(byteArrayOf(4, 1, 5, 2, 6, 3), r90)
    val (back, bw, bh) = MaskGeometry.rotateMask(r90, w90, h90, -90)
    assertEquals(3 to 2, bw to bh)
    assertArrayEquals(src, back)
    assertArrayEquals(byteArrayOf(6, 5, 4, 3, 2, 1), MaskGeometry.rotateMask(src, 3, 2, 180).first)
    assertArrayEquals(src, MaskGeometry.rotateMask(src, 3, 2, 360).first)
  }

  @Test fun tapsPickTheMostConfidentSubject() {
    assertEquals(1, MaskGeometry.pickSubject(listOf(0.2f, 0.9f, 0.6f)))
    assertNull(MaskGeometry.pickSubject(listOf(0.1f, 0.3f)))
    assertNull(MaskGeometry.pickSubject(emptyList()))
  }

  @Test fun spotlightSettingsAreClampedAndPointsSanitised() {
    val p = PortraitParams.fromMap(mapOf("blur" to 2.0, "retouch" to -1, "pop" to 0.3))
    assertEquals(1f, p.blur)
    assertEquals(0f, p.retouch)
    assertEquals(0.4f, p.brighten)
    assertEquals(0.3f, p.pop)
    val pts = PortraitParams.points(listOf(0.5, 0.5, 1.5, -0.2, Double.NaN, 0.1, 0.7))
    assertEquals(listOf(0.5f to 0.5f, 1f to 0f), pts) // NaN pair and the dangling value are dropped
    assertTrue(PortraitParams.points("nope").isEmpty())
    assertEquals(8, PortraitParams.points(List(40) { 0.5 }).size)
  }

  @Test fun captureOptionsCarryLooksAndTaps() {
    val look = List(20) { 0.1 * it / 20 }
    val o = CaptureOptions.fromMap(mapOf("mode" to "spotlight", "points" to listOf(0.4, 0.6), "look" to look, "lookStrength" to 0.5))
    assertEquals(CaptureMode.PORTRAIT, o.mode)
    assertEquals(listOf(0.4f to 0.6f), o.previewPoints)
    assertEquals(20, o.look?.size)
    assertEquals(0.5f, o.lookStrength)
    assertTrue(o.needsProcessing)
    // A malformed look is ignored rather than crashing a capture.
    assertNull(CaptureOptions.fromMap(mapOf("look" to listOf(1, 2, 3))).look)
    // A plain photo with a look is graded; without one it is saved directly; RAW is never touched.
    assertTrue(CaptureOptions.fromMap(mapOf("mode" to "standard", "look" to look)).needsProcessing)
    assertFalse(CaptureOptions.fromMap(mapOf("mode" to "standard")).needsProcessing)
    assertFalse(CaptureOptions.fromMap(mapOf("mode" to "raw", "look" to look)).needsProcessing)
  }
}
