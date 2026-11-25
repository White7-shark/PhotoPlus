package com.photoplus.rewards

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import kotlin.math.max
import kotlin.math.min

/**
 * Rewards for the free plan: points from daily check-ins, rewarded ads and
 * tasks, spent on Premium passes and single Premium tools; and the share
 * campaign (30 shares give two days of Premium). Held natively, like
 * [com.photoplus.ads.AdRewards]: ad points come only from the ad SDK's reward
 * callback, and Premium time is read by [com.photoplus.entitlement.Entitlements]
 * here, so the app's screens cannot grant either.
 *
 * Balanced so that rewards are a real taste of Premium, not a free
 * substitute: about 240 points on a busy day, a day of Premium for 600, and
 * never more than [CAP_DAYS] days of Premium from rewards in any 30 days.
 *
 * Time only moves forward: a clock set back does not reopen a check-in, an
 * ad or a share lock (the latest time seen is used).
 */
class RewardsLedger(private val store: Store, private val clock: () -> Long = System::currentTimeMillis, private val zoneOffsetMs: (Long) -> Int = { 0 }) {
  interface Store {
    fun getLong(key: String, default: Long = 0): Long
    fun putLong(key: String, value: Long)
    fun getString(key: String): String?
    fun putString(key: String, value: String?)
    fun keys(): Set<String>
  }

  class RewardsException(val code: String, message: String) : Exception(message)

  enum class Task(val points: Int, val daily: Boolean, val event: String, val goal: Int) {
    FIRST_PHOTO(30, false, "photo", 1),
    FIRST_EDIT(30, false, "edit", 1),
    FIRST_PRODUCT(40, false, "product", 1),
    FIRST_FILM(40, false, "film", 1),
    FIRST_PROJECT(30, false, "project", 1),
    FIRST_GLIDE(40, false, "glide", 1),
    DAILY_PHOTOS(15, true, "photo", 3),
    DAILY_EDIT(15, true, "edit", 1),
    DAILY_STUDIO(20, true, "studio", 1),
    ;

    val id: String get() = name.lowercase().replace('_', '-')
  }

  /** What points buy. Tools are unlocked for [TOOL_HOURS] hours. */
  enum class Item(val cost: Int, val premiumDays: Int) {
    TOOL(250, 0),
    PREMIUM_1(600, 1),
    PREMIUM_3(1500, 3),
    PREMIUM_7(3200, 7),
    ;

    val id: String get() = name.lowercase().replace('_', '-')
  }

  private val lock = LOCK

  /** Never earlier than a time already seen. */
  fun now(): Long = synchronized(lock) {
    val t = max(clock(), store.getLong(K_MAX_SEEN))
    store.putLong(K_MAX_SEEN, t)
    t
  }

  private fun day(t: Long): Long = Math.floorDiv(t + zoneOffsetMs(t), DAY_MS)

  // ---- Points

  fun points(): Int = store.getLong(K_POINTS).toInt()

  private fun add(points: Int): Int {
    val next = min(MAX_BALANCE, points() + points)
    store.putLong(K_POINTS, next.toLong())
    store.putLong(K_EARNED, store.getLong(K_EARNED) + points)
    return next
  }

  // ---- Check-in

  /** The day of the streak a check-in today would be (1..7), and whether today is done. */
  fun checkInStatus(): Pair<Int, Boolean> = synchronized(lock) {
    val today = day(now())
    val last = store.getLong(K_CHECKIN_DAY, Long.MIN_VALUE)
    val streak = store.getLong(K_STREAK).toInt()
    when {
      last == today -> streak to true
      last == today - 1 -> (streak % STREAK.size) + 1 to false
      else -> 1 to false
    }
  }

  fun checkIn(): Int = synchronized(lock) {
    val (next, done) = checkInStatus()
    if (done) throw RewardsException("ALREADY_DONE", "You've checked in today. Come back tomorrow.")
    store.putLong(K_CHECKIN_DAY, day(now()))
    store.putLong(K_STREAK, next.toLong())
    val pts = STREAK[next - 1]
    add(pts)
    pts
  }

  // ---- Rewarded ads

  fun adsToday(): Int = if (store.getLong(K_AD_DAY, Long.MIN_VALUE) == day(now())) store.getLong(K_ADS).toInt() else 0

  /** Seconds until the next ad may be watched for points (0: now), or -1 when today's ads are used up. */
  fun adWait(): Long = synchronized(lock) {
    if (adsToday() >= ADS_PER_DAY) return -1
    val since = now() - store.getLong(K_LAST_AD)
    if (since >= AD_GAP_MS) 0 else (AD_GAP_MS - since + 999) / 1000
  }

  /** Called from the ad SDK's reward callback only. */
  fun adWatched(): Int = synchronized(lock) {
    if (adWait() != 0L) return 0
    val today = day(now())
    store.putLong(K_ADS, (adsToday() + 1).toLong())
    store.putLong(K_AD_DAY, today)
    store.putLong(K_LAST_AD, now())
    add(AD_POINTS)
    AD_POINTS
  }

  // ---- Tasks

  /** Something the person did (a photo taken, an edit saved...), counted toward tasks. */
  fun record(event: String) = synchronized(lock) {
    if (Task.entries.none { it.event == event }) throw RewardsException("INVALID_ARGUMENT", "Unknown activity")
    val today = day(now())
    val key = "$K_EVENT$event"
    val count = if (store.getLong("$key:day", Long.MIN_VALUE) == today) store.getLong(key) else 0
    store.putLong(key, min(99, count + 1))
    store.putLong("$key:day", today)
    store.putLong("$K_EVER$event", 1)
  }

  fun progress(t: Task): Int = synchronized(lock) {
    if (!t.daily) return store.getLong("$K_EVER${t.event}").toInt().coerceAtMost(t.goal)
    val key = "$K_EVENT${t.event}"
    val today = day(now())
    (if (store.getLong("$key:day", Long.MIN_VALUE) == today) store.getLong(key).toInt() else 0).coerceAtMost(t.goal)
  }

  fun claimed(t: Task): Boolean = synchronized(lock) {
    val v = store.getLong("$K_TASK${t.id}", Long.MIN_VALUE)
    if (t.daily) v == day(now()) else v != Long.MIN_VALUE
  }

  fun claim(id: String): Int = synchronized(lock) {
    val t = Task.entries.firstOrNull { it.id == id } ?: throw RewardsException("INVALID_ARGUMENT", "Unknown task")
    if (claimed(t)) throw RewardsException("ALREADY_DONE", "You've already collected this.")
    if (progress(t) < t.goal) throw RewardsException("NOT_YET", "Finish the task first.")
    store.putLong("$K_TASK${t.id}", if (t.daily) day(now()) else now())
    add(t.points)
    t.points
  }

  // ---- Shares

  /** When sharing to [target] is open again (0: now). */
  fun shareOpensAt(target: String): Long = synchronized(lock) {
    val last = store.getLong("$K_SHARE$target", Long.MIN_VALUE)
    if (last == Long.MIN_VALUE) return 0
    val opens = last + SHARE_LOCK_MS
    if (opens > now()) opens else 0
  }

  fun shares(): Int = store.getLong(K_SHARES).toInt()

  /** When the campaign can be done again after a completed one (0: open now). */
  fun campaignOpensAt(): Long {
    val done = store.getLong(K_CAMPAIGN_DONE)
    return if (done == 0L || now() >= done + CAMPAIGN_REPEAT_MS) 0 else done + CAMPAIGN_REPEAT_MS
  }

  fun sharePassReady(): Boolean = store.getLong(K_SHARE_PASS_READY) == 1L

  /** A share to [target] (an app's package) was made; counts once per app per [SHARE_LOCK_MS]. */
  fun shared(target: String): Boolean = synchronized(lock) {
    if (!TARGET.matches(target)) throw RewardsException("INVALID_ARGUMENT", "Unknown app")
    if (shareOpensAt(target) != 0L || campaignOpensAt() != 0L || sharePassReady()) return false
    store.putLong("$K_SHARE$target", now())
    val n = shares() + 1
    if (n >= SHARE_GOAL) {
      store.putLong(K_SHARES, 0)
      store.putLong(K_SHARE_PASS_READY, 1)
      store.putLong(K_CAMPAIGN_DONE, now())
    } else {
      store.putLong(K_SHARES, n.toLong())
    }
    true
  }

  fun claimSharePass(): Long = synchronized(lock) {
    if (!sharePassReady()) throw RewardsException("NOT_YET", "Share Photo Plus $SHARE_GOAL times first.")
    val until = grant(SHARE_DAYS)
    store.putLong(K_SHARE_PASS_READY, 0)
    until
  }

  // ---- Premium passes and tools

  fun premiumUntil(): Long = store.getLong(K_PREMIUM_UNTIL)

  fun premiumActive(): Boolean = premiumUntil() > now()

  fun toolUntil(tool: String): Long = store.getLong("$K_TOOL$tool")

  fun toolActive(tool: String): Boolean = toolUntil(tool) > now()

  /** Premium days from rewards in the last 30 days. */
  fun daysUsed(): Int = synchronized(lock) {
    val from = now() - WINDOW_MS
    grants().filter { it.first >= from }.sumOf { it.second }
  }

  private fun grants(): List<Pair<Long, Int>> =
    store.getString(K_GRANTS).orEmpty().split(';').mapNotNull { g ->
      val (t, d) = g.split(':').takeIf { it.size == 2 } ?: return@mapNotNull null
      (t.toLongOrNull() ?: return@mapNotNull null) to (d.toIntOrNull() ?: return@mapNotNull null)
    }

  private fun grant(days: Int): Long {
    if (daysUsed() + days > CAP_DAYS) {
      throw RewardsException("CAP_REACHED", "Rewards give up to $CAP_DAYS days of Premium in 30 days. Premium itself has no limit.")
    }
    val t = now()
    val until = max(t, premiumUntil()) + days * DAY_MS
    store.putLong(K_PREMIUM_UNTIL, until)
    val kept = grants().filter { it.first >= t - WINDOW_MS } + (t to days)
    store.putString(K_GRANTS, kept.joinToString(";") { "${it.first}:${it.second}" })
    return until
  }

  fun redeem(itemId: String, tool: String?): Long = synchronized(lock) {
    val item = Item.entries.firstOrNull { it.id == itemId } ?: throw RewardsException("INVALID_ARGUMENT", "Unknown reward")
    if (item == Item.TOOL && (tool == null || tool !in TOOLS)) throw RewardsException("INVALID_ARGUMENT", "Choose a tool")
    if (points() < item.cost) throw RewardsException("NOT_ENOUGH", "You need ${item.cost - points()} more points.")
    val until = if (item == Item.TOOL) {
      val t = now()
      val u = max(t, toolUntil(tool!!)) + TOOL_HOURS * HOUR_MS
      store.putLong("$K_TOOL$tool", u)
      u
    } else {
      grant(item.premiumDays)
    }
    store.putLong(K_POINTS, (points() - item.cost).toLong())
    until
  }

  fun state(subscribed: Boolean): Map<String, Any?> = synchronized(lock) {
    val (next, done) = checkInStatus()
    val t = now()
    mapOf(
      "subscribed" to subscribed,
      "points" to points(),
      "earned" to store.getLong(K_EARNED).toDouble(),
      "now" to t.toDouble(),
      "checkIn" to mapOf("day" to next, "done" to done, "schedule" to STREAK.toList()),
      "ads" to mapOf("today" to adsToday(), "perDay" to ADS_PER_DAY, "points" to AD_POINTS, "wait" to adWait().toDouble()),
      "tasks" to Task.entries.map {
        mapOf("id" to it.id, "points" to it.points, "daily" to it.daily, "goal" to it.goal, "progress" to progress(it), "claimed" to claimed(it))
      },
      "shares" to mapOf(
        "count" to shares(),
        "goal" to SHARE_GOAL,
        "days" to SHARE_DAYS,
        "lockHours" to (SHARE_LOCK_MS / HOUR_MS).toInt(),
        "ready" to sharePassReady(),
        "opensAt" to campaignOpensAt().toDouble(),
        "locks" to store.keys().filter { it.startsWith(K_SHARE) }.associate { k ->
          val target = k.removePrefix(K_SHARE)
          target to shareOpensAt(target).toDouble()
        }.filterValues { it > 0 },
      ),
      "premiumUntil" to premiumUntil().toDouble(),
      "tools" to TOOLS.associateWith { toolUntil(it).toDouble() }.filterValues { it > t },
      "daysUsed" to daysUsed(),
      "capDays" to CAP_DAYS,
      "store" to Item.entries.map { mapOf("id" to it.id, "cost" to it.cost, "days" to it.premiumDays, "hours" to if (it == Item.TOOL) TOOL_HOURS else 0) },
    )
  }

  companion object {
    private val LOCK = Any()
    const val HOUR_MS = 3_600_000L
    const val DAY_MS = 24 * HOUR_MS
    const val WINDOW_MS = 30 * DAY_MS
    val STREAK = intArrayOf(10, 15, 20, 25, 30, 40, 60)
    const val AD_POINTS = 20
    const val ADS_PER_DAY = 8
    const val AD_GAP_MS = 60_000L
    const val SHARE_GOAL = 30
    const val SHARE_DAYS = 2
    const val SHARE_LOCK_MS = 8 * HOUR_MS
    const val CAMPAIGN_REPEAT_MS = 30 * DAY_MS
    const val CAP_DAYS = 10
    const val TOOL_HOURS = 24
    const val MAX_BALANCE = 20_000
    /** Tools a single unlock can open (the native checks name them). */
    val TOOLS = setOf("sceneSwap", "relight", "productPro", "exportHd", "studioLight", "hair", "batch")
    private val TARGET = Regex("^[A-Za-z0-9_.]{1,120}$")

    private const val K_POINTS = "points"
    private const val K_EARNED = "earned"
    private const val K_MAX_SEEN = "maxSeen"
    private const val K_CHECKIN_DAY = "checkInDay"
    private const val K_STREAK = "streak"
    private const val K_AD_DAY = "adDay"
    private const val K_ADS = "adsToday"
    private const val K_LAST_AD = "lastAd"
    private const val K_EVENT = "event:"
    private const val K_EVER = "ever:"
    private const val K_TASK = "task:"
    private const val K_SHARE = "share:"
    private const val K_SHARES = "shares"
    private const val K_SHARE_PASS_READY = "sharePassReady"
    private const val K_CAMPAIGN_DONE = "campaignDone"
    private const val K_PREMIUM_UNTIL = "premiumUntil"
    private const val K_GRANTS = "grants"
    private const val K_TOOL = "tool:"

    @Volatile private var shared: RewardsLedger? = null

    fun get(context: Context): RewardsLedger = shared ?: synchronized(LOCK) {
      shared ?: run {
        val prefs: SharedPreferences = context.applicationContext.getSharedPreferences("pp_rewards", Context.MODE_PRIVATE)
        RewardsLedger(
          object : Store {
            override fun getLong(key: String, default: Long) = prefs.getLong(key, default)
            override fun putLong(key: String, value: Long) = prefs.edit { putLong(key, value) }
            override fun getString(key: String): String? = prefs.getString(key, null)
            override fun putString(key: String, value: String?) = prefs.edit { putString(key, value) }
            override fun keys(): Set<String> = prefs.all.keys
          },
          zoneOffsetMs = { java.util.TimeZone.getDefault().getOffset(it) },
        ).also { shared = it }
      }
    }

    /** Null until the app has started (tests, or very early). */
    fun installed(): RewardsLedger? = shared
  }
}
