package com.photoplus.edit

import com.photoplus.engine.EngineException
import com.photoplus.engine.EngineOp
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNotEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows

/** Phase 4 editing: perspective in the document, size-aware sharpening, the new engine operations. */
class PerspectiveAndOpsTest {
  @Test fun perspectiveIsParsedClampedAndRunsBeforeStraighten() {
    val doc = EditDocument.fromMap(
      mapOf("quarterTurns" to 1, "flipH" to true, "perspectiveV" to 0.4, "perspectiveH" to -3.0, "straighten" to 2.0),
    )
    assertEquals(0.4f, doc.perspectiveV, 1e-6f)
    assertEquals(-1f, doc.perspectiveH) // clamped
    assertEquals(listOf(3, 4, 15, 9), doc.geometryOps().map { it.typeId })
    assertEquals(EngineOp.Perspective(0.4f, -1f), doc.geometryOps()[2])
    // Older documents have no perspective: nothing changes for them.
    assertEquals(listOf(3), EditDocument.fromMap(mapOf("quarterTurns" to 1)).geometryOps().map { it.typeId })
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("perspectiveV" to Double.NaN)) }
  }

  @Test fun perspectiveIsPartOfTheGeometryThatPropsAndFacesUse() {
    val doc = EditDocument.fromMap(
      mapOf("perspectiveV" to 0.3, "straighten" to 4.0, "crop" to mapOf("x" to 0.1, "y" to 0.1, "w" to 0.5, "h" to 0.5), "exposure" to 1.0),
    )
    val g = doc.geometryOnly()
    assertEquals(EditDocument(perspectiveV = 0.3f, straighten = 4f, crop = doc.crop), g)
    assertTrue(g.toneOps().isEmpty())
    // The slow-stage cache must notice a perspective change.
    assertNotEquals(EditRenderer.heavyKey(doc), EditRenderer.heavyKey(doc.copy(perspectiveV = 0.31f)))
    assertNotEquals(EditRenderer.heavyKey(doc), EditRenderer.heavyKey(doc.copy(perspectiveH = 0.1f)))
  }

  @Test fun sharpeningFollowsTheImageSizeSoPreviewAndSaveMatch() {
    val doc = EditDocument(sharpen = 0.5f)
    fun radius(edge: Int) = (doc.toneOps(edge).single() as EngineOp.Sharpen).radius
    assertEquals(0.6f, radius(800), 1e-6f) // never below the smallest useful radius
    assertEquals(1f, radius(2000), 1e-6f)
    assertEquals(2f, radius(4000), 1e-6f)
    assertEquals(4f, radius(20000), 1e-6f) // capped
    // Same look relative to the picture: radius / size stays constant in the normal range.
    assertEquals(radius(1500) / 1500f, radius(3000) / 3000f, 1e-7f)
    val op = doc.toneOps(2000).single() as EngineOp.Sharpen
    assertEquals(0.6f, op.amount, 1e-6f)
    assertEquals(EditDocument.SHARPEN_THRESHOLD, op.threshold)
    assertEquals(0f, (doc.toneOps().single() as EngineOp.Sharpen).radius) // unknown size: the engine's default
  }

  @Test fun newOperationsPackTheirParametersAsTheEngineReadsThem() {
    assertEquals(13, EngineOp.Blur(3f).typeId)
    assertEquals(3f, EngineOp.Blur(3f).params()[0])
    assertEquals(14, EngineOp.Denoise(0.5f).typeId)
    assertEquals(0.5f, EngineOp.Denoise(0.5f).params()[0])
    assertEquals(15, EngineOp.Perspective(0.2f, -0.1f).typeId)
    assertTrue(floatArrayOf(0.2f, -0.1f).contentEquals(EngineOp.Perspective(0.2f, -0.1f).params()))
    assertTrue(floatArrayOf(1.2f, 2f, 0.01f, 0f).contentEquals(EngineOp.Sharpen(1.2f, 2f, 0.01f).params()))
    assertEquals(3f, EngineOp.Resize(10, 20, filter = 3).params()[2])
    for (op in listOf(EngineOp.Blur(1f), EngineOp.Denoise(1f), EngineOp.Perspective(0f, 0f), EngineOp.Sharpen(1f))) {
      assertTrue(op.params().size <= EngineOp.PARAMS)
    }
  }
}
