package com.photoplus.video

/**
 * The per-pixel median of three neighbouring frames' subject masks (same
 * size, one byte per pixel): a person model's one-frame mistake (a patch of
 * wall taken for the person for a single frame) is outvoted, while real
 * movement, which every frame agrees on, passes through. At the ends of the
 * clip a missing neighbour counts as the current frame.
 */
object MaskVote {
  fun median(previous: ByteArray?, current: ByteArray, next: ByteArray?): ByteArray {
    val p = previous?.takeIf { it.size == current.size } ?: current
    val n = next?.takeIf { it.size == current.size } ?: current
    val out = ByteArray(current.size)
    for (i in current.indices) {
      val a = p[i].toInt() and 0xFF
      val b = current[i].toInt() and 0xFF
      val c = n[i].toInt() and 0xFF
      out[i] = maxOf(minOf(a, b), minOf(maxOf(a, b), c)).toByte()
    }
    return out
  }
}
