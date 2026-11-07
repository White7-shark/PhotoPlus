package com.photoplus.beauty

import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.exp
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * Uniforms for the live beauty shaders (the assets in beauty/), computed the
 * way the engine measures the face (portrait/face.cpp). Mirrored by
 * scripts/beauty-live.mjs for the desktop parity check; the unit test pins
 * both to the same numbers (src/test/resources/beauty_fixture.json).
 */
object LiveBeautyUniforms {
  /** Low-pass sigma (fraction of the short side), as the engine's grade. */
  const val LOW_SIGMA = 0.015f
  /** Skin-smoothing base sigma (fraction of the short side). */
  const val SMALL_SIGMA = 0.004f

  data class Size(val factor: Int, val width: Int, val height: Int)

  /** The low-resolution target for a [w] x [h] frame (short side ~256). */
  fun lowSize(w: Int, h: Int): Size {
    val f = max(1, (min(w, h) / 256.0).roundToInt())
    return Size(f, max(1, ceil(w / f.toDouble()).toInt()), max(1, ceil(h / f.toDouble()).toInt()))
  }

  data class Blur(val taps: Int, val weights: FloatArray)

  /** Normalised Gaussian weights for offsets 0..taps-1 (13 slots). */
  fun blurWeights(sigma: Float): Blur {
    val taps = min(13, max(2, ceil(3 * sigma).toInt() + 1))
    val w = FloatArray(13)
    for (i in 0 until taps) w[i] = exp(-0.5f * i * i / (sigma * sigma))
    var sum = w[0]
    for (i in 1 until taps) sum += 2 * w[i]
    for (i in 0 until taps) w[i] /= sum
    return Blur(taps, w)
  }

  /** Face-frame uniforms in pixels of a [w] x [h] frame. */
  data class Face(
    val frame: FloatArray,  // centre x, y, right x, y
    val half: FloatArray,   // half width, half height
    val marks: FloatArray,  // eye, nose, mouth lines (face units)
    val eyes: FloatArray,   // left eye x, y, right eye x, y
    val eyeW: Float,
    val mouth: FloatArray,  // centre x, y, width
    val faceWidth: Float,
  )

  /** From 131 landmarks (normalised x, y pairs), or null when they are unusable. */
  fun face(points: FloatArray, w: Int, h: Int): Face? {
    if (points.size < 262) return null
    val xs = FloatArray(131) { points[2 * it] * w }
    val ys = FloatArray(131) { points[2 * it + 1] * h }
    fun centre(first: Int, n: Int): FloatArray {
      var sx = 0f
      var sy = 0f
      for (i in first until first + n) {
        sx += xs[i]
        sy += ys[i]
      }
      return floatArrayOf(sx / n, sy / n)
    }
    var a = centre(56, 16)
    var b = centre(72, 16)
    if (a[0] > b[0]) a = b.also { b = a }
    val ed = hypot(b[0] - a[0], b[1] - a[1])
    if (!(ed > 1e-3f)) return null
    val rx = (b[0] - a[0]) / ed
    val ry = (b[1] - a[1]) / ed
    val dx = -ry
    val dy = rx
    val fc = centre(0, 36)
    fun alongR(x: Float, y: Float) = (x - fc[0]) * rx + (y - fc[1]) * ry
    fun alongD(x: Float, y: Float) = (x - fc[0]) * dx + (y - fc[1]) * dy
    var halfW = 1f
    var halfH = 1f
    var minX = Float.MAX_VALUE
    var maxX = -Float.MAX_VALUE
    for (i in 0 until 36) {
      halfW = max(halfW, abs(alongR(xs[i], ys[i])))
      halfH = max(halfH, abs(alongD(xs[i], ys[i])))
      minX = min(minX, xs[i])
      maxX = max(maxX, xs[i])
    }
    fun width(first: Int, n: Int): Float {
      var lo = Float.MAX_VALUE
      var hi = -Float.MAX_VALUE
      for (i in first until first + n) {
        val v = alongR(xs[i], ys[i])
        lo = min(lo, v)
        hi = max(hi, v)
      }
      return hi - lo
    }
    val mid = floatArrayOf((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
    val nose = centre(128, 3)
    // Inner mouth: upper lip bottom (9) and lower lip top (9).
    var mx = 0f
    var my = 0f
    for (i in 99 until 117) {
      mx += xs[i]
      my += ys[i]
    }
    mx /= 18
    my /= 18
    val mouthOut = centre(88, 11)
    return Face(
      frame = floatArrayOf(fc[0], fc[1], rx, ry),
      half = floatArrayOf(halfW, halfH),
      marks = floatArrayOf(alongD(mid[0], mid[1]) / halfH, alongD(nose[0], nose[1]) / halfH, alongD(mx, my) / halfH),
      eyes = floatArrayOf(a[0], a[1], b[0], b[1]),
      eyeW = max(4f, 0.5f * (width(56, 16) + width(72, 16))),
      mouth = floatArrayOf(mouthOut[0], mouthOut[1], max(4f, width(88, 11))),
      faceWidth = maxX - minX,
    )
  }

  /** The studio light's mean shading over the face (the engine averages over the oval). */
  fun studioMean(lightAngle: Float): Float {
    val la = lightAngle.coerceIn(-1f, 1f)
    var lx = 0.85f * la
    var ly = -0.35f
    var lz = 0.6f
    val n = sqrt(lx * lx + ly * ly + lz * lz)
    lx /= n
    ly /= n
    lz /= n
    var sum = 0.0
    var count = 0
    for (j in -40..40) {
      for (i in -40..40) {
        val u = i / 40f
        val v = j / 40f
        if (u * u + v * v > 1f) continue
        val su = u / 1.15f
        val sv = v / 1.15f
        val z = sqrt(max(0f, 1f - su * su - sv * sv))
        sum += max(0f, su * lx + sv * ly + z * lz)
        count++
      }
    }
    return (sum / count).toFloat()
  }
}
