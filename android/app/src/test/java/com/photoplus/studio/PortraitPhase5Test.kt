package com.photoplus.studio

import com.photoplus.camera.CaptureMode
import com.photoplus.camera.CaptureOptions
import com.photoplus.edit.EditDocument
import com.photoplus.edit.PropFit.Pt
import com.photoplus.engine.EngineException
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

/** Phase 5: face landmarks for the engine, portrait settings, camera beauty. */
class PortraitPhase5Test {
  private fun ring(cx: Float, cy: Float, r: Float, n: Int) =
    List(n) { k -> Pt(cx + r * cos(2 * Math.PI * k / n).toFloat(), cy + r * sin(2 * Math.PI * k / n).toFloat()) }

  /** Contours as a detector returns them (some with a different point count). */
  private fun contours(): Map<FaceLandmarks.Contour, List<Pt>> = FaceLandmarks.ALL.associateWith { c ->
    when (c) {
      FaceLandmarks.OVAL -> ring(500f, 400f, 200f, 40) // 40, not 36: must be resampled
      FaceLandmarks.LEFT_EYE -> ring(580f, 360f, 30f, 16)
      FaceLandmarks.RIGHT_EYE -> ring(420f, 360f, 30f, 16)
      else -> List(c.count) { k -> Pt(400f + 20f * k, 480f + c.first) }
    }
  }

  @Test fun contoursBecomeTheEnginesLayoutWithExactCounts() {
    assertEquals(131, FaceLandmarks.ALL.sumOf { it.count })
    assertEquals(FaceLandmarks.POINTS, FaceLandmarks.NOSE_BOTTOM.first + FaceLandmarks.NOSE_BOTTOM.count)
    // Contours follow each other with no gaps or overlaps.
    FaceLandmarks.ALL.zipWithNext().forEach { (a, b) -> assertEquals(a.first + a.count, b.first) }
    val lm = FaceLandmarks.fromContours(contours(), 1000f, 800f)!!
    assertTrue(lm.detailed)
    assertEquals(262, lm.points.size)
    assertTrue(lm.points.all { it.isFinite() && it in 0f..1f })
    // The oval was resampled onto its circle: every point stays on it.
    for (i in 0 until 36) {
      val p = lm.point(i)
      val r = kotlin.math.hypot(p.x * 1000f - 500f, p.y * 800f - 400f)
      assertTrue(abs(r - 200f) < 6f, "oval point $i off the outline ($r)")
    }
    assertEquals(0.4f, lm.width(), 0.01f)
    // A missing contour means no measured landmarks.
    assertNull(FaceLandmarks.fromContours(contours() - FaceLandmarks.UPPER_LIP_TOP, 1000f, 800f))
  }

  @Test fun resamplingKeepsEndsAndSpacing() {
    val line = listOf(Pt(0f, 0f), Pt(10f, 0f))
    val r = FaceLandmarks.resample(line, 6, closed = false)
    assertEquals(6, r.size)
    assertEquals(Pt(0f, 0f), r.first())
    assertEquals(10f, r.last().x, 1e-4f)
    r.zipWithNext().forEach { (a, b) -> assertEquals(2f, b.x - a.x, 1e-4f) }
    assertEquals(List(4) { Pt(3f, 3f) }, FaceLandmarks.resample(listOf(Pt(3f, 3f)), 4, closed = true))
  }

  @Test fun facesWithoutContoursGetGentleApproximateOutlines() {
    val lm = FaceLandmarks.approximate(
      400f, 300f, 600f, 560f, Pt(460f, 400f), Pt(540f, 400f), Pt(470f, 490f), Pt(530f, 490f), Pt(500f, 505f), 1000f, 800f,
    )
    assertFalse(lm.detailed) // the engine treats approximations more gently
    assertTrue(lm.points.all { it.isFinite() })
    // Eyes sit on the measured eye positions; the mouth between its corners.
    val eyeL = (0 until 16).map { lm.point(FaceLandmarks.LEFT_EYE.first + it) }
    val eyeR = (0 until 16).map { lm.point(FaceLandmarks.RIGHT_EYE.first + it) }
    assertEquals(0.54f, eyeL.map { it.x }.average().toFloat(), 0.01f)
    assertEquals(0.46f, eyeR.map { it.x }.average().toFloat(), 0.01f)
    val lips = (0 until 11).map { lm.point(FaceLandmarks.UPPER_LIP_TOP.first + it) }
    assertTrue(lips.all { it.x in 0.46f..0.54f })
    // Without any landmarks the box alone still gives a plausible face.
    val boxOnly = FaceLandmarks.approximate(0f, 0f, 100f, 120f, null, null, null, null, null, 100f, 120f)
    assertTrue(boxOnly.points.all { it.isFinite() })
  }

  @Test fun landmarksFollowTheBitmapsOrientation() {
    val lm = FaceLandmarks.fromContours(contours(), 1000f, 800f)!!
    // Found upright; the bitmap still needs a 90° turn: map back like masks are.
    val inBitmap = lm.mapped { x, y -> MaskGeometry.uprightToBitmap(x, y, 90) }
    val back = inBitmap.mapped { x, y -> MaskGeometry.rotatePoint(x, y, 90) }
    for (i in 0 until FaceLandmarks.FLOATS) assertEquals(lm.points[i], back.points[i], 1e-5f)
    val (pts, detailed) = FaceLandmarks.pack(listOf(lm, inBitmap))
    assertEquals(2 * 262, pts.size)
    assertEquals(listOf(1, 1), detailed.toList())
    assertThrows<IllegalArgumentException> { FaceLandmarks(FloatArray(10), true) }
  }

  @Test fun portraitParamsCarryTheAdvancedControlsAndKnowWhenToRun() {
    assertEquals(10, PortraitParams().toArray().size)
    assertEquals(listOf(-1f, -1f, -1f), PortraitParams().toArray().slice(4..6)) // presets drive the skin
    val faceOnly = PortraitParams(blur = 0f, retouch = 0f, brighten = 0f, pop = 0f)
    assertFalse(faceOnly.needsPortrait) // face touches alone skip segmentation
    assertTrue(faceOnly.copy(bgColor = -1f).needsPortrait)
    assertTrue(faceOnly.copy(smooth = 0.4f).needsPortrait)
    assertFalse(faceOnly.copy(smooth = 0f, blemish = 0f, even = 0f).needsPortrait)
    assertTrue(FaceTuning(eyes = 0.1f).any)
    assertFalse(FaceTuning(lightAngle = 1f).any) // a direction alone does nothing
    assertEquals(9, FaceTuning().toArray().size)
  }

  @Test fun editDocumentParsesAndClampsThePortraitBlock() {
    val doc = EditDocument.fromMap(
      mapOf(
        "portrait" to mapOf(
          "smooth" to 0.4, "blemish" to 2.0, "even" to -1.0, "eyes" to 0.5, "teeth" to 0.3, "lips" to 0.2,
          "underEye" to 0.6, "shine" to 0.5, "slim" to 0.3, "faceLight" to 0.4, "studio" to 0.7, "lightAngle" to -3.0,
          "bgLight" to -0.5, "bgColor" to -1.0, "edge" to 0.8,
        ),
      ),
    )
    val p = doc.portrait!!
    assertEquals(1f, p.blemish) // clamped
    assertEquals(0f, p.even)
    assertEquals(-1f, p.lightAngle)
    assertEquals(-1f, p.bgColor)
    assertTrue(p.skin && p.background && p.active)
    assertTrue(doc.hasPortraitWork)
    val t = p.tuning()
    assertEquals(0.5f, t.eyes)
    assertEquals(0.7f, t.studio)
    // Older documents have no portrait block; neutral blocks do nothing.
    assertNull(EditDocument.fromMap(mapOf("retouch" to 0.5)).portrait)
    assertFalse(EditDocument(portrait = EditDocument.PortraitChoice()).hasPortraitWork)
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("portrait" to mapOf("eyes" to Double.NaN))) }
    // The slow-stage cache notices portrait changes.
    assertTrue(
      com.photoplus.edit.EditRenderer.heavyKey(doc) != com.photoplus.edit.EditRenderer.heavyKey(doc.copy(portrait = p.copy(eyes = 0.6f))),
    )
  }

  @Test fun cameraBeautyTurnsOnProcessingButNeverForRaw() {
    val beauty = mapOf("retouch" to 0.75, "brighten" to 0.4, "eyes" to 0.3, "underEye" to 0.45, "shine" to 0.35, "faceLight" to 0.2)
    val photo = CaptureOptions.fromMap(mapOf("mode" to "standard", "beauty" to beauty))
    assertTrue(photo.beautyActive)
    assertTrue(photo.needsProcessing) // a standard photo is processed when beauty is on
    assertEquals(0.75f, photo.beautyRetouch)
    assertEquals(0.45f, photo.beauty.underEye)
    assertFalse(CaptureOptions.fromMap(mapOf("mode" to "standard")).needsProcessing)
    val raw = CaptureOptions.fromMap(mapOf("mode" to "raw", "beauty" to beauty))
    assertEquals(CaptureMode.RAW, raw.mode)
    assertFalse(raw.needsProcessing)
    // Bad values are ignored, not trusted.
    val bad = CaptureOptions.beautyTuning(mapOf("eyes" to Double.NaN, "shine" to 7.0))
    assertEquals(0f, bad.eyes)
    assertEquals(1f, bad.shine)
  }
}
