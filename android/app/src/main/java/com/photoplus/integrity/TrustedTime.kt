package com.photoplus.integrity

import kotlin.math.abs
import kotlin.math.max

/**
 * Time the person cannot change. Each online check stores the server's time
 * with the phone's time since boot (which only moves forward and cannot be
 * set); until the next restart, the true time is that server time plus the
 * time since boot. After a restart the phone's clock is accepted only if it
 * has not gone back before a time already known to be true.
 *
 * It answers three questions: what time it really is ([now]), whether the
 * phone's clock is wrong ([clockWrong]), and how long it has been since the
 * last online check ([offlineMs]) — counted by the boot timer too, so
 * setting the clock back does not make that shorter.
 */
class TrustedTime(
  private val store: Store,
  private val wall: () -> Long = System::currentTimeMillis,
  /** Milliseconds since boot (SystemClock.elapsedRealtime), unaffected by the clock. */
  private val elapsed: () -> Long,
  /** Which boot this is (Settings.Global.BOOT_COUNT; -1 when unknown). */
  private val boot: () -> Long,
) {
  interface Store {
    fun getLong(key: String, default: Long): Long
    fun putLong(key: String, value: Long)
  }

  private val lock = Any()

  /** A server time and the boot timer when it was read (this boot only). */
  private fun anchor(): Pair<Long, Long>? {
    val server = store.getLong(K_SERVER, 0)
    if (server == 0L || store.getLong(K_BOOT, Long.MIN_VALUE) != boot() || boot() < 0) return null
    val at = store.getLong(K_ELAPSED, 0)
    // The boot timer never goes back within a boot; if it did, this is another boot.
    if (elapsed() < at) return null
    return server to at
  }

  /** The true time: from the last online check while the phone has not restarted, else the clock, never before a true time already seen. */
  fun now(): Long = synchronized(lock) {
    val a = anchor()
    val t = if (a != null) a.first + (elapsed() - a.second) else max(wall(), store.getLong(K_SEEN, 0))
    if (t > store.getLong(K_SEEN, 0)) store.putLong(K_SEEN, t)
    t
  }

  /** Whether the true time is known (an online check this boot). */
  fun anchored(): Boolean = synchronized(lock) { anchor() != null }

  /** How far the phone's clock is from the true time, or null when that can't be told. */
  fun skewMs(): Long? = synchronized(lock) {
    val a = anchor()
    if (a != null) return wall() - (a.first + (elapsed() - a.second))
    // Restarted: the clock may not be behind a time known to be true.
    val seen = store.getLong(K_SEEN, 0)
    if (seen > 0 && wall() < seen - TOLERANCE_MS) wall() - seen else null
  }

  fun clockWrong(): Boolean = skewMs()?.let { abs(it) > TOLERANCE_MS } ?: false

  /** An online check: [serverMs] is the server's time when it answered. */
  fun synced(serverMs: Long) = synchronized(lock) {
    store.putLong(K_SERVER, serverMs)
    store.putLong(K_ELAPSED, elapsed())
    store.putLong(K_BOOT, boot())
    store.putLong(K_ONLINE, serverMs)
    store.putLong(K_UPTIME, 0)
    store.putLong(K_UPTIME_MARK, elapsed())
    store.putLong(K_UPTIME_BOOT, boot())
    if (serverMs > store.getLong(K_SEEN, 0)) store.putLong(K_SEEN, serverMs)
  }

  /** The first run counts as online (the app was just downloaded). */
  fun firstRun() = synchronized(lock) {
    if (store.getLong(K_ONLINE, 0) == 0L) {
      store.putLong(K_ONLINE, wall())
      store.putLong(K_UPTIME_MARK, elapsed())
      store.putLong(K_UPTIME_BOOT, boot())
    }
  }

  fun lastOnline(): Long = store.getLong(K_ONLINE, 0)

  /**
   * Time since the last online check: the longer of the true time since then
   * and the phone's running time since then (counted across restarts), so
   * neither a clock set back nor a restart shortens it.
   */
  fun offlineMs(): Long = synchronized(lock) {
    val online = lastOnline()
    if (online == 0L) return 0
    // Running time since the check, added up boot by boot.
    var up = store.getLong(K_UPTIME, 0)
    val mark = store.getLong(K_UPTIME_MARK, 0)
    if (store.getLong(K_UPTIME_BOOT, Long.MIN_VALUE) == boot() && boot() >= 0 && elapsed() >= mark) {
      up += elapsed() - mark
    } else {
      // A new boot: the time since this boot started.
      up += elapsed()
    }
    store.putLong(K_UPTIME, up)
    store.putLong(K_UPTIME_MARK, elapsed())
    store.putLong(K_UPTIME_BOOT, boot())
    max(now() - online, up)
  }

  companion object {
    /** How far the phone's clock may be off (networks keep it within seconds). */
    const val TOLERANCE_MS = 5 * 60_000L
    private const val K_SERVER = "t.server"
    private const val K_ELAPSED = "t.elapsed"
    private const val K_BOOT = "t.boot"
    private const val K_SEEN = "t.seen"
    private const val K_ONLINE = "t.online"
    private const val K_UPTIME = "t.uptime"
    private const val K_UPTIME_MARK = "t.uptimeMark"
    private const val K_UPTIME_BOOT = "t.uptimeBoot"
  }
}
