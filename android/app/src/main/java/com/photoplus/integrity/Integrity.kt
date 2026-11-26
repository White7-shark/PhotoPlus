package com.photoplus.integrity

import com.photoplus.R
import com.photoplus.i18n.Words
import android.Manifest
import android.annotation.SuppressLint
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.os.Build
import android.os.SystemClock
import android.provider.Settings
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import com.google.android.gms.tasks.Tasks
import com.google.android.play.core.appupdate.AppUpdateManagerFactory
import com.google.android.play.core.install.model.UpdateAvailability
import java.text.DateFormat
import java.util.Date
import java.util.concurrent.TimeUnit
import kotlin.math.abs

/**
 * The app's integrity (like a messaging app's): the phone's time must be
 * right, the app must go online at least once a week, and a new version
 * must be installed within 7 days. Any of these failing blocks the app
 * until it is put right — checked again online ([check]).
 *
 * Times come from [TrustedTime]. Rewards (points, passes, share locks) use
 * the same trusted time, and refuse to work while the app is blocked.
 */
object Integrity {
  enum class Reason(val id: String) { TIME("time"), OFFLINE("offline"), UPDATE("update") }

  data class Status(
    val reason: Reason?,
    val skewMs: Long?,
    val offlineMs: Long,
    val lastOnline: Long,
    val now: Long,
    val update: UpdatePolicy.State,
    val lastCheckOk: Boolean,
  )

  const val MAX_OFFLINE_MS = 7 * UpdatePolicy.DAY_MS
  private const val TAG = "Integrity"
  private const val CHANNEL = "updates"
  private const val MIN_CHECK_GAP_MS = 10 * 60_000L

  private lateinit var prefs: SharedPreferences
  private lateinit var app: Context
  @Volatile private var lastNetworkCheck = 0L
  @Volatile private var lastCheckOk = true
  private val listeners = java.util.concurrent.CopyOnWriteArraySet<(Status) -> Unit>()

  private val store = object : TrustedTime.Store {
    override fun getLong(key: String, default: Long) = prefs.getLong(key, default)
    override fun putLong(key: String, value: Long) = prefs.edit { putLong(key, value) }
  }

  lateinit var time: TrustedTime
    private set
  private lateinit var updates: UpdatePolicy

  fun install(context: Context) {
    if (::prefs.isInitialized) return
    app = context.applicationContext
    prefs = app.getSharedPreferences("pp_integrity", Context.MODE_PRIVATE)
    time = TrustedTime(store, elapsed = SystemClock::elapsedRealtime, boot = { bootCount(app) })
    updates = UpdatePolicy(store)
    time.firstRun()
  }

  fun installed(): Boolean = ::prefs.isInitialized

  private fun bootCount(context: Context): Long =
    runCatching { Settings.Global.getInt(context.contentResolver, Settings.Global.BOOT_COUNT).toLong() }.getOrDefault(-1L)

  private fun installedVersion(): Long = runCatching {
    app.packageManager.getPackageInfo(app.packageName, 0).longVersionCode
  }.getOrDefault(0L)

  fun status(): Status {
    val skew = time.skewMs()
    val offline = time.offlineMs()
    val now = time.now()
    val update = updates.state(now, installedVersion())
    val reason = when {
      skew != null && abs(skew) > TrustedTime.TOLERANCE_MS -> Reason.TIME
      offline > MAX_OFFLINE_MS -> Reason.OFFLINE
      update.required -> Reason.UPDATE
      else -> null
    }
    return Status(reason, skew, offline, time.lastOnline(), now, update, lastCheckOk)
  }

  fun blocked(): Boolean = installed() && status().reason != null

  /**
   * Online check (off the main thread): the true time, then Google Play for
   * a new version. [force] skips the 10-minute gap between checks. Returns
   * the new status; offline, the status stands as it was.
   */
  fun check(force: Boolean): Status {
    val since = SystemClock.elapsedRealtime() - lastNetworkCheck
    if (!force && lastNetworkCheck != 0L && since < MIN_CHECK_GAP_MS) return status()
    lastNetworkCheck = SystemClock.elapsedRealtime()
    val reading = TimeSource.fetch()
    lastCheckOk = reading != null
    if (reading != null) {
      val (server, at) = reading
      time.synced(server + (SystemClock.elapsedRealtime() - at))
      checkUpdate()
    }
    return status().also { s -> listeners.forEach { runCatching { it(s) } } }
  }

  private fun checkUpdate() {
    runCatching {
      val info = Tasks.await(AppUpdateManagerFactory.create(app).appUpdateInfo, 20, TimeUnit.SECONDS)
      val available = info.availableVersionCode().toLong().takeIf {
        info.updateAvailability() == UpdateAvailability.UPDATE_AVAILABLE ||
          info.updateAvailability() == UpdateAvailability.DEVELOPER_TRIGGERED_UPDATE_IN_PROGRESS
      }
      updates.observed(available, installedVersion(), time.now())
    }.onFailure {
      // Not installed from Google Play (a test build), or Play is unreachable.
      Log.i(TAG, "update check unavailable: ${it.message}")
      updates.observed(null, installedVersion(), time.now())
    }
  }

  fun later() = updates.later(time.now(), installedVersion())

  fun addListener(l: (Status) -> Unit) = listeners.add(l)

  fun removeListener(l: (Status) -> Unit) = listeners.remove(l)

  /** Every 2 days while an update waits: a notification (and the banner comes back). */
  fun remindIfDue() {
    val s = status()
    if (!s.update.reminderDue) return
    updates.reminded(s.now)
    notifyUpdate(s.update.deadline)
  }

  @SuppressLint("MissingPermission")
  private fun notifyUpdate(deadline: Long) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
      ContextCompat.checkSelfPermission(app, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
    ) {
      return
    }
    val nm = app.getSystemService(NotificationManager::class.java)
    nm.createNotificationChannel(NotificationChannel(CHANNEL, Words.get(app, R.string.native_channel_updates), NotificationManager.IMPORTANCE_DEFAULT).apply {
      description = Words.get(app, R.string.native_channel_updates_about)
    })
    val open = app.packageManager.getLaunchIntentForPackage(app.packageName)
    val tap = PendingIntent.getActivity(app, 11, open, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
    val day = DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(deadline))
    val n = NotificationCompat.Builder(app, CHANNEL)
      .setSmallIcon(app.applicationInfo.icon)
      .setContentTitle(Words.get(app, R.string.native_update_title))
      .setContentText(Words.get(app, R.string.native_update_text, "day" to day))
      .setStyle(NotificationCompat.BigTextStyle().bigText(Words.get(app, R.string.native_update_long, "day" to day)))
      .setContentIntent(tap)
      .setAutoCancel(true)
      .build()
    NotificationManagerCompat.from(app).notify(4201, n)
  }
}
