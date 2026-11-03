package com.photoplus.device

import android.content.ComponentCallbacks2
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

class MemoryPressureMonitorTest {
  @Test fun mapsSupportedSignals() {
    val m = MemoryPressureMonitor
    assertEquals(MemoryPressureMonitor.Level.MODERATE, m.levelFor(ComponentCallbacks2.TRIM_MEMORY_BACKGROUND, systemLowOnMemory = false))
    assertEquals(MemoryPressureMonitor.Level.CRITICAL, m.levelFor(ComponentCallbacks2.TRIM_MEMORY_BACKGROUND, systemLowOnMemory = true))
    assertEquals(MemoryPressureMonitor.Level.CRITICAL, m.levelFor(ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN, systemLowOnMemory = true))
    assertEquals(MemoryPressureMonitor.Level.NORMAL, m.levelFor(ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN, systemLowOnMemory = false))
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
