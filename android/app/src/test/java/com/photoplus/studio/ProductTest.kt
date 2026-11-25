package com.photoplus.studio

import com.photoplus.engine.EngineException
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows

class ProductTest {
  @Test fun setupsAreReadClampedAndPackedForTheEngine() {
    val setup = ProductSetup.fromMap(
      mapOf(
        "backdrop" to mapOf("kind" to "surface", "wall" to "#102030", "floor" to "#FFFFFF", "horizon" to 2, "light" to 0.3),
        "shadow" to mapOf("contact" to 0.7, "cast" to 0.5, "castAngle" to 120, "reflection" to -1),
        "correction" to mapOf("autoWhite" to false, "autoExposure" to true, "exposure" to 0.5, "warmth" to -3),
        "layout" to mapOf("fill" to 0.85, "y" to 0.9, "ground" to true, "flip" to true),
      ),
    )
    assertEquals(ProductBackdropKind.SURFACE, setup.backdrop.kind)
    assertEquals(0.95f, setup.backdrop.horizon)
    assertEquals(70f, setup.shadow.castAngle)
    assertEquals(0f, setup.shadow.reflection)
    assertEquals(-1f, setup.correction.warmth)
    val ints = setup.ints(1200, 1500)
    assertEquals(14, ints.size)
    assertArrayEquals(intArrayOf(3, 0x10, 0x20, 0x30, 255, 255, 255, 0, 1, 1200, 1500, 1, 1, 7), ints)
    val floats = setup.floats(floatArrayOf(240f, 241f, 250f))
    assertEquals(21, floats.size)
    assertArrayEquals(floatArrayOf(240f, 241f, 250f), floats.copyOfRange(10, 13))
    assertArrayEquals(floatArrayOf(0.85f, 0.5f, 0.9f), floats.copyOfRange(18, 21))
    // No plain backdrop measured: no reference (the engine skips auto colour).
    assertArrayEquals(floatArrayOf(-1f, -1f, -1f), setup.floats(null).copyOfRange(10, 13))
  }

  @Test fun badSetupsAreRefused() {
    assertThrows<EngineException> { ProductSetup.fromMap(mapOf("backdrop" to mapOf("kind" to "marble"))) }
    assertThrows<EngineException> { ProductSetup.fromMap(mapOf("backdrop" to mapOf("wall" to "white"))) }
    assertThrows<EngineException> { ProductSetup.fromMap(mapOf("backdrop" to mapOf("kind" to "scene"))) }
    assertThrows<EngineException> { ProductSetup.fromMap(mapOf("backdrop" to mapOf("kind" to "scene", "plate" to "../x"))) }
    assertThrows<EngineException> { ProductSetup.fromMap(mapOf("shadow" to mapOf("contact" to Double.NaN))) }
    assertNotNull(ProductSetup.fromMap(mapOf("backdrop" to mapOf("kind" to "scene", "plate" to "cafe-wall"))).backdrop.plate)
  }

  @Test fun freeAndPremiumAreTold() {
    // Free: a white or paper backdrop with its shadows, up to 480p.
    assertNull(ProductSetup.fromMap(mapOf("backdrop" to mapOf("kind" to "solid"))).premiumReason())
    assertNull(ProductSetup().premiumReason())
    assertNull(ExportProfile.fromMap(mapOf("resolution" to 480), transparent = false).premiumReason())
    // Premium: other backdrops, cast shadows, reflections, larger sizes, transparent PNG.
    assertNotNull(ProductSetup.fromMap(mapOf("backdrop" to mapOf("kind" to "radial"))).premiumReason())
    assertNotNull(ProductSetup.fromMap(mapOf("shadow" to mapOf("cast" to 0.4))).premiumReason())
    assertNotNull(ProductSetup.fromMap(mapOf("shadow" to mapOf("reflection" to 0.4))).premiumReason())
    assertNotNull(ExportProfile.fromMap(mapOf("resolution" to 720), transparent = false).premiumReason())
    assertNotNull(ExportProfile.fromMap(mapOf("resolution" to 480), transparent = true).premiumReason())
  }

  @Test fun exportProfilesGiveTheFrame() {
    val square = ExportProfile.fromMap(mapOf("aspectW" to 1, "aspectH" to 1, "resolution" to 1080), transparent = false)
    assertEquals(1080 to 1080, square.frame())
    val portrait = ExportProfile.fromMap(mapOf("aspectW" to 4, "aspectH" to 5, "resolution" to 1080, "quality" to 300), transparent = false)
    assertEquals(1080 to 1350, portrait.frame())
    assertEquals(100, portrait.quality)
    val story = ExportProfile.fromMap(mapOf("aspectW" to 9, "aspectH" to 16, "resolution" to 2160), transparent = false)
    assertEquals(2160 to 3840, story.frame())
    val preview = story.previewFrame(1000)
    assertTrue(preview.second == 1000 && preview.first in 560..563)
    // A transparent backdrop is always a PNG.
    assertTrue(ExportProfile.fromMap(mapOf("format" to "jpeg"), transparent = true).png)
    assertThrows<EngineException> { ExportProfile.fromMap(mapOf("aspectW" to 1, "aspectH" to 9), transparent = false) }
    assertThrows<EngineException> { ExportProfile.fromMap(mapOf("resolution" to 999), transparent = false) }
    assertThrows<EngineException> { ExportProfile.fromMap(mapOf("format" to "gif"), transparent = false) }
  }

  @Test fun matteSettingsAreClamped() {
    val m = ProductMatte.fromMap(mapOf("tolerance" to 3, "softness" to -1, "shift" to 0.5))
    assertArrayEquals(floatArrayOf(1f, 0f, 0.5f, 1f), m.toArray())
  }
}
