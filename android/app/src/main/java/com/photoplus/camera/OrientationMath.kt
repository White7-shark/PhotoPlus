package com.photoplus.camera

import android.view.Surface

/**
 * Device orientation → capture rotation. The UI is portrait-locked, so the
 * camera follows the physical orientation from OrientationEventListener;
 * icons counter-rotate by [uiDegrees].
 */
object OrientationMath {
  /** Maps sensor degrees to a Surface rotation with ±10° hysteresis around the boundaries. */
  fun rotationForDegrees(degrees: Int, current: Int): Int {
    fun bucket(d: Int) = when ((d + 360) % 360) {
      in 45..134 -> Surface.ROTATION_270
      in 135..224 -> Surface.ROTATION_180
      in 225..314 -> Surface.ROTATION_90
      else -> Surface.ROTATION_0
    }
    val candidate = bucket(degrees)
    if (candidate == current) return current
    // Only switch when clearly inside the new bucket.
    return if (bucket(degrees - 10) == candidate && bucket(degrees + 10) == candidate) candidate else current
  }

  fun uiDegrees(rotation: Int) = when (rotation) {
    Surface.ROTATION_90 -> 90
    Surface.ROTATION_180 -> 180
    Surface.ROTATION_270 -> 270
    else -> 0
  }
}
