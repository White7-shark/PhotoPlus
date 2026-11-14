package com.photoplus.studio

import com.photoplus.engine.EngineException
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
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
    assertEquals(17, l.toArray().size)
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
    assertEquals(SceneBackground.Transparent, SceneBackground.fromMap(mapOf("kind" to "transparent")))
    assertThrows<EngineException> { SceneBackground.fromMap(mapOf("kind" to "plate", "id" to "../../etc/passwd")) }
    assertThrows<EngineException> { SceneBackground.fromMap(mapOf("kind" to "plain", "color" to "red")) }
    assertThrows<EngineException> { SceneBackground.fromMap(mapOf("kind" to "moon")) }
    assertThrows<EngineException> { SceneBackground.fromMap(null) }
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
}
