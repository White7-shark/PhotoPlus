package com.photoplus.lucid

/**
 * What a picture is like, for the Lucid theme: how bright it is (average,
 * darkest and brightest tenth — text must stay readable over all of it) and
 * its main colours. Plain ARGB pixels in, so it is tested on the JVM.
 * (From Dr. File's Lucid theme.)
 */
object ImageStats {
  data class Stats(val mean: Double, val p10: Double, val p50: Double, val p90: Double, val colors: List<Int>, val saturation: Double)

  private fun lin(c: Int): Double {
    val s = c / 255.0
    return if (s <= 0.04045) s / 12.92 else Math.pow((s + 0.055) / 1.055, 2.4)
  }

  /** WCAG relative luminance of an ARGB colour, 0..1. */
  fun luminance(argb: Int): Double =
    0.2126 * lin((argb shr 16) and 255) + 0.7152 * lin((argb shr 8) and 255) + 0.0722 * lin(argb and 255)

  fun of(pixels: IntArray): Stats {
    if (pixels.isEmpty()) return Stats(0.0, 0.0, 0.0, 0.0, emptyList(), 0.0)
    val lums = DoubleArray(pixels.size) { luminance(pixels[it]) }.sortedArray()
    fun pct(p: Double) = lums[((lums.size - 1) * p).toInt()]
    // Colours: a coarse histogram (4 bits a channel), the most common, kept apart from each other.
    val counts = HashMap<Int, Int>()
    val sums = HashMap<Int, LongArray>()
    var satSum = 0.0
    for (p in pixels) {
      val r = (p shr 16) and 255
      val g = (p shr 8) and 255
      val b = p and 255
      val max = maxOf(r, g, b)
      val min = minOf(r, g, b)
      satSum += if (max == 0) 0.0 else (max - min) / max.toDouble()
      val key = ((r shr 4) shl 8) or ((g shr 4) shl 4) or (b shr 4)
      counts[key] = (counts[key] ?: 0) + 1
      sums.getOrPut(key) { LongArray(3) }.let { it[0] += r.toLong(); it[1] += g.toLong(); it[2] += b.toLong() }
    }
    val picked = mutableListOf<Int>()
    for ((key, n) in counts.entries.sortedByDescending { it.value }) {
      // The bin's real average colour, not its corner.
      val sum = sums.getValue(key)
      val c = (0xFF shl 24) or ((sum[0] / n).toInt() shl 16) or ((sum[1] / n).toInt() shl 8) or (sum[2] / n).toInt()
      if (picked.none { distance(it, c) < 60 }) picked += c
      if (picked.size == 5) break
    }
    return Stats(lums.average(), pct(0.1), pct(0.5), pct(0.9), picked, satSum / pixels.size)
  }

  /** Several pictures (a video's frames) as one: the widest brightness spread, the common colours. */
  fun combine(all: List<Stats>): Stats? {
    if (all.isEmpty()) return null
    val colors = all.flatMap { it.colors.take(2) }.distinct().take(5)
    return Stats(
      all.map { it.mean }.average(),
      all.minOf { it.p10 },
      all.map { it.p50 }.average(),
      all.maxOf { it.p90 },
      colors,
      all.map { it.saturation }.average(),
    )
  }

  private fun distance(a: Int, b: Int): Double {
    val dr = ((a shr 16) and 255) - ((b shr 16) and 255)
    val dg = ((a shr 8) and 255) - ((b shr 8) and 255)
    val db = (a and 255) - (b and 255)
    return Math.sqrt((dr * dr + dg * dg + db * db).toDouble())
  }
}
