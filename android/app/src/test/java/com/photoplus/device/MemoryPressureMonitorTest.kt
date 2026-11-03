package com.photoplus.device

import android.content.ComponentCallbacks2
import org.junit.Assert.assertEquals
import org.junit.Test

@Suppress("DEPRECATION")
class MemoryPressureMonitorTest {
  @Test fun mapsTrimLevels() {
    assertEquals(MemoryPressureMonitor.Level.MODERATE, MemoryPressureMonitor.levelFor(ComponentCallbacks2.TRIM_MEMORY_RUNNING_LOW))
    assertEquals(MemoryPressureMonitor.Level.CRITICAL, MemoryPressureMonitor.levelFor(ComponentCallbacks2.TRIM_MEMORY_RUNNING_CRITICAL))
    assertEquals(MemoryPressureMonitor.Level.NORMAL, MemoryPressureMonitor.levelFor(ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN))
    assertEquals(MemoryPressureMonitor.Level.MODERATE, MemoryPressureMonitor.levelFor(ComponentCallbacks2.TRIM_MEMORY_BACKGROUND))
    assertEquals(MemoryPressureMonitor.Level.CRITICAL, MemoryPressureMonitor.levelFor(ComponentCallbacks2.TRIM_MEMORY_COMPLETE))
    assertEquals(MemoryPressureMonitor.Level.NORMAL, MemoryPressureMonitor.levelFor(ComponentCallbacks2.TRIM_MEMORY_RUNNING_MODERATE))
  }

  @Test fun dispatchNotifiesListenersAndRemembersLevel() {
    val seen = mutableListOf<MemoryPressureMonitor.Level>()
    val listener = MemoryPressureMonitor.Listener { seen.add(it) }
    MemoryPressureMonitor.addListener(listener)
    MemoryPressureMonitor.dispatch(MemoryPressureMonitor.Level.CRITICAL)
    MemoryPressureMonitor.removeListener(listener)
    MemoryPressureMonitor.dispatch(MemoryPressureMonitor.Level.NORMAL)
    assertEquals(listOf(MemoryPressureMonitor.Level.CRITICAL), seen)
    assertEquals(MemoryPressureMonitor.Level.NORMAL, MemoryPressureMonitor.lastLevel)
  }
}
