package com.photoplus.rewards

import com.photoplus.R
import com.photoplus.i18n.Words
import android.content.ClipData
import android.content.ComponentName
import android.content.Intent
import androidx.core.content.FileProvider
import androidx.core.graphics.drawable.toBitmap
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.entitlement.Entitlements
import com.photoplus.specs.NativePhotoPlusRewardsSpec
import com.photoplus.util.toWritableMap
import java.io.File

/**
 * Rewards for the free plan (see [RewardsLedger]): the state, check-ins,
 * tasks, redeeming points, and the share campaign. The app's own share sheet
 * lists every app that takes a picture; a share opens that app's share
 * screen directly with the campaign's picture and words, so it counts for
 * exactly that app, once every eight hours. Nothing works while the app is
 * blocked (wrong time, a week offline, an overdue update). Ad points are added by
 * [com.photoplus.ads.AdsModule] from the ad SDK's reward callback.
 */
@ReactModule(name = RewardsModule.NAME)
class RewardsModule(reactContext: ReactApplicationContext) : NativePhotoPlusRewardsSpec(reactContext) {
  private val ledger = RewardsLedger.get(reactContext)

  override fun getName() = NAME

  private fun state() = ledger.state(Entitlements.subscribed).toWritableMap()

  /** Rewards are for the free plan: a subscriber has Premium already. */
  private inline fun freeOnly(promise: Promise, run: () -> Unit) {
    if (com.photoplus.integrity.Integrity.blocked()) {
      promise.reject("BLOCKED", Words.get(reactApplicationContext, R.string.native_photo_plus_needs_to_check_in))
      return
    }
    if (Entitlements.subscribed) {
      promise.reject("SUBSCRIBED", Words.get(reactApplicationContext, R.string.native_you_have_premium_every_tool_is))
      return
    }
    try {
      run()
    } catch (e: RewardsLedger.RewardsException) {
      val message = if (e.word != 0) Words.get(reactApplicationContext, e.word, *e.args) else e.message
      promise.reject(e.code, message, e)
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

  /**
   * Every app on the phone that takes a picture (the in-app share sheet):
   * { pkg, activity, label, icon (a PNG path) }, one entry per share screen
   * (an app can offer several, such as a chat and a status).
   */
  override fun shareTargets(promise: Promise) {
    Thread {
      try {
        val pm = reactApplicationContext.packageManager
        val dir = File(reactApplicationContext.cacheDir, "share-icons").apply { mkdirs() }
        val send = Intent(Intent.ACTION_SEND).setType("image/*")
        val list = Arguments.createArray()
        pm.queryIntentActivities(send, 0)
          .filter { it.activityInfo.packageName != reactApplicationContext.packageName && it.activityInfo.exported }
          .forEach { ri ->
            val info = ri.activityInfo
            val icon = File(dir, "${info.packageName}_${info.name.hashCode().toUInt()}.png")
            if (!icon.isFile) {
              runCatching {
                val d = ri.loadIcon(pm)
                val bmp = d.toBitmap(ICON_PX, ICON_PX)
                icon.outputStream().use { bmp.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
              }
            }
            list.pushMap(Arguments.createMap().apply {
              putString("pkg", info.packageName)
              putString("activity", info.name)
              putString("label", ri.loadLabel(pm).toString().take(40))
              putString("app", info.applicationInfo.loadLabel(pm).toString().take(40))
              putString("icon", if (icon.isFile) icon.absolutePath else "")
            })
          }
        promise.resolve(list)
      } catch (e: Exception) {
        promise.reject("SHARE_TARGETS", e.message ?: "Apps couldn't be listed", e)
      }
    }.start()
  }

  /**
   * Opens one app's share screen ([target] package, [activity] its share
   * screen) with the campaign picture [asset] (assets/promo, in a language's folder) and [text]. The
   * share counts for that app, once every eight hours.
   */
  override fun share(target: String, activity: String, asset: String, text: String, promise: Promise) = freeOnly(promise) {
    val host = reactApplicationContext.currentActivity
    if (host == null) {
      promise.reject("NO_ACTIVITY", Words.get(reactApplicationContext, R.string.native_photo_plus_needs_to_be_open))
      return@freeOnly
    }
    if (!ASSET.matches(asset) || !NAME_RE.matches(target) || (activity.isNotEmpty() && !NAME_RE.matches(activity))) {
      promise.reject("INVALID_ARGUMENT", Words.get(reactApplicationContext, R.string.native_unknown_app_or_picture))
      return@freeOnly
    }
    if (ledger.shareOpensAt(target) != 0L) {
      promise.reject("LOCKED", Words.get(reactApplicationContext, R.string.native_you_shared_to_this_app_recently))
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
      if (activity.isNotEmpty()) component = ComponentName(target, activity) else setPackage(target)
    }
    // The words also go to the clipboard: an app that keeps only the
    // picture (some feeds do) can have them pasted, link and hashtags too.
    runCatching {
      val clipboard = reactApplicationContext.getSystemService(android.content.ClipboardManager::class.java)
      clipboard?.setPrimaryClip(android.content.ClipData.newPlainText("Photo Plus", text.take(2000)))
    }
    try {
      host.startActivity(send)
    } catch (_: android.content.ActivityNotFoundException) {
      promise.reject("APP_NOT_FOUND", Words.get(reactApplicationContext, R.string.native_that_app_isn_t_on_this))
      return@freeOnly
    } catch (_: SecurityException) {
      promise.reject("APP_NOT_FOUND", Words.get(reactApplicationContext, R.string.native_that_app_can_t_take_shares))
      return@freeOnly
    }
    val counted = ledger.shared(target)
    promise.resolve(Arguments.createMap().apply {
      putBoolean("opened", true)
      putBoolean("counted", counted)
      putString("target", target)
      putMap("state", state())
    })
  }

  /** The campaign pictures are copied out of the app once (shared from the app's files). */
  private fun promoFile(asset: String): File {
    val dir = File(reactApplicationContext.filesDir, "promo").apply { mkdirs() }
    val out = File(dir, asset)
    out.parentFile?.mkdirs()
    if (!out.isFile || out.length() == 0L) {
      reactApplicationContext.assets.open("promo/$asset").use { input -> out.outputStream().use { input.copyTo(it) } }
    }
    return out
  }

  override fun addListener(eventName: String) = Unit

  override fun removeListeners(count: Double) = Unit

  companion object {
    const val NAME = "PhotoPlusRewards"
    private const val ICON_PX = 96
    private val NAME_RE = Regex("^[A-Za-z0-9_.$]{1,200}$")
    /** A campaign picture: assets/promo/<name>.jpg, or assets/promo/<language>/<name>.jpg. */
    private val ASSET = Regex("^(?:[a-z]{2}(?:-[a-z]{2})?/)?[a-z0-9-]{1,40}\\.jpg$")
  }
}
