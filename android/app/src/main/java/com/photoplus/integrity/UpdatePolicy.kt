package com.photoplus.integrity

import kotlin.math.min

/**
 * A new version on Google Play: the person has [DEADLINE_MS] (7 days) from
 * when it was first seen to update. Until then a banner asks (Update / Later;
 * Later hides it for a while) and a notification reminds them every
 * [REMINDER_MS] (2 days); after the deadline the app is blocked until they
 * update. Installing the new version clears it. Times are trusted time.
 */
class UpdatePolicy(private val store: TrustedTime.Store) {
  data class State(
    val pending: Boolean,
    val versionCode: Long,
    val deadline: Long,
    val required: Boolean,
    val banner: Boolean,
    val reminderDue: Boolean,
  )

  /** What Play said: [available] a newer version code, or null for none. */
  fun observed(available: Long?, installed: Long, now: Long) {
    if (available == null || available <= installed) {
      // Up to date (or updated): nothing pending.
      if (store.getLong(K_VERSION, 0) <= installed) clear()
      return
    }
    if (store.getLong(K_VERSION, 0) <= installed) {
      // A new update: the deadline starts now.
      store.putLong(K_FIRST_SEEN, now)
      store.putLong(K_REMINDED, now)
      store.putLong(K_LATER, 0)
    }
    // A still newer version keeps the deadline of the first one.
    store.putLong(K_VERSION, available)
  }

  fun state(now: Long, installed: Long): State {
    val version = store.getLong(K_VERSION, 0)
    if (version <= installed) return State(false, 0, 0, false, false, false)
    val first = store.getLong(K_FIRST_SEEN, now)
    val deadline = first + DEADLINE_MS
    val required = now >= deadline
    return State(
      pending = true,
      versionCode = version,
      deadline = deadline,
      required = required,
      banner = !required && now >= store.getLong(K_LATER, 0),
      reminderDue = !required && now - store.getLong(K_REMINDED, first) >= REMINDER_MS,
    )
  }

  /** Later: the banner comes back at the next reminder, and always on the last day. */
  fun later(now: Long, installed: Long) {
    val s = state(now, installed)
    if (!s.pending) return
    store.putLong(K_LATER, min(now + REMINDER_MS, s.deadline - LAST_DAY_MS))
  }

  fun reminded(now: Long) {
    store.putLong(K_REMINDED, now)
    store.putLong(K_LATER, 0)
  }

  private fun clear() {
    store.putLong(K_VERSION, 0)
    store.putLong(K_FIRST_SEEN, 0)
    store.putLong(K_REMINDED, 0)
    store.putLong(K_LATER, 0)
  }

  companion object {
    const val DAY_MS = 86_400_000L
    const val DEADLINE_MS = 7 * DAY_MS
    const val REMINDER_MS = 2 * DAY_MS
    const val LAST_DAY_MS = DAY_MS
    private const val K_VERSION = "u.version"
    private const val K_FIRST_SEEN = "u.firstSeen"
    private const val K_REMINDED = "u.reminded"
    private const val K_LATER = "u.later"
  }
}
