package com.photoplus.video

import com.photoplus.camera.SessionConfig
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

/** Glide: options from JS, encoder timing and bitrate, steadied face landmarks, video sessions. */
class GlideTest {
  @Test
  fun `options follow the request and stay in range`() {
    val o = GlideOptions.fromMap(
      mapOf(
        "steady" to 0.8, "maxZoom" to 1.5, "denoise" to 2.0, "sharpen" to -1, "blur" to 0.6, "pop" to 0.4,
        "beauty" to mapOf("retouch" to 0.75, "brighten" to 0.4, "eyes" to 0.3, "underEye" to 0.45, "shine" to 0.35, "faceLight" to 0.2),
      ),
    )
    assertEquals(0.8f, o.steady, 1e-6f)
    assertEquals(1.25f, o.maxZoom, 1e-6f) // the crop never zooms past 1.25x
    assertEquals(1f, o.denoise, 1e-6f)
    assertEquals(0f, o.sharpen, 1e-6f)
    assertTrue(o.spotlight)
    assertTrue(o.portrait)
    assertTrue(o.faces)
    assertEquals(0.75f, o.retouch, 1e-6f)
    assertEquals(0.45f, o.beauty.underEye, 1e-6f)
    assertEquals(0.2f, o.beauty.faceLight, 1e-6f)
  }

  @Test
  fun `a plain clip is only steadied and cleaned`() {
    // What JS sends with Spotlight and Glow off (beauty: null).
    val o = GlideOptions.fromMap(mapOf("steady" to 1, "maxZoom" to 1.12, "blur" to 0, "pop" to 0, "beauty" to null))
    assertFalse(o.spotlight)
    assertFalse(o.portrait)
    assertFalse(o.faces)
    assertEquals(1.12f, o.maxZoom, 1e-6f)
    // Garbage falls back to defaults.
    val d = GlideOptions.fromMap(mapOf("steady" to Double.NaN, "maxZoom" to "big"))
    assertEquals(GlideOptions(), d)
  }

  @Test
  fun `frame times are exact and do not drift`() {
    assertEquals(0L, GlideOptions.frameTimeUs(0, 30f))
    assertEquals(33_333L, GlideOptions.frameTimeUs(1, 30f))
    assertEquals(7_000_000L, GlideOptions.frameTimeUs(210, 30f)) // no accumulated rounding
    assertEquals(1_000_000L, GlideOptions.frameTimeUs(24, 24f))
    assertTrue(GlideOptions.frameTimeUs(219, 29.97f) < GlideOptions.frameTimeUs(220, 29.97f))
    assertTrue(GlideOptions.MAX_FRAMES >= (GlideOptions.MAX_DURATION_MS * 31 / 1000).toInt())
  }

  @Test
  fun `bitrate is about 12 Mbit for 1080p30 and bounded`() {
    assertEquals(12_441_600, GlideOptions.bitrate(1920, 1080, 30f))
    assertEquals(2_000_000, GlideOptions.bitrate(320, 240, 30f))
    assertEquals(40_000_000, GlideOptions.bitrate(3840, 2160, 60f))
    // An odd frame rate report is clamped, not trusted.
    assertEquals(GlideOptions.bitrate(1920, 1080, 60f), GlideOptions.bitrate(1920, 1080, 240f))
  }

  @Test
  fun `landmarks are steadied but big moves are taken at once`() {
    val s = LandmarkSmoother(follow = 0.5f, jump = 0.04f)
    assertArrayEquals(floatArrayOf(0.5f, 0.5f), s.next(floatArrayOf(0.5f, 0.5f)), 1e-6f)
    // A pixel or two of detector jitter: half way.
    assertArrayEquals(floatArrayOf(0.51f, 0.5f), s.next(floatArrayOf(0.52f, 0.5f)), 1e-6f)
    // The head turned: no lag.
    assertArrayEquals(floatArrayOf(0.7f, 0.5f), s.next(floatArrayOf(0.7f, 0.5f)), 1e-6f)
    // The face went away: start fresh.
    assertNull(s.next(null))
    assertArrayEquals(floatArrayOf(0.2f, 0.2f), s.next(floatArrayOf(0.2f, 0.2f)), 1e-6f)
    // A different point count is a new face too.
    assertArrayEquals(floatArrayOf(0.21f, 0.2f, 0.3f), s.next(floatArrayOf(0.21f, 0.2f, 0.3f)), 1e-6f)
  }

  @Test
  fun `the smoother hands out copies`() {
    val s = LandmarkSmoother()
    val out = s.next(floatArrayOf(0.5f, 0.5f))!!
    out[0] = 9f
    assertArrayEquals(floatArrayOf(0.5f, 0.5f), s.next(floatArrayOf(0.5f, 0.5f)), 1e-6f)
  }

  @Test
  fun `sessions switch to video only when asked`() {
    val photo = SessionConfig()
    assertFalse(photo.video)
    val video = SessionConfig.fromMap(mapOf("video" to true, "aspectRatio" to "16:9"), photo)
    assertTrue(video.video)
    // Other changes keep the current choice.
    assertTrue(SessionConfig.fromMap(mapOf("resolution" to "max"), video).video)
    assertFalse(SessionConfig.fromMap(mapOf("video" to false), video).video)
  }
}
