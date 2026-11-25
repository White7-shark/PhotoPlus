package com.photoplus.integrity

import android.content.Context
import androidx.work.Constraints
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.NetworkType
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.Worker
import androidx.work.WorkerParameters
import java.util.concurrent.TimeUnit

/**
 * The regular online check, in the background whenever there is a
 * connection: the true time (which also counts as the weekly online visit)
 * and Google Play for a new version, with the 2-day update reminder.
 */
class IntegrityWorker(context: Context, params: WorkerParameters) : Worker(context, params) {
  override fun doWork(): Result {
    Integrity.install(applicationContext)
    Integrity.check(force = true)
    Integrity.remindIfDue()
    return Result.success()
  }

  companion object {
    private const val NAME = "photoplus-integrity"

    fun schedule(context: Context) {
      val request = PeriodicWorkRequestBuilder<IntegrityWorker>(6, TimeUnit.HOURS, 1, TimeUnit.HOURS)
        .setConstraints(Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build())
        .build()
      WorkManager.getInstance(context).enqueueUniquePeriodicWork(NAME, ExistingPeriodicWorkPolicy.KEEP, request)
    }
  }
}
