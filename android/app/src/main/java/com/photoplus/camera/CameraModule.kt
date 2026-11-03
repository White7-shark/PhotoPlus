package com.photoplus.camera

import android.os.Handler
import android.os.Looper
import androidx.camera.core.ImageCapture
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.engine.ImageCodec
import com.photoplus.specs.NativePhotoPlusCameraSpec
import com.photoplus.storage.AppPaths
import com.photoplus.util.toWritableMap
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@ReactModule(name = CameraModule.NAME)
class CameraModule(reactContext: ReactApplicationContext) : NativePhotoPlusCameraSpec(reactContext) {
  private val paths = AppPaths(reactContext)
  private val main = Handler(Looper.getMainLooper())

  override fun getName() = NAME

  override fun isSessionReady(promise: Promise) {
    main.post { promise.resolve(CameraSession.isReady) }
  }

  override fun capturePhoto(options: ReadableMap, promise: Promise) {
    val flash = when (if (options.hasKey("flash")) options.getString("flash") else null) {
      "on" -> ImageCapture.FLASH_MODE_ON
      "auto" -> ImageCapture.FLASH_MODE_AUTO
      else -> ImageCapture.FLASH_MODE_OFF
    }
    val stamp = SimpleDateFormat("yyyyMMdd_HHmmss_SSS", Locale.US).format(Date())
    val target = File(paths.captures, "PP_$stamp.jpg")
    main.post {
      CameraSession.capture(
        target,
        flash,
        onSaved = { file ->
          // Display orientation, consistent with how the engine decodes it.
          val bounds = ImageCodec.readDisplayBounds(file)
          promise.resolve(
            mapOf(
              "path" to file.absolutePath,
              "width" to bounds.width,
              "height" to bounds.height,
              "sizeBytes" to file.length().toDouble(),
              "capturedAt" to System.currentTimeMillis().toDouble(),
            ).toWritableMap(),
          )
        },
        onError = { code, message -> promise.reject(code, message) },
      )
    }
  }

  override fun setTorch(enabled: Boolean, promise: Promise) {
    main.post { promise.resolve(CameraSession.setTorch(enabled)) }
  }

  companion object {
    const val NAME = "PhotoPlusCamera"
  }
}
