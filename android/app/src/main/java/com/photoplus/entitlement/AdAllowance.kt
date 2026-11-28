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
 *
 * Offline, no ad can load: the use is still unlocked, but the ad is owed
 * (counted per tool, kept across days). With [MAX_OWED] owed the tool is
 * frozen until the phone is online and every owed ad has been watched; while
 * any are owed, each ad watched pays one off before it unlocks a new use.
 */
open class AdAllowance(private val prefsName: String, private val tool: String, val perDay: Int = 2) {
  private val kDay = "day"
  private val kUsed = "used"
  private val kCredits = "credits"
  private val kOwed = "owed"

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

  /** Offline: a use unlocked on credit (the ad owed); null when [MAX_OWED] are owed already (frozen). */
  internal fun offlineUnlock(t: Today, owed: Int): Pair<Today, Int>? =
    if (owed >= MAX_OWED || left(t) <= t.credits) null else withAd(t) to owed + 1

  /** An ad watched to the end online: pays off an owed one first, else unlocks a use. */
  internal fun watched(t: Today, owed: Int): Pair<Today, Int> = if (owed > 0) t to owed - 1 else withAd(t) to 0

  protected open fun unlimited() = Entitlements.allows(tool)

  private fun owedOf(context: Context) = context.getSharedPreferences(prefsName, Context.MODE_PRIVATE).getInt(kOwed, 0).coerceIn(0, MAX_OWED)

  private fun writeOwed(context: Context, owed: Int) {
    context.getSharedPreferences(prefsName, Context.MODE_PRIVATE).edit { putInt(kOwed, owed.coerceIn(0, MAX_OWED)) }
  }

  private fun read(context: Context): Today {
    val prefs = context.getSharedPreferences(prefsName, Context.MODE_PRIVATE)
    return today(dayOf(clock()), prefs.getString(kDay, null), prefs.getInt(kUsed, 0), prefs.getInt(kCredits, 0))
  }

  private fun write(context: Context, t: Today) {
    context.getSharedPreferences(prefsName, Context.MODE_PRIVATE).edit {
      putString(kDay, dayOf(clock())).putInt(kUsed, t.used).putInt(kCredits, t.credits)
    }
  }

  /**
   * { remaining (-1 unlimited), limit, credits (uses unlocked by ads, not yet made), needsAd,
   * owed (ads owed from offline unlocks), maxOwed, frozen (owed reached maxOwed) }.
   */
  fun state(context: Context): Map<String, Any> {
    if (unlimited()) {
      return mapOf("remaining" to -1, "limit" to perDay, "credits" to 0, "needsAd" to false, "owed" to 0, "maxOwed" to MAX_OWED, "frozen" to false)
    }
    val t = read(context)
    val owed = owedOf(context)
    return mapOf(
      "remaining" to left(t), "limit" to perDay, "credits" to t.credits,
      "needsAd" to ((left(t) > 0 && t.credits == 0) || owed > 0 && t.credits == 0),
      "owed" to owed, "maxOwed" to MAX_OWED, "frozen" to (owed >= MAX_OWED),
    )
  }

  fun owed(context: Context): Int = if (unlimited()) 0 else owedOf(context)

  /** Whether a finished one may be made now (doesn't use it). */
  fun allowed(context: Context): Boolean = unlimited() || canMake(read(context))

  /** A rewarded ad was watched to the end: pays off an owed ad, else unlocks one of today's uses. */
  @Synchronized
  fun grantAd(context: Context) {
    if (unlimited()) return
    val (t, owed) = watched(read(context), owedOf(context))
    write(context, t)
    writeOwed(context, owed)
  }

  /** Offline (no ad can load): unlocks a use and owes its ad; false when the tool is frozen. */
  @Synchronized
  fun grantOffline(context: Context): Boolean {
    if (unlimited()) return true
    val (t, owed) = offlineUnlock(read(context), owedOf(context)) ?: return false
    write(context, t)
    writeOwed(context, owed)
    return true
  }

  /** Counts a finished one (free plan only). */
  @Synchronized
  fun use(context: Context) {
    if (unlimited()) return
    write(context, afterVideo(read(context)))
  }

  companion object {
    /** Ads that may be owed from offline unlocks before a tool freezes. */
    const val MAX_OWED = 3

    /** The tools with ad-unlocked daily uses, by name. */
    fun of(tool: String): AdAllowance? = when (tool) {
      "motion" -> MotionAllowance
      "arts", "masters", "pencil" -> ArtsAllowance
      "toon" -> ToonAllowance
      else -> null
    }
  }
}

/** Photo Motion on the free plan: two finished videos a day, each unlocked by an ad (and watermarked). */
object MotionAllowance : AdAllowance("photoplus.motion", "motion")

/**
 * Photo Arts on the free plan (paintings and pencil drawings together): five
 * finished works a day, each unlocked by an ad, and watermarked.
 */
object ArtsAllowance : AdAllowance("photoplus.arts", "masters", perDay = 5) {
  override fun unlimited() = Entitlements.allows("masters") || Entitlements.allows("pencil")
}

/** Toon Studio on the free plan: five finished pictures a day, each unlocked by an ad, and watermarked. */
object ToonAllowance : AdAllowance("photoplus.toon", "toon", perDay = 5)
