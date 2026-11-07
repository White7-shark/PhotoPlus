package com.photoplus

import com.facebook.react.BaseReactPackage
import com.facebook.react.bridge.NativeModule
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.model.ReactModuleInfo
import com.facebook.react.module.model.ReactModuleInfoProvider
import com.facebook.react.uimanager.ViewManager
import com.photoplus.ads.AdsModule
import com.photoplus.camera.CameraModule
import com.photoplus.camera.CameraPreviewManager
import com.photoplus.device.DeviceModule
import com.photoplus.engine.EngineModule
import com.photoplus.storage.DatabaseModule
import com.photoplus.storage.StorageModule

/** Registers Photo Plus TurboModules and native components (lazy-loaded). */
class PhotoPlusPackage : BaseReactPackage() {

  override fun getModule(name: String, reactContext: ReactApplicationContext): NativeModule? = when (name) {
    DeviceModule.NAME -> DeviceModule(reactContext)
    EngineModule.NAME -> EngineModule(reactContext)
    DatabaseModule.NAME -> DatabaseModule(reactContext)
    StorageModule.NAME -> StorageModule(reactContext)
    CameraModule.NAME -> CameraModule(reactContext)
    AdsModule.NAME -> AdsModule(reactContext)
    else -> null
  }

  override fun getReactModuleInfoProvider() = ReactModuleInfoProvider {
    listOf(DeviceModule.NAME, EngineModule.NAME, DatabaseModule.NAME, StorageModule.NAME, CameraModule.NAME, AdsModule.NAME)
      .associateWith { name ->
        ReactModuleInfo(
          name = name,
          className = name,
          canOverrideExistingModule = false,
          needsEagerInit = false,
          isCxxModule = false,
          isTurboModule = true,
        )
      }
  }

  override fun createViewManagers(reactContext: ReactApplicationContext): List<ViewManager<*, *>> =
    listOf(CameraPreviewManager(), com.photoplus.video.VideoPlayerManager())
}
