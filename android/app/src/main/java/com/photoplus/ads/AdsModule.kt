package com.photoplus.ads

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
        promise.reject("NO_ACTIVITY", "Photo Plus needs to be open")
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
      promise.reject("INVALID_ARGUMENT", "Unknown photo")
      return
    }
    val p = rewards.progress(file)
    promise.resolve((p.toMap() + ("premium" to (Entitlements.plan == Plan.PREMIUM))).toWritableMap())
  }

  override fun showRewardedAd(photoPath: String, promise: Promise) {
    val file = photoFile(photoPath)
    if (file == null) {
      promise.reject("INVALID_ARGUMENT", "Unknown photo")
      return
    }
    main.post {
      val activity = activity()
      when {
        activity == null -> promise.reject("NO_ACTIVITY", "Photo Plus needs to be open")
        !consent.canRequestAds() -> promise.reject("ADS_UNAVAILABLE", "Ads can't be shown right now")
        !showing.compareAndSet(false, true) -> promise.reject("BUSY", "An ad is already showing")
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
                promise.reject("AD_NOT_AVAILABLE", "No ad is available right now. Please try again in a moment.")
              }
            })
          }
        }
      }
    }
  }

  private fun show(activity: Activity, ad: RewardedAd, file: File, promise: Promise) {
    var earned = false
    ad.fullScreenContentCallback = object : FullScreenContentCallback() {
      override fun onAdDismissedFullScreenContent() {
        showing.set(false)
        val p = rewards.progress(file)
        promise.resolve((p.toMap() + ("rewarded" to earned)).toWritableMap())
        preload()
      }

      override fun onAdFailedToShowFullScreenContent(error: AdError) {
        showing.set(false)
        promise.reject("AD_NOT_AVAILABLE", "The ad couldn't be shown. Please try again.")
        preload()
      }
    }
    ad.show(activity) { _ ->
      earned = true
      rewards.reward(file)
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
