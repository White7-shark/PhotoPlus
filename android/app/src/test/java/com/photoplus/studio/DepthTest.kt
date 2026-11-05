package com.photoplus.studio

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class DepthTest {
  private val n = DepthMath.IN_W * DepthMath.IN_H

  @Test fun inputIsNormalisedPlanarRgb() {
    val argb = IntArray(n) { 0xFF000000.toInt() }
    argb[0] = 0xFFFF8000.toInt() // r 255, g 128, b 0
    val x = DepthMath.toInput(argb)
    assertEquals(3 * n, x.size)
    assertEquals((1f - 0.485f) / 0.229f, x[0], 1e-4f)
    assertEquals((128f / 255f - 0.456f) / 0.224f, x[n], 1e-4f)
    assertEquals((0f - 0.406f) / 0.225f, x[2 * n], 1e-4f)
    assertEquals(-0.485f / 0.229f, x[1], 1e-4f) // black pixel
  }

  @Test fun depthIsStretchedNearBrightAndRobustToOutliers() {
    // A ramp from far (0) to near (1), with a few wild values.
    val d = FloatArray(n) { (it % DepthMath.IN_W).toFloat() / (DepthMath.IN_W - 1) }
    for (i in 0 until 50) d[i * 997] = 1000f
    d[5] = Float.NaN
    val b = requireNotNull(DepthMath.toBytes(d)) { "a ramp has depth" }
    val row = DepthMath.IN_W * 200
    val far = b[row].toInt() and 0xFF
    val mid = b[row + DepthMath.IN_W / 2].toInt() and 0xFF
    val near = b[row + DepthMath.IN_W - 1].toInt() and 0xFF
    assertTrue(far <= 3, "far $far")
    assertTrue(near >= 252, "near $near")
    assertTrue(mid in 118..137, "mid $mid") // outliers did not squash the scale
    assertEquals(0, b[5].toInt() and 0xFF)
  }

  @Test fun aFlatMapCarriesNoDepth() {
    assertNull(DepthMath.toBytes(FloatArray(n) { 0.7f }))
  }

  @Test fun headCropCoversTheTopOfThePerson() {
    // A 100x200 image with a standing person: head at x 40..60, y 20..40,
    // body x 30..70 down to y 180.
    val w = 100
    val h = 200
    val m = ByteArray(w * h)
    for (y in 20 until 180) for (x in 0 until w) {
      val inside = if (y < 40) x in 40..60 else x in 30..70
      if (inside) m[y * w + x] = 0xFF.toByte()
    }
    val c = MaskGeometry.headCrop(m, w, h)!!
    assertTrue(c.x <= 40 && c.x + c.width >= 61, "covers the head horizontally: $c")
    assertTrue(c.y <= 20 && c.y + c.height >= 41, "covers the head vertically: $c")
    assertTrue(c.y + c.height < 120, "does not take the whole body: $c")
    assertTrue(c.x >= 0 && c.y >= 0 && c.x + c.width <= w && c.y + c.height <= h)
  }

  @Test fun headCropNeedsSomebody() {
    assertNull(MaskGeometry.headCrop(ByteArray(64 * 64), 64, 64))
    // A stray pixel is not a person.
    val m = ByteArray(200 * 64).also { it[5 * 200 + 7] = 0xFF.toByte() }
    assertNull(MaskGeometry.headCrop(m, 200, 64))
  }
}
