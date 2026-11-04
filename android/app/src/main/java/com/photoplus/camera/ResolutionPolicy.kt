package com.photoplus.camera

import kotlin.math.abs

/** A width × height pair, independent of android.util.Size for JVM tests. */
data class PixelSize(val width: Int, val height: Int) {
  val pixels: Long get() = width.toLong() * height
  val megapixels: Double get() = pixels / 1_000_000.0
  val longEdge: Int get() = maxOf(width, height)
}

/**
 * Picks capture sizes from what the sensor actually offers, so the UI only
 * lists resolutions that exist for the chosen aspect ratio.
 */
object ResolutionPolicy {
  private const val RATIO_TOLERANCE = 0.02

  fun matchesRatio(size: PixelSize, ratio: AspectRatioMode): Boolean {
    val stream = ratio.streamRatio
    val target = stream.width.toDouble() / stream.height
    val actual = maxOf(size.width, size.height).toDouble() / minOf(size.width, size.height)
    return abs(actual - target) / target < RATIO_TOLERANCE
  }

  /** Largest size of the ratio not exceeding the preset's megapixels (or largest overall for MAX). */
  fun choose(supported: List<PixelSize>, ratio: AspectRatioMode, preset: ResolutionPreset): PixelSize? {
    val candidates = supported.filter { matchesRatio(it, ratio) }.sortedByDescending { it.pixels }
    if (candidates.isEmpty()) return supported.maxByOrNull { it.pixels }
    val limit = preset.megapixels ?: return candidates.first()
    // Allow 10% over so e.g. a 12.2 MP sensor still counts as "12 MP".
    return candidates.firstOrNull { it.megapixels <= limit * 1.1 } ?: candidates.last()
  }

  /** Presets that produce distinct sizes on this sensor, largest first. */
  fun availablePresets(supported: List<PixelSize>, ratio: AspectRatioMode): List<Pair<ResolutionPreset, PixelSize>> {
    val seen = mutableSetOf<PixelSize>()
    return ResolutionPreset.entries.mapNotNull { preset ->
      val size = choose(supported, ratio, preset) ?: return@mapNotNull null
      if (preset != ResolutionPreset.MAX && preset.megapixels != null && size.megapixels < preset.megapixels * 0.6) {
        return@mapNotNull null // sensor has nothing near this class
      }
      if (seen.add(size)) preset to size else null
    }
  }
}
