package com.photoplus.lucid

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class ImageStatsTest {
  private fun argb(r: Int, g: Int, b: Int) = (0xFF shl 24) or (r shl 16) or (g shl 8) or b

  @Test
  fun `brightness percentiles and main colours of a picture`() {
    // 80% deep blue sky, 20% bright sand.
    val px = IntArray(100) { if (it < 80) argb(20, 40, 120) else argb(240, 220, 180) }
    val s = ImageStats.of(px)
    assertTrue(s.p10 < 0.05 && s.p50 < 0.05, "mostly dark")
    assertTrue(s.p90 > 0.6, "the bright tenth is the sand")
    assertEquals(argb(20, 40, 120), s.colors[0], "most common colour first")
    assertEquals(2, s.colors.size)
    assertEquals(1.0, ImageStats.luminance(argb(255, 255, 255)), 1e-9)
    assertEquals(0.0, ImageStats.luminance(argb(0, 0, 0)), 1e-9)
  }

  @Test
  fun `a clip's frames combine to the widest spread`() {
    val dark = ImageStats.of(IntArray(50) { argb(10, 10, 10) })
    val light = ImageStats.of(IntArray(50) { argb(250, 250, 250) })
    val both = ImageStats.combine(listOf(dark, light))!!
    assertEquals(dark.p10, both.p10, 1e-9)
    assertEquals(light.p90, both.p90, 1e-9)
    assertNull(ImageStats.combine(emptyList()))
    assertEquals(0.0, ImageStats.of(IntArray(0)).mean)
  }
}
