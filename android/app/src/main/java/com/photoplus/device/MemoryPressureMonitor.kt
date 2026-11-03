package com.photoplus.device

import android.content.ComponentCallbacks2
import android.content.Context
import android.content.res.Configuration
import java.util.concurrent.CopyOnWriteArraySet

/**
 * Translates Android trim-memory callbacks into three levels and fans them
 * out to native subsystems (engine budget) and JavaScript (UI can drop
 * caches/previews). Installed once from MainApplication.
 */
object MemoryPressureMonitor : ComponentCallbacks2 {
  enum class Level { NORMAL, MODERATE, CRITICAL }

  fun interface Listener {
    fun onMemoryPressure(level: Level)
  }

  private val listeners = CopyOnWriteArraySet<Listener>()
  @Volatile private var installed = false
  @Volatile private var appContext: Context? = null
  @Volatile var lastLevel: Level = Level.NORMAL
    private set

  fun install(context: Context) {
    if (installed) return
    synchronized(this) {
      if (installed) return
      appContext = context.applicationContext
      context.applicationContext.registerComponentCallbacks(this)
      installed = true
    }
  }

  fun addListener(listener: Listener) {
    listeners.add(listener)
  }

  fun removeListener(listener: Listener) {
    listeners.remove(listener)
  }

  /**
   * Maps trim levels using only the signals Android still supports: since
   * API 34 every level except UI_HIDDEN and BACKGROUND is deprecated (and no
   * longer delivered), so severity comes from ActivityManager's live
   * `lowMemory` flag instead of the old RUNNING_* levels. Visible for tests.
   */
  fun levelFor(trimLevel: Int, systemLowOnMemory: Boolean): Level = when {
    systemLowOnMemory -> Level.CRITICAL
    trimLevel >= ComponentCallbacks2.TRIM_MEMORY_BACKGROUND -> Level.MODERATE
    else -> Level.NORMAL
  }

  fun dispatch(level: Level) {
    lastLevel = level
    listeners.forEach { it.onMemoryPressure(level) }
  }

  override fun onTrimMemory(level: Int) {
    if (level == ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN) return // backgrounded, not pressure
    val context = appContext ?: return
    dispatch(levelFor(level, DeviceInfo.memoryInfo(context).lowMemory))
  }

  // Required by the ComponentCallbacks interface; the platform deprecated it
  // in favour of onTrimMemory, which is what this monitor relies on.
  @Deprecated("Superseded by onTrimMemory")
  override fun onLowMemory() = Unit

  override fun onConfigurationChanged(newConfig: Configuration) = Unit
}
