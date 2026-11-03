package com.photoplus.device

import com.facebook.react.bridge.LifecycleEventListener
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.specs.NativePhotoPlusDeviceSpec
import com.photoplus.util.emitSafely
import com.photoplus.util.toWritableMap
import java.util.concurrent.Executors

@ReactModule(name = DeviceModule.NAME)
class DeviceModule(reactContext: ReactApplicationContext) :
  NativePhotoPlusDeviceSpec(reactContext), MemoryPressureMonitor.Listener, LifecycleEventListener {

  // Camera characteristics queries can block on some HALs; keep them off the
  // JS and UI threads.
  private val executor = Executors.newSingleThreadExecutor { r -> Thread(r, "pp-capabilities") }

  init {
    MemoryPressureMonitor.install(reactContext)
    MemoryPressureMonitor.addListener(this)
    reactContext.addLifecycleEventListener(this)
  }

  override fun getName() = NAME

  override fun getCapabilities(promise: Promise) {
    executor.execute {
      try {
        promise.resolve(CapabilityProbe.collect(reactApplicationContext).toWritableMap())
      } catch (t: Throwable) {
        promise.reject("CAPABILITIES_FAILED", t.message, t)
      }
    }
  }

  override fun getMemorySnapshot(promise: Promise) {
    try {
      promise.resolve(CapabilityProbe.memory(reactApplicationContext).toWritableMap())
    } catch (t: Throwable) {
      promise.reject("MEMORY_FAILED", t.message, t)
    }
  }

  override fun addListener(eventName: String) = Unit
  override fun removeListeners(count: Double) = Unit

  override fun onMemoryPressure(level: MemoryPressureMonitor.Level) {
    reactApplicationContext.emitSafely(EVENT_MEMORY_PRESSURE, mapOf("level" to level.name.lowercase()))
  }

  // Returning to the foreground is the signal that pressure has eased.
  override fun onHostResume() {
    if (MemoryPressureMonitor.lastLevel != MemoryPressureMonitor.Level.NORMAL) {
      MemoryPressureMonitor.dispatch(MemoryPressureMonitor.Level.NORMAL)
    }
  }

  override fun onHostPause() = Unit
  override fun onHostDestroy() = Unit

  override fun invalidate() {
    MemoryPressureMonitor.removeListener(this)
    reactApplicationContext.removeLifecycleEventListener(this)
    executor.shutdown()
    super.invalidate()
  }

  companion object {
    const val NAME = "PhotoPlusDevice"
    const val EVENT_MEMORY_PRESSURE = "PhotoPlusMemoryPressure"
  }
}
