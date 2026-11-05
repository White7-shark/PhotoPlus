package com.photoplus.engine

import android.graphics.Bitmap
import android.os.SystemClock
import androidx.core.graphics.createBitmap
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.device.DeviceInfo
import com.photoplus.device.MemoryPressureMonitor
import com.photoplus.specs.NativePhotoPlusEngineSpec
import com.photoplus.studio.PortraitParams
import com.photoplus.storage.AppPaths
import com.photoplus.util.emitSafely
import com.photoplus.util.toWritableMap
import java.io.File
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.ThreadFactory
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

@ReactModule(name = EngineModule.NAME)
class EngineModule(reactContext: ReactApplicationContext) :
  NativePhotoPlusEngineSpec(reactContext), MemoryPressureMonitor.Listener {

  private val paths = AppPaths(reactContext)
  private val budget = MemoryBudgetPolicy.forTotalRam(
    DeviceInfo.totalRamBytes(reactContext),
    DeviceInfo.isLowRamDevice(reactContext),
  )
  private val engineAvailable = NativeEngine.ensureLoaded()
  private val portraitProcessor by lazy { com.photoplus.studio.PortraitProcessor() }
  private val jobs = JobManager(createExecutor(budget.workers), NativeCancelTokens)
  private val ioExecutor = ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS, LinkedBlockingQueue(), namedThreads("pp-io")).apply {
    allowCoreThreadTimeOut(true)
  }

  init {
    if (engineAvailable) NativeEngine.nativeSetMemoryBudget(budget.engineBytes)
    MemoryPressureMonitor.addListener(this)
  }

  override fun getName() = NAME

  override fun getEngineInfo(promise: Promise) {
    promise.resolve(
      mapOf(
        "available" to engineAvailable,
        "version" to if (engineAvailable) NativeEngine.nativeVersion() else null,
        "loadError" to NativeEngine.loadError?.message,
        "workers" to budget.workers,
        "memoryBudgetBytes" to budget.engineBytes.toDouble(),
        "maxDecodeDimension" to budget.maxDecodeDimension,
        "operations" to listOf("tone", "grayscale", "rotate", "flip", "crop", "resize", "sharpen", "saturation"),
        "outputFormats" to OutputFormat.entries.map { it.name.lowercase() },
      ).toWritableMap(),
    )
  }

  override fun process(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val parsed = try {
      ProcessRequest.fromMap(request.toHashMap())
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val input: File
    val output: File
    try {
      input = paths.resolve(parsed.inputPath)
      output = paths.resolve(parsed.outputPath)
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }

    jobs.submit(jobId, object : JobListener<Map<String, Any?>> {
      override fun onStateChanged(jobId: String, state: JobState) {
        reactApplicationContext.emitSafely(EVENT_JOB_STATE, mapOf("jobId" to jobId, "state" to state.name.lowercase()))
      }
      override fun onSuccess(jobId: String, result: Map<String, Any?>) = promise.resolve(result.toWritableMap())
      override fun onFailure(jobId: String, error: EngineException) = promise.reject(error.code, error.message, error)
    }) { token -> runJob(parsed, input, output, token) }
  }

  private fun runJob(request: ProcessRequest, input: File, output: File, token: Long): Map<String, Any?> {
    val started = SystemClock.elapsedRealtime()
    val maxDim = minOf(request.maxDimension, budget.maxDecodeDimension)
    val decoded = ImageCodec.decode(input, maxDim)
    var result: Bitmap? = null
    var handle = 0L
    try {
      check(!decoded.bitmap.hasAlpha() || !decoded.bitmap.isPremultiplied) { "Engine input must be unpremultiplied" }
      val ops = request.operationsForDecodeScale(decoded.scale)
      val decodeMs = SystemClock.elapsedRealtime() - started
      handle = NativeEngine.nativeProcess(decoded.bitmap, request.typeIds(ops), request.packedParams(ops), token)
      val width = NativeEngine.nativeResultWidth(handle)
      val height = NativeEngine.nativeResultHeight(handle)
      // Engine output is straight alpha; mark the bitmap so Android does not
      // reinterpret it as premultiplied when encoding.
      result = createBitmap(width, height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
      NativeEngine.nativeCopyResultToBitmap(handle, result)
      NativeEngine.nativeReleaseResult(handle)
      handle = 0L
      val processMs = SystemClock.elapsedRealtime() - started - decodeMs
      ImageCodec.encode(result, output, request.format, request.quality)
      return mapOf(
        "outputPath" to output.absolutePath,
        "width" to width,
        "height" to height,
        "sourceWidth" to decoded.sourceWidth,
        "sourceHeight" to decoded.sourceHeight,
        "format" to request.format.name.lowercase(),
        "sizeBytes" to output.length().toDouble(),
        "timings" to mapOf(
          "decodeMs" to decodeMs.toDouble(),
          "processMs" to processMs.toDouble(),
          "totalMs" to (SystemClock.elapsedRealtime() - started).toDouble(),
        ),
      )
    } finally {
      if (handle != 0L) NativeEngine.nativeReleaseResult(handle)
      result?.recycle()
      decoded.bitmap.recycle()
    }
  }

  /**
   * Restores an existing photo (Enhance / Super-res on a saved image): one
   * frame, decoded straight into the engine's input, written in place into
   * the output bitmap. Resolves with the engine's before/after report.
   */
  override fun enhance(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val input: File
    val output: File
    try {
      input = paths.resolve(map["inputPath"] as? String ?: "")
      output = paths.resolve(map["outputPath"] as? String ?: "")
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val upscale = if ((map["upscale"] as? Number)?.toInt() == 2) 2 else 1
    fun unit(key: String, default: Float) = ((map[key] as? Number)?.toFloat() ?: default).coerceIn(0f, 1f)
    val params = floatArrayOf(unit("denoise", 1f), unit("sharpen", 1f), unit("detail", 0.35f), unit("vibrance", 0.25f))
    val autoTone = map["autoTone"] as? Boolean ?: true
    val requestedMax = (map["maxDimension"] as? Number)?.toInt() ?: budget.maxDecodeDimension
    // Output size is bounded by the budget's decode limit whatever the upscale.
    val maxDim = minOf(requestedMax, budget.maxDecodeDimension) / upscale

    jobs.submit(jobId, object : JobListener<Map<String, Any?>> {
      override fun onStateChanged(jobId: String, state: JobState) {
        reactApplicationContext.emitSafely(EVENT_JOB_STATE, mapOf("jobId" to jobId, "state" to state.name.lowercase()))
      }
      override fun onSuccess(jobId: String, result: Map<String, Any?>) = promise.resolve(result.toWritableMap())
      override fun onFailure(jobId: String, error: EngineException) = promise.reject(error.code, error.message, error)
    }) { token ->
      val started = SystemClock.elapsedRealtime()
      val decoded = ImageCodec.decode(input, maxDim)
      var result: Bitmap? = null
      try {
        val frame = decoded.bitmap
        result = createBitmap(frame.width * upscale, frame.height * upscale, Bitmap.Config.ARGB_8888).apply {
          isPremultiplied = false
          setHasAlpha(false)
        }
        val report = FloatArray(7)
        NativeEngine.nativeEnhance(arrayOf(frame), upscale, params, autoTone, result, token, report)
        val after = NativeEngine.nativeAnalyzeQuality(result)
        ImageCodec.encode(result, output, OutputFormat.JPEG, (map["quality"] as? Number)?.toInt()?.coerceIn(50, 100) ?: 95)
        mapOf(
          "outputPath" to output.absolutePath,
          "width" to result.width,
          "height" to result.height,
          "sourceWidth" to decoded.sourceWidth,
          "sourceHeight" to decoded.sourceHeight,
          "sizeBytes" to output.length().toDouble(),
          "report" to mapOf(
            "noiseBefore" to report[3].toDouble(),
            "noiseAfter" to report[4].toDouble(),
            "blurBefore" to report[5].toDouble(),
            "deblurSigma" to report[6].toDouble(),
            "sharpnessAfter" to after[2].toDouble(),
            "upscale" to upscale,
          ),
          "timings" to mapOf("totalMs" to (SystemClock.elapsedRealtime() - started).toDouble()),
        )
      } finally {
        result?.recycle()
        decoded.bitmap.recycle()
      }
    }
  }

  /**
   * Spotlight on a saved photo: optional enhance, then the chosen subject
   * stands out (taps are in the photo as displayed), with natural skin
   * retouching. Masks come from on-device models; see PortraitProcessor.
   */
  override fun portrait(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val io = resolveIo(map, promise) ?: return
    val params = PortraitParams.fromMap(map)
    val points = PortraitParams.points(map["points"])
    val enhanceFirst = map["enhance"] as? Boolean ?: true
    val look = lookOf(map["look"])
    val lookStrength = ((map["lookStrength"] as? Number)?.toFloat() ?: 0.8f).coerceIn(0f, 1f)
    val maxDim = minOf((map["maxDimension"] as? Number)?.toInt() ?: budget.maxDecodeDimension, budget.maxDecodeDimension, 4096)
    jobs.submit(jobId, jobListener(promise)) { token ->
      val started = SystemClock.elapsedRealtime()
      val decoded = ImageCodec.decode(io.first, maxDim)
      var enhanced: Bitmap? = null
      try {
        var work = decoded.bitmap
        if (enhanceFirst) {
          enhanced = createBitmap(work.width, work.height, Bitmap.Config.ARGB_8888).apply {
            isPremultiplied = false
            setHasAlpha(false)
          }
          NativeEngine.nativeEnhance(arrayOf(work), 1, floatArrayOf(1f, 1f, 0.35f, 0.25f), true, enhanced, token, FloatArray(7))
          work = enhanced
        } else if (!work.isMutable) {
          work = work.copy(Bitmap.Config.ARGB_8888, true)
          enhanced = work
        }
        // Decoding applied EXIF orientation, so the bitmap is already upright.
        val r = portraitProcessor.apply(work, 0, points, params, token)
        look?.let { NativeEngine.nativeStyleApply(work, it, lookStrength, token) }
        ImageCodec.encode(work, io.second, OutputFormat.JPEG, 95)
        mapOf(
          "outputPath" to io.second.absolutePath,
          "width" to work.width,
          "height" to work.height,
          "sizeBytes" to io.second.length().toDouble(),
          "portrait" to mapOf("people" to r.people, "spots" to r.spots, "skin" to r.skinCoverage.toDouble(), "subject" to r.subjectCoverage.toDouble()),
          "timings" to mapOf("totalMs" to (SystemClock.elapsedRealtime() - started).toDouble()),
        )
      } finally {
        enhanced?.recycle()
        decoded.bitmap.recycle()
      }
    }
  }

  /** Applies a look ("Vibe Match") to a saved photo. */
  override fun applyLook(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val io = resolveIo(map, promise) ?: return
    val look = lookOf(map["look"])
    if (look == null) {
      promise.reject(EngineException.INVALID_ARGUMENT, "Choose a look first")
      return
    }
    val strength = ((map["strength"] as? Number)?.toFloat() ?: 0.8f).coerceIn(0f, 1f)
    jobs.submit(jobId, jobListener(promise)) { token ->
      val decoded = ImageCodec.decode(io.first, minOf(budget.maxDecodeDimension, 6144))
      val work = if (decoded.bitmap.isMutable) decoded.bitmap else decoded.bitmap.copy(Bitmap.Config.ARGB_8888, true)
      try {
        NativeEngine.nativeStyleApply(work, look, strength, token)
        ImageCodec.encode(work, io.second, OutputFormat.JPEG, 95)
        mapOf("outputPath" to io.second.absolutePath, "width" to work.width, "height" to work.height, "sizeBytes" to io.second.length().toDouble())
      } finally {
        if (work !== decoded.bitmap) work.recycle()
        decoded.bitmap.recycle()
      }
    }
  }

  /** Measures a reference photo's look, plus camera hints (brightness, warmth). */
  override fun analyzeLook(path: String, promise: Promise) {
    if (!requireEngine(promise)) return
    ioExecutor.execute {
      try {
        val decoded = ImageCodec.decode(paths.resolve(path), 1024)
        try {
          val look = NativeEngine.nativeStyleAnalyze(decoded.bitmap)
          promise.resolve(mapOf("look" to look.map { it.toDouble() }).toWritableMap())
        } finally {
          decoded.bitmap.recycle()
        }
      } catch (e: EngineException) {
        promise.reject(e.code, e.message, e)
      } catch (e: Exception) {
        promise.reject(EngineException.INTERNAL, e.message, e)
      }
    }
  }

  private fun resolveIo(map: Map<String, Any?>, promise: Promise): Pair<File, File>? = try {
    paths.resolve(map["inputPath"] as? String ?: "") to paths.resolve(map["outputPath"] as? String ?: "")
  } catch (e: EngineException) {
    promise.reject(e.code, e.message, e)
    null
  }

  private fun lookOf(raw: Any?): FloatArray? =
    (raw as? List<*>)?.mapNotNull { (it as? Number)?.toFloat() }
      ?.takeIf { it.size == NativeEngine.STYLE_FLOATS && it.all { v -> v.isFinite() } }
      ?.toFloatArray()

  private fun jobListener(promise: Promise) = object : JobListener<Map<String, Any?>> {
    override fun onStateChanged(jobId: String, state: JobState) {
      reactApplicationContext.emitSafely(EVENT_JOB_STATE, mapOf("jobId" to jobId, "state" to state.name.lowercase()))
    }
    override fun onSuccess(jobId: String, result: Map<String, Any?>) = promise.resolve(result.toWritableMap())
    override fun onFailure(jobId: String, error: EngineException) = promise.reject(error.code, error.message, error)
  }

  /** Measures noise, blur and exposure of a photo (drives the "Enhance suggested" hint). */
  override fun analyzeQuality(path: String, promise: Promise) {
    if (!requireEngine(promise)) return
    ioExecutor.execute {
      try {
        val decoded = ImageCodec.decode(paths.resolve(path), ANALYSIS_DIMENSION)
        try {
          val q = NativeEngine.nativeAnalyzeQuality(decoded.bitmap)
          promise.resolve(
            mapOf(
              "noise" to q[0].toDouble(),
              // At the analysis size (longest edge ANALYSIS_DIMENSION): softness
              // as seen on screen. Scaling up to source pixels would also scale
              // the estimator's ~0.5 px floor and flag every large photo as soft.
              "blurPx" to q[1].toDouble(),
              "sharpness" to q[2].toDouble(),
              "meanLuma" to q[3].toDouble(),
              "highlightsClipped" to q[4].toDouble(),
              "shadowsClipped" to q[5].toDouble(),
            ).toWritableMap(),
          )
        } finally {
          decoded.bitmap.recycle()
        }
      } catch (e: EngineException) {
        promise.reject(e.code, e.message, e)
      } catch (e: Exception) {
        promise.reject(EngineException.INTERNAL, e.message, e)
      }
    }
  }

  override fun cancel(jobId: String) {
    jobs.cancel(jobId)
  }

  override fun cancelAll() {
    jobs.cancelAll()
  }

  override fun getMemoryStats(promise: Promise) {
    if (!requireEngine(promise)) return
    val s = NativeEngine.nativeMemoryStats()
    promise.resolve(
      mapOf(
        "currentBytes" to s[0].toDouble(),
        "peakBytes" to s[1].toDouble(),
        "budgetBytes" to s[2].toDouble(),
        "liveAllocations" to s[3].toDouble(),
        "totalAllocations" to s[4].toDouble(),
        "failedAllocations" to s[5].toDouble(),
        "activeJobs" to jobs.activeJobCount,
      ).toWritableMap(),
    )
  }

  override fun getProfileReport(promise: Promise) {
    if (!requireEngine(promise)) return
    promise.resolve(NativeEngine.nativeProfileReport())
  }

  override fun resetProfiler() {
    if (engineAvailable) NativeEngine.nativeResetProfiler()
  }

  override fun resetMemoryPeak() {
    if (engineAvailable) NativeEngine.nativeResetMemoryPeak()
  }

  override fun generateTestImage(width: Double, height: Double, promise: Promise) {
    val w = width.toInt()
    val h = height.toInt()
    if (w !in 16..8192 || h !in 16..8192) {
      promise.reject(EngineException.INVALID_ARGUMENT, "Test image size must be between 16 and 8192")
      return
    }
    ioExecutor.execute {
      try {
        paths.testImages.mkdirs()
        val file = File(paths.testImages, "test_${w}x${h}.jpg")
        ImageCodec.generateTestImage(file, w, h)
        promise.resolve(file.absolutePath)
      } catch (e: EngineException) {
        promise.reject(e.code, e.message, e)
      } catch (e: Exception) {
        promise.reject(EngineException.INTERNAL, e.message, e)
      }
    }
  }

  override fun readImageMetadata(path: String, promise: Promise) {
    ioExecutor.execute {
      try {
        promise.resolve(ImageCodec.readMetadata(paths.resolve(path)).toWritableMap())
      } catch (e: EngineException) {
        promise.reject(e.code, e.message, e)
      } catch (e: Exception) {
        promise.reject(EngineException.IO_ERROR, e.message, e)
      }
    }
  }

  override fun addListener(eventName: String) = Unit
  override fun removeListeners(count: Double) = Unit

  override fun onMemoryPressure(level: MemoryPressureMonitor.Level) {
    if (!engineAvailable) return
    val applied = when (level) {
      MemoryPressureMonitor.Level.NORMAL -> budget
      MemoryPressureMonitor.Level.MODERATE -> MemoryBudgetPolicy.underPressure(budget, severe = false)
      MemoryPressureMonitor.Level.CRITICAL -> MemoryBudgetPolicy.underPressure(budget, severe = true)
    }
    NativeEngine.nativeSetMemoryBudget(applied.engineBytes)
  }

  override fun invalidate() {
    MemoryPressureMonitor.removeListener(this)
    jobs.shutdown()
    ioExecutor.shutdown()
    super.invalidate()
  }

  private fun requireEngine(promise: Promise): Boolean {
    if (engineAvailable) return true
    promise.reject(EngineException.INTERNAL, "Processing engine is unavailable on this device: ${NativeEngine.loadError?.message}")
    return false
  }

  companion object {
    const val NAME = "PhotoPlusEngine"
    const val EVENT_JOB_STATE = "PhotoPlusJobState"
    private const val ANALYSIS_DIMENSION = 1600

    private fun namedThreads(prefix: String): ThreadFactory {
      val counter = AtomicInteger()
      return ThreadFactory { r ->
        Thread(r, "$prefix-${counter.incrementAndGet()}").apply {
          priority = Thread.NORM_PRIORITY - 1 // keep the UI thread responsive
        }
      }
    }

    private fun createExecutor(workers: Int) =
      ThreadPoolExecutor(workers, workers, 30, TimeUnit.SECONDS, LinkedBlockingQueue(), namedThreads("pp-engine")).apply {
        allowCoreThreadTimeOut(true)
      }
  }
}
