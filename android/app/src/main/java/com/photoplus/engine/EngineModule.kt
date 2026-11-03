package com.photoplus.engine

import android.graphics.Bitmap
import android.os.SystemClock
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.device.DeviceInfo
import com.photoplus.device.MemoryPressureMonitor
import com.photoplus.specs.NativePhotoPlusEngineSpec
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
      result = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
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
