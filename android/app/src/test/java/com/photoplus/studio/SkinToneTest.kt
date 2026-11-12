package com.photoplus.studio

import com.photoplus.edit.EditDocument
import com.photoplus.edit.EditRenderer
import com.photoplus.engine.EngineException
import org.json.JSONObject
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNotEquals
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows
import java.io.File

/** Skin Tone: shades match the app's catalogue exactly; requests are range-checked; edits carry the choice. */
class SkinToneTest {
  @Test
  fun `shades match the app catalogue`() {
    // Unit tests run from android/app.
    val json = sequenceOf("../../src/services/skintones/skintones.json", "src/services/skintones/skintones.json")
      .map { File(it) }.first { it.isFile }.readText()
    val list = JSONObject(json).getJSONArray("shades")
    assertEquals(SkinShade.entries.size, list.length())
    assertTrue(SkinShade.entries.size > 10)
    for (i in 0 until list.length()) {
      val o = list.getJSONObject(i)
      val shade = SkinShade.entries[i]
      assertEquals(o.getString("id"), shade.id)
      assertEquals(o.getString("color").removePrefix("#").toInt(16), shade.color, shade.id)
      assertEquals(o.getDouble("glow").toFloat(), shade.glow, 1e-6f, shade.id)
    }
    assertEquals(0xF1 / 255f, SkinShade.IVORY.r, 1e-6f)
    assertEquals(0xDD / 255f, SkinShade.IVORY.g, 1e-6f)
    assertEquals(0xC6 / 255f, SkinShade.IVORY.b, 1e-6f)
  }

  @Test
  fun `choices are parsed and clamped`() {
    assertEquals(SkinToneChoice(SkinShade.HONEY, 0.5f), SkinToneChoice.fromMap(mapOf("id" to "honey", "strength" to 0.5)))
    assertEquals(1f, SkinToneChoice.fromMap(mapOf("id" to "mocha", "strength" to 9))!!.strength)
    assertEquals(1f, SkinToneChoice.fromMap(mapOf("id" to "mocha", "strength" to Double.NaN))!!.strength)
    assertFalse(SkinToneChoice.fromMap(mapOf("id" to "mocha", "strength" to 0))!!.active)
    assertNull(SkinToneChoice.fromMap(mapOf("id" to "blue")))
    assertNull(SkinToneChoice.fromMap(null))
  }

  @Test
  fun `edits carry a shade and re-render when it changes`() {
    val doc = EditDocument.fromMap(mapOf("skinTone" to mapOf("id" to "caramel", "strength" to 0.8)))
    assertEquals(SkinToneChoice(SkinShade.CARAMEL, 0.8f), doc.skinTone)
    assertNull(EditDocument.fromMap(emptyMap()).skinTone)
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("skinTone" to mapOf("id" to "blue"))) }
    val other = EditDocument.fromMap(mapOf("skinTone" to mapOf("id" to "espresso", "strength" to 0.8)))
    assertNotEquals(EditRenderer.heavyKey(doc), EditRenderer.heavyKey(other))
  }
}
