package com.photoplus.rewards

import com.photoplus.rewards.RewardsLedger.Companion.DAY_MS
import com.photoplus.rewards.RewardsLedger.Companion.HOUR_MS
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows

class RewardsLedgerTest {
  private class MapStore : RewardsLedger.Store {
    val longs = HashMap<String, Long>()
    val strings = HashMap<String, String?>()
    override fun getLong(key: String, default: Long) = longs[key] ?: default
    override fun putLong(key: String, value: Long) {
      longs[key] = value
    }
    override fun getString(key: String) = strings[key]
    override fun putString(key: String, value: String?) {
      strings[key] = value
    }
    override fun keys(): Set<String> = longs.keys + strings.keys
  }

  private var t = 1_800_000_000_000L
  private val ledger = RewardsLedger(MapStore(), { t })

  private fun code(block: () -> Unit) = assertThrows<RewardsLedger.RewardsException> { block() }.code

  @Test fun checkInsBuildAStreakAndAMissedDayStartsAgain() {
    assertEquals(10, ledger.checkIn())
    assertEquals("ALREADY_DONE", code { ledger.checkIn() })
    val days = (2..7).map {
      t += DAY_MS
      ledger.checkIn()
    }
    assertEquals(listOf(15, 20, 25, 30, 40, 60), days)
    t += DAY_MS
    assertEquals(10, ledger.checkIn()) // a new week
    t += 2 * DAY_MS
    assertEquals(10, ledger.checkIn()) // a day was missed
    assertEquals(10 + 200 + 10, ledger.points())
  }

  @Test fun adsGivePointsWithAGapAndADailyLimit() {
    assertEquals(20, ledger.adWatched())
    assertEquals(0, ledger.adWatched()) // too soon
    assertTrue(ledger.adWait() in 1..60)
    repeat(7) {
      t += 61_000
      assertEquals(20, ledger.adWatched())
    }
    t += 61_000
    assertEquals(-1, ledger.adWait())
    assertEquals(0, ledger.adWatched())
    assertEquals(8 * 20, ledger.points())
    t += DAY_MS
    assertEquals(0, ledger.adWait())
  }

  @Test fun tasksPayOnceOrOnceADayWhenDone() {
    assertEquals("NOT_YET", code { ledger.claim("first-photo") })
    ledger.record("photo")
    assertEquals(30, ledger.claim("first-photo"))
    assertEquals("ALREADY_DONE", code { ledger.claim("first-photo") })
    assertEquals("NOT_YET", code { ledger.claim("daily-photos") })
    ledger.record("photo")
    ledger.record("photo")
    assertEquals(15, ledger.claim("daily-photos"))
    t += DAY_MS
    assertEquals("NOT_YET", code { ledger.claim("daily-photos") }) // a new day, new photos needed
    assertEquals("ALREADY_DONE", code { ledger.claim("first-photo") })
    assertEquals("INVALID_ARGUMENT", code { ledger.record("hack") })
    assertEquals("INVALID_ARGUMENT", code { ledger.claim("nope") })
  }

  @Test fun pointsBuyPremiumTimeAndToolsWithinTheMonthlyCap() {
    assertEquals("NOT_ENOUGH", code { ledger.redeem("premium-1", null) })
    // Twenty days of every ad: enough for a week of Premium.
    repeat(20) {
      t += DAY_MS
      repeat(8) {
        t += 61_000
        ledger.adWatched()
      }
    }
    assertEquals(3200, ledger.points())

    val store = MapStore().apply { longs["points"] = 20_000 }
    val rich = RewardsLedger(store, { t })
    // A tool for a day.
    assertEquals(t + 24 * HOUR_MS, rich.redeem("tool", "relight"))
    assertTrue(rich.toolActive("relight"))
    assertFalse(rich.toolActive("hair"))
    assertEquals("INVALID_ARGUMENT", code { rich.redeem("tool", "everything") })
    // Premium days stack, up to 10 in any 30 days.
    rich.redeem("premium-1", null)
    assertTrue(rich.premiumActive())
    rich.redeem("premium-1", null)
    assertEquals(t + 2 * DAY_MS, rich.premiumUntil())
    rich.redeem("premium-7", null)
    assertEquals(9, rich.daysUsed())
    assertEquals("CAP_REACHED", code { rich.redeem("premium-3", null) })
    assertEquals(20_000 - 250 - 600 - 600 - 3200, rich.points()) // a refused redemption costs nothing
    t += 31 * DAY_MS
    assertFalse(rich.premiumActive())
    assertFalse(rich.toolActive("relight"))
    assertEquals(0, rich.daysUsed())
    rich.redeem("premium-3", null)
  }

  @Test fun thirtySharesGiveTwoDaysOncePerAppEveryEightHours() {
    assertTrue(ledger.shared("com.whatsapp"))
    assertFalse(ledger.shared("com.whatsapp")) // locked
    assertTrue(ledger.shareOpensAt("com.whatsapp") == t + 8 * HOUR_MS)
    t += 8 * HOUR_MS
    assertTrue(ledger.shared("com.whatsapp"))
    val apps = listOf("com.facebook.katana", "com.instagram.android", "org.telegram.messenger", "com.twitter.android", "com.snapchat.android")
    var n = 2
    while (n < 30) {
      for (a in apps + "com.whatsapp") if (n < 30 && ledger.shared(a)) n++
      t += 8 * HOUR_MS
    }
    assertEquals(0, ledger.shares())
    assertTrue(ledger.sharePassReady())
    assertFalse(ledger.shared("com.linkedin.android")) // the reward waits to be collected
    val until = ledger.claimSharePass()
    assertEquals(t + 2 * DAY_MS, until)
    assertEquals("NOT_YET", code { ledger.claimSharePass() })
    // The campaign comes back after 30 days.
    assertTrue(ledger.campaignOpensAt() > t)
    t += 30 * DAY_MS
    assertEquals(0, ledger.campaignOpensAt())
    assertEquals("INVALID_ARGUMENT", code { ledger.shared("../etc") })
  }

  @Test fun aClockSetBackDoesNotReopenAnything() {
    ledger.checkIn()
    ledger.shared("com.whatsapp")
    t -= 3 * DAY_MS
    assertEquals("ALREADY_DONE", code { ledger.checkIn() })
    assertFalse(ledger.shared("com.whatsapp"))
    val s = ledger.state(subscribed = false)
    assertEquals(10, s["points"])
    @Suppress("UNCHECKED_CAST")
    val shares = s["shares"] as Map<String, Any?>
    assertEquals(30, shares["goal"])
    assertTrue((shares["locks"] as Map<*, *>).containsKey("com.whatsapp"))
  }
}
