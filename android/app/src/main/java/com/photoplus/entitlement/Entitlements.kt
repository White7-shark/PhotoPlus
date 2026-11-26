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

  /** A Premium subscription (Play Billing will be verified here), or a tester's simulation. */
  val subscribed: Boolean
    get() = simulating

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
