package com.photoplus.integrity

import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.WritableMap
import com.facebook.react.module.annotations.ReactModule
import com.facebook.react.modules.core.DeviceEventManagerModule
import com.google.android.play.core.appupdate.AppUpdateManagerFactory
import com.google.android.play.core.appupdate.AppUpdateOptions
import com.google.android.play.core.install.model.AppUpdateType
import com.google.android.play.core.install.model.UpdateAvailability
import com.photoplus.specs.NativePhotoPlusIntegritySpec
import com.photoplus.util.toWritableMap
import java.util.concurrent.Executors

/**
 * The app's integrity for the screens ([Integrity]): the status (blocked and
 * why, the update banner), an online check now, Later for the banner, the
 * update itself (Google Play's in-app update, else the Play listing) and the
 * phone's date & time settings.
 */
@ReactModule(name = IntegrityModule.NAME)
class IntegrityModule(reactContext: ReactApplicationContext) : NativePhotoPlusIntegritySpec(reactContext) {
  private val worker = Executors.newSingleThreadExecutor()
  private val listener: (Integrity.Status) -> Unit = { s -> emit(map(s)) }

  init {
    Integrity.install(reactContext)
    Integrity.addListener(listener)
  }

  override fun getName() = NAME

  override fun invalidate() {
    Integrity.removeListener(listener)
    worker.shutdown()
    super.invalidate()
  }

  private fun map(s: Integrity.Status): WritableMap = mapOf(
    "blocked" to (s.reason != null),
    "reason" to s.reason?.id,
    "skewMinutes" to s.skewMs?.let { it / 60_000.0 },
    "offlineDays" to s.offlineMs / 86_400_000.0,
    "maxOfflineDays" to (Integrity.MAX_OFFLINE_MS / 86_400_000L).toInt(),
    "lastOnline" to s.lastOnline.toDouble(),
    "now" to s.now.toDouble(),
    "online" to s.lastCheckOk,
    "update" to mapOf(
      "pending" to s.update.pending,
      "required" to s.update.required,
      "banner" to s.update.banner,
      "deadline" to s.update.deadline.toDouble(),
    ),
  ).toWritableMap()

  private fun emit(state: WritableMap) {
    if (!reactApplicationContext.hasActiveReactInstance()) return
    reactApplicationContext.getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter::class.java).emit(EVENT, state)
  }

  override fun getState(promise: Promise) {
    worker.execute { promise.resolve(map(Integrity.status())) }
  }

  override fun check(force: Boolean, promise: Promise) {
    worker.execute {
      try {
        promise.resolve(map(Integrity.check(force)))
      } catch (e: Exception) {
        promise.reject("CHECK_FAILED", e.message ?: "The check couldn't run", e)
      }
    }
  }

  override fun later(promise: Promise) {
    worker.execute {
      Integrity.later()
      promise.resolve(map(Integrity.status()))
    }
  }

  override fun startUpdate(promise: Promise) {
    val activity = reactApplicationContext.currentActivity
    if (activity == null) {
      promise.reject("NO_ACTIVITY", "Photo Plus needs to be open")
      return
    }
    val manager = AppUpdateManagerFactory.create(reactApplicationContext)
    manager.appUpdateInfo
      .addOnSuccessListener { info ->
        val ready = info.updateAvailability() == UpdateAvailability.UPDATE_AVAILABLE ||
          info.updateAvailability() == UpdateAvailability.DEVELOPER_TRIGGERED_UPDATE_IN_PROGRESS
        if (ready && info.isUpdateTypeAllowed(AppUpdateType.IMMEDIATE)) {
          manager.startUpdateFlow(info, activity, AppUpdateOptions.newBuilder(AppUpdateType.IMMEDIATE).build())
          promise.resolve("play")
        } else {
          promise.resolve(openStore())
        }
      }
      .addOnFailureListener { promise.resolve(openStore()) }
  }

  /** The Play listing (the Play app, else the web). */
  private fun openStore(): String {
    val pkg = reactApplicationContext.packageName
    val activity = reactApplicationContext.currentActivity ?: return "none"
    return try {
      activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("market://details?id=$pkg")))
      "store"
    } catch (_: ActivityNotFoundException) {
      activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://play.google.com/store/apps/details?id=$pkg")))
      "web"
    }
  }

  override fun openDateSettings() {
    val activity = reactApplicationContext.currentActivity ?: return
    runCatching { activity.startActivity(Intent(Settings.ACTION_DATE_SETTINGS)) }
      .onFailure { runCatching { activity.startActivity(Intent(Settings.ACTION_SETTINGS)) } }
  }

  override fun addListener(eventName: String) = Unit

  override fun removeListeners(count: Double) = Unit

  companion object {
    const val NAME = "PhotoPlusIntegrity"
    const val EVENT = "PhotoPlusIntegrityChanged"
  }
}
