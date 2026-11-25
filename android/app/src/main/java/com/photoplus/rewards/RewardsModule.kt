package com.photoplus.rewards

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.ClipData
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableArray
import com.facebook.react.module.annotations.ReactModule
import com.facebook.react.modules.core.DeviceEventManagerModule
import com.photoplus.entitlement.Entitlements
import com.photoplus.specs.NativePhotoPlusRewardsSpec
import com.photoplus.util.toWritableMap
import java.io.File

/**
 * Rewards for the free plan (see [RewardsLedger]): the state, check-ins,
 * tasks, redeeming points, and the share campaign. A share opens the chosen
 * app with the campaign's picture and words; it counts for the app that was
 * picked, once per app every eight hours. Ad points are added by
 * [com.photoplus.ads.AdsModule] from the ad SDK's reward callback.
 */
@ReactModule(name = RewardsModule.NAME)
class RewardsModule(reactContext: ReactApplicationContext) : NativePhotoPlusRewardsSpec(reactContext) {
  private val ledger = RewardsLedger.get(reactContext)
  private var chooserReceiver: BroadcastReceiver? = null

  override fun getName() = NAME

  override fun invalidate() {
    chooserReceiver?.let { runCatching { reactApplicationContext.unregisterReceiver(it) } }
    chooserReceiver = null
    super.invalidate()
  }

  private fun state() = ledger.state(Entitlements.subscribed).toWritableMap()

  /** Rewards are for the free plan: a subscriber has Premium already. */
  private inline fun freeOnly(promise: Promise, run: () -> Unit) {
    if (Entitlements.subscribed) {
      promise.reject("SUBSCRIBED", "You have Premium: every tool is already yours.")
      return
    }
    try {
      run()
    } catch (e: RewardsLedger.RewardsException) {
      promise.reject(e.code, e.message, e)
    }
  }

  override fun getState(promise: Promise) = promise.resolve(state())

  override fun checkIn(promise: Promise) = freeOnly(promise) {
    val pts = ledger.checkIn()
    promise.resolve(Arguments.createMap().apply { putInt("points", pts); putMap("state", state()) })
  }

  override fun recordActivity(event: String) {
    runCatching { ledger.record(event) }
  }

  override fun claimTask(id: String, promise: Promise) = freeOnly(promise) {
    val pts = ledger.claim(id)
    promise.resolve(Arguments.createMap().apply { putInt("points", pts); putMap("state", state()) })
  }

  override fun redeem(item: String, tool: String, promise: Promise) = freeOnly(promise) {
    val until = ledger.redeem(item, tool.takeIf { it.isNotEmpty() })
    promise.resolve(Arguments.createMap().apply { putDouble("until", until.toDouble()); putMap("state", state()) })
  }

  override fun claimSharePass(promise: Promise) = freeOnly(promise) {
    val until = ledger.claimSharePass()
    promise.resolve(Arguments.createMap().apply { putDouble("until", until.toDouble()); putMap("state", state()) })
  }

  /** Which of [packages] are installed and can take a picture. */
  override fun installedApps(packages: ReadableArray, promise: Promise) {
    val wanted = (0 until packages.size()).mapNotNull { packages.getString(it) }.toSet()
    val pm = reactApplicationContext.packageManager
    val send = Intent(Intent.ACTION_SEND).setType("image/*")
    val found = pm.queryIntentActivities(send, 0).map { it.activityInfo.packageName }.filter { it in wanted }.distinct()
    promise.resolve(Arguments.fromList(found))
  }

  /**
   * Opens [target] (a package; empty for the system's own share sheet) with
   * the campaign picture [asset] (assets/promo) and [text]. Resolves with
   * { opened, counted, target, state }; with the share sheet the count comes
   * when the person picks an app (the "rewardsChanged" event).
   */
  override fun share(target: String, asset: String, text: String, promise: Promise) = freeOnly(promise) {
    val activity = reactApplicationContext.currentActivity
    if (activity == null) {
      promise.reject("NO_ACTIVITY", "Photo Plus needs to be open")
      return@freeOnly
    }
    if (!ASSET.matches(asset)) {
      promise.reject("INVALID_ARGUMENT", "Unknown picture")
      return@freeOnly
    }
    if (target.isNotEmpty() && ledger.shareOpensAt(target) != 0L) {
      promise.reject("LOCKED", "You shared to this app recently. It opens again in a few hours.")
      return@freeOnly
    }
    val file = promoFile(asset)
    val uri = FileProvider.getUriForFile(reactApplicationContext, "${reactApplicationContext.packageName}.files", file)
    val send = Intent(Intent.ACTION_SEND).apply {
      type = "image/jpeg"
      putExtra(Intent.EXTRA_STREAM, uri)
      putExtra(Intent.EXTRA_TEXT, text.take(2000))
      addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
      clipData = ClipData.newUri(reactApplicationContext.contentResolver, "Photo Plus", uri)
    }
    if (target.isNotEmpty()) {
      send.setPackage(target)
      try {
        activity.startActivity(send)
      } catch (_: android.content.ActivityNotFoundException) {
        promise.reject("APP_NOT_FOUND", "That app isn't on this phone")
        return@freeOnly
      }
      val counted = ledger.shared(target)
      promise.resolve(Arguments.createMap().apply {
        putBoolean("opened", true)
        putBoolean("counted", counted)
        putString("target", target)
        putMap("state", state())
      })
    } else {
      listenForChoice()
      val chosen = Intent(ACTION_CHOSEN).setPackage(reactApplicationContext.packageName)
      val flags = PendingIntent.FLAG_UPDATE_CURRENT or (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0)
      val pending = PendingIntent.getBroadcast(reactApplicationContext, 7, chosen, flags)
      activity.startActivity(Intent.createChooser(send, "Share Photo Plus", pending.intentSender))
      promise.resolve(Arguments.createMap().apply {
        putBoolean("opened", true)
        putBoolean("counted", false)
        putString("target", "")
        putMap("state", state())
      })
    }
  }

  /** The campaign pictures are copied out of the app once (shared from the app's files). */
  private fun promoFile(asset: String): File {
    val dir = File(reactApplicationContext.filesDir, "promo").apply { mkdirs() }
    val out = File(dir, asset)
    if (!out.isFile || out.length() == 0L) {
      reactApplicationContext.assets.open("promo/$asset").use { input -> out.outputStream().use { input.copyTo(it) } }
    }
    return out
  }

  private fun listenForChoice() {
    if (chooserReceiver != null) return
    val receiver = object : BroadcastReceiver() {
      override fun onReceive(context: Context, intent: Intent) {
        @Suppress("DEPRECATION")
        val component = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
          intent.getParcelableExtra(Intent.EXTRA_CHOSEN_COMPONENT, ComponentName::class.java)
        } else {
          intent.getParcelableExtra(Intent.EXTRA_CHOSEN_COMPONENT)
        }
        val pkg = component?.packageName ?: return
        val counted = runCatching { ledger.shared(pkg) }.getOrDefault(false)
        reactApplicationContext
          .getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter::class.java)
          .emit(EVENT, Arguments.createMap().apply {
            putString("target", pkg)
            putBoolean("counted", counted)
            putMap("state", state())
          })
      }
    }
    ContextCompat.registerReceiver(reactApplicationContext, receiver, IntentFilter(ACTION_CHOSEN), ContextCompat.RECEIVER_NOT_EXPORTED)
    chooserReceiver = receiver
  }

  override fun addListener(eventName: String) = Unit

  override fun removeListeners(count: Double) = Unit

  companion object {
    const val NAME = "PhotoPlusRewards"
    const val EVENT = "PhotoPlusRewardsChanged"
    private const val ACTION_CHOSEN = "com.photoplus.rewards.SHARE_CHOSEN"
    private val ASSET = Regex("^[a-z0-9-]{1,40}\\.jpg$")
  }
}
