package com.photoplus.camera

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.ImageFormat
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraMetadata
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.CaptureResult
import android.hardware.camera2.TotalCaptureResult
import android.hardware.camera2.params.ColorSpaceTransform
import android.hardware.camera2.params.RggbChannelVector
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.util.Rational
import android.util.Size
import android.view.OrientationEventListener
import android.view.Surface
import androidx.camera.camera2.interop.Camera2CameraControl
import androidx.camera.camera2.interop.Camera2CameraInfo
import androidx.camera.camera2.interop.Camera2Interop
import androidx.camera.camera2.interop.CaptureRequestOptions
import androidx.camera.camera2.interop.ExperimentalCamera2Interop
import androidx.camera.core.Camera
import androidx.camera.core.CameraSelector
import androidx.camera.core.CameraState
import androidx.camera.core.FocusMeteringAction
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageCapture
import androidx.camera.core.Preview
import androidx.camera.core.SurfaceOrientedMeteringPointFactory
import androidx.camera.core.UseCaseGroup
import androidx.camera.core.ViewPort
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.core.MirrorMode
import androidx.camera.video.FallbackStrategy
import androidx.camera.video.FileOutputOptions
import androidx.camera.video.Quality
import androidx.camera.video.QualitySelector
import androidx.camera.video.Recorder
import androidx.camera.video.Recording
import androidx.camera.video.VideoCapture
import androidx.camera.video.VideoRecordEvent
import com.photoplus.beauty.BeautyAnalyzer
import com.photoplus.beauty.BeautyEffect
import com.photoplus.beauty.BeautyProcessor
import com.photoplus.beauty.LiveBeautyState
import com.photoplus.engine.NativeEngine
import androidx.core.graphics.scale
import java.util.concurrent.atomic.AtomicBoolean
import androidx.camera.view.PreviewView
import androidx.annotation.OptIn
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/**
 * Owner of the single active camera session (CameraX for the main workflow,
 * Camera2 interop for manual controls). All public methods run on the main
 * thread.
 *
 * Reliability:
 *  - bound to the Activity lifecycle: backgrounding, calls and other apps
 *    taking the camera release it; returning re-acquires it;
 *  - CameraState errors are reported to JS and critical ones trigger a
 *    rebind with exponential backoff (camera recovery);
 *  - device orientation is tracked continuously, so photos are rotated
 *    correctly even though the UI is portrait-locked.
 *
 * Camera2 interop is still marked experimental in CameraX 1.6, but it is
 * the only supported route to manual sensor controls and per-frame
 * metadata, so the opt-in is deliberate and scoped to this class.
 */
@OptIn(markerClass = [ExperimentalCamera2Interop::class])
object CameraController {
  private const val TAG = "PhotoPlusCamera"
  private const val MAX_RETRIES = 3
  /** How often Auto Light reads the scene. */
  private const val METER_INTERVAL_MS = 700L
  /** The longest the shutter waits for a changed exposure to settle. */
  private const val EXPOSURE_SETTLE_MS = 900L

  interface SessionCallbacks {
    fun onReady(lensFacing: String)
    fun onError(code: String, message: String)
  }

  /** Bridge to JavaScript events (set by CameraModule). */
  fun interface EventSink {
    fun emit(name: String, payload: Map<String, Any?>)
  }

  @Volatile var events: EventSink? = null

  private val main = Handler(Looper.getMainLooper())
  private val analysisExecutor = Executors.newSingleThreadExecutor { r -> Thread(r, "pp-analysis") }

  private var appContext: Context? = null
  private var owner: Any? = null
  private var lifecycleOwner: LifecycleOwner? = null
  private var previewView: PreviewView? = null
  private var callbacks: SessionCallbacks? = null
  private var lensFacing = "back"
  private var camera: Camera? = null
  @Volatile var imageCapture: ImageCapture? = null
    private set
  /** Glide sessions: the video stream (null in photo sessions). */
  @Volatile var videoCapture: VideoCapture<Recorder>? = null
    private set
  private var recording: Recording? = null
  private var analysis: ImageAnalysis? = null
  private var orientationListener: OrientationEventListener? = null
  private var retries = 0
  private var pendingRetry: Runnable? = null

  var sessionConfig = SessionConfig()
    private set
  /** What the user asked for; [applyManualControls] derives what the hardware can do now. */
  var manual = ManualSettings()
    private set
  /** Kelvin needs the live colour matrix, which arrives with the first frame. */
  @Volatile private var kelvinWaitingForTransform = false
  private var flash = FlashSetting.OFF
  private var peakingEnabled = false
  /** Live face tracking for props worn in the camera. */
  private var facesEnabled = false
  /** The live beauty effect's GL processor while bound, and whether the GPU let it down this session. */
  private var beautyProcessor: BeautyProcessor? = null
  private var beautyFailed = false
  @Volatile var targetRotation = Surface.ROTATION_0
    private set
  private var capabilityInputs: CameraCapabilities.Inputs? = null
  var captureSize: Size? = null
    private set

  // Auto Light (see AutoLight.kt): a steady reading of the scene, the screen
  // light for selfies, and Low Light Boost for the live view in the dark.
  private val lightExecutor = Executors.newSingleThreadExecutor { r -> Thread(r, "pp-light") }
  private val lightSteadier = LightSteadier()
  /** The steady reading the shutter uses (null until the first preview frame is read). */
  @Volatile var lightReading: LightReading? = null
    private set
  private var lightReported: LightReading? = null
  private var lightMetering = false
  private var screenFlashReady = false
  private var lowLightBoostOn = false
  private var lowLightBoostFailed = false
  /** Turned off for a flash capture, back on after it. */
  private var lowLightBoostHeld = false
  /** The user's exposure compensation while a capture uses its own. */
  private var exposureBeforeCapture: Int? = null
  /** A capture is changing the light (exposure, screen light): readings would mislead. */
  private var lightCapturing = false
  private val meterTick = object : Runnable {
    override fun run() {
      meterOnce()
      main.postDelayed(this, METER_INTERVAL_MS)
    }
  }

  val isReady: Boolean get() = camera != null && (imageCapture != null || videoCapture != null)
  val isRecording: Boolean get() = recording != null
  val activeLensFacing: String get() = lensFacing

  /** Latest values reported by the sensor, for the Pro readout and manual defaults. */
  object Live {
    @Volatile var iso: Int? = null
    @Volatile var exposureNs: Long? = null
    @Volatile var focusDiopters: Float? = null
    @Volatile var aperture: Float? = null
    @Volatile var colorTransform: ColorSpaceTransform? = null
    @Volatile var lastEmit = 0L
  }

  fun hasPermission(context: Context) =
    ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED

  // ---------------------------------------------------------------- binding

  fun bind(
    context: Context,
    lifecycleOwner: LifecycleOwner,
    previewView: PreviewView,
    lensFacing: String,
    sessionOwner: Any,
    callbacks: SessionCallbacks,
  ) {
    if (!hasPermission(context)) {
      callbacks.onError("PERMISSION_DENIED", "Camera permission has not been granted")
      return
    }
    appContext = context.applicationContext
    owner = sessionOwner
    this.lifecycleOwner = lifecycleOwner
    this.previewView = previewView
    this.callbacks = callbacks
    if (this.lensFacing != lensFacing) manual = ManualSettings() // ranges differ per camera
    this.lensFacing = lensFacing
    cancelRetry()
    startOrientationTracking(context)
    bindUseCases(sessionOwner)
  }

  fun unbind(context: Context, sessionOwner: Any) {
    if (owner !== sessionOwner) return
    owner = null
    cancelRetry()
    stopOrientationTracking()
    stopMetering()
    camera = null
    imageCapture = null
    analysis?.clearAnalyzer()
    analysis = null
    previewView = null
    callbacks = null
    val processor = beautyProcessor
    beautyProcessor = null
    beautyFailed = false
    val future = ProcessCameraProvider.getInstance(context)
    future.addListener({
      if (owner == null) {
        try { future.get().unbindAll() } catch (e: Exception) { Log.w(TAG, "unbind failed", e) }
      }
      processor?.release()
    }, ContextCompat.getMainExecutor(context))
  }

  // ---------------------------------------------------------------- Glide

  /** How a recording ended: the file (when usable) or an error code. */
  fun interface RecordingDone {
    fun onDone(file: java.io.File?, durationMs: Long, error: String?)
  }

  /**
   * Records up to [maxMs] into [file] (Glide sessions only). Ends by itself
   * at the limit or on [stopRecording]; [done] runs on the main thread.
   */
  @android.annotation.SuppressLint("MissingPermission") // checked: audio only when granted
  fun startRecording(file: java.io.File, maxMs: Long, audio: Boolean, done: RecordingDone): Boolean {
    val context = appContext ?: return false
    val vc = videoCapture ?: return false
    if (recording != null) return false
    val options = FileOutputOptions.Builder(file).setDurationLimitMillis(maxMs).build()
    var pending = vc.output.prepareRecording(context, options)
    val micGranted = ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
    if (audio && micGranted) pending = pending.withAudioEnabled()
    recording = pending.start(ContextCompat.getMainExecutor(context)) { event ->
      if (event is VideoRecordEvent.Finalize) {
        recording = null
        val ms = event.recordingStats.recordedDurationNanos / 1_000_000
        // Reaching the 7-second limit is the normal end; a stream that went
        // away mid-way still leaves a usable clip.
        val usable = event.error == VideoRecordEvent.Finalize.ERROR_NONE ||
          event.error == VideoRecordEvent.Finalize.ERROR_DURATION_LIMIT_REACHED ||
          (event.error == VideoRecordEvent.Finalize.ERROR_SOURCE_INACTIVE && file.length() > 0)
        if (usable && file.length() > 0) {
          done.onDone(file, ms, null)
        } else {
          file.delete()
          done.onDone(null, ms, "RECORDING_FAILED")
        }
      }
    }
    return true
  }

  fun stopRecording() {
    recording?.stop()
  }

  /** Applies a new session configuration; rebinds only when the streams change. */
  fun configure(config: SessionConfig) {
    val changed = config != sessionConfig
    sessionConfig = config
    val o = owner
    if (changed && o != null) bindUseCases(o)
  }

  private fun bindUseCases(sessionOwner: Any) {
    val context = appContext ?: return
    val view = previewView ?: return
    val lifecycle = lifecycleOwner ?: return
    val future = ProcessCameraProvider.getInstance(context)
    future.addListener({
      if (owner !== sessionOwner) return@addListener
      try {
        val provider = future.get()
        val selector = if (lensFacing == "front") CameraSelector.DEFAULT_FRONT_CAMERA else CameraSelector.DEFAULT_BACK_CAMERA
        if (!provider.hasCamera(selector)) {
          callbacks?.onError("NO_CAMERA", "No $lensFacing camera is available on this device")
          return@addListener
        }
        val info = provider.getCameraInfo(selector)
        val inputs = readCapabilityInputs(info, lensFacing)
        capabilityInputs = inputs

        val stream = sessionConfig.aspectRatio.streamRatio
        val aspect = if (stream == AspectRatioMode.RATIO_16_9) AspectRatioStrategy.RATIO_16_9_FALLBACK_AUTO_STRATEGY
        else AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY
        val target = ResolutionPolicy.choose(inputs.jpegSizes, sessionConfig.aspectRatio, sessionConfig.resolution)
        val captureSelector = ResolutionSelector.Builder()
          .setAspectRatioStrategy(aspect)
          .setAllowedResolutionMode(ResolutionSelector.PREFER_HIGHER_RESOLUTION_OVER_CAPTURE_RATE)
          .apply {
            if (target != null) {
              setResolutionStrategy(
                ResolutionStrategy(Size(target.width, target.height), ResolutionStrategy.FALLBACK_RULE_CLOSEST_LOWER_THEN_HIGHER),
              )
            }
          }
          .build()
        val streamSelector = ResolutionSelector.Builder().setAspectRatioStrategy(aspect).build()

        val previewBuilder = Preview.Builder().setResolutionSelector(streamSelector)
        Camera2Interop.Extender(previewBuilder).setSessionCaptureCallback(liveCallback)
        val preview = previewBuilder.build().also { it.surfaceProvider = view.surfaceProvider }

        val captureBuilder = ImageCapture.Builder()
          .setCaptureMode(ImageCapture.CAPTURE_MODE_MAXIMIZE_QUALITY)
          .setResolutionSelector(captureSelector)
          .setFlashMode(flashMode(flash))
          .setTargetRotation(targetRotation)
        if (sessionConfig.rawEnabled && inputs.rawSupported) {
          captureBuilder.setOutputFormat(ImageCapture.OUTPUT_FORMAT_RAW_JPEG)
        }
        // Glide sessions record video: preview + video stream (no still capture,
        // which keeps the stream combination supported on every camera level).
        val video = if (sessionConfig.video) {
          val recorder = Recorder.Builder()
            .setQualitySelector(QualitySelector.from(Quality.FHD, FallbackStrategy.lowerQualityOrHigherThan(Quality.HD)))
            .build()
          VideoCapture.Builder(recorder)
            .setMirrorMode(MirrorMode.MIRROR_MODE_ON_FRONT_ONLY) // selfies as the preview shows them
            .setTargetRotation(targetRotation)
            .build()
        } else {
          null
        }
        val capture = if (video == null) captureBuilder.build() else null
        // Selfie Light: the screen as the front camera's flash. CameraX lights
        // the overlay at full brightness and lets exposure settle on it first.
        screenFlashReady = false
        if (capture != null && lensFacing == "front") {
          val window = (lifecycle as? android.app.Activity)?.window
          if (window != null) {
            try {
              view.setScreenFlashWindow(window)
              view.screenFlash?.let { capture.screenFlash = it }
              screenFlashReady = capture.screenFlash != null
            } catch (e: Exception) {
              Log.w(TAG, "screen light unavailable", e)
            }
          }
        }

        val groupBuilder = UseCaseGroup.Builder().addUseCase(preview)
        if (capture != null) groupBuilder.addUseCase(capture)
        if (video != null) groupBuilder.addUseCase(video)
        // Live beauty: a GPU effect on the preview, fed by its own analysis
        // (masks, the face, the scene). Phones that cannot run it still get
        // every photo finished with the recipe.
        val oldProcessor = beautyProcessor
        beautyProcessor = null
        val liveBeauty = sessionConfig.beauty && video == null && !beautyFailed && BeautyProcessor.supported(context)
        if (liveBeauty) {
          val processor = BeautyProcessor(context) { error -> onBeautyFailure(sessionOwner, error) }
          beautyProcessor = processor
          groupBuilder.addEffect(BeautyEffect(processor) { error -> onBeautyFailure(sessionOwner, error) })
        }
        events?.emit("PhotoPlusBeautyLive", mapOf(
          "requested" to sessionConfig.beauty,
          "live" to liveBeauty,
          "reason" to if (sessionConfig.beauty && !liveBeauty) (LiveBeautyState.unavailableReason ?: "unsupported") else null,
        ))
        // One analysis stream: live beauty (which also tracks faces for props),
        // else face tracking while props are on (they need the frame rate),
        // otherwise focus peaking when it is enabled.
        FaceTracker.mirrored = lensFacing == "front"
        FaceTracker.reset()
        BeautyAnalyzer.mirrored = lensFacing == "front"
        BeautyAnalyzer.reset()
        LiveBeautyState.reset()
        val analyzer: ImageAnalysis.Analyzer? = when {
          liveBeauty -> BeautyAnalyzer
          facesEnabled -> FaceTracker
          peakingEnabled -> FocusPeakingAnalyzer
          else -> null
        }
        val peaking = analyzer?.let { buildAnalysis(aspect, view.display?.rotation ?: Surface.ROTATION_0, it).also { a -> groupBuilder.addUseCase(a) } }
        // What-you-see-is-what-you-get: crop every output to the chosen ratio
        // (this is also how 1:1 is produced from a 4:3 stream).
        groupBuilder.setViewPort(buildViewPort(view))

        provider.unbindAll()
        oldProcessor?.release()
        val cam = provider.bindToLifecycle(lifecycle, selector, groupBuilder.build())
        camera = cam
        imageCapture = capture
        videoCapture = video
        analysis = peaking
        captureSize = capture?.resolutionInfo?.resolution ?: video?.resolutionInfo?.resolution
        observeCamera(cam, lifecycle, sessionOwner)
        applyManualControls()
        startMetering()
        callbacks?.onReady(lensFacing)
      } catch (e: Exception) {
        Log.w(TAG, "Unable to bind camera", e)
        camera = null
        imageCapture = null
        videoCapture = null
        callbacks?.onError("BIND_FAILED", e.message ?: "Unable to start the camera")
        scheduleRetry(sessionOwner, "BIND_FAILED")
      }
    }, ContextCompat.getMainExecutor(context))
  }

  /** The live preview failed on the GPU: carry on without it (photos still get the recipe). */
  private fun onBeautyFailure(sessionOwner: Any, error: Throwable) {
    Log.w(TAG, "live beauty off", error)
    val context = appContext ?: return
    ContextCompat.getMainExecutor(context).execute {
      if (beautyFailed || owner !== sessionOwner) return@execute
      beautyFailed = true
      bindUseCases(sessionOwner)
    }
  }

  private fun buildViewPort(view: PreviewView): ViewPort {
    val ratio = sessionConfig.aspectRatio
    val rotation = view.display?.rotation ?: Surface.ROTATION_0
    val portrait = rotation == Surface.ROTATION_0 || rotation == Surface.ROTATION_180
    // ViewPort ratios are expressed in the display's orientation.
    val rational = if (portrait) Rational(ratio.height, ratio.width) else Rational(ratio.width, ratio.height)
    return ViewPort.Builder(rational, rotation).setScaleType(ViewPort.FILL_CENTER).build()
  }

  private fun buildAnalysis(aspect: AspectRatioStrategy, displayRotation: Int, analyzer: ImageAnalysis.Analyzer): ImageAnalysis {
    val selector = ResolutionSelector.Builder()
      .setAspectRatioStrategy(aspect)
      .setResolutionStrategy(ResolutionStrategy(Size(640, 480), ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER))
      .build()
    return ImageAnalysis.Builder()
      .setResolutionSelector(selector)
      .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
      .setTargetRotation(displayRotation)
      .build()
      .also { it.setAnalyzer(analysisExecutor, analyzer) }
  }

  // --------------------------------------------------- state and recovery

  private fun observeCamera(cam: Camera, lifecycle: LifecycleOwner, sessionOwner: Any) {
    val info = cam.cameraInfo
    info.cameraState.removeObservers(lifecycle)
    info.zoomState.removeObservers(lifecycle)
    info.torchState.removeObservers(lifecycle)
    info.cameraState.observe(lifecycle) { state ->
      if (owner !== sessionOwner || camera !== cam) return@observe
      val error = state.error
      events?.emit("PhotoPlusCameraState", mapOf(
        "state" to stateName(state.type),
        "error" to error?.let { errorName(it.code) },
        "critical" to (error?.type == CameraState.ErrorType.CRITICAL),
      ))
      when {
        state.type == CameraState.Type.OPEN -> retries = 0
        error != null && error.type == CameraState.ErrorType.CRITICAL && state.type == CameraState.Type.CLOSED ->
          scheduleRetry(sessionOwner, errorName(error.code))
      }
    }
    info.zoomState.observe(lifecycle) { z ->
      events?.emit("PhotoPlusCameraZoom", mapOf(
        "ratio" to z.zoomRatio.toDouble(),
        "linear" to z.linearZoom.toDouble(),
        "min" to z.minZoomRatio.toDouble(),
        "max" to z.maxZoomRatio.toDouble(),
      ))
    }
    info.torchState.observe(lifecycle) { t -> events?.emit("PhotoPlusCameraTorch", mapOf("on" to (t == 1))) }
  }

  private fun scheduleRetry(sessionOwner: Any, reason: String) {
    if (retries >= MAX_RETRIES) {
      callbacks?.onError("CAMERA_UNAVAILABLE", "The camera could not be recovered ($reason)")
      return
    }
    val delay = 500L shl retries
    retries++
    events?.emit("PhotoPlusCameraState", mapOf("state" to "recovering", "attempt" to retries, "error" to reason))
    cancelRetry()
    val r = Runnable { if (owner === sessionOwner) bindUseCases(sessionOwner) }
    pendingRetry = r
    main.postDelayed(r, delay)
  }

  private fun cancelRetry() {
    pendingRetry?.let { main.removeCallbacks(it) }
    pendingRetry = null
  }

  // ------------------------------------------------------------ orientation

  private fun startOrientationTracking(context: Context) {
    if (orientationListener != null) return
    orientationListener = object : OrientationEventListener(context.applicationContext) {
      override fun onOrientationChanged(degrees: Int) {
        if (degrees == ORIENTATION_UNKNOWN) return
        val rotation = OrientationMath.rotationForDegrees(degrees, targetRotation)
        if (rotation != targetRotation) {
          targetRotation = rotation
          // Only the photo follows the device. Analysis frames feed the peaking
          // overlay drawn over the portrait-locked preview, so they keep the
          // display rotation (set in buildAnalysis).
          imageCapture?.targetRotation = rotation
          events?.emit("PhotoPlusCameraOrientation", mapOf("degrees" to OrientationMath.uiDegrees(rotation)))
        }
      }
    }.also { if (it.canDetectOrientation()) it.enable() }
  }

  private fun stopOrientationTracking() {
    orientationListener?.disable()
    orientationListener = null
  }

  // --------------------------------------------------------------- controls

  fun capabilities(): Map<String, Any?>? = capabilityInputs?.let { inputs ->
    CameraCapabilities.build(inputs) + mapOf(
      "captureSize" to captureSize?.let { mapOf("width" to it.width, "height" to it.height) },
    )
  }

  fun focusAt(x: Float, y: Float, onResult: (Boolean) -> Unit) {
    val cam = camera
    val view = previewView
    if (cam == null || view == null || view.width == 0) {
      onResult(false)
      return
    }
    val point = view.meteringPointFactory.createPoint(x * view.width, y * view.height)
    // In manual-focus mode, metering still sets exposure/white balance.
    val flags = if (manual.manualFocus) FocusMeteringAction.FLAG_AE or FocusMeteringAction.FLAG_AWB
    else FocusMeteringAction.FLAG_AF or FocusMeteringAction.FLAG_AE or FocusMeteringAction.FLAG_AWB
    val action = FocusMeteringAction.Builder(point, flags).setAutoCancelDuration(5, TimeUnit.SECONDS).build()
    val future = cam.cameraControl.startFocusAndMetering(action)
    future.addListener({
      val ok = try { future.get().isFocusSuccessful } catch (_: Exception) { false }
      events?.emit("PhotoPlusCameraFocus", mapOf("x" to x.toDouble(), "y" to y.toDouble(), "success" to ok))
      onResult(ok)
    }, ContextCompat.getMainExecutor(view.context))
  }

  fun cancelFocus() {
    camera?.cameraControl?.cancelFocusAndMetering()
  }

  /** How far the phone is turned clockwise from the (portrait-locked) display, in degrees. */
  fun deviceTurnDegrees(): Int {
    val display = previewView?.display?.rotation ?: Surface.ROTATION_0
    return (OrientationMath.uiDegrees(display) - OrientationMath.uiDegrees(targetRotation) + 360) % 360
  }

  fun currentZoomRatio(): Float = camera?.cameraInfo?.zoomState?.value?.zoomRatio ?: 1f

  fun setZoomRatio(ratio: Float, onResult: (Float) -> Unit) {
    val cam = camera ?: return onResult(1f)
    val z = cam.cameraInfo.zoomState.value
    val clamped = ratio.coerceIn(z?.minZoomRatio ?: 1f, z?.maxZoomRatio ?: 1f)
    cam.cameraControl.setZoomRatio(clamped)
    onResult(clamped)
  }

  fun setExposureIndex(index: Int, onResult: (Int) -> Unit) {
    val cam = camera ?: return onResult(0)
    val state = cam.cameraInfo.exposureState
    if (!state.isExposureCompensationSupported) return onResult(0)
    val clamped = index.coerceIn(state.exposureCompensationRange.lower, state.exposureCompensationRange.upper)
    cam.cameraControl.setExposureCompensationIndex(clamped)
    onResult(clamped)
  }

  fun setFlash(setting: FlashSetting) {
    val changed = flash != setting
    flash = setting
    imageCapture?.flashMode = flashMode(setting)
    if (changed) lightReading?.let { emitLight(it) } // the label depends on the flash choice
  }

  // ------------------------------------------------------------ Auto Light

  private fun startMetering() {
    lightSteadier.reset()
    lightReading = null
    lightReported = null
    lowLightBoostOn = false
    lowLightBoostFailed = false
    lowLightBoostHeld = false
    lightCapturing = false
    main.removeCallbacks(meterTick)
    if (!sessionConfig.video) main.postDelayed(meterTick, METER_INTERVAL_MS)
  }

  private fun stopMetering() {
    main.removeCallbacks(meterTick)
    exposureBeforeCapture = null
    lightCapturing = false
  }

  /** Main thread: one reading from the live view (a 160 px copy), metered off the main thread. */
  private fun meterOnce() {
    val view = previewView ?: return
    val cam = camera ?: return
    if (sessionConfig.video || lightMetering || lightCapturing || !NativeEngine.ensureLoaded()) return
    val small = try {
      val full = view.bitmap ?: return
      val w = 160
      val h = (full.height * w / full.width.coerceAtLeast(1)).coerceIn(16, 480)
      val scaled = if (full.config == android.graphics.Bitmap.Config.ARGB_8888) full.scale(w, h)
      else full.copy(android.graphics.Bitmap.Config.ARGB_8888, false).let { c -> c.scale(w, h).also { if (it !== c) c.recycle() } }
      if (scaled !== full) full.recycle()
      scaled
    } catch (e: Exception) {
      Log.w(TAG, "light reading skipped", e)
      return
    }
    val input = LightReading.meterInput(
      Live.iso, Live.exposureNs, Live.aperture, lensFacing == "front", cam.cameraInfo.hasFlashUnit(), faceLuma(), Live.focusDiopters,
    )
    lightMetering = true
    lightExecutor.execute {
      val reading = try {
        val out = FloatArray(LightReading.METER_OUT)
        NativeEngine.nativeLightMeter(small, input, out)
        LightReading.fromMeter(out)
      } catch (e: Exception) {
        Log.w(TAG, "light reading failed", e)
        null
      } finally {
        small.recycle()
      }
      main.post {
        lightMetering = false
        if (reading != null && camera === cam) onReading(reading)
      }
    }
  }

  /** The main face's brightness from the live beauty analysis, when it is recent. */
  private fun faceLuma(): Float? {
    val a = LiveBeautyState.analysis ?: return null
    if (a.face == null || a.scene.size < 2 || a.scene[0] < 0.5f) return null
    if (SystemClock.elapsedRealtime() - a.timestampMs > 2_000L) return null
    return a.scene[1].takeIf { it.isFinite() }?.coerceIn(0f, 1f)
  }

  private fun onReading(reading: LightReading) {
    lightSteadier.feed(reading)
    val steady = lightSteadier.current ?: return
    lightReading = steady
    updateLowLightBoost(steady)
    if (AutoLight.worthReporting(lightReported, steady)) emitLight(steady)
  }

  private fun emitLight(reading: LightReading) {
    val context = lightContext() ?: return
    lightReported = reading
    events?.emit("PhotoPlusCameraLight", mapOf(
      "plan" to reading.plan.id,
      "label" to AutoLight.label(reading, context, flash),
      "lux" to reading.lux?.toDouble(),
      "frames" to reading.frames,
      "backlit" to reading.backlit,
      "screenLight" to context.screenFlashReady,
      "lowLightBoost" to lowLightBoostOn,
    ))
  }

  private fun updateLowLightBoost(reading: LightReading) {
    val cam = camera ?: return
    if (lowLightBoostHeld || lowLightBoostFailed || sessionConfig.video) return
    if (!cam.cameraInfo.isLowLightBoostSupported) return
    val want = AutoLight.wantLowLightBoost(lowLightBoostOn, reading)
    if (want != lowLightBoostOn) setLowLightBoost(cam, want)
  }

  private fun setLowLightBoost(cam: Camera, on: Boolean) {
    val context = appContext ?: return
    lowLightBoostOn = on
    try {
      val future = cam.cameraControl.enableLowLightBoostAsync(on)
      future.addListener({
        try {
          future.get()
        } catch (e: Exception) {
          // Not available with this session (or the camera went away): stay off.
          Log.w(TAG, "low light boost unavailable", e)
          lowLightBoostOn = false
          lowLightBoostFailed = true
        }
      }, ContextCompat.getMainExecutor(context))
    } catch (e: Exception) {
      Log.w(TAG, "low light boost unavailable", e)
      lowLightBoostOn = false
      lowLightBoostFailed = true
    }
  }

  /** What the camera can do for Auto Light right now (main thread). */
  fun lightContext(): LightContext? {
    val cam = camera ?: return null
    val ev = cam.cameraInfo.exposureState
    val supported = ev.isExposureCompensationSupported
    return LightContext(
      front = lensFacing == "front",
      hasFlash = cam.cameraInfo.hasFlashUnit(),
      exposureStep = if (supported) ev.exposureCompensationStep.toFloat() else 0f,
      exposureMin = if (supported) ev.exposureCompensationRange.lower else 0,
      exposureMax = if (supported) ev.exposureCompensationRange.upper else 0,
      exposureIndex = if (supported) ev.exposureCompensationIndex else 0,
      screenFlashReady = screenFlashReady,
    )
  }

  /**
   * Readies the camera for an Auto Light capture, then runs [ready] on the
   * main thread: the screen light's colour, Low Light Boost off for a flash,
   * and the burst's exposure (waiting, briefly, for it to settle).
   */
  fun prepareLight(decision: LightDecision?, ready: () -> Unit) {
    val cam = camera
    val context = appContext
    if (decision == null || cam == null || context == null) {
      ready()
      return
    }
    lightCapturing = true
    if (decision.flash != LightFlash.NONE && lowLightBoostOn) {
      lowLightBoostHeld = true
      setLowLightBoost(cam, false)
    }
    if (decision.flash == LightFlash.SCREEN) {
      previewView?.setScreenFlashOverlayColor(android.graphics.Color.rgb(
        (decision.screenColor shr 16) and 0xFF, (decision.screenColor shr 8) and 0xFF, decision.screenColor and 0xFF,
      ))
    }
    val index = decision.exposureIndex
    if (index == null) {
      ready()
      return
    }
    exposureBeforeCapture = cam.cameraInfo.exposureState.exposureCompensationIndex
    val once = AtomicBoolean(false)
    val go = Runnable { if (once.compareAndSet(false, true)) ready() }
    val future = cam.cameraControl.setExposureCompensationIndex(index)
    future.addListener(go, ContextCompat.getMainExecutor(context))
    main.postDelayed(go, EXPOSURE_SETTLE_MS)
  }

  /** After an Auto Light capture's frames are taken: the user's exposure and Low Light Boost back. */
  fun finishLight() {
    val cam = camera
    lightCapturing = false
    exposureBeforeCapture?.let { cam?.cameraControl?.setExposureCompensationIndex(it) }
    exposureBeforeCapture = null
    if (lowLightBoostHeld) {
      lowLightBoostHeld = false
      lightReading?.let { updateLowLightBoost(it) }
    }
  }

  fun setTorch(enabled: Boolean): Boolean {
    val cam = camera ?: return false
    if (!cam.cameraInfo.hasFlashUnit()) return false
    cam.cameraControl.enableTorch(enabled)
    return true
  }

  fun setFocusPeaking(enabled: Boolean) {
    if (peakingEnabled == enabled) return
    peakingEnabled = enabled
    if (!enabled) FocusPeakingAnalyzer.clearOverlay()
    owner?.let { bindUseCases(it) }
  }

  /** Props in the camera: faces are reported as PhotoPlusCameraFaces events while enabled. */
  fun setFaceTracking(enabled: Boolean) {
    if (facesEnabled == enabled) return
    facesEnabled = enabled
    FaceTracker.sink = if (enabled) FaceTracker.Sink { faces -> events?.emit("PhotoPlusCameraFaces", mapOf("faces" to faces)) } else null
    owner?.let { bindUseCases(it) }
  }

  /** Returns the settings actually applied (clamped to the sensor's ranges). */
  fun setManual(settings: ManualSettings): ManualSettings {
    manual = settings
    return applyManualControls()
  }

  fun resetManual() {
    manual = ManualSettings()
    kelvinWaitingForTransform = false
    val cam = camera ?: return
    Camera2CameraControl.from(cam.cameraControl).clearCaptureRequestOptions()
  }

  private fun applyManualControls(): ManualSettings {
    kelvinWaitingForTransform = false
    val cam = camera ?: return manual
    val inputs = capabilityInputs ?: return manual
    val control = Camera2CameraControl.from(cam.cameraControl)
    val ranges = CameraCapabilities.manualRanges(inputs)
    val trusted = inputs.hardwareLevel != "legacy" && inputs.manualSensor
    val options = CaptureRequestOptions.Builder()
    var applied = manual

    val exposure = if (trusted && ranges != null) ManualMath.resolveExposure(manual, ranges, Live.iso, Live.exposureNs) else null
    if (exposure != null) {
      options.setCaptureRequestOption(CaptureRequest.CONTROL_AE_MODE, CameraMetadata.CONTROL_AE_MODE_OFF)
      options.setCaptureRequestOption(CaptureRequest.SENSOR_SENSITIVITY, exposure.first)
      options.setCaptureRequestOption(CaptureRequest.SENSOR_EXPOSURE_TIME, exposure.second)
      applied = applied.copy(iso = exposure.first, exposureTimeNs = exposure.second)
    } else {
      applied = applied.copy(iso = null, exposureTimeNs = null)
      if (manual.aeLock && inputs.aeLockAvailable) options.setCaptureRequestOption(CaptureRequest.CONTROL_AE_LOCK, true)
    }

    val focus = manual.focusDiopters
    if (focus != null && trusted && ranges != null && inputs.minFocusDiopters > 0f) {
      val d = ManualMath.clampFocus(focus, ranges)
      options.setCaptureRequestOption(CaptureRequest.CONTROL_AF_MODE, CameraMetadata.CONTROL_AF_MODE_OFF)
      options.setCaptureRequestOption(CaptureRequest.LENS_FOCUS_DISTANCE, d)
      applied = applied.copy(focusDiopters = d)
    } else {
      applied = applied.copy(focusDiopters = null)
    }

    when (val wb = manual.whiteBalance) {
      WhiteBalance.Auto -> if (manual.awbLock && inputs.awbLockAvailable) options.setCaptureRequestOption(CaptureRequest.CONTROL_AWB_LOCK, true)
      is WhiteBalance.Kelvin -> {
        val transform = Live.colorTransform
        kelvinWaitingForTransform = transform == null
        if (inputs.manualPostProcessing && inputs.hardwareLevel != "legacy" && transform != null) {
          val g = ManualMath.kelvinToRggbGains(wb.kelvin)
          options.setCaptureRequestOption(CaptureRequest.CONTROL_AWB_MODE, CameraMetadata.CONTROL_AWB_MODE_OFF)
          options.setCaptureRequestOption(CaptureRequest.COLOR_CORRECTION_MODE, CameraMetadata.COLOR_CORRECTION_MODE_TRANSFORM_MATRIX)
          // Keep the camera's own colour matrix so only the white point moves.
          options.setCaptureRequestOption(CaptureRequest.COLOR_CORRECTION_TRANSFORM, transform)
          options.setCaptureRequestOption(CaptureRequest.COLOR_CORRECTION_GAINS, RggbChannelVector(g[0], g[1], g[2], g[3]))
        } else {
          applied = applied.copy(whiteBalance = WhiteBalance.Auto)
        }
      }
      else -> {
        val mode = awbModeFor(wb)
        if (mode != null && inputs.awbModes.contains(wb.id)) options.setCaptureRequestOption(CaptureRequest.CONTROL_AWB_MODE, mode)
        else applied = applied.copy(whiteBalance = WhiteBalance.Auto)
      }
    }
    control.setCaptureRequestOptions(options.build())
    // The request is kept as asked: a downgrade today (e.g. Kelvin before the
    // first frame) is retried on the next apply instead of being remembered.
    return applied
  }

  private fun awbModeFor(wb: WhiteBalance): Int? = when (wb) {
    WhiteBalance.Incandescent -> CameraMetadata.CONTROL_AWB_MODE_INCANDESCENT
    WhiteBalance.Fluorescent -> CameraMetadata.CONTROL_AWB_MODE_FLUORESCENT
    WhiteBalance.Daylight -> CameraMetadata.CONTROL_AWB_MODE_DAYLIGHT
    WhiteBalance.Cloudy -> CameraMetadata.CONTROL_AWB_MODE_CLOUDY_DAYLIGHT
    WhiteBalance.Shade -> CameraMetadata.CONTROL_AWB_MODE_SHADE
    else -> null
  }

  private fun flashMode(setting: FlashSetting) = when (setting) {
    FlashSetting.ON -> ImageCapture.FLASH_MODE_ON
    FlashSetting.AUTO -> ImageCapture.FLASH_MODE_AUTO
    FlashSetting.OFF -> ImageCapture.FLASH_MODE_OFF
  }

  // ------------------------------------------------------- live metadata

  private val liveCallback = object : CameraCaptureSession.CaptureCallback() {
    override fun onCaptureCompleted(session: CameraCaptureSession, request: CaptureRequest, result: TotalCaptureResult) {
      Live.iso = result.get(CaptureResult.SENSOR_SENSITIVITY)
      Live.exposureNs = result.get(CaptureResult.SENSOR_EXPOSURE_TIME)
      Live.focusDiopters = result.get(CaptureResult.LENS_FOCUS_DISTANCE)
      Live.aperture = result.get(CaptureResult.LENS_APERTURE)
      result.get(CaptureResult.COLOR_CORRECTION_TRANSFORM)?.let {
        Live.colorTransform = it
        if (kelvinWaitingForTransform) {
          kelvinWaitingForTransform = false
          main.post { applyManualControls() }
        }
      }
      val now = SystemClock.elapsedRealtime()
      if (now - Live.lastEmit >= 250L) {
        Live.lastEmit = now
        val afState = result.get(CaptureResult.CONTROL_AF_STATE)
        main.post {
          events?.emit("PhotoPlusCameraMetadata", mapOf(
            "iso" to Live.iso,
            "exposureTimeNs" to Live.exposureNs?.toDouble(),
            "focusDiopters" to Live.focusDiopters?.toDouble(),
            "aperture" to Live.aperture?.toDouble(),
            "focusLocked" to (afState == CaptureResult.CONTROL_AF_STATE_FOCUSED_LOCKED || afState == CaptureResult.CONTROL_AF_STATE_PASSIVE_FOCUSED),
          ))
        }
      }
    }
  }

  // --------------------------------------------------- capability inputs

  private fun readCapabilityInputs(info: androidx.camera.core.CameraInfo, facing: String): CameraCapabilities.Inputs {
    val c2 = Camera2CameraInfo.from(info)
    val caps = c2.getCameraCharacteristic(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)?.toSet() ?: emptySet()
    val map = c2.getCameraCharacteristic(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
    val sizes = buildList {
      map?.getOutputSizes(ImageFormat.JPEG)?.forEach { add(PixelSize(it.width, it.height)) }
      map?.getHighResolutionOutputSizes(ImageFormat.JPEG)?.forEach { add(PixelSize(it.width, it.height)) }
    }.distinct()
    val iso = c2.getCameraCharacteristic(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE)
    val exp = c2.getCameraCharacteristic(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE)
    val awb = c2.getCameraCharacteristic(CameraCharacteristics.CONTROL_AWB_AVAILABLE_MODES)?.asList()?.mapNotNull { CameraCapabilities.awbModeName(it) } ?: emptyList()
    val level = when (c2.getCameraCharacteristic(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL)) {
      CameraMetadata.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY -> "legacy"
      CameraMetadata.INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED -> "limited"
      CameraMetadata.INFO_SUPPORTED_HARDWARE_LEVEL_FULL -> "full"
      CameraMetadata.INFO_SUPPORTED_HARDWARE_LEVEL_3 -> "level3"
      CameraMetadata.INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL -> "external"
      else -> "unknown"
    }
    val zoom = info.zoomState.value
    val ev = info.exposureState
    val centre = SurfaceOrientedMeteringPointFactory(1f, 1f).createPoint(0.5f, 0.5f)
    val rawSupported = ImageCapture.getImageCaptureCapabilities(info).supportedOutputFormats.contains(ImageCapture.OUTPUT_FORMAT_RAW_JPEG)
    return CameraCapabilities.Inputs(
      lensFacing = facing,
      hardwareLevel = level,
      zoomMin = zoom?.minZoomRatio ?: 1f,
      zoomMax = zoom?.maxZoomRatio ?: 1f,
      exposureSupported = ev.isExposureCompensationSupported,
      exposureMinIndex = ev.exposureCompensationRange.lower,
      exposureMaxIndex = ev.exposureCompensationRange.upper,
      exposureStep = ev.exposureCompensationStep.toFloat(),
      hasFlash = info.hasFlashUnit(),
      focusMeteringSupported = info.isFocusMeteringSupported(FocusMeteringAction.Builder(centre).build()),
      manualSensor = caps.contains(CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR),
      manualPostProcessing = caps.contains(CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING),
      isoRange = iso?.let { it.lower to it.upper },
      exposureRangeNs = exp?.let { it.lower to it.upper },
      minFocusDiopters = c2.getCameraCharacteristic(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE) ?: 0f,
      awbModes = awb,
      aeLockAvailable = c2.getCameraCharacteristic(CameraCharacteristics.CONTROL_AE_LOCK_AVAILABLE) == true,
      awbLockAvailable = c2.getCameraCharacteristic(CameraCharacteristics.CONTROL_AWB_LOCK_AVAILABLE) == true,
      rawSupported = rawSupported,
      jpegSizes = sizes,
    )
  }

  private fun stateName(type: CameraState.Type) = when (type) {
    CameraState.Type.PENDING_OPEN -> "pendingOpen"
    CameraState.Type.OPENING -> "opening"
    CameraState.Type.OPEN -> "open"
    CameraState.Type.CLOSING -> "closing"
    CameraState.Type.CLOSED -> "closed"
  }

  private fun errorName(code: Int) = when (code) {
    CameraState.ERROR_MAX_CAMERAS_IN_USE -> "MAX_CAMERAS_IN_USE"
    CameraState.ERROR_CAMERA_IN_USE -> "CAMERA_IN_USE"
    CameraState.ERROR_OTHER_RECOVERABLE_ERROR -> "RECOVERABLE_ERROR"
    CameraState.ERROR_STREAM_CONFIG -> "STREAM_CONFIG"
    CameraState.ERROR_CAMERA_DISABLED -> "CAMERA_DISABLED"
    CameraState.ERROR_CAMERA_FATAL_ERROR -> "CAMERA_FATAL_ERROR"
    CameraState.ERROR_DO_NOT_DISTURB_MODE_ENABLED -> "DO_NOT_DISTURB"
    else -> "ERROR_$code"
  }
}
