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

  /**
   * Phones sold as "N GB" report less because of kernel/carve-out reservations,
   * so tiers compare against 90% of the marketed size. MUST stay identical to
   * `nominal()` in src/services/capabilities/tiering.ts so the UI tier and the
   * native budget always agree (both test suites pin the same boundaries).
   */
  fun nominal(gb: Int): Long = (gb * GB * 9) / 10

  fun forTotalRam(totalRamBytes: Long, isLowRamDevice: Boolean = false): Budget = when {
    isLowRamDevice || totalRamBytes < nominal(3) -> Budget(192 * MB, 1, 3072) // unsupported
    totalRamBytes < nominal(4) -> Budget(320 * MB, 1, 4096) // entry
    totalRamBytes < nominal(6) -> Budget(512 * MB, 2, 6144) // standard
    else -> Budget(768 * MB, 2, 8192) // performance
  }

  /** Budget to apply while the OS reports memory pressure. */
  fun underPressure(base: Budget, severe: Boolean): Budget =
    base.copy(engineBytes = if (severe) base.engineBytes / 4 else base.engineBytes / 2)
}
