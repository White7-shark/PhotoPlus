package com.photoplus.entitlement

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import com.photoplus.BuildConfig
import com.photoplus.edit.Plan

/**
 * The plan this device is entitled to, held natively so that what leaves the
 * app (exports, shared and saved photos, camera photos) follows the real plan
 * and not whatever JavaScript says. Until Google Play Billing ships everyone
 * is on the free plan; debug builds can switch plans to test Premium.
 */
object Entitlements {
  private const val PREFS = "photoplus.entitlements"
  private const val KEY_DEBUG_PLAN = "debugPlan"
  @Volatile private var prefs: SharedPreferences? = null

  fun install(context: Context) {
    if (prefs == null) prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
  }

  val plan: Plan
    get() {
      if (BuildConfig.DEBUG && prefs?.getString(KEY_DEBUG_PLAN, null) == "premium") return Plan.PREMIUM
      // Purchases (Play Billing) will be verified here.
      return Plan.FREE
    }

  /** Debug builds only: lets testers try Premium. Returns false in release builds. */
  fun setDebugPlan(plan: Plan): Boolean {
    if (!BuildConfig.DEBUG) return false
    prefs?.edit { putString(KEY_DEBUG_PLAN, plan.name.lowercase()) }
    return true
  }
}
