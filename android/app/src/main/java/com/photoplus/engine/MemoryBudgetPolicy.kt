package com.photoplus.engine

/**
 * Chooses how much native memory the engine may hold and how many jobs may
 * run at once, from the device's total RAM. Numbers follow the product spec's
 * targets (3 GB minimum, 4 GB recommended, 6 GB+ for demanding features) and
 * will be tuned from real-device profiling.
 */
object MemoryBudgetPolicy {
  private const val GB = 1024L * 1024L * 1024L
  private const val MB = 1024L * 1024L

  data class Budget(val engineBytes: Long, val workers: Int, val maxDecodeDimension: Int)

  fun forTotalRam(totalRamBytes: Long, isLowRamDevice: Boolean = false): Budget = when {
    isLowRamDevice || totalRamBytes < 3 * GB -> Budget(192 * MB, 1, 3072)
    totalRamBytes < 4 * GB -> Budget(320 * MB, 1, 4096)
    totalRamBytes < 6 * GB -> Budget(512 * MB, 2, 6144)
    else -> Budget(768 * MB, 2, 8192)
  }

  /** Budget to apply while the OS reports memory pressure. */
  fun underPressure(base: Budget, severe: Boolean): Budget =
    base.copy(engineBytes = if (severe) base.engineBytes / 4 else base.engineBytes / 2)
}
