package com.photoplus.camera

import com.facebook.react.module.annotations.ReactModule
import com.facebook.react.uimanager.SimpleViewManager
import com.facebook.react.uimanager.ThemedReactContext
import com.facebook.react.uimanager.ViewManagerDelegate
import com.facebook.react.viewmanagers.PhotoPlusCameraPreviewManagerDelegate
import com.facebook.react.viewmanagers.PhotoPlusCameraPreviewManagerInterface

@ReactModule(name = CameraPreviewManager.NAME)
class CameraPreviewManager : SimpleViewManager<CameraPreviewView>(), PhotoPlusCameraPreviewManagerInterface<CameraPreviewView> {
  private val delegate = PhotoPlusCameraPreviewManagerDelegate(this)

  override fun getDelegate(): ViewManagerDelegate<CameraPreviewView> = delegate
  override fun getName() = NAME
  override fun createViewInstance(context: ThemedReactContext) = CameraPreviewView(context)

  override fun setLensFacing(view: CameraPreviewView, value: String?) {
    view.lensFacing = if (value == "front") "front" else "back"
  }

  override fun setActive(view: CameraPreviewView, value: Boolean) {
    view.active = value
  }

  override fun getExportedCustomDirectEventTypeConstants(): Map<String, Any> = mapOf(
    CameraEvent.READY to mapOf("registrationName" to "onCameraReady"),
    CameraEvent.ERROR to mapOf("registrationName" to "onCameraError"),
  )

  companion object {
    const val NAME = "PhotoPlusCameraPreview"
  }
}
