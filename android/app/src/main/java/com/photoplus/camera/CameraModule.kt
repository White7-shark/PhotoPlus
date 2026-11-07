package com.photoplus.camera

import java.io.File

import android.os.Handler
import android.os.Looper
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.device.DeviceInfo
import com.photoplus.engine.MemoryBudgetPolicy
import com.photoplus.engine.NativeEngine
import com.photoplus.specs.NativePhotoPlusCameraSpec
import com.photoplus.storage.AppPaths
import com.photoplus.util.emitSafely
import com.photoplus.util.toWritableMap

/**
 * JavaScript surface of the camera. CameraX requires its calls on the main
 * thread, so every method hops there; capture processing runs on
 * CaptureProcessor's own threads and reports back through events.
 */
@ReactModule(name = CameraModule.NAME)
class CameraModule(reactContext: ReactApplicationContext) : NativePhotoPlusCameraSpec(reactContext) {
  private val main = Handler(Looper.getMainLooper())
  private val budget = MemoryBudgetPolicy.forTotalRam(
    DeviceInfo.totalRamBytes(reactContext),
    DeviceInfo.isLowRamDevice(reactContext),
  )
  private val tier = CapturePolicy.tierFor(budget.engineBytes)
  private val paths = AppPaths(reactContext)
  private val processor = CaptureProcessor(paths.captures, tier, com.photoplus.edit.PropArt(reactContext.assets, paths.customProps))

  init {
    // The engine budget is process-wide; set it here too in case the engine
    // module has not been created yet.
    if (NativeEngine.ensureLoaded()) NativeEngine.nativeSetMemoryBudget(budget.engineBytes)
    CameraController.events = CameraController.EventSink { name, payload -> reactApplicationContext.emitSafely(name, payload) }
  }

  override fun getName() = NAME

  override fun isSessionReady(promise: Promise) {
    main.post { promise.resolve(CameraController.isReady) }
  }

  override fun getCapabilities(promise: Promise) {
    main.post {
      val caps = CameraController.capabilities()
      if (caps == null) {
        promise.resolve(null)
        return@post
      }
      val plans = CaptureMode.entries.associate { mode ->
        val plan = CapturePolicy.plan(mode, tier)
        mode.id to mapOf("frames" to plan.frames, "workingDimension" to plan.decodeDimension, "upscale" to plan.upscale)
      }
      promise.resolve((caps + mapOf("tier" to tier.name.lowercase(), "capturePlans" to plans)).toWritableMap())
    }
  }

  override fun setFaceTracking(enabled: Boolean) {
    main.post { CameraController.setFaceTracking(enabled) }
  }

  override fun configure(config: ReadableMap) {
    val map = config.toHashMap()
    main.post { CameraController.configure(SessionConfig.fromMap(map, CameraController.sessionConfig)) }
  }

  override fun capturePhoto(options: ReadableMap, promise: Promise) {
    val request = CaptureOptions.fromMap(options.toHashMap())
    main.post {
      val capture = CameraController.imageCapture
      if (capture == null || !CameraController.isReady) {
        promise.reject("NOT_READY", "Camera is not ready")
        return@post
      }
      CameraController.setFlash(request.flash)
      processor.capture(capture, request, CameraController.activeLensFacing == "front", CameraController.deviceTurnDegrees(), object : CaptureProcessor.Listener {
        override fun onShutter(captureId: String) = emit(EVENT_SHUTTER, mapOf("captureId" to captureId))
        override fun onProgress(captureId: String, stage: String, progress: Double) =
          emit(EVENT_PROGRESS, mapOf("captureId" to captureId, "stage" to stage, "progress" to progress))
        override fun onSuccess(captureId: String, result: Map<String, Any?>) =
          promise.resolve((result + ("captureId" to captureId)).toWritableMap())
        override fun onFailure(captureId: String, code: String, message: String) = promise.reject(code, message)
      })
    }
  }

  /**
   * Glide: records up to 7 s (the session must be configured with video).
   * Resolves when the clip is finished, at the limit or on [stopVideo], with
   * { path, durationMs }.
   */
  override fun startVideo(options: ReadableMap, promise: Promise) {
    val map = options.toHashMap()
    val audio = map["audio"] as? Boolean ?: true
    val maxMs = ((map["maxMs"] as? Number)?.toLong() ?: com.photoplus.video.GlideOptions.MAX_DURATION_MS)
      .coerceIn(1_000L, com.photoplus.video.GlideOptions.MAX_DURATION_MS)
    main.post {
      val dir = File(paths.captures, "glide").apply { mkdirs() }
      // Untouched recordings are kept a day (the Glide preview's "As recorded"
      // view); the finished clips live in the gallery.
      val cutoff = System.currentTimeMillis() - 24 * 3600 * 1000L
      dir.listFiles()?.filter { it.isFile && it.lastModified() < cutoff }?.forEach { it.delete() }
      val file = File(dir, "GLIDE_${System.currentTimeMillis()}_raw.mp4")
      val started = CameraController.startRecording(file, maxMs, audio) { out, ms, error ->
        if (out != null) {
          promise.resolve(mapOf("path" to out.absolutePath, "durationMs" to ms.toDouble()).toWritableMap())
        } else {
          promise.reject(error ?: "RECORDING_FAILED", "The video could not be recorded")
        }
      }
      if (!started) promise.reject("NOT_READY", "Video recording is not ready")
      else emit(EVENT_RECORDING, mapOf("recording" to true, "maxMs" to maxMs.toDouble()))
    }
  }

  override fun stopVideo() {
    main.post { CameraController.stopRecording() }
  }

  override fun cancelCapture(captureId: String) {
    processor.cancel(captureId)
  }

  override fun setZoom(ratio: Double, promise: Promise) {
    main.post { CameraController.setZoomRatio(ratio.toFloat()) { promise.resolve(it.toDouble()) } }
  }

  override fun setExposureCompensation(index: Double, promise: Promise) {
    main.post { CameraController.setExposureIndex(index.toInt()) { promise.resolve(it) } }
  }

  override fun focusAt(x: Double, y: Double, promise: Promise) {
    main.post { CameraController.focusAt(x.toFloat().coerceIn(0f, 1f), y.toFloat().coerceIn(0f, 1f)) { promise.resolve(it) } }
  }

  override fun cancelFocus() {
    main.post { CameraController.cancelFocus() }
  }

  override fun setFlash(mode: String) {
    main.post { CameraController.setFlash(FlashSetting.parse(mode)) }
  }

  override fun setTorch(enabled: Boolean, promise: Promise) {
    main.post { promise.resolve(CameraController.setTorch(enabled)) }
  }

  override fun setFocusPeaking(enabled: Boolean, color: Double, threshold: Double) {
    FocusPeakingAnalyzer.color = color.toLong().toInt()
    FocusPeakingAnalyzer.threshold = threshold.toFloat().coerceIn(0.02f, 1f)
    main.post { CameraController.setFocusPeaking(enabled) }
  }

  override fun setManual(settings: ReadableMap, promise: Promise) {
    val parsed = ManualSettings.fromMap(settings.toHashMap())
    main.post { promise.resolve(manualToMap(CameraController.setManual(parsed)).toWritableMap()) }
  }

  override fun resetManual() {
    main.post { CameraController.resetManual() }
  }

  override fun addListener(eventName: String) = Unit
  override fun removeListeners(count: Double) = Unit

  override fun invalidate() {
    processor.shutdown()
    CameraController.events = null
    super.invalidate()
  }

  private fun emit(name: String, payload: Map<String, Any?>) = reactApplicationContext.emitSafely(name, payload)

  companion object {
    const val NAME = "PhotoPlusCamera"
    const val EVENT_SHUTTER = "PhotoPlusCaptureShutter"
    const val EVENT_PROGRESS = "PhotoPlusCaptureProgress"
    /** Glide: { recording: true, maxMs } when a clip starts. */
    const val EVENT_RECORDING = "PhotoPlusRecording"

    fun manualToMap(m: ManualSettings): Map<String, Any?> = mapOf(
      "iso" to m.iso,
      "exposureTimeNs" to m.exposureTimeNs?.toDouble(),
      "focusDiopters" to m.focusDiopters?.toDouble(),
      "whiteBalance" to m.whiteBalance.id,
      "kelvin" to (m.whiteBalance as? WhiteBalance.Kelvin)?.kelvin,
      "aeLock" to m.aeLock,
      "awbLock" to m.awbLock,
    )
  }
}
