package com.photoplus.studio

/**
 * Geometry between what the user sees (the upright photo, or the camera's
 * portrait-locked preview) and the pixels the engine works on (the decoded
 * bitmap, which may still need an EXIF rotation to be upright). Pure Kotlin
 * so every mapping is unit-tested.
 */
object MaskGeometry {
  fun normalise(degrees: Int) = ((degrees % 360) + 360) % 360

  /** Rotates a normalised point clockwise with its image. */
  fun rotatePoint(x: Float, y: Float, degrees: Int): Pair<Float, Float> = when (normalise(degrees)) {
    90 -> (1f - y) to x
    180 -> (1f - x) to (1f - y)
    270 -> y to (1f - x)
    else -> x to y
  }

  /**
   * A tap on the camera preview → the same spot in the upright photo.
   * The preview is portrait-locked (and mirrored for the front camera) while
   * the photo follows the phone: [deviceDegrees] is how far the phone was
   * turned clockwise from the display orientation.
   */
  fun previewToUpright(x: Float, y: Float, deviceDegrees: Int, mirrored: Boolean): Pair<Float, Float> {
    val mx = if (mirrored) 1f - x else x
    return rotatePoint(mx, y, deviceDegrees)
  }

  /** Upright photo → decoded bitmap that needs [remainingRotation] (clockwise) to be upright. */
  fun uprightToBitmap(x: Float, y: Float, remainingRotation: Int): Pair<Float, Float> =
    rotatePoint(x, y, -remainingRotation)

  /** Clockwise rotation of a row-major byte mask; returns the rotated bytes and new size. */
  fun rotateMask(src: ByteArray, w: Int, h: Int, degrees: Int): Triple<ByteArray, Int, Int> {
    require(src.size >= w * h) { "mask too small" }
    return when (normalise(degrees)) {
      90 -> {
        val out = ByteArray(w * h)
        for (y in 0 until h) for (x in 0 until w) out[x * h + (h - 1 - y)] = src[y * w + x]
        Triple(out, h, w)
      }
      180 -> {
        val out = ByteArray(w * h)
        for (y in 0 until h) for (x in 0 until w) out[(h - 1 - y) * w + (w - 1 - x)] = src[y * w + x]
        Triple(out, w, h)
      }
      270 -> {
        val out = ByteArray(w * h)
        for (y in 0 until h) for (x in 0 until w) out[(w - 1 - x) * h + y] = src[y * w + x]
        Triple(out, h, w)
      }
      else -> Triple(src.copyOf(w * h), w, h)
    }
  }

  /** A pixel rectangle. */
  data class Crop(val x: Int, val y: Int, val width: Int, val height: Int)

  /**
   * Where the head(s) of the people in a subject [mask] (row-major, 0..255,
   * [w] x [h]) should be: the top 30% of the people, padded, inside the
   * image. Null when the mask holds nobody.
   */
  fun headCrop(mask: ByteArray, w: Int, h: Int): Crop? {
    require(mask.size >= w * h) { "mask too small" }
    val minRow = maxOf(1, w / 100) // ignore stray pixels
    var top = -1
    var bottom = -1
    for (y in 0 until h) {
      var n = 0
      for (x in 0 until w) if ((mask[y * w + x].toInt() and 0xFF) > 127) n++
      if (n >= minRow) {
        if (top < 0) top = y
        bottom = y
      }
    }
    if (top < 0) return null
    val headBottom = minOf(h - 1, top + maxOf(8, ((bottom - top + 1) * 0.3f).toInt()))
    var left = w
    var right = -1
    for (y in top..headBottom) for (x in 0 until w) {
      if ((mask[y * w + x].toInt() and 0xFF) > 127) {
        if (x < left) left = x
        if (x > right) right = x
      }
    }
    if (right < left) return null
    val pad = maxOf(4, (maxOf(right - left + 1, headBottom - top + 1) * 0.25f).toInt())
    val x0 = maxOf(0, left - pad)
    val y0 = maxOf(0, top - pad)
    val x1 = minOf(w, right + 1 + pad)
    val y1 = minOf(h, headBottom + 1 + pad)
    return Crop(x0, y0, x1 - x0, y1 - y0)
  }

  /** Index of the subject mask (if any) containing the point, preferring the most confident. */
  fun pickSubject(confidences: List<Float>, threshold: Float = 0.5f): Int? {
    var best: Int? = null
    var bestValue = threshold
    confidences.forEachIndexed { i, c ->
      if (c >= bestValue) {
        best = i
        bestValue = c
      }
    }
    return best
  }
}
