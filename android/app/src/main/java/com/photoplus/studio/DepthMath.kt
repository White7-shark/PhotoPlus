package com.photoplus.studio

import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Pure helpers around the depth model's input and output (unit-tested on the
 * JVM). The model sees a 686x518 landscape photo, normalised with ImageNet
 * statistics in planar (channel-first) order, and returns relative depth
 * where larger means nearer.
 */
object DepthMath {
  const val IN_W = 686
  const val IN_H = 518

  private val MEAN = floatArrayOf(0.485f, 0.456f, 0.406f)
  private val STD = floatArrayOf(0.229f, 0.224f, 0.225f)

  /** ARGB pixels of an [IN_W] x [IN_H] image → normalised planar RGB floats. */
  fun toInput(argb: IntArray, out: FloatArray = FloatArray(3 * IN_W * IN_H)): FloatArray {
    val n = IN_W * IN_H
    require(argb.size >= n && out.size >= 3 * n) { "depth input has the wrong size" }
    val sr = 1f / (255f * STD[0])
    val sg = 1f / (255f * STD[1])
    val sb = 1f / (255f * STD[2])
    val or = MEAN[0] / STD[0]
    val og = MEAN[1] / STD[1]
    val ob = MEAN[2] / STD[2]
    for (i in 0 until n) {
      val c = argb[i]
      out[i] = ((c shr 16) and 0xFF) * sr - or
      out[n + i] = ((c shr 8) and 0xFF) * sg - og
      out[2 * n + i] = (c and 0xFF) * sb - ob
    }
    return out
  }

  /**
   * Relative depth → bytes, 0 far … 255 near. Stretched between the 1st and
   * 99th percentiles so a few extreme values (sky, a hand at the lens) do
   * not flatten everything else. Null when the map carries no depth at all.
   */
  fun toBytes(depth: FloatArray, n: Int = depth.size): ByteArray? {
    require(n in 1..depth.size) { "bad depth size" }
    // Percentiles from a regular sample: fast and plenty accurate.
    val step = max(1, n / 20_000)
    val sample = FloatArray((n + step - 1) / step) { depth[it * step] }.filter { it.isFinite() }.sorted()
    if (sample.size < 16) return null
    val lo = sample[(sample.size * 0.01f).toInt()]
    val hi = sample[(sample.size * 0.99f).toInt().coerceAtMost(sample.size - 1)]
    if (!(hi - lo > 1e-6f)) return null
    val k = 255f / (hi - lo)
    return ByteArray(n) { i ->
      val v = depth[i]
      if (v.isFinite()) ((v - lo) * k).coerceIn(0f, 255f).roundToInt().toByte() else 0
    }
  }
}
