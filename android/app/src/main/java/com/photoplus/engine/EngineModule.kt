package com.photoplus.engine

import android.graphics.Bitmap
import android.os.SystemClock
import androidx.core.graphics.createBitmap
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.module.annotations.ReactModule
import android.content.ContentValues
import android.provider.MediaStore
import com.photoplus.device.DeviceInfo
import com.photoplus.edit.EditDocument
import com.photoplus.edit.EditRenderer
import com.photoplus.edit.ExportPolicy
import com.photoplus.edit.Plan
import com.photoplus.edit.ResolvedWatermark
import com.photoplus.edit.WatermarkSpec
import com.photoplus.entitlement.Entitlements
import com.photoplus.studio.FaceFinder
import com.photoplus.device.MemoryPressureMonitor
import com.photoplus.specs.NativePhotoPlusEngineSpec
import com.photoplus.studio.PortraitParams
import com.photoplus.storage.AppPaths
import com.photoplus.edit.FrameStyles
import com.photoplus.edit.CustomProps
import com.photoplus.util.emitSafely
import com.photoplus.util.toWritableMap
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.ThreadFactory
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

@ReactModule(name = EngineModule.NAME)
class EngineModule(reactContext: ReactApplicationContext) :
  NativePhotoPlusEngineSpec(reactContext), MemoryPressureMonitor.Listener {

  private val paths = AppPaths(reactContext)
  private val adRewards = com.photoplus.ads.AdRewards.preferences(reactContext)
  private val budget = MemoryBudgetPolicy.forTotalRam(
    DeviceInfo.totalRamBytes(reactContext),
    DeviceInfo.isLowRamDevice(reactContext),
  )
  private val engineAvailable = NativeEngine.ensureLoaded()
  private val portraitProcessor by lazy { com.photoplus.studio.PortraitProcessor() }
  private val jobs = JobManager(
    createExecutor(budget.workers),
    NativeCancelTokens,
    Executors.newSingleThreadScheduledExecutor(namedThreads("pp-progress")),
  )
  private val ioExecutor = ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS, LinkedBlockingQueue(), namedThreads("pp-io")).apply {
    allowCoreThreadTimeOut(true)
  }

  /** GPU status from the start-up self-test (null until it has run). */
  @Volatile private var gpu: Array<String>? = null

  init {
    if (engineAvailable) {
      NativeEngine.nativeSetMemoryBudget(budget.engineBytes)
      // The GPU self-test compiles shaders and compares against the CPU, so it
      // runs off the UI thread; previews use the CPU until it has passed.
      startGpu()
    }
    MemoryPressureMonitor.addListener(this)
  }

  private fun startGpu() = ioExecutor.execute { gpu = runCatching { NativeEngine.nativeGpuInit() }.getOrNull() }

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
        "operations" to OPERATIONS,
        "outputFormats" to OutputFormat.entries.map { it.name.lowercase() },
        "gpu" to gpu.let { g ->
          mapOf(
            "ready" to (g?.getOrNull(1) == "true"),
            "checked" to (g != null),
            "renderer" to g?.getOrNull(3)?.takeIf { it.isNotEmpty() },
          )
        },
        "models" to com.photoplus.ml.ModelRuntime.stats(),
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

    jobs.submit(jobId, jobListener(promise)) { token -> runJob(parsed, input, output, token) }
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

    jobs.submit(jobId, jobListener(promise)) { token ->
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
        val clean = markIfFree(map, output)
        mapOf(
          "outputPath" to output.absolutePath,
          "cleanPath" to clean,
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
        val clean = markIfFree(map, io.second)
        mapOf(
          "outputPath" to io.second.absolutePath,
          "cleanPath" to clean,
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

  /**
   * Transform: the photo restored as if a high-end lens had taken it (colour
   * fringes, soft corners, lens blur, noise), optionally at twice the
   * detail, then its effects re-applied on top (Spotlight with the same taps
   * and settings, Vibe Match with the same look), so nothing the person chose
   * is lost. `keepStyle` is for photos whose effects are already baked in:
   * their colours are left exactly as they are.
   */
  override fun transform(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val io = resolveIo(map, promise) ?: return
    val upscale = if ((map["upscale"] as? Number)?.toInt() == 2) 2 else 1
    val keepStyle = map["keepStyle"] as? Boolean ?: false
    @Suppress("UNCHECKED_CAST")
    val spotlight = map["spotlight"] as? Map<String, Any?>
    val beauty = map["beauty"] as? Map<*, *>
    val look = lookOf(map["look"])
    val lookStrength = ((map["lookStrength"] as? Number)?.toFloat() ?: 0.8f).coerceIn(0f, 1f)
    // The output (not the input) is what the budget bounds.
    val maxOut = minOf((map["maxDimension"] as? Number)?.toInt() ?: budget.maxDecodeDimension, budget.maxDecodeDimension, 4096)
    jobs.submit(jobId, jobListener(promise)) { token ->
      val started = SystemClock.elapsedRealtime()
      val decoded = ImageCodec.decode(io.first, maxOut / upscale)
      var output: Bitmap? = null
      try {
        val src = decoded.bitmap
        output = createBitmap(src.width * upscale, src.height * upscale, Bitmap.Config.ARGB_8888).apply {
          isPremultiplied = false // the engine writes straight alpha
          setHasAlpha(false)
        }
        val report = FloatArray(NativeEngine.TRANSFORM_REPORT_SIZE)
        val params = floatArrayOf(upscale.toFloat(), if (keepStyle) 0f else 1f, 1f, if (keepStyle) 0.2f else 0.4f)
        NativeEngine.nativeTransform(src, output, params, token, report)
        decoded.bitmap.recycle() // free the input before the effects run
        // Decoding applied EXIF orientation, so the bitmap is already upright.
        val portrait = spotlight?.let {
          portraitProcessor.apply(output, 0, PortraitParams.points(it["points"]), PortraitParams.fromMap(it), token)
        } ?: beauty?.let { b ->
          fun unit(k: String) = (b[k] as? Number)?.toFloat()?.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f
          portraitProcessor.apply(
            output, 0, emptyList(), PortraitParams(blur = 0f, retouch = unit("retouch"), brighten = unit("brighten"), pop = 0f),
            token, com.photoplus.camera.CaptureOptions.beautyTuning(b),
          )
        }
        look?.let { NativeEngine.nativeStyleApply(output, it, lookStrength, token) }
        ImageCodec.encode(output, io.second, OutputFormat.JPEG, 95)
        val clean = markIfFree(map, io.second)
        mapOf(
          "outputPath" to io.second.absolutePath,
          "cleanPath" to clean,
          "width" to output.width,
          "height" to output.height,
          "sizeBytes" to io.second.length().toDouble(),
          "transform" to mapOf(
            "fringesFixedPx" to report[0].toDouble(),
            "deblur" to report[1].toDouble(),
            "deblurCorners" to report[2].toDouble(),
            "noiseBefore" to report[3].toDouble(),
            "noiseAfter" to report[4].toDouble(),
            "gradeBefore" to report[5].toDouble(),
            "upscale" to upscale,
          ),
          "portrait" to portrait?.let {
            mapOf("people" to it.people, "spots" to it.spots, "skin" to it.skinCoverage.toDouble(), "subject" to it.subjectCoverage.toDouble())
          },
          "timings" to mapOf("totalMs" to (SystemClock.elapsedRealtime() - started).toDouble()),
        )
      } finally {
        output?.recycle()
        if (!decoded.bitmap.isRecycled) decoded.bitmap.recycle()
      }
    }
  }

  /**
   * What the camera's lens left in a photo. Read at full resolution (lens
   * softness is a matter of single pixels), within the device's budget.
   */
  override fun analyzeLens(path: String, promise: Promise) {
    if (!requireEngine(promise)) return
    ioExecutor.execute {
      try {
        val decoded = ImageCodec.decode(paths.resolve(path), minOf(budget.maxDecodeDimension, 4096))
        try {
          val r = FloatArray(NativeEngine.LENS_REPORT_SIZE)
          NativeEngine.nativeLensAnalyze(decoded.bitmap, 0L, r)
          promise.resolve(
            mapOf(
              "detailBlur" to r[0].toDouble(),
              "cornerBlur" to r[1].toDouble(),
              "fringePx" to r[2].toDouble(),
              "noise" to r[3].toDouble(),
              "grade" to r[4].toDouble(),
              "reliable" to (r[6] > 0.5f),
              "width" to decoded.bitmap.width,
              "height" to decoded.bitmap.height,
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

  // ---- Editor ---------------------------------------------------------------

  /**
   * Photos saved to the library on the free plan carry the Photo Plus mark
   * (the user's chosen style), with a clean companion kept for editing.
   * Returns the clean path, or null when no mark applies.
   */
  private fun markIfFree(map: Map<String, Any?>, file: File): String? {
    if (map["watermark"] == null) return null
    return com.photoplus.edit.PhotoWatermark.applyToFile(file, watermarkOf(map), Entitlements.plan)
  }

  private val editRenderer by lazy { EditRenderer(reactApplicationContext.assets, paths.customProps) { portraitProcessor } }
  private val previewLock = Any()
  private var previewSource: Pair<String, Bitmap>? = null

  /** The decoded photo for previews, kept while the same photo is being edited. */
  private fun previewBitmap(file: File, maxDim: Int): Bitmap = synchronized(previewLock) {
    val key = "${file.absolutePath}|${file.lastModified()}|$maxDim"
    previewSource?.takeIf { it.first == key && !it.second.isRecycled }?.let { return it.second }
    previewSource?.second?.recycle()
    val decoded = ImageCodec.decode(file, maxDim).bitmap
    previewSource = key to decoded
    decoded
  }

  @Suppress("UNCHECKED_CAST")
  private fun docOf(map: Map<String, Any?>) = EditDocument.fromMap(map["doc"] as? Map<String, Any?>)

  @Suppress("UNCHECKED_CAST")
  private fun watermarkOf(map: Map<String, Any?>) = WatermarkSpec.fromMap(map["watermark"] as? Map<String, Any?>)

  /**
   * Renders a photo with its edits: previews (small, cached stages) and
   * copies saved to the library. With `watermark`, shows the mark exactly as
   * an export would draw it for the current plan.
   */
  override fun renderEdit(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val io = resolveIo(map, promise) ?: return
    val doc: EditDocument
    val mark: ResolvedWatermark?
    try {
      doc = docOf(map)
      mark = if (map["watermark"] != null) ExportPolicy.resolve(watermarkOf(map), Entitlements.plan) else null
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val preview = map["preview"] as? Boolean ?: true
    val maxDim = minOf((map["maxDimension"] as? Number)?.toInt() ?: if (preview) 1280 else budget.maxDecodeDimension, budget.maxDecodeDimension, 8192)
    val skipProps = (map["skipProps"] as? List<*>)?.mapNotNull { (it as? Number)?.toInt() }?.toSet() ?: emptySet()
    val skipFrame = map["skipFrame"] as? Boolean ?: false
    // Saving to the library on the free plan: the copy carries the mark and a
    // clean companion is kept for editing again (as with camera photos).
    val cleanOut = (map["cleanOutputPath"] as? String)?.let {
      try {
        paths.resolve(it)
      } catch (e: EngineException) {
        promise.reject(e.code, e.message, e)
        return
      }
    }
    val plan = Entitlements.plan
    val bake = !preview && cleanOut != null && plan == Plan.FREE
    val saveMark = if (bake) ExportPolicy.resolve(watermarkOf(map), plan) else null
    jobs.submit(jobId, jobListener(promise)) { token ->
      val started = SystemClock.elapsedRealtime()
      val source = if (preview) previewBitmap(io.first, maxDim) else ImageCodec.decode(io.first, maxDim).bitmap
      try {
        val r = editRenderer.render(
          source, doc,
          EditRenderer.Options(
            skipProps, skipFrame, if (preview) mark else null, maxDim,
            cacheKey = if (preview) io.first.absolutePath else null,
            preview = preview,
          ),
          token,
        )
        try {
          var cleanPath: String? = null
          if (saveMark != null && cleanOut != null) {
            ImageCodec.encode(r.bitmap, cleanOut, OutputFormat.JPEG, 95)
            cleanPath = cleanOut.absolutePath
            val visible = com.photoplus.edit.WatermarkRenderer.apply(r.bitmap, saveMark)
            ExportPolicy.verify(saveMark, plan, visible, r.bitmap.width, r.bitmap.height)
          }
          ImageCodec.encode(r.bitmap, io.second, OutputFormat.JPEG, if (preview) 88 else 95)
          mapOf(
            "outputPath" to io.second.absolutePath,
            "cleanPath" to cleanPath,
            "width" to r.bitmap.width,
            "height" to r.bitmap.height,
            "sizeBytes" to io.second.length().toDouble(),
            "photoRect" to r.photoRect.map { it.toDouble() },
            "portrait" to r.portrait?.let {
              mapOf(
                "people" to it.people,
                "spots" to it.spots,
                "skin" to it.skinCoverage.toDouble(),
                "subject" to it.subjectCoverage.toDouble(),
                "skinToneApplied" to it.skinToneApplied,
              )
            },
            "timings" to mapOf("totalMs" to (SystemClock.elapsedRealtime() - started).toDouble()),
          )
        } finally {
          r.bitmap.recycle()
        }
      } finally {
        if (!preview) source.recycle()
      }
    }
  }

  private val glideProcessor by lazy { com.photoplus.video.GlideProcessor() }

  /**
   * Glide: a recorded clip made steady, clean and sharp, with optional
   * Spotlight and Glow. Free-plan clips carry the mark in every frame, as
   * free photos do. Progress arrives as job progress events (with a stage).
   */
  override fun glide(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val io = resolveIo(map, promise) ?: return
    val options = com.photoplus.video.GlideOptions.fromMap(map)
    val mark = if (map["watermark"] != null) ExportPolicy.resolve(watermarkOf(map), Entitlements.plan) else null
    jobs.submit(jobId, jobListener(promise)) { token ->
      val r = glideProcessor.process(io.first, io.second, options, token, { stage, fraction ->
        reactApplicationContext.emitSafely(
          EVENT_JOB_PROGRESS, mapOf("jobId" to jobId, "progress" to fraction, "stage" to stage),
        )
      }) { frame -> mark?.let { com.photoplus.edit.WatermarkRenderer.apply(frame, it) } }
      mapOf(
        "outputPath" to io.second.absolutePath,
        "width" to r.width,
        "height" to r.height,
        "frames" to r.frames,
        "fps" to r.fps.toDouble(),
        "durationMs" to r.durationMs.toDouble(),
        "zoom" to r.zoom.toDouble(),
        "shakeBefore" to r.shakeBefore.toDouble(),
        "shakeAfter" to r.shakeAfter.toDouble(),
        "processMs" to r.processMs.toDouble(),
        "watermarked" to (mark != null),
      )
    }
  }

  /**
   * Everything that leaves the app goes through here. The plan is read
   * natively: on the free plan the Photo Plus mark is always drawn (whatever
   * the request says) and verified visible before the file is written.
   * Writes to the exports folder (the only place sharing accepts files from)
   * and, for the gallery, into Pictures/Photo Plus.
   */
  override fun exportImage(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val input = try {
      paths.resolve(map["inputPath"] as? String ?: "")
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val plan = Entitlements.plan
    // Free photos unlocked with rewarded ads (counted natively) export clean.
    val adUnlocked = plan == Plan.FREE && adRewards.isUnlocked(input)
    val doc: EditDocument
    val mark: ResolvedWatermark?
    val format: OutputFormat
    try {
      doc = docOf(map)
      mark = ExportPolicy.resolveForExport(watermarkOf(map), plan, adUnlocked)
      format = OutputFormat.parse(map["format"] as? String)
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val quality = ((map["quality"] as? Number)?.toInt() ?: 92).coerceIn(60, 100)
    val toGallery = map["destination"] == "gallery"
    jobs.submit(jobId, jobListener(promise)) { token ->
      val source = ImageCodec.decode(input, minOf(budget.maxDecodeDimension, 8192)).bitmap
      try {
        val r = editRenderer.render(source, doc, EditRenderer.Options(watermark = mark, maxEdge = minOf(budget.maxDecodeDimension, 8192)), token)
        try {
          ExportPolicy.verify(mark, plan, r.watermarkVisible, r.bitmap.width, r.bitmap.height, adUnlocked)
          paths.exports.mkdirs()
          pruneExports()
          val stamp = java.text.SimpleDateFormat("yyyyMMdd_HHmmss_SSS", java.util.Locale.US).format(java.util.Date())
          val out = File(paths.exports, "PhotoPlus_$stamp.${format.extension}")
          ImageCodec.encode(r.bitmap, out, format, quality)
          val galleryUri = if (toGallery) saveToGallery(out, format) else null
          mapOf(
            "outputPath" to out.absolutePath,
            "width" to r.bitmap.width,
            "height" to r.bitmap.height,
            "sizeBytes" to out.length().toDouble(),
            "galleryUri" to galleryUri,
            "watermark" to mapOf(
              "applied" to (mark != null),
              "enforced" to (mark?.enforced ?: false),
              "plan" to plan.name.lowercase(),
              "unlockedByAds" to adUnlocked,
            ),
          )
        } finally {
          r.bitmap.recycle()
        }
      } finally {
        source.recycle()
      }
    }
  }

  /** Exports are hand-offs, not a library: keep them a day. */
  private fun pruneExports() {
    val cutoff = System.currentTimeMillis() - 24 * 3600 * 1000L
    paths.exports.listFiles()?.filter { it.isFile && it.lastModified() < cutoff }?.forEach { it.delete() }
  }

  /** Adds the export to the device gallery (Pictures/Photo Plus); no permission needed. */
  private fun saveToGallery(file: File, format: OutputFormat): String {
    val resolver = reactApplicationContext.contentResolver
    val values = ContentValues().apply {
      put(MediaStore.Images.Media.DISPLAY_NAME, file.name)
      put(MediaStore.Images.Media.MIME_TYPE, format.mime)
      put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/Photo Plus")
      put(MediaStore.Images.Media.IS_PENDING, 1)
    }
    val uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values)
      ?: throw EngineException(EngineException.IO_ERROR, "The gallery is not available")
    try {
      resolver.openOutputStream(uri).use { out ->
        if (out == null) throw EngineException(EngineException.IO_ERROR, "The gallery is not available")
        file.inputStream().use { it.copyTo(out) }
      }
      resolver.update(uri, ContentValues().apply { put(MediaStore.Images.Media.IS_PENDING, 0) }, null, null)
    } catch (e: Exception) {
      resolver.delete(uri, null, null)
      throw e
    }
    return uri.toString()
  }

  /** Faces in the photo as edited (turned, straightened, cropped), for fitting props. */
  override fun detectFaces(request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    ioExecutor.execute {
      try {
        val doc = docOf(map).geometryOnly()
        val source = ImageCodec.decode(paths.resolve(map["inputPath"] as? String ?: ""), 1600).bitmap
        try {
          val r = EditRenderer(reactApplicationContext.assets, paths.customProps) { portraitProcessor }.render(source, doc, EditRenderer.Options(), 0L)
          try {
            promise.resolve(mapOf("faces" to FaceFinder.find(r.bitmap)).toWritableMap())
          } finally {
            r.bitmap.recycle()
          }
        } finally {
          source.recycle()
        }
      } catch (e: EngineException) {
        promise.reject(e.code, e.message, e)
      } catch (e: Exception) {
        promise.reject(EngineException.INTERNAL, e.message, e)
      }
    }
  }

  /**
   * The perspective fix the photo needs, from its near-vertical lines (walls,
   * doors, buildings), measured after its turn and mirror. { vertical: -1..1 },
   * 0 when nothing reliable was found.
   */
  override fun detectPerspective(request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    ioExecutor.execute {
      try {
        val full = docOf(map)
        val doc = EditDocument(quarterTurns = full.quarterTurns, flipH = full.flipH)
        val source = ImageCodec.decode(paths.resolve(map["inputPath"] as? String ?: ""), PERSPECTIVE_DIMENSION).bitmap
        try {
          val r = EditRenderer(reactApplicationContext.assets, paths.customProps) { portraitProcessor }.render(source, doc, EditRenderer.Options(), 0L)
          try {
            val v = NativeEngine.nativeDetectKeystone(r.bitmap)
            promise.resolve(mapOf("vertical" to (if (v.isFinite()) v.coerceIn(-1f, 1f) else 0f).toDouble()).toWritableMap())
          } finally {
            r.bitmap.recycle()
          }
        } finally {
          source.recycle()
        }
      } catch (e: EngineException) {
        promise.reject(e.code, e.message, e)
      } catch (e: Exception) {
        promise.reject(EngineException.INTERNAL, e.message, e)
      }
    }
  }

  /**
   * Every frame design around this photo, small, so people see exactly what
   * each one looks like before choosing. Cached per photo.
   */
  override fun frameThumbnails(request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    ioExecutor.execute {
      try {
        val input = paths.resolve(map["inputPath"] as? String ?: "")
        val edge = ((map["size"] as? Number)?.toInt() ?: 240).coerceIn(96, 512)
        val dir = File(paths.processingCache, "frames/${(input.absolutePath + input.lastModified() + edge).hashCode().toUInt().toString(16)}")
        val out = LinkedHashMap<String, Any?>()
        val missing = FrameStyles.ALL.filter { !File(dir, "${it.id}.jpg").isFile }
        if (missing.isNotEmpty()) {
          dir.mkdirs()
          val photo = ImageCodec.decode(input, edge * 2).bitmap
          try {
            for (style in missing) {
              val (cw, ch) = style.canvasSize(photo.width, photo.height, edge)
              val canvas = createBitmap(cw, ch, Bitmap.Config.ARGB_8888).apply {
                isPremultiplied = false
                setHasAlpha(false)
              }
              try {
                NativeEngine.nativeFrame(photo, canvas, style.floats(1f, 0f, 0f), style.ints(), 0L)
                ImageCodec.encode(canvas, File(dir, "${style.id}.jpg"), OutputFormat.JPEG, 85)
              } finally {
                canvas.recycle()
              }
            }
          } finally {
            photo.recycle()
          }
        }
        for (style in FrameStyles.ALL) out[style.id] = File(dir, "${style.id}.jpg").absolutePath
        promise.resolve(mapOf("thumbnails" to out).toWritableMap())
      } catch (e: EngineException) {
        promise.reject(e.code, e.message, e)
      } catch (e: Exception) {
        promise.reject(EngineException.INTERNAL, e.message, e)
      }
    }
  }

  /**
   * Premium: cuts the object the taps chose out of a photo and saves it as a
   * prop (transparent PNG in app storage). Resolves with its id and file.
   */
  override fun createCustomProp(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    if (Entitlements.plan != Plan.PREMIUM) {
      promise.reject("PREMIUM_REQUIRED", "Making your own props is part of Premium")
      return
    }
    val map = request.toHashMap()
    val points = com.photoplus.studio.PortraitParams.points(map["points"])
    ioExecutor.execute {
      try {
        val photo = ImageCodec.decode(paths.resolve(map["inputPath"] as? String ?: ""), 2048).bitmap
        try {
          val mask = portraitProcessor.cutout(photo, points)
            ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Nothing was found where you tapped")
          val saved = CustomProps.save(photo, mask, paths.customProps)
          promise.resolve(
            mapOf("id" to saved.id, "path" to saved.file.absolutePath, "width" to saved.width, "height" to saved.height).toWritableMap(),
          )
        } finally {
          photo.recycle()
        }
      } catch (e: EngineException) {
        promise.reject(e.code, e.message, e)
      } catch (e: Exception) {
        promise.reject(EngineException.INTERNAL, e.message, e)
      }
    }
  }

  override fun deleteCustomProp(id: String, promise: Promise) {
    ioExecutor.execute { promise.resolve(CustomProps.delete(paths.customProps, id)) }
  }

  override fun getPlan(promise: Promise) = promise.resolve(Entitlements.plan.name.lowercase())

  override fun setDebugPlan(plan: String, promise: Promise) {
    promise.resolve(Entitlements.setDebugPlan(if (plan == "premium") Plan.PREMIUM else Plan.FREE))
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
        val clean = markIfFree(map, io.second)
        mapOf("outputPath" to io.second.absolutePath, "cleanPath" to clean, "width" to work.width, "height" to work.height, "sizeBytes" to io.second.length().toDouble())
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
    val io = paths.resolve(map["inputPath"] as? String ?: "") to paths.resolve(map["outputPath"] as? String ?: "")
    // Edits always go to a new file: the original photo is never overwritten.
    if (io.first == io.second) throw EngineException(EngineException.INVALID_ARGUMENT, "Output must differ from the original")
    io
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
    override fun onProgress(jobId: String, fraction: Float) {
      reactApplicationContext.emitSafely(EVENT_JOB_PROGRESS, mapOf("jobId" to jobId, "progress" to fraction.toDouble()))
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
        "cachedBytes" to s.getOrElse(6) { 0L }.toDouble(),
        "reusedAllocations" to s.getOrElse(7) { 0L }.toDouble(),
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
    if (level == MemoryPressureMonitor.Level.NORMAL) {
      // Pressure is over: bring the GPU back if it was released.
      if (gpu == null) startGpu()
    } else {
      // Give back the engine's reusable buffers; under severe pressure also
      // the GPU's (it starts again, with its self-test, when next needed).
      NativeEngine.nativeMemoryTrim()
      if (level == MemoryPressureMonitor.Level.CRITICAL) {
        ioExecutor.execute {
          NativeEngine.nativeGpuShutdown()
          gpu = null
        }
      }
    }
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
    const val EVENT_JOB_PROGRESS = "PhotoPlusJobProgress"

    /** What the engine can do (reported to the app and diagnostics). */
    val OPERATIONS = listOf(
      "tone", "grayscale", "rotate", "flip", "crop", "resize", "sharpen", "saturation", "warmth",
      "blur", "denoise", "perspective", "lut", "colorMatrix", "blend", "masks", "maskRefine",
    )
    private const val ANALYSIS_DIMENSION = 1600
    /** Lines are found as well at this size and it is quick. */
    private const val PERSPECTIVE_DIMENSION = 1024

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
