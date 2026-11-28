package com.photoplus.ads

import com.photoplus.R
import com.photoplus.i18n.Words
import com.photoplus.net.Connectivity
import android.app.Activity
import android.os.Handler
import android.os.Looper
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.annotations.ReactModule
import com.google.android.gms.ads.AdError
import com.google.android.gms.ads.AdRequest
import com.google.android.gms.ads.FullScreenContentCallback
import com.google.android.gms.ads.LoadAdError
import com.google.android.gms.ads.MobileAds
import com.google.android.gms.ads.rewarded.RewardedAd
import com.google.android.gms.ads.rewarded.RewardedAdLoadCallback
import com.google.android.ump.ConsentInformation
import com.google.android.ump.ConsentRequestParameters
import com.google.android.ump.UserMessagingPlatform
import com.photoplus.BuildConfig
import com.photoplus.entitlement.Entitlements
import com.photoplus.edit.Plan
import com.photoplus.specs.NativePhotoPlusAdsSpec
import com.photoplus.storage.AppPaths
import com.photoplus.util.toWritableMap
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Rewarded ads for the free plan's watermark-free unlock ([AdRewards]).
 *
 * Consent comes first (Google's User Messaging Platform shows a form only
 * where the law requires one); ads are requested only when consent allows.
 * A reward is counted only from the SDK's reward callback. Debug builds
 * always use Google's test ad unit.
 */
@ReactModule(name = AdsModule.NAME)
class AdsModule(reactContext: ReactApplicationContext) : NativePhotoPlusAdsSpec(reactContext) {
  private val main = Handler(Looper.getMainLooper())
  private val rewards = AdRewards.preferences(reactContext)
  private val ledger = com.photoplus.rewards.RewardsLedger.get(reactContext)
  private val paths = AppPaths(reactContext)
  private val consent: ConsentInformation by lazy { UserMessagingPlatform.getConsentInformation(reactContext) }
  private val started = AtomicBoolean(false)
  private val showing = AtomicBoolean(false)
  private var loaded: RewardedAd? = null

  override fun getName() = NAME

  private val adUnit: String get() = if (BuildConfig.DEBUG) TEST_REWARDED else BuildConfig.ADMOB_REWARDED_ID

  private fun activity(): Activity? = reactApplicationContext.currentActivity

  override fun initialize(promise: Promise) {
    main.post {
      val activity = activity()
      if (activity == null) {
        promise.reject("NO_ACTIVITY", Words.get(reactApplicationContext, R.string.native_photo_plus_needs_to_be_open))
        return@post
      }
      consent.requestConsentInfoUpdate(
        activity,
        ConsentRequestParameters.Builder().build(),
        {
          UserMessagingPlatform.loadAndShowConsentFormIfRequired(activity) { _ ->
            startAds()
            promise.resolve(state())
          }
        },
        { _ ->
          // Consent could not be checked (offline): use what was decided before.
          startAds()
          promise.resolve(state())
        },
      )
    }
  }

  private fun state() = mapOf(
    "canRequestAds" to consent.canRequestAds(),
    "privacyOptionsRequired" to (consent.privacyOptionsRequirementStatus == ConsentInformation.PrivacyOptionsRequirementStatus.REQUIRED),
  ).toWritableMap()

  private fun startAds() {
    if (!consent.canRequestAds() || !started.compareAndSet(false, true)) return
    MobileAds.initialize(reactApplicationContext) { preload() }
  }

  private fun preload() {
    if (loaded != null || !consent.canRequestAds()) return
    RewardedAd.load(reactApplicationContext, adUnit, AdRequest.Builder().build(), object : RewardedAdLoadCallback() {
      override fun onAdLoaded(ad: RewardedAd) {
        loaded = ad
      }

      override fun onAdFailedToLoad(error: LoadAdError) {
        loaded = null
      }
    })
  }

  /** Only the app's own photos can be unlocked. */
  private fun photoFile(path: String): File? = runCatching { paths.resolve(path) }.getOrNull()?.takeIf { it.isFile }

  override fun getRewardProgress(photoPath: String, promise: Promise) {
    val file = photoFile(photoPath)
    if (file == null) {
      promise.reject("INVALID_ARGUMENT", Words.get(reactApplicationContext, R.string.native_unknown_photo))
      return
    }
    val p = rewards.progress(file)
    promise.resolve((p.toMap() + ("premium" to (Entitlements.plan == Plan.PREMIUM))).toWritableMap())
  }

  override fun showRewardedAd(photoPath: String, promise: Promise) {
    val file = photoFile(photoPath)
    if (file == null) {
      promise.reject("INVALID_ARGUMENT", Words.get(reactApplicationContext, R.string.native_unknown_photo))
      return
    }
    main.post {
      val activity = activity()
      when {
        activity == null -> promise.reject("NO_ACTIVITY", Words.get(reactApplicationContext, R.string.native_photo_plus_needs_to_be_open))
        !consent.canRequestAds() -> promise.reject("ADS_UNAVAILABLE", Words.get(reactApplicationContext, R.string.native_ads_can_t_be_shown_right))
        !showing.compareAndSet(false, true) -> promise.reject("BUSY", Words.get(reactApplicationContext, R.string.native_an_ad_is_already_showing))
        else -> {
          val ad = loaded
          if (ad != null) {
            loaded = null
            show(activity, ad, file, promise)
          } else {
            // Not preloaded yet (first use, or the last load failed): load now.
            startAds()
            RewardedAd.load(reactApplicationContext, adUnit, AdRequest.Builder().build(), object : RewardedAdLoadCallback() {
              override fun onAdLoaded(fresh: RewardedAd) = show(activity, fresh, file, promise)
              override fun onAdFailedToLoad(error: LoadAdError) {
                showing.set(false)
                promise.reject("AD_NOT_AVAILABLE", Words.get(reactApplicationContext, R.string.native_no_ad_is_available_right_now))
              }
            })
          }
        }
      }
    }
  }

  private fun show(activity: Activity, ad: RewardedAd, file: File, promise: Promise) =
    showAd(activity, ad, promise, onReward = { rewards.reward(file) }) { earned ->
      (rewards.progress(file).toMap() + ("rewarded" to earned)).toWritableMap()
    }

  /**
   * Rewards (free plan): one rewarded ad for points, counted by the ledger
   * from the SDK's reward callback only, within the day's ads and the gap
   * between them.
   */
  /**
   * A rewarded ad that unlocks one of today's free uses of a tool ("motion":
   * a Photo Motion video, "pencil": a Pencil Art drawing) on the free plan.
   * The use is granted only from the ad network's reward callback.
   * Resolves { rewarded, allowance }.
   */
  override fun showRewardedAdForTool(tool: String, promise: Promise) {
    val allowance = com.photoplus.entitlement.AdAllowance.of(tool)
    if (allowance == null) {
      promise.reject("INVALID_ARGUMENT", "Unknown tool")
      return
    }
    if (com.photoplus.integrity.Integrity.blocked()) {
      promise.reject("BLOCKED", Words.get(reactApplicationContext, R.string.native_photo_plus_needs_to_check_in))
      return
    }
    if (Entitlements.subscribed) {
      promise.reject("SUBSCRIBED", Words.get(reactApplicationContext, R.string.native_you_have_premium_already))
      return
    }
    val owed = allowance.owed(reactApplicationContext)
    // Owed ads are paid off even when today's uses are gone; otherwise a use must be left.
    if (owed == 0 && (allowance.state(reactApplicationContext)["remaining"] as? Int ?: 0) <= 0) {
      promise.reject("DAILY_LIMIT", Words.get(reactApplicationContext, when (tool) {
        "pencil", "arts", "masters" -> R.string.native_arts_daily_used
        "toon" -> R.string.native_toon_daily_used
        else -> R.string.native_motion_daily_used
      }))
      return
    }
    // Offline no ad can load: the use is unlocked now and its ad owed (up to MAX_OWED, then the tool freezes).
    if (!Connectivity.online(reactApplicationContext)) {
      offlineUnlock(allowance, promise)
      return
    }
    main.post {
      val activity = activity()
      when {
        activity == null -> promise.reject("NO_ACTIVITY", Words.get(reactApplicationContext, R.string.native_photo_plus_needs_to_be_open))
        !consent.canRequestAds() -> promise.reject("ADS_UNAVAILABLE", Words.get(reactApplicationContext, R.string.native_ads_can_t_be_shown_right))
        !showing.compareAndSet(false, true) -> promise.reject("BUSY", Words.get(reactApplicationContext, R.string.native_an_ad_is_already_showing))
        else -> {
          var earned = false
          val present = { ad: RewardedAd ->
            showAd(activity, ad, promise, onReward = {
              earned = true
              allowance.grantAd(reactApplicationContext)
            }) { _ ->
              mapOf("rewarded" to earned, "allowance" to allowance.state(reactApplicationContext)).toWritableMap()
            }
          }
          val ad = loaded
          if (ad != null) {
            loaded = null
            present(ad)
          } else {
            startAds()
            RewardedAd.load(reactApplicationContext, adUnit, AdRequest.Builder().build(), object : RewardedAdLoadCallback() {
              override fun onAdLoaded(fresh: RewardedAd) = present(fresh)
              override fun onAdFailedToLoad(error: LoadAdError) {
                showing.set(false)
                // The connection dropped on the way: the same as offline.
                if (error.code == AdRequest.ERROR_CODE_NETWORK_ERROR && !Connectivity.online(reactApplicationContext)) {
                  offlineUnlock(allowance, promise)
                  return
                }
                promise.reject("AD_NOT_AVAILABLE", Words.get(reactApplicationContext, R.string.native_no_ad_is_available_right_now))
              }
            })
          }
        }
      }
    }
  }

  /**
   * Offline: unlocks the use and owes its ad, resolving { rewarded: true, offline: true, allowance };
   * with every allowed ad already owed, rejects OWED_ADS (go online and watch them).
   */
  private fun offlineUnlock(allowance: com.photoplus.entitlement.AdAllowance, promise: Promise) {
    val owed = allowance.owed(reactApplicationContext)
    if (allowance.grantOffline(reactApplicationContext)) {
      promise.resolve(mapOf("rewarded" to true, "offline" to true, "allowance" to allowance.state(reactApplicationContext)).toWritableMap())
    } else {
      promise.reject("OWED_ADS", Words.get(reactApplicationContext, R.string.native_ads_owed_frozen, "n" to owed))
    }
  }

  override fun showRewardedAdForPoints(promise: Promise) {
    if (com.photoplus.integrity.Integrity.blocked()) {
      promise.reject("BLOCKED", Words.get(reactApplicationContext, R.string.native_photo_plus_needs_to_check_in))
      return
    }
    if (Entitlements.subscribed) {
      promise.reject("SUBSCRIBED", Words.get(reactApplicationContext, R.string.native_you_have_premium_already))
      return
    }
    val wait = ledger.adWait()
    if (wait != 0L) {
      promise.reject(if (wait < 0) "DAILY_LIMIT" else "TOO_SOON", if (wait < 0) "You've watched today's ads. More tomorrow." else "The next ad is ready in $wait s.")
      return
    }
    main.post {
      val activity = activity()
      when {
        activity == null -> promise.reject("NO_ACTIVITY", Words.get(reactApplicationContext, R.string.native_photo_plus_needs_to_be_open))
        !consent.canRequestAds() -> promise.reject("ADS_UNAVAILABLE", Words.get(reactApplicationContext, R.string.native_ads_can_t_be_shown_right))
        !showing.compareAndSet(false, true) -> promise.reject("BUSY", Words.get(reactApplicationContext, R.string.native_an_ad_is_already_showing))
        else -> {
          var earned = 0
          val present = { ad: RewardedAd ->
            showAd(activity, ad, promise, onReward = { earned = ledger.adWatched() }) { _ ->
              mapOf("rewarded" to (earned > 0), "points" to earned, "state" to ledger.state(false)).toWritableMap()
            }
          }
          val ad = loaded
          if (ad != null) {
            loaded = null
            present(ad)
          } else {
            startAds()
            RewardedAd.load(reactApplicationContext, adUnit, AdRequest.Builder().build(), object : RewardedAdLoadCallback() {
              override fun onAdLoaded(fresh: RewardedAd) = present(fresh)
              override fun onAdFailedToLoad(error: LoadAdError) {
                showing.set(false)
                promise.reject("AD_NOT_AVAILABLE", Words.get(reactApplicationContext, R.string.native_no_ad_is_available_right_now))
              }
            })
          }
        }
      }
    }
  }

  private fun showAd(activity: Activity, ad: RewardedAd, promise: Promise, onReward: () -> Unit, result: (Boolean) -> Any) {
    var earned = false
    ad.fullScreenContentCallback = object : FullScreenContentCallback() {
      override fun onAdDismissedFullScreenContent() {
        showing.set(false)
        promise.resolve(result(earned))
        preload()
      }

      override fun onAdFailedToShowFullScreenContent(error: AdError) {
        showing.set(false)
        promise.reject("AD_NOT_AVAILABLE", Words.get(reactApplicationContext, R.string.native_the_ad_couldn_t_be_shown))
        preload()
      }
    }
    ad.show(activity) { _ ->
      earned = true
      onReward()
    }
  }

  override fun privacyOptionsRequired(promise: Promise) {
    promise.resolve(consent.privacyOptionsRequirementStatus == ConsentInformation.PrivacyOptionsRequirementStatus.REQUIRED)
  }

  override fun showPrivacyOptions(promise: Promise) {
    main.post {
      val activity = activity()
      if (activity == null) {
        promise.resolve(false)
        return@post
      }
      UserMessagingPlatform.showPrivacyOptionsForm(activity) { error -> promise.resolve(error == null) }
    }
  }

  companion object {
    const val NAME = "PhotoPlusAds"
    /** Google's public test rewarded ad unit. */
    const val TEST_REWARDED = "ca-app-pub-3940256099942544/5224354917"
  }
}
