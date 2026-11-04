package com.photoplus.camera

import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** Device class for capture work; mirrors MemoryBudgetPolicy's tiers. */
enum class DeviceTier { ENTRY, STANDARD, PERFORMANCE }

/** How many frames to take and the longest edge each is decoded at. */
data class ModePlan(val frames: Int, val decodeDimension: Int, val upscale: Int)

/**
 * Sizes processed captures so they finish in seconds and fit the engine's
 * memory budget on every tier. Processing cost grows with pixels × frames,
 * so bursts work at a smaller size than single-frame Enhance, and
 * super-resolution, which doubles each axis, starts smaller still.
 */
object CapturePolicy {
  private const val MB = 1024L * 1024L
  private const val MIN_DIMENSION = 640

  fun tierFor(engineBudgetBytes: Long): DeviceTier = when {
    engineBudgetBytes >= 768 * MB -> DeviceTier.PERFORMANCE
    engineBudgetBytes >= 512 * MB -> DeviceTier.STANDARD
    else -> DeviceTier.ENTRY
  }

  fun plan(mode: CaptureMode, tier: DeviceTier): ModePlan {
    fun pick(entry: Int, standard: Int, performance: Int) = when (tier) {
      DeviceTier.ENTRY -> entry
      DeviceTier.STANDARD -> standard
      DeviceTier.PERFORMANCE -> performance
    }
    return when (mode) {
      CaptureMode.STANDARD, CaptureMode.RAW -> ModePlan(1, 0, 1)
      CaptureMode.ENHANCE -> ModePlan(1, pick(2048, 3072, 4096), 1)
      CaptureMode.NIGHT -> ModePlan(if (tier == DeviceTier.ENTRY) 4 else 6, pick(1600, 2400, 3200), 1)
      CaptureMode.SUPER_RES -> ModePlan(if (tier == DeviceTier.ENTRY) 6 else 8, pick(960, 1280, 1600), 2)
    }
  }

  /** A smaller plan to retry with when the engine runs out of budget, or null if already minimal. */
  fun reduced(plan: ModePlan): ModePlan? {
    val next = (plan.decodeDimension * 0.7).roundToInt()
    return if (plan.decodeDimension <= MIN_DIMENSION || next < MIN_DIMENSION / 2) null
    else plan.copy(decodeDimension = max(next, MIN_DIMENSION))
  }
}

/** An integer rectangle (left, top inclusive; right, bottom exclusive). */
data class IntRect(val left: Int, val top: Int, val right: Int, val bottom: Int) {
  val width: Int get() = right - left
  val height: Int get() = bottom - top
}

/**
 * Where a captured JPEG's pixels end up. ImageDecoder applies the JPEG's
 * EXIF orientation itself, so the CameraX crop rectangle (in buffer
 * coordinates) has to be rotated the same way; any rotation the EXIF does
 * not cover is written back as an EXIF tag on the output instead of
 * rotating pixels (no copy).
 */
object CaptureGeometry {
  data class DecodePlan(
    val targetWidth: Int,
    val targetHeight: Int,
    /** Crop in target (scaled, oriented) coordinates; null = whole image. */
    val crop: IntRect?,
    /** Clockwise rotation still needed to display upright, applied via EXIF. */
    val remainingRotation: Int,
  ) {
    val outputWidth: Int get() = crop?.width ?: targetWidth
    val outputHeight: Int get() = crop?.height ?: targetHeight
  }

  /** EXIF orientation → clockwise degrees (flips are not produced by cameras and count as 0). */
  fun exifRotation(orientation: Int): Int = when (orientation) {
    6 -> 90   // ORIENTATION_ROTATE_90
    3 -> 180  // ORIENTATION_ROTATE_180
    8 -> 270  // ORIENTATION_ROTATE_270
    else -> 0
  }

  fun normalise(degrees: Int) = ((degrees % 360) + 360) % 360

  /** Rotates [rect] clockwise with its image of [width] × [height]. */
  fun rotateRect(rect: IntRect, width: Int, height: Int, degrees: Int): IntRect = when (normalise(degrees)) {
    90 -> IntRect(height - rect.bottom, rect.left, height - rect.top, rect.right)
    180 -> IntRect(width - rect.right, height - rect.bottom, width - rect.left, height - rect.top)
    270 -> IntRect(rect.top, width - rect.right, rect.bottom, width - rect.left)
    else -> rect
  }

  fun decodePlan(
    bufferWidth: Int,
    bufferHeight: Int,
    crop: IntRect,
    exifDegrees: Int,
    rotationDegrees: Int,
    maxDimension: Int,
  ): DecodePlan {
    val exif = normalise(exifDegrees)
    val swap = exif == 90 || exif == 270
    val orientedW = if (swap) bufferHeight else bufferWidth
    val orientedH = if (swap) bufferWidth else bufferHeight
    val clamped = IntRect(
      crop.left.coerceIn(0, bufferWidth), crop.top.coerceIn(0, bufferHeight),
      crop.right.coerceIn(0, bufferWidth), crop.bottom.coerceIn(0, bufferHeight),
    ).takeIf { it.width > 0 && it.height > 0 } ?: IntRect(0, 0, bufferWidth, bufferHeight)
    val oriented = rotateRect(clamped, bufferWidth, bufferHeight, exif)
    val longest = max(oriented.width, oriented.height)
    val scale = if (maxDimension in 1 until longest) maxDimension.toDouble() / longest else 1.0
    val targetW = max(1, (orientedW * scale).roundToInt())
    val targetH = max(1, (orientedH * scale).roundToInt())
    val full = oriented.left == 0 && oriented.top == 0 && oriented.right == orientedW && oriented.bottom == orientedH
    val scaledCrop = if (full) null else {
      val l = floor(oriented.left * scale).toInt().coerceIn(0, targetW - 1)
      val t = floor(oriented.top * scale).toInt().coerceIn(0, targetH - 1)
      val w = max(1, min((oriented.width * scale).roundToInt(), targetW - l))
      val h = max(1, min((oriented.height * scale).roundToInt(), targetH - t))
      IntRect(l, t, l + w, t + h)
    }
    return DecodePlan(targetW, targetH, scaledCrop, normalise(rotationDegrees - exif))
  }

  /** Display size of an output after its remaining (EXIF) rotation. */
  fun displaySize(width: Int, height: Int, rotation: Int): Pair<Int, Int> =
    if (normalise(rotation) == 90 || normalise(rotation) == 270) height to width else width to height
}
