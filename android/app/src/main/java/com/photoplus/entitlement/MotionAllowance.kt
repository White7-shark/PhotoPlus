package com.photoplus.entitlement

import android.content.Context
import androidx.core.content.edit
import java.time.Instant
import java.time.ZoneId

/**
 * Photo Motion on the free plan: two finished videos a day, each one
 * unlocked by watching a short rewarded ad (previews are free and
 * unlimited). Counted on the phone with the trusted clock (Integrity), so
 * changing the date doesn't give more; Premium (or Photo Motion redeemed
 * with Rewards points) has no limit and no ads.
 */
object MotionAllowance {
  const val FREE_PER_DAY = 2
  private const val PREFS = "photoplus.motion"
  private const val K_DAY = "day"
  private const val K_USED = "used"
  private const val K_CREDITS = "credits"

  internal var clock: () -> Long = {
    if (com.photoplus.integrity.Integrity.installed()) com.photoplus.integrity.Integrity.time.now() else System.currentTimeMillis()
  }

  internal fun dayOf(ms: Long): String = Instant.ofEpochMilli(ms).atZone(ZoneId.systemDefault()).toLocalDate().toString()

  /** Today's count from what was stored: another day's doesn't count. */
  internal data class Today(val used: Int, val credits: Int)

  internal fun today(day: String, storedDay: String?, used: Int, credits: Int): Today =
    if (storedDay == day) Today(used.coerceAtLeast(0), credits.coerceAtLeast(0)) else Today(0, 0)

  /** Videos left today; an ad unlocks one at a time, never more than are left. */
  internal fun left(t: Today) = (FREE_PER_DAY - t.used).coerceAtLeast(0)
  internal fun withAd(t: Today) = if (left(t) > t.credits) t.copy(credits = t.credits + 1) else t
  internal fun afterVideo(t: Today) = Today(t.used + 1, (t.credits - 1).coerceAtLeast(0))
  internal fun canMake(t: Today) = left(t) > 0 && t.credits > 0

  private fun unlimited() = Entitlements.allows("motion")

  private fun read(context: Context): Today {
    val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    return today(dayOf(clock()), prefs.getString(K_DAY, null), prefs.getInt(K_USED, 0), prefs.getInt(K_CREDITS, 0))
  }

  private fun write(context: Context, t: Today) {
    context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit {
      putString(K_DAY, dayOf(clock())).putInt(K_USED, t.used).putInt(K_CREDITS, t.credits)
    }
  }

  /** { remaining (-1 unlimited), limit, credits (videos unlocked by ads, not yet made), needsAd }. */
  fun state(context: Context): Map<String, Any> {
    if (unlimited()) return mapOf("remaining" to -1, "limit" to FREE_PER_DAY, "credits" to 0, "needsAd" to false)
    val t = read(context)
    return mapOf("remaining" to left(t), "limit" to FREE_PER_DAY, "credits" to t.credits, "needsAd" to (left(t) > 0 && t.credits == 0))
  }

  /** Whether a finished video may be made now (doesn't use it). */
  fun allowed(context: Context): Boolean = unlimited() || canMake(read(context))

  /** A rewarded ad was watched to the end: unlocks one of today's videos. */
  @Synchronized
  fun grantAd(context: Context) {
    if (unlimited()) return
    write(context, withAd(read(context)))
  }

  /** Counts a finished video (free plan only). */
  @Synchronized
  fun use(context: Context) {
    if (unlimited()) return
    write(context, afterVideo(read(context)))
  }
}
