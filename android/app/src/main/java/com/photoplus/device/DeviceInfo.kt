package com.photoplus.device

import android.app.ActivityManager
import android.content.Context

object DeviceInfo {
  fun activityManager(context: Context) =
    context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager

  fun memoryInfo(context: Context): ActivityManager.MemoryInfo =
    ActivityManager.MemoryInfo().also { activityManager(context).getMemoryInfo(it) }

  fun totalRamBytes(context: Context): Long = memoryInfo(context).totalMem

  fun isLowRamDevice(context: Context): Boolean = activityManager(context).isLowRamDevice
}
