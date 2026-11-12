package com.photoplus.studio

import com.photoplus.camera.CaptureOptions
import com.photoplus.edit.EditDocument
import com.photoplus.engine.EngineException
import org.json.JSONObject
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows
import java.io.File

/** Backdrops: ids and order match the app's catalogue and the engine; requests are range-checked. */
class BackdropTest {
  @Test
  fun `designs match the app catalogue in the engine's order`() {
    // Unit tests run from android/app.
    val json = sequenceOf("../../src/services/backdrops/backdrops.json", "src/services/backdrops/backdrops.json")
      .map { File(it) }.first { it.isFile }.readText()
    val list = JSONObject(json).getJSONArray("backdrops")
    assertEquals(Backdrop.entries.size, list.length())
    assertTrue(Backdrop.entries.size > 10)
    for (i in 0 until list.length()) {
      assertEquals(list.getJSONObject(i).getString("id"), Backdrop.entries[i].id)
      assertEquals(i, Backdrop.entries[i].engineId)
    }
    assertEquals(0, Backdrop.SIGNATURE.engineId) // PP_BACKDROP_SIGNATURE
    assertEquals(13, Backdrop.DREAM.engineId) // PP_BACKDROP_DREAM
    assertTrue(Backdrop.SIGNATURE.usesDepth)
    assertFalse(Backdrop.STAGE.usesDepth)
  }

  @Test
  fun `choices are parsed and clamped`() {
    assertEquals(BackdropChoice(Backdrop.AURORA, 0.5f), BackdropChoice.fromMap(mapOf("id" to "aurora", "strength" to 0.5)))
    assertEquals(1f, BackdropChoice.fromMap(mapOf("id" to "neon", "strength" to 9))!!.strength)
    assertEquals(1f, BackdropChoice.fromMap(mapOf("id" to "neon", "strength" to Double.NaN))!!.strength)
    assertFalse(BackdropChoice.fromMap(mapOf("id" to "neon", "strength" to 0))!!.active)
    assertNull(BackdropChoice.fromMap(mapOf("id" to "beach")))
    assertNull(BackdropChoice.fromMap(null))
  }

  @Test
  fun `captures and edits carry a backdrop`() {
    val shot = CaptureOptions.fromMap(mapOf("captureId" to "c", "mode" to "spotlight", "backdrop" to mapOf("id" to "signature", "strength" to 0.6)))
    assertEquals(Backdrop.SIGNATURE, shot.backdrop?.design)
    assertTrue(shot.needsProcessing)
    val doc = EditDocument.fromMap(mapOf("backdrop" to mapOf("id" to "canvas", "strength" to 0.8)))
    assertEquals(BackdropChoice(Backdrop.CANVAS, 0.8f), doc.backdrop)
    assertNull(EditDocument.fromMap(emptyMap()).backdrop)
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("backdrop" to mapOf("id" to "beach"))) }
  }
}
