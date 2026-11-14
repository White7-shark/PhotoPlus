package com.photoplus.video

import com.photoplus.edit.Plan
import com.photoplus.engine.EngineException
import com.photoplus.studio.SceneBackground
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

/** Glide length by plan, and Glide in a new scene: the request, the person's usual box, the steadied cut-out. */
class GlideSceneTest {
  @Test
  fun `free clips stop at 10 s and Premium at 30 s, whatever the app asks`() {
    assertEquals(10_000L, GlideOptions.maxDurationMs(Plan.FREE))
    assertEquals(30_000L, GlideOptions.maxDurationMs(Plan.PREMIUM))
    assertEquals(10_000L, GlideOptions.clampDurationMs(30_000L, Plan.FREE))
    assertEquals(30_000L, GlideOptions.clampDurationMs(60_000L, Plan.PREMIUM))
    assertEquals(10_000L, GlideOptions.clampDurationMs(null, Plan.FREE))
    assertEquals(1_000L, GlideOptions.clampDurationMs(10L, Plan.PREMIUM))
    // Processing a longer clip (an import) stops at the plan's limit too.
    assertEquals(301, GlideOptions.maxFrames(30f, Plan.FREE))
    assertEquals(901, GlideOptions.maxFrames(30f, Plan.PREMIUM))
    assertEquals(GlideOptions.MAX_FRAMES, GlideOptions.maxFrames(60f, Plan.PREMIUM))
  }

  @Test
  fun `a scene request is read and checked`() {
    val o = GlideOptions.fromMap(
      mapOf(
        "scene" to mapOf(
          "background" to mapOf("kind" to "plate", "id" to "grand-hall"),
          "crop" to mapOf("x" to 0.2, "y" to 0.1, "w" to 0.5, "h" to 0.8),
          "placement" to mapOf("x" to 0.5, "y" to 0.93, "height" to 0.72),
          "look" to mapOf("colorMatch" to 0.7, "bgBlur" to 0.3, "spotlight" to 0.4),
          "box" to listOf(0.3, 0.12, 0.7, 0.96),
        ),
      ),
    )
    assertNotNull(o.scene)
    val scene = o.scene!!
    assertEquals(SceneBackground.Plate("grand-hall"), scene.background)
    assertEquals(0.72f, scene.placement.height, 1e-6f)
    assertEquals(0.4f, scene.look.spotlight, 1e-6f)
    assertArrayEquals(floatArrayOf(0.3f, 0.12f, 0.7f, 0.96f), scene.box, 1e-6f)
    assertTrue(o.subject)
    assertFalse(o.portrait)
    assertNull(GlideOptions.fromMap(emptyMap()).scene)
    // A video needs a background, and the person's place in the clip.
    assertThrows(EngineException::class.java) {
      GlideScene.fromMap(mapOf("background" to mapOf("kind" to "transparent"), "box" to listOf(0, 0, 1, 1)))
    }
    assertThrows(EngineException::class.java) {
      GlideScene.fromMap(mapOf("background" to mapOf("kind" to "plain", "color" to "#112233"), "box" to listOf(0.5, 0.5, 0.5, 0.5)))
    }
  }

  @Test
  fun `the person's usual box is the median of each side, and cut edges are a majority`() {
    // 10 x 10 masks: a person in columns 3..6, rows 2..9 (touching the bottom).
    fun mask(top: Int, left: Int = 3, right: Int = 6) = ByteArray(100) { i ->
      val x = i % 10
      val y = i / 10
      if (x in left..right && y in top..9) 255.toByte() else 0
    }
    val a = GlideSubject.frameBox(mask(2), 10, 10)!!
    assertArrayEquals(floatArrayOf(0.3f, 0.2f, 0.7f, 1f), a.first, 1e-6f)
    assertArrayEquals(booleanArrayOf(false, false, false, true), a.second)
    // One frame with an arm up (top at row 0) does not change the box.
    val frames = listOf(a, GlideSubject.frameBox(mask(2), 10, 10)!!, GlideSubject.frameBox(mask(0), 10, 10)!!)
    val s = GlideSubject.summarise(frames)!!
    assertArrayEquals(floatArrayOf(0.3f, 0.2f, 0.7f, 1f), s.box, 1e-6f)
    assertTrue(s.cutBottom)
    assertFalse(s.cutTop)
    assertEquals(3, s.frames)
    assertNull(GlideSubject.frameBox(ByteArray(100), 10, 10))
    assertNull(GlideSubject.summarise(emptyList()))
  }

  @Test
  fun `the steadying warp follows a smaller cut-out, and the box grows with the crop`() {
    val w = floatArrayOf(1.1f, 0.02f, -30f, -0.02f, 1.1f, 40f)
    assertArrayEquals(floatArrayOf(1.1f, 0.02f, -15f, -0.02f, 1.1f, 20f), SceneComposer.warpFor(w, 0.5f), 1e-5f)
    assertArrayEquals(floatArrayOf(0.25f, 0.25f, 0.75f, 0.75f), SceneComposer.boxIn(floatArrayOf(0.25f, 0.25f, 0.75f, 0.75f), 1f), 1e-6f)
    val z = SceneComposer.boxIn(floatArrayOf(0.3f, 0.1f, 0.7f, 0.9f), 1.1f)
    assertArrayEquals(floatArrayOf(0.28f, 0.06f, 0.72f, 0.94f), z, 1e-5f)
  }
}
