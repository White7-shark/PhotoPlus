package com.photoplus.edit

import com.photoplus.engine.EngineException
import com.photoplus.engine.EngineOp
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows

class EditTest {
  // ---- watermark policy ------------------------------------------------------

  @Test fun freeExportsAlwaysCarryThePhotoPlusMark() {
    // Asking for no watermark, custom text and a logo on the free plan...
    val asked = WatermarkSpec(
      enabled = false,
      custom = CustomBranding("My Studio", 0xFFFF0000.toInt(), "/data/logo.png", Align.LEFT),
      size = 0f,
      opacity = 0f,
    )
    val mark = ExportPolicy.resolve(asked, Plan.FREE)
    // ...still gets the Photo Plus mark, clearly visible.
    assertNotNull(mark)
    mark!!
    assertTrue(mark.brand)
    assertEquals("Photo Plus", mark.text)
    assertNull(mark.logoPath)
    assertTrue(mark.showEmblem)
    assertEquals(WatermarkSpec.WHITE, mark.color)
    assertTrue(mark.enforced)
    assertEquals(ExportPolicy.FREE_HEIGHT.start, mark.height, 1e-6f)
    assertEquals(ExportPolicy.FREE_OPACITY.start, mark.opacity, 1e-6f)
    // No request at all: the default signature mark, bottom right.
    val default = ExportPolicy.resolve(null, Plan.FREE)!!
    assertEquals(WatermarkStyle.SIGNATURE, default.style)
    assertEquals(WatermarkPosition.BR, default.position)
    assertFalse(default.enforced)
  }

  @Test fun freeUsersStyleTheMarkWithinLimits() {
    val mark = ExportPolicy.resolve(
      WatermarkSpec(style = WatermarkStyle.PILL, font = WatermarkFont.ELEGANT, layout = WatermarkLayout.STACKED, size = 1f, opacity = 1f, position = WatermarkPosition.TL),
      Plan.FREE,
    )!!
    assertEquals(WatermarkStyle.PILL, mark.style)
    assertEquals(WatermarkFont.ELEGANT, mark.font)
    assertEquals(WatermarkLayout.STACKED, mark.layout)
    assertEquals(WatermarkPosition.TL, mark.position)
    assertEquals(ExportPolicy.FREE_HEIGHT.endInclusive, mark.height, 1e-6f)
    assertEquals("PHOTO PLUS", ExportPolicy.resolve(WatermarkSpec(style = WatermarkStyle.MINIMAL), Plan.FREE)!!.text)
  }

  @Test fun premiumBrandsFreelyOrExportsClean() {
    assertNull(ExportPolicy.resolve(WatermarkSpec(enabled = false), Plan.PREMIUM))
    val mark = ExportPolicy.resolve(
      WatermarkSpec(custom = CustomBranding("Ama Studio", 0xFF112233.toInt(), "/data/logo.png", Align.RIGHT), size = 1f, opacity = 0f),
      Plan.PREMIUM,
    )!!
    assertEquals("Ama Studio", mark.text)
    assertEquals(0xFF112233.toInt(), mark.color)
    assertEquals("/data/logo.png", mark.logoPath)
    assertFalse(mark.showEmblem)
    assertFalse(mark.brand)
    assertEquals(ExportPolicy.PREMIUM_HEIGHT.endInclusive, mark.height, 1e-6f)
    assertEquals(ExportPolicy.PREMIUM_OPACITY.start, mark.opacity, 1e-6f)
  }

  @Test fun anExportWhoseMarkDidNotLandIsRefused() {
    val mark = ExportPolicy.resolve(null, Plan.FREE)!!
    // 4000 x 3000 photo: mark ~ 0.0575 x 3000 = 172 px tall.
    val e = assertThrows<EngineException> { ExportPolicy.verify(mark, Plan.FREE, 50, 4000, 3000) }
    assertEquals(ExportPolicy.WATERMARK_MISSING, e.code)
    ExportPolicy.verify(mark, Plan.FREE, 20_000, 4000, 3000) // visible: fine
    assertThrows<EngineException> { ExportPolicy.verify(null, Plan.FREE, 0, 4000, 3000) }
    // Premium choices (none, or a faint mark) are never refused.
    ExportPolicy.verify(null, Plan.PREMIUM, 0, 4000, 3000)
  }

  @Test fun specParsingIsStrict() {
    val spec = WatermarkSpec.fromMap(
      mapOf("style" to "badge", "font" to "bold", "position" to "tc", "size" to 7.0, "custom" to mapOf("text" to "x".repeat(99), "color" to "#00ff88")),
    )
    assertEquals(WatermarkStyle.BADGE, spec.style)
    assertEquals(WatermarkPosition.TC, spec.position)
    assertEquals(1f, spec.size)
    assertEquals(WatermarkSpec.MAX_TEXT, spec.custom!!.text.length)
    assertEquals(0xFF00FF88.toInt(), spec.custom!!.color)
    assertThrows<EngineException> { WatermarkSpec.fromMap(mapOf("style" to "neon")) }
    assertThrows<EngineException> { WatermarkSpec.fromMap(mapOf("opacity" to Double.NaN)) }
    assertNull(WatermarkSpec.parseColor("red"))
  }

  @Test fun theMarkAlwaysSitsInsideThePhoto() {
    for (p in WatermarkPosition.entries) {
      val (x, y) = WatermarkPlacement.origin(p, 600, 200, 4000, 3000)
      assertTrue(x >= 0 && y >= 0 && x + 600 <= 4000 && y + 200 <= 3000, "$p inside")
    }
    val margin = (0.035f * 3000).toInt()
    assertEquals(4000 - 600 - margin to 3000 - 200 - margin, WatermarkPlacement.origin(WatermarkPosition.BR, 600, 200, 4000, 3000))
    assertEquals(margin to margin, WatermarkPlacement.origin(WatermarkPosition.TL, 600, 200, 4000, 3000))
    // A mark bigger than the photo is clamped to it.
    assertEquals(0 to 0, WatermarkPlacement.origin(WatermarkPosition.BR, 900, 900, 400, 300))
  }

  @Test fun inkAdaptsToWhatIsBehindTheMark() {
    assertTrue(WatermarkRenderer.inkFor(0.85f).dark)
    assertFalse(WatermarkRenderer.inkFor(0.3f).dark)
  }

  // ---- edit document ---------------------------------------------------------

  @Test fun documentsAreValidatedAndReplayedInOrder() {
    val doc = EditDocument.fromMap(
      mapOf(
        "quarterTurns" to 5.0, "flipH" to true, "straighten" to 80.0,
        "crop" to mapOf("x" to 0.1, "y" to 0.2, "w" to 2.0, "h" to 0.5),
        "exposure" to 0.5, "warmth" to 0.3, "saturation" to -0.2, "sharpen" to 0.5,
        "look" to mapOf("id" to "film", "strength" to 0.7),
        "focus" to mapOf("kind" to "radial", "x" to 0.5, "y" to 0.4, "size" to 0.3, "strength" to 0.6),
        "retouch" to 0.75, "spotlight" to mapOf("points" to listOf(0.4, 0.5), "blur" to 0.6), "transform" to true,
        "props" to listOf(mapOf("id" to "fedora", "cx" to 0.5, "cy" to 0.2, "width" to 0.4, "rotation" to 5.0)),
        "frame" to mapOf("id" to "arch", "zoom" to 0.5),
      ),
    )
    assertEquals(1, doc.quarterTurns)
    assertEquals(45f, doc.straighten)
    assertEquals(0.9f, doc.crop!!.w, 1e-6f) // clamped to the photo
    assertTrue(doc.hasPortraitWork)
    assertTrue(doc.transform)
    assertEquals(listOf(0.4f to 0.5f), doc.spotlight!!.points)
    assertEquals(1f, doc.frame!!.zoom)
    assertEquals(listOf(3, 4, 9), doc.geometryOps().map { it.typeId })
    assertEquals(listOf(1, 10, 8, 11, 12, 7), doc.toneOps().map { it.typeId })
    assertEquals(11, (doc.toneOps()[3] as EngineOp.Filter).engineId)
    val crop = doc.cropOp(1000, 800)!!
    assertEquals(EngineOp.Crop(100, 160, 900, 400), crop)
    assertNull(EditDocument().cropOp(100, 100))
    assertTrue(EditDocument().toneOps().isEmpty())
  }

  @Test fun unknownContentIsRefused() {
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("look" to mapOf("id" to "neon"))) }
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("props" to listOf(mapOf("id" to "../../etc/passwd")))) }
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("frame" to mapOf("id" to "nope"))) }
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("exposure" to Double.POSITIVE_INFINITY)) }
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("props" to List(13) { mapOf("id" to "cap") })) }
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("spotlight" to mapOf("points" to listOf(0.5)))) }
  }

  // ---- frames ----------------------------------------------------------------

  @Test fun framesKeepThePhotoSharpAndBounded() {
    for (style in FrameStyles.ALL) {
      val (cw, ch) = style.canvasSize(3000, 4000, 4096)
      assertTrue(maxOf(cw, ch) <= 4096, style.id)
      // The window never shows the photo larger than it is.
      val windowW = cw * style.window[2]
      val windowH = ch * style.window[3]
      assertTrue(windowW <= 3000.5f && windowH <= 4000.5f, "${style.id} no upscaling")
      assertEquals(style.aspect, cw.toFloat() / ch, 0.01f)
      assertEquals(16, style.floats(1f, 0f, 0f).size)
      assertEquals(9, style.ints().size)
    }
    assertEquals(22, FrameStyles.ALL.size)
    assertNull(FrameStyles.byId("nope"))
  }
}
