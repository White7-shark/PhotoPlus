package com.photoplus.studio

import com.photoplus.engine.EngineException
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows
import kotlin.math.max

class SceneStudioTest {
  @Test fun strokesParseClampAndEncodeForTheEngine() {
    val list = StudioStroke.listFrom(
      listOf(
        mapOf("brush" to 1, "radius" to 0.03, "points" to listOf(0.1, 0.2, 0.3, 0.4, 0.5)),
        mapOf("brush" to 2, "radius" to 9.0, "points" to listOf(-5.0, 2.0)),
        mapOf("brush" to 0, "points" to listOf(0.5)), // too short: dropped
        "junk",
      ),
    )
    assertEquals(2, list.size)
    assertArrayEquals(floatArrayOf(0.1f, 0.2f, 0.3f, 0.4f), list[0].points) // odd trailing value dropped
    assertEquals(0.3f, list[1].radius)
    assertArrayEquals(floatArrayOf(-0.1f, 1.1f), list[1].points)
    val enc = StudioStroke.encode(list)
    assertArrayEquals(floatArrayOf(2f, 1f, 0.03f, 2f, 0.1f, 0.2f, 0.3f, 0.4f, 2f, 0.3f, 1f, -0.1f, 1.1f), enc)
    assertArrayEquals(floatArrayOf(0f), StudioStroke.encode(emptyList()))
    assertThrows<EngineException> { StudioStroke.listFrom(listOf(mapOf("brush" to 7, "points" to listOf(0.1, 0.1)))) }
    assertThrows<EngineException> { StudioStroke.listFrom(List(StudioStroke.MAX_STROKES + 1) { emptyMap<String, Any?>() }) }
  }

  @Test fun settingsPlacementAndLookAreClamped() {
    val c = CutoutSettings.fromMap(mapOf("softness" to 3.0, "shift" to -9.0, "hairDetail" to Double.NaN))
    assertArrayEquals(floatArrayOf(1f, -1f, 0.5f, 1f), c.toArray())
    val p = ScenePlacement.fromMap(mapOf("x" to 0.3, "y" to 0.9, "height" to 50.0, "flip" to true, "angle" to 90.0))
    assertEquals(3f, p.height)
    assertEquals(45f, p.angle)
    assertEquals(1f, p.toArray()[5])
    val l = SceneLook.fromMap(mapOf("lightX" to -4.0, "rim" to 0.5, "bgBlur" to 2.0))
    assertEquals(-1f, l.lightX)
    assertEquals(0.5f, l.rim)
    assertEquals(1f, l.bgBlur)
    assertEquals(18, l.toArray().size)
    assertEquals(0f, l.toArray()[17]) // no colour bounce
    assertEquals(0.6f, SceneLook.fromMap(mapOf("bounce" to 0.6)).toArray()[17], 1e-6f)
    assertEquals(1f, SceneLook.fromMap(mapOf("bounce" to 7.0)).bounce)
    assertEquals(0f, l.toArray()[10]) // Spotlight off
    assertEquals(0f, l.toArray()[11]) // the colour match measured each time
    val spot = SceneLook.fromMap(mapOf("spotlight" to 0.7))
    assertEquals(0.7f, spot.toArray()[10], 1e-6f)
    // Video: a kept colour match (gain, contrast, balance).
    val kept = spot.copy(harmony = floatArrayOf(1.1f, 1.02f, 0.95f, 1f, 1.05f)).toArray()
    assertArrayEquals(floatArrayOf(1f, 1.1f, 1.02f, 0.95f, 1f, 1.05f), kept.copyOfRange(11, 17))
    // A fixed box for video.
    val boxed = p.toArray(floatArrayOf(0.2f, 0.1f, 0.8f, 0.95f))
    assertEquals(11, boxed.size)
    assertArrayEquals(floatArrayOf(1f, 0.2f, 0.1f, 0.8f, 0.95f), boxed.copyOfRange(6, 11))
    val crop = SceneCrop.fromMap(mapOf("x" to 0.9, "y" to 0.0, "w" to 0.5, "h" to 1.0))
    assertEquals(0.5f, crop.x) // kept inside the background
  }

  @Test fun backgroundsAreChecked() {
    assertEquals(SceneBackground.Plate("white-studio"), SceneBackground.fromMap(mapOf("kind" to "plate", "id" to "white-studio")))
    val plain = SceneBackground.fromMap(mapOf("kind" to "plain", "color" to "#2B2D31", "vignette" to true)) as SceneBackground.Plain
    assertEquals(0xFF2B2D31.toInt(), plain.color)
    assertTrue(plain.vignette)
    assertEquals(0f, plain.light) // flat unless asked
    val lit = SceneBackground.fromMap(mapOf("kind" to "plain", "color" to "#C4962F", "light" to 3.0)) as SceneBackground.Plain
    assertEquals(1f, lit.light)
    assertEquals(0f, (SceneBackground.fromMap(mapOf("kind" to "plain", "color" to "#C4962F", "light" to Double.NaN)) as SceneBackground.Plain).light)
    assertEquals(SceneBackground.Transparent, SceneBackground.fromMap(mapOf("kind" to "transparent")))
    assertThrows<EngineException> { SceneBackground.fromMap(mapOf("kind" to "plate", "id" to "../../etc/passwd")) }
    assertThrows<EngineException> { SceneBackground.fromMap(mapOf("kind" to "plain", "color" to "red")) }
    assertThrows<EngineException> { SceneBackground.fromMap(mapOf("kind" to "moon")) }
    assertThrows<EngineException> { SceneBackground.fromMap(null) }
  }

  @Test fun outfitColoursAreReadFromTheCutOutReport() {
    val r = FloatArray(26)
    r[17] = 2f
    floatArrayOf(30.2f, 40f, 90f, 235f, 235f, 232f).copyInto(r, 18)
    r[24] = 0.7f
    r[25] = 0.3f
    val outfit = SceneStudio.outfit(r)
    assertEquals(listOf("#1e285a", "#ebebe8"), outfit.map { it["color"] })
    assertEquals(0.7, outfit[0]["share"] as Double, 1e-6)
    r[17] = 9f // never more than two
    assertEquals(2, SceneStudio.outfit(r).size)
    assertTrue(SceneStudio.outfit(FloatArray(16)).isEmpty()) // an older, shorter report
  }

  @Test fun hairRequestsAreChecked() {
    assertEquals(null, HairSpec.fromMap(null))
    assertEquals(null, HairSpec.fromMap(mapOf("style" to "", "color" to "")))
    val h = HairSpec.fromMap(mapOf("style" to "box-braids", "color" to "#B85C2E"))!!
    assertEquals("box-braids", h.style)
    assertEquals(0xB85C2E, h.color)
    assertEquals(1f, h.strength)
    assertEquals(null, HairSpec.fromMap(mapOf("color" to "#121012"))!!.style)
    assertEquals(0.5f, HairSpec.fromMap(mapOf("color" to "#121012", "strength" to 0.5))!!.strength)
    assertThrows<EngineException> { HairSpec.fromMap(mapOf("style" to "../etc")) }
    assertThrows<EngineException> { HairSpec.fromMap(mapOf("color" to "red")) }
  }

  @Test fun hairEditsAndBrushesAreChecked() {
    val h = HairSpec.fromMap(
      mapOf(
        "style" to "long-waves",
        "edit" to mapOf("length" to 0.5, "volume" to 9, "part" to -0.4, "size" to 3, "rotate" to -90, "roots" to 0.6),
        "strokes" to listOf(
          mapOf("brush" to 0, "radius" to 0.05, "points" to listOf(0.2, 0.3, 0.25, 0.35)),
          mapOf("brush" to 3, "radius" to 0.02, "points" to listOf(0.5, 0.5)),
        ),
      ),
    )!!
    val e = h.edit
    assertEquals(0.5f, e.length)
    assertEquals(1f, e.volume) // held in range
    assertTrue(e.partSet)
    assertEquals(-0.4f, e.part)
    assertEquals(1.4f, e.size)
    assertEquals(-30f, e.rotate)
    val arr = e.toArray()
    assertEquals(15, arr.size) // what the native side reads
    assertEquals(1f, arr[4]) // the parting is set
    assertEquals(2, h.strokes.size)
    assertEquals(3, h.strokes[1].brush)
    // Without an edit: as the engine made it (size 1, no parting).
    val plain = HairSpec.fromMap(mapOf("style" to "bob"))!!.edit
    assertFalse(plain.partSet)
    assertEquals(1f, plain.size)
    // A hair brush beyond the four is refused; the cut-out's brushes stay 0..2.
    assertThrows<EngineException> { HairSpec.fromMap(mapOf("style" to "bob", "strokes" to listOf(mapOf("brush" to 4, "points" to listOf(0.1, 0.1))))) }
    assertThrows<EngineException> { StudioStroke.listFrom(listOf(mapOf("brush" to 3, "points" to listOf(0.1, 0.1)))) }
  }

  @Test fun exportsKeepThePersonsOwnDetail() {
    // A person 3000 px tall filling 75% of a 4:5 frame: a 4000 px tall frame…
    val (w, h) = SceneStudio.exportSize(0.8f, 3000, 0.75f, 8192)
    assertEquals(4000, h)
    assertEquals(3200, w)
    // …capped by the long edge…
    val (w2, h2) = SceneStudio.exportSize(0.8f, 3000, 0.75f, 2048)
    assertEquals(2048, max(w2, h2))
    // …and never smaller than a preview.
    val (w3, h3) = SceneStudio.exportSize(1.5f, 200, 0.5f, 4096)
    assertEquals(SceneStudio.PREVIEW_EDGE, max(w3, h3))
    assertFalse(w3 < 16 || h3 < 16)
  }

  @Test fun sampleSizeKeepsAtLeastTheEdgeAsked() {
    assertEquals(1, SceneStudio.sampleSize(2560, 1707, 2000))
    assertEquals(2, SceneStudio.sampleSize(2560, 1707, 1280))
    assertEquals(4, SceneStudio.sampleSize(5120, 3000, 1280))
  }

  @Test fun studioLightRequestsAreChecked() {
    assertEquals(null, SceneLight.fromMap(null))
    val soft = SceneLight.fromMap(mapOf("preset" to "soft-studio", "angle" to -20, "softness" to 0.85))!!
    assertFalse(soft.premium) // a free look
    assertEquals(-20f, soft.angle)
    val neon = SceneLight.fromMap(mapOf("preset" to "neon-gels", "keyColour" to listOf(1.45, 0.55, 1.25), "rimRight" to 0.6))!!
    assertTrue(neon.premium)
    assertEquals(1.45f, neon.keyColour[0])
    // Out of range is held in range; the face box is appended for the engine.
    val wild = SceneLight.fromMap(mapOf("preset" to "custom", "angle" to 999, "strength" to 7))!!
    assertEquals(180f, wild.angle)
    assertEquals(1f, wild.strength)
    assertTrue(wild.premium)
    val a = soft.toArray(floatArrayOf(0.3f, 0.2f, 0.6f, 0.5f))
    assertEquals(21, a.size)
    assertEquals(0.6f, a[19])
    assertThrows<EngineException> { SceneLight.fromMap(mapOf("preset" to "../x")) }
    assertThrows<EngineException> { SceneLight.fromMap(mapOf("preset" to "loop", "keyColour" to listOf(1, 2))) }
  }

  @Test fun finishRequestsAreChecked() {
    assertEquals(null, SceneFinish.fromMap(null))
    assertEquals(null, SceneFinish.fromMap(emptyMap())) // nothing to do
    val basic = SceneFinish.fromMap(mapOf("vignette" to 0.4, "warmth" to 0.2))!!
    assertFalse(basic.premium)
    assertEquals(17, basic.toArray().size) // what the native side reads
    val film = SceneFinish.fromMap(mapOf("fade" to 0.15, "grain" to 0.3, "shadowTone" to listOf(-0.05, 0.05, 0.08), "toneAmount" to 0.6))!!
    assertTrue(film.premium)
    assertEquals(0.08f, film.toArray()[9])
    assertTrue(SceneFinish.fromMap(mapOf("mono" to true))!!.premium)
    assertEquals(1f, SceneFinish.fromMap(mapOf("contrast" to 5))!!.contrast)
    assertThrows<EngineException> { SceneFinish.fromMap(mapOf("fade" to 0.1, "highlightTone" to listOf(1))) }
  }

  @Test fun gradientBackgroundsAreChecked() {
    val g = SceneBackground.fromMap(mapOf("kind" to "gradient", "from" to "#F2C58F", "to" to "#B4664A", "radial" to true))
    assertTrue(g is SceneBackground.Gradient)
    g as SceneBackground.Gradient
    assertEquals(0xF2C58F, g.from)
    assertTrue(g.radial)
    assertEquals(90f, g.angle)
    assertThrows<EngineException> { SceneBackground.fromMap(mapOf("kind" to "gradient", "from" to "peach", "to" to "#000000")) }
  }

  @Test fun lipsAreCheckedAndOffWhenNotAsked() {
    assertNull(LipsSpec.fromMap(null))
    assertEquals(LipsSpec(0xE8508A, 0.85f), LipsSpec.fromMap(mapOf("color" to "#E8508A")))
    assertEquals(LipsSpec(0xF4A6B8, 0.85f, natural = true), LipsSpec.fromMap(mapOf("color" to "#F4A6B8", "natural" to true)))
    assertEquals(LipsSpec(0xF4A6B8, 1f), LipsSpec.fromMap(mapOf("color" to "#f4a6b8", "strength" to 3)))
    assertNull(LipsSpec.fromMap(mapOf("color" to "#F4A6B8", "strength" to 0)))
    for (bad in listOf(mapOf("color" to "pink"), mapOf("strength" to 0.5), mapOf("color" to "#12345"))) {
      val e = assertThrows<EngineException> { LipsSpec.fromMap(bad) }
      assertEquals(EngineException.INVALID_ARGUMENT, e.code)
    }
  }
}
