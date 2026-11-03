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
  @Volatile var lastLevel: Level = Level.NORMAL
    private set

  fun install(context: Context) {
    if (installed) return
    synchronized(this) {
      if (installed) return
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

  /** Maps trim levels; visible for unit tests. */
  @Suppress("DEPRECATION")
  fun levelFor(trimLevel: Int): Level = when {
    trimLevel >= ComponentCallbacks2.TRIM_MEMORY_COMPLETE -> Level.CRITICAL
    trimLevel >= ComponentCallbacks2.TRIM_MEMORY_BACKGROUND -> Level.MODERATE
    trimLevel == ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN -> Level.NORMAL
    trimLevel >= ComponentCallbacks2.TRIM_MEMORY_RUNNING_CRITICAL -> Level.CRITICAL
    trimLevel >= ComponentCallbacks2.TRIM_MEMORY_RUNNING_LOW -> Level.MODERATE
    else -> Level.NORMAL
  }

  fun dispatch(level: Level) {
    lastLevel = level
    listeners.forEach { it.onMemoryPressure(level) }
  }

  override fun onTrimMemory(level: Int) {
    if (level == ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN) return // not pressure, just backgrounded
    dispatch(levelFor(level))
  }

  @Deprecated("Deprecated in Android API")
  override fun onLowMemory() = dispatch(Level.CRITICAL)

  override fun onConfigurationChanged(newConfig: Configuration) = Unit
}
