package com.photoplus.entitlement

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import com.photoplus.BuildConfig
import com.photoplus.edit.Plan
import com.photoplus.rewards.RewardsLedger

/**
 * The plan this device is entitled to, held natively so that what leaves the
 * app (exports, shared and saved photos, camera photos) follows the real plan
 * and not whatever JavaScript says. Until Google Play Billing ships everyone
 * is on the free plan. Testers can simulate Premium (Settings, Testing) in
 * debug builds and in builds made with PREMIUM_SIMULATION (build.gradle; to
 * be turned off for production).
 */
object Entitlements {
  private const val PREFS = "photoplus.entitlements"
  private const val KEY_DEBUG_PLAN = "debugPlan"
  private const val KEY_TRIAL_START = "trialStart"
  /** The free trial's length (the Play offer's, billing/plans.ts TRIAL_DAYS). */
  const val TRIAL_DAYS = 7
  private const val DAY_MS = 86_400_000L
  @Volatile private var prefs: SharedPreferences? = null

  fun install(context: Context) {
    if (prefs == null) prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    RewardsLedger.get(context)
  }

  /** Whether this build lets testers simulate Premium. */
  val simulationAvailable: Boolean
    get() = BuildConfig.DEBUG || BuildConfig.PREMIUM_SIMULATION

  /** Premium is being simulated (testers). */
  val simulating: Boolean
    get() = simulationAvailable && prefs?.getString(KEY_DEBUG_PLAN, null) == "premium"

  internal var clock: () -> Long = {
    if (com.photoplus.integrity.Integrity.installed()) com.photoplus.integrity.Integrity.time.now() else System.currentTimeMillis()
  }

  /** When a trial started at [start] ends (0: none). */
  internal fun trialEnds(start: Long): Long = if (start > 0) start + TRIAL_DAYS * DAY_MS else 0

  /** A trial is on at [now] (it starts once, and ends on its own). */
  internal fun trialOn(start: Long, now: Long): Boolean = start > 0 && now >= start && now < trialEnds(start)

  private fun trialStart(): Long = prefs?.getLong(KEY_TRIAL_START, 0L) ?: 0L

  /**
   * The 7-day free trial, simulated for testing: Premium from now for seven
   * days, once per install, on the trusted clock. In production the trial is
   * Google Play's subscription offer (TRIAL_OFFER_ID): Play Billing starts it,
   * and the purchase is verified here instead.
   */
  val trialActive: Boolean
    get() = simulationAvailable && trialOn(trialStart(), clock())

  /** Starts the simulated trial; false where the build doesn't allow it or it was used already. */
  fun startTrialSimulation(): Boolean {
    if (!simulationAvailable || trialStart() > 0) return false
    prefs?.edit { putLong(KEY_TRIAL_START, clock()) }
    return true
  }

  /** { available (this build can simulate it), active, used, endsAt (ms, 0 none), days }. */
  fun trialState(): Map<String, Any> {
    val start = trialStart()
    return mapOf(
      "available" to simulationAvailable, "active" to trialActive, "used" to (start > 0),
      "endsAt" to trialEnds(start).toDouble(), "days" to TRIAL_DAYS,
    )
  }

  /** A Premium subscription (Play Billing will be verified here), a tester's simulation, or the simulated trial. */
  val subscribed: Boolean
    get() = simulating || trialActive

  /** Premium: a subscription, or Premium time earned with rewards (free plan). */
  val plan: Plan
    get() = if (subscribed || RewardsLedger.installed()?.premiumActive() == true) Plan.PREMIUM else Plan.FREE

  /**
   * Whether a Premium [tool] may be used: on Premium, or unlocked for a day
   * with reward points (ids: RewardsLedger.TOOLS).
   */
  fun allows(tool: String): Boolean = plan == Plan.PREMIUM || RewardsLedger.installed()?.toolActive(tool) == true

  /** Testers: simulate Premium (or stop). Returns false where the build doesn't allow it. */
  fun setDebugPlan(plan: Plan): Boolean {
    if (!simulationAvailable) return false
    prefs?.edit { putString(KEY_DEBUG_PLAN, plan.name.lowercase()) }
    return true
  }
}
