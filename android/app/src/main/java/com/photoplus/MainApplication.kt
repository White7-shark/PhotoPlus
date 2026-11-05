package com.photoplus

import android.app.Application
import com.facebook.react.PackageList
import com.facebook.react.ReactApplication
import com.facebook.react.ReactHost
import com.facebook.react.ReactNativeApplicationEntryPoint.loadReactNative
import com.facebook.react.defaults.DefaultReactHost.getDefaultReactHost
import com.photoplus.device.MemoryPressureMonitor
import com.photoplus.edit.WatermarkRenderer
import com.photoplus.entitlement.Entitlements
import com.photoplus.studio.DepthEstimator

class MainApplication : Application(), ReactApplication {

  override val reactHost: ReactHost by lazy {
    getDefaultReactHost(
      context = applicationContext,
      packageList =
        PackageList(this).packages.apply {
          add(PhotoPlusPackage())
        },
    )
  }

  override fun onCreate() {
    super.onCreate()
    // Install before React starts so early memory pressure is never missed.
    MemoryPressureMonitor.install(this)
    DepthEstimator.install(this)
    Entitlements.install(this)
    WatermarkRenderer.install(this)
    loadReactNative(this)
  }
}
