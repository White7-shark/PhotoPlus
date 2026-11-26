package com.photoplus.entitlement

import android.content.Context
import androidx.core.content.edit
import java.time.Instant
import java.time.ZoneId

/**
 * Masters on the free plan: one finished painting a day (previews are
 * unlimited). Counted on the phone with the trusted clock (Integrity), so
 * changing the date doesn't give more; Premium (or Masters redeemed with
 * Rewards points) has no limit.
 */
object ArtAllowance {
  const val FREE_PER_DAY = 1
  private const val PREFS = "photoplus.art"
  private const val K_DAY = "day"
  private const val K_USED = "used"

  internal var clock: () -> Long = {
    if (com.photoplus.integrity.Integrity.installed()) com.photoplus.integrity.Integrity.time.now() else System.currentTimeMillis()
  }

  /** The local calendar day of [ms] (the allowance comes back at midnight). */
  internal fun dayOf(ms: Long): String = Instant.ofEpochMilli(ms).atZone(ZoneId.systemDefault()).toLocalDate().toString()

  private fun unlimited() = Entitlements.allows("masters")

  /** Used today, from what was stored: a count from another day doesn't count. */
  internal fun usedOn(today: String, storedDay: String?, storedUsed: Int): Int = if (storedDay == today) storedUsed.coerceAtLeast(0) else 0

  /** Free paintings left after [used] today. */
  internal fun left(used: Int): Int = (FREE_PER_DAY - used).coerceAtLeast(0)

  /** Free paintings left today; -1 when there is no limit. */
  fun remaining(context: Context): Int {
    if (unlimited()) return -1
    val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    return left(usedOn(dayOf(clock()), prefs.getString(K_DAY, null), prefs.getInt(K_USED, 0)))
  }

  /** Whether one more finished painting is allowed now (doesn't use it). */
  fun allowed(context: Context): Boolean = unlimited() || remaining(context) > 0

  /** Counts a finished painting (free plan only). */
  @Synchronized
  fun use(context: Context) {
    if (unlimited()) return
    val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    val today = dayOf(clock())
    val used = usedOn(today, prefs.getString(K_DAY, null), prefs.getInt(K_USED, 0))
    prefs.edit { putString(K_DAY, today).putInt(K_USED, used + 1) }
  }
}
