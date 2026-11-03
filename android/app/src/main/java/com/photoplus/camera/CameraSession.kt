package com.photoplus.camera

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.util.Log
import androidx.camera.core.Camera
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import java.io.File
import java.util.concurrent.Executor
import java.util.concurrent.Executors

/**
 * Process-wide owner of the single active camera session.
 *
 * Binding is tied to the Activity lifecycle via CameraX, which means the
 * camera is released automatically when the app is backgrounded, a call
 * comes in, or another app takes the camera, and re-acquired on resume —
 * the "handles interruptions" acceptance criterion.
 *
 * Only one preview can own the session; a new bind replaces the old owner.
 */
object CameraSession {
  private const val TAG = "PhotoPlusCamera"

  interface Callbacks {
    fun onReady(lensFacing: String)
    fun onError(code: String, message: String)
  }

  private val captureExecutor: Executor = Executors.newSingleThreadExecutor { r -> Thread(r, "pp-capture") }

  private var owner: Any? = null // guarded by main thread
  private var camera: Camera? = null
  private var imageCapture: ImageCapture? = null

  val isReady: Boolean get() = imageCapture != null && camera != null

  fun hasPermission(context: Context) =
    ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED

  /** Must be called on the main thread. */
  fun bind(
    context: Context,
    lifecycleOwner: LifecycleOwner,
    previewView: PreviewView,
    lensFacing: String,
    sessionOwner: Any,
    callbacks: Callbacks,
  ) {
    if (!hasPermission(context)) {
      callbacks.onError("PERMISSION_DENIED", "Camera permission has not been granted")
      return
    }
    owner = sessionOwner
    val future = ProcessCameraProvider.getInstance(context)
    future.addListener({
      // The owner may have changed (or unbound) while the provider loaded.
      if (owner !== sessionOwner) return@addListener
      try {
        val provider = future.get()
        val selector = if (lensFacing == "front") CameraSelector.DEFAULT_FRONT_CAMERA else CameraSelector.DEFAULT_BACK_CAMERA
        if (!provider.hasCamera(selector)) {
          callbacks.onError("NO_CAMERA", "No $lensFacing camera is available on this device")
          return@addListener
        }
        val preview = Preview.Builder().build().also { it.surfaceProvider = previewView.surfaceProvider }
        val capture = ImageCapture.Builder()
          .setCaptureMode(ImageCapture.CAPTURE_MODE_MAXIMIZE_QUALITY)
          .build()
        provider.unbindAll()
        camera = provider.bindToLifecycle(lifecycleOwner, selector, preview, capture)
        imageCapture = capture
        callbacks.onReady(lensFacing)
      } catch (e: Exception) {
        Log.w(TAG, "Unable to bind camera", e)
        camera = null
        imageCapture = null
        callbacks.onError("BIND_FAILED", e.message ?: "Unable to start the camera")
      }
    }, ContextCompat.getMainExecutor(context))
  }

  /** Releases the session if [sessionOwner] still owns it. Main thread. */
  fun unbind(context: Context, sessionOwner: Any) {
    if (owner !== sessionOwner) return
    owner = null
    camera = null
    imageCapture = null
    val future = ProcessCameraProvider.getInstance(context)
    future.addListener({
      // A new owner may have bound in the meantime; never tear it down.
      if (owner == null) {
        try { future.get().unbindAll() } catch (e: Exception) { Log.w(TAG, "unbind failed", e) }
      }
    }, ContextCompat.getMainExecutor(context))
  }

  fun capture(
    target: File,
    flashMode: Int,
    onSaved: (File) -> Unit,
    onError: (code: String, message: String) -> Unit,
  ) {
    val capture = imageCapture
    if (capture == null) {
      onError("NOT_READY", "Camera is not ready")
      return
    }
    target.parentFile?.mkdirs()
    capture.flashMode = flashMode
    val options = ImageCapture.OutputFileOptions.Builder(target).build()
    capture.takePicture(options, captureExecutor, object : ImageCapture.OnImageSavedCallback {
      override fun onImageSaved(output: ImageCapture.OutputFileResults) = onSaved(target)
      override fun onError(exception: ImageCaptureException) {
        target.delete()
        onError("CAPTURE_FAILED", exception.message ?: "Capture failed (code ${exception.imageCaptureError})")
      }
    })
  }

  fun setTorch(enabled: Boolean): Boolean {
    val cam = camera ?: return false
    if (!cam.cameraInfo.hasFlashUnit()) return false
    cam.cameraControl.enableTorch(enabled)
    return true
  }
}
