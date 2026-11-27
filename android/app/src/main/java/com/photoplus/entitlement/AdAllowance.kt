package com.photoplus.entitlement

import android.content.Context
import androidx.core.content.edit
import java.time.Instant
import java.time.ZoneId

/**
 * A Premium tool the free plan may use a few times a day, each time
 * unlocked by watching a short rewarded ad (previews are free and
 * unlimited). Counted on the phone with the trusted clock (Integrity), so
 * changing the date doesn't give more; Premium (or the tool redeemed with
 * Rewards points) has no limit and no ads.
 */
open class AdAllowance(private val prefsName: String, private val tool: String, val perDay: Int = 2) {
  private val kDay = "day"
  private val kUsed = "used"
  private val kCredits = "credits"

  internal var clock: () -> Long = {
    if (com.photoplus.integrity.Integrity.installed()) com.photoplus.integrity.Integrity.time.now() else System.currentTimeMillis()
  }

  internal fun dayOf(ms: Long): String = Instant.ofEpochMilli(ms).atZone(ZoneId.systemDefault()).toLocalDate().toString()

  /** Today's count from what was stored: another day's doesn't count. */
  internal data class Today(val used: Int, val credits: Int)

  internal fun today(day: String, storedDay: String?, used: Int, credits: Int): Today =
    if (storedDay == day) Today(used.coerceAtLeast(0), credits.coerceAtLeast(0)) else Today(0, 0)

  /** Uses left today; an ad unlocks one at a time, never more than are left. */
  internal fun left(t: Today) = (perDay - t.used).coerceAtLeast(0)
  internal fun withAd(t: Today) = if (left(t) > t.credits) t.copy(credits = t.credits + 1) else t
  internal fun afterVideo(t: Today) = Today(t.used + 1, (t.credits - 1).coerceAtLeast(0))
  internal fun canMake(t: Today) = left(t) > 0 && t.credits > 0

  private fun unlimited() = Entitlements.allows(tool)

  private fun read(context: Context): Today {
    val prefs = context.getSharedPreferences(prefsName, Context.MODE_PRIVATE)
    return today(dayOf(clock()), prefs.getString(kDay, null), prefs.getInt(kUsed, 0), prefs.getInt(kCredits, 0))
  }

  private fun write(context: Context, t: Today) {
    context.getSharedPreferences(prefsName, Context.MODE_PRIVATE).edit {
      putString(kDay, dayOf(clock())).putInt(kUsed, t.used).putInt(kCredits, t.credits)
    }
  }

  /** { remaining (-1 unlimited), limit, credits (uses unlocked by ads, not yet made), needsAd }. */
  fun state(context: Context): Map<String, Any> {
    if (unlimited()) return mapOf("remaining" to -1, "limit" to perDay, "credits" to 0, "needsAd" to false)
    val t = read(context)
    return mapOf("remaining" to left(t), "limit" to perDay, "credits" to t.credits, "needsAd" to (left(t) > 0 && t.credits == 0))
  }

  /** Whether a finished one may be made now (doesn't use it). */
  fun allowed(context: Context): Boolean = unlimited() || canMake(read(context))

  /** A rewarded ad was watched to the end: unlocks one of today's uses. */
  @Synchronized
  fun grantAd(context: Context) {
    if (unlimited()) return
    write(context, withAd(read(context)))
  }

  /** Counts a finished one (free plan only). */
  @Synchronized
  fun use(context: Context) {
    if (unlimited()) return
    write(context, afterVideo(read(context)))
  }

  companion object {
    /** The tools with ad-unlocked daily uses, by name. */
    fun of(tool: String): AdAllowance? = when (tool) {
      "motion" -> MotionAllowance
      "pencil" -> PencilAllowance
      else -> null
    }
  }
}

/** Photo Motion on the free plan: two finished videos a day, each unlocked by an ad (and watermarked). */
object MotionAllowance : AdAllowance("photoplus.motion", "motion")

/** Pencil Art on the free plan: two finished drawings a day, each unlocked by an ad. */
object PencilAllowance : AdAllowance("photoplus.pencil", "pencil")
