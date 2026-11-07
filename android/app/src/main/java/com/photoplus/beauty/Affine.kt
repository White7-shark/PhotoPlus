package com.photoplus.beauty

/**
 * A 2-D affine map x' = a·x + c·y + e, y' = b·x + d·y + f (pure Kotlin, so
 * the preview-space geometry is unit-tested off the device). Matches the
 * layout of android.graphics.Matrix values: [a c e / b d f / 0 0 1].
 */
data class Affine(val a: Float, val b: Float, val c: Float, val d: Float, val e: Float, val f: Float) {
  fun apply(x: Float, y: Float): Pair<Float, Float> = (a * x + c * y + e) to (b * x + d * y + f)

  /** This map after [first]: x → this(first(x)). */
  fun after(first: Affine): Affine = Affine(
    a = a * first.a + c * first.b,
    b = b * first.a + d * first.b,
    c = a * first.c + c * first.d,
    d = b * first.c + d * first.d,
    e = a * first.e + c * first.f + e,
    f = b * first.e + d * first.f + f,
  )

  fun inverse(): Affine? {
    val det = a * d - b * c
    if (kotlin.math.abs(det) < 1e-12f || !det.isFinite()) return null
    val ia = d / det
    val ib = -b / det
    val ic = -c / det
    val id = a / det
    return Affine(ia, ib, ic, id, -(ia * e + ic * f), -(ib * e + id * f))
  }

  /** Column-major GLSL mat3. */
  fun toMat3(): FloatArray = floatArrayOf(a, b, 0f, c, d, 0f, e, f, 1f)

  /** Column-major GLSL mat2 of the linear part. */
  fun linearMat2(): FloatArray = floatArrayOf(a, b, c, d)

  companion object {
    val IDENTITY = Affine(1f, 0f, 0f, 1f, 0f, 0f)
    fun translate(x: Float, y: Float) = Affine(1f, 0f, 0f, 1f, x, y)
    fun scale(sx: Float, sy: Float) = Affine(sx, 0f, 0f, sy, 0f, 0f)

    /** From android.graphics.Matrix.getValues() (9 floats, row-major). */
    fun fromValues(v: FloatArray): Affine = Affine(v[0], v[3], v[1], v[4], v[2], v[5])
  }
}

/**
 * Preview space: what the user sees in the viewfinder — upright, cropped to
 * the viewport, mirrored for the front camera — as fractions (x right, y
 * down, 0..1). Live masks, landmarks and the beauty shader's positions all
 * live here.
 */
object PreviewSpace {
  data class Crop(val left: Int, val top: Int, val right: Int, val bottom: Int) {
    val width get() = right - left
    val height get() = bottom - top
  }

  /** Upright size of the crop after turning by [rotation] degrees clockwise. */
  fun uprightSize(crop: Crop, rotation: Int): Pair<Int, Int> =
    if (norm(rotation) % 180 == 90) crop.height to crop.width else crop.width to crop.height

  /** Analysis-buffer pixels → preview fractions. */
  fun fromBuffer(crop: Crop, rotation: Int, mirror: Boolean): Affine {
    val (uw, uh) = uprightSize(crop, rotation)
    val w = crop.width.toFloat()
    val h = crop.height.toFloat()
    // Pixels relative to the crop, then turned clockwise into the upright frame.
    val turn = when (norm(rotation)) {
      90 -> Affine(0f, 1f, -1f, 0f, h, 0f)    // (x, y) -> (h - y, x)
      180 -> Affine(-1f, 0f, 0f, -1f, w, h)   // (x, y) -> (w - x, h - y)
      270 -> Affine(0f, -1f, 1f, 0f, 0f, w)   // (x, y) -> (y, w - x)
      else -> Affine.IDENTITY
    }
    var m = Affine.scale(1f / uw.coerceAtLeast(1), 1f / uh.coerceAtLeast(1))
      .after(turn).after(Affine.translate(-crop.left.toFloat(), -crop.top.toFloat()))
    if (mirror) m = Affine(-1f, 0f, 0f, 1f, 1f, 0f).after(m)
    return m
  }

  /**
   * The beauty shader's orientation: target uv (0..1, bottom-left origin) of
   * an output buffer [outW] x [outH] → preview fractions, through sensor
   * coordinates. [outSensorToBuffer] and [analysisSensorToBuffer] come from
   * CameraX (SurfaceOutput, ImageInfo); [previewFromAnalysis] from [fromBuffer].
   */
  fun orient(outW: Int, outH: Int, outSensorToBuffer: Affine, analysisSensorToBuffer: Affine, previewFromAnalysis: Affine): Affine? {
    val bufferFromUv = Affine(outW.toFloat(), 0f, 0f, -outH.toFloat(), 0f, outH.toFloat())
    val sensorFromOut = outSensorToBuffer.inverse() ?: return null
    return previewFromAnalysis.after(analysisSensorToBuffer).after(sensorFromOut).after(bufferFromUv)
  }

  /** Without camera transforms (desktop parity check): uv → (u, 1 - v). */
  val FLIP_Y = Affine(1f, 0f, 0f, -1f, 0f, 1f)

  private fun norm(r: Int) = ((r % 360) + 360) % 360
}
