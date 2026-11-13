package com.photoplus.video

import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

class MaskVoteTest {
  private fun bytes(vararg v: Int) = ByteArray(v.size) { v[it].toByte() }

  @Test fun aOneFrameMistakeIsOutvoted() {
    // Pixel 1: wall taken for the person in one frame only -> gone.
    // Pixel 2: the person in all three -> kept. Pixel 3: the person arriving
    // (two frames agree) -> kept.
    val prev = bytes(0, 0, 255, 0)
    val cur = bytes(0, 255, 255, 255)
    val next = bytes(0, 0, 255, 255)
    assertArrayEquals(bytes(0, 0, 255, 255), MaskVote.median(prev, cur, next))
  }

  @Test fun softValuesTakeTheMiddleOne() {
    val out = MaskVote.median(bytes(10), bytes(200), bytes(120))
    assertEquals(120, out[0].toInt() and 0xFF)
  }

  @Test fun missingOrMismatchedNeighboursCountAsTheCurrentFrame() {
    val cur = bytes(0, 255, 128)
    assertArrayEquals(cur, MaskVote.median(null, cur, null))
    assertArrayEquals(cur, MaskVote.median(bytes(1, 2), cur, bytes(9)))
    // At the ends one neighbour is missing: the current frame and the other vote.
    assertArrayEquals(bytes(0, 255, 128), MaskVote.median(null, cur, bytes(255, 0, 128)))
  }
}
