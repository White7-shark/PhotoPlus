package com.photoplus.camera

import android.graphics.Bitmap
import android.graphics.ImageDecoder
import android.graphics.Rect
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.core.ImageProxy
import androidx.core.graphics.createBitmap
import androidx.core.graphics.scale
import androidx.exifinterface.media.ExifInterface
import com.photoplus.edit.PhotoWatermark
import com.photoplus.entitlement.Entitlements
import com.photoplus.engine.EngineException
import com.photoplus.engine.ImageCodec
import com.photoplus.engine.JobListener
import com.photoplus.engine.JobManager
import com.photoplus.engine.NativeCancelTokens
import com.photoplus.engine.NativeEngine
import com.photoplus.engine.OutputFormat
import com.photoplus.studio.MaskGeometry
import com.photoplus.studio.PortraitParams
import com.photoplus.studio.PortraitProcessor
import com.photoplus.studio.PortraitReport
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.nio.ByteBuffer
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Turns a shutter press into a saved photo for every capture mode.
 *
 * The camera → engine path avoids copies: each in-memory JPEG is decoded
 * straight from CameraX's direct buffer into the bitmap the engine reads
 * (cropped and scaled by the decoder itself), the engine writes into the
 * output bitmap in place (pp_enhance_into), and orientation is recorded as
 * an EXIF tag rather than by rotating pixels.
 */
class CaptureProcessor(
  private val capturesDir: File,
  private val tier: DeviceTier,
  /** Artwork for props worn in the camera. */
  private val propArt: com.photoplus.edit.PropArt? = null,
) {
  /** How preview positions map onto this capture's upright photo. */
  private data class PropFrame(val deviceDegrees: Int, val mirrored: Boolean)


  interface Listener {
    fun onShutter(captureId: String) {}
    fun onProgress(captureId: String, stage: String, progress: Double) {}
    fun onSuccess(captureId: String, result: Map<String, Any?>)
    fun onFailure(captureId: String, code: String, message: String)
  }

  private val main = Handler(Looper.getMainLooper())
  private val captureExecutor: ExecutorService = Executors.newSingleThreadExecutor { r -> Thread(r, "pp-capture") }
  private val processExecutor = ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS, LinkedBlockingQueue()) { r ->
    Thread(r, "pp-process").apply { priority = Thread.NORM_PRIORITY - 1 }
  }.apply { allowCoreThreadTimeOut(true) }
  private val jobs = JobManager(processExecutor, NativeCancelTokens)
  private val processing = AtomicBoolean(false)
  private val portraitProcessor by lazy { PortraitProcessor() }
  private val cancelledBursts = java.util.concurrent.ConcurrentHashMap.newKeySet<String>()

  val busy: Boolean get() = processing.get()

  /**
   * Main thread. [light] is Auto Light's decision for this shot (null: the
   * shot as asked): it may turn it into a Moonlight or Bright Balance burst,
   * add a Natural Flash frame, or light a selfie with the screen.
   */
  fun capture(
    capture: ImageCapture,
    original: CaptureOptions,
    frontCamera: Boolean,
    deviceDegrees: Int,
    listener: Listener,
    light: LightDecision? = null,
  ) {
    val request = if (light != null) original.copy(mode = light.mode) else original
    capture.flashMode = when {
      light?.flash == LightFlash.SCREEN -> screenOrOff(capture)
      light?.flash == LightFlash.NATURAL -> ImageCapture.FLASH_MODE_OFF // the ambient frames; the flash frame comes last
      else -> when (request.effectiveFlash) {
        FlashSetting.ON -> ImageCapture.FLASH_MODE_ON
        FlashSetting.AUTO -> ImageCapture.FLASH_MODE_AUTO
        FlashSetting.OFF -> ImageCapture.FLASH_MODE_OFF
      }
    }
    val mirror = frontCamera && request.mirrorFront
    val upright = request.previewPoints.map { (x, y) -> MaskGeometry.previewToUpright(x, y, deviceDegrees, frontCamera) }
    // The saved photo is upright; it matches the mirrored preview only when front photos are mirrored.
    val propFrame = PropFrame(deviceDegrees, frontCamera && !request.mirrorFront)
    when {
      request.mode == CaptureMode.RAW -> captureRaw(capture, request, mirror, listener)
      request.needsProcessing || light != null -> captureProcessed(capture, request, mirror, upright, propFrame, listener, light)
      else -> captureStandard(capture, request, mirror, propFrame, listener)
    }
  }

  private fun screenOrOff(capture: ImageCapture): Int = try {
    capture.flashMode = ImageCapture.FLASH_MODE_SCREEN
    ImageCapture.FLASH_MODE_SCREEN
  } catch (e: Exception) {
    android.util.Log.w("PhotoPlusCapture", "screen light unavailable", e)
    ImageCapture.FLASH_MODE_OFF
  }

  fun cancel(captureId: String) {
    cancelledBursts.add(captureId)
    jobs.cancel(captureId)
  }

  fun shutdown() {
    jobs.shutdown()
    captureExecutor.shutdown()
  }

  // -------------------------------------------------------------- standard

  private fun captureStandard(capture: ImageCapture, request: CaptureOptions, mirror: Boolean, propFrame: PropFrame, listener: Listener) {
    val file = newFile("jpg")
    val started = SystemClock.elapsedRealtime()
    capture.takePicture(fileOptions(file, mirror), captureExecutor, object : ImageCapture.OnImageSavedCallback {
      override fun onCaptureStarted() = listener.onShutter(request.captureId)
      override fun onImageSaved(output: ImageCapture.OutputFileResults) {
        applyProps(file, request, propFrame)
        val clean = markForPlan(file, request)
        listener.onSuccess(request.captureId, photoResult(file, request.mode, started) + mapOf("cleanPath" to clean))
      }
      override fun onError(exception: ImageCaptureException) {
        file.delete()
        listener.onFailure(request.captureId, "CAPTURE_FAILED", describe(exception))
      }
    })
  }

  // ------------------------------------------------------------------- RAW

  private fun captureRaw(capture: ImageCapture, request: CaptureOptions, mirror: Boolean, listener: Listener) {
    if (capture.outputFormat != ImageCapture.OUTPUT_FORMAT_RAW_JPEG) {
      listener.onFailure(request.captureId, "RAW_UNAVAILABLE", "RAW capture is not enabled for this camera")
      return
    }
    val jpeg = newFile("jpg")
    val dng = File(jpeg.parentFile, jpeg.nameWithoutExtension + ".dng")
    val started = SystemClock.elapsedRealtime()
    val finished = AtomicBoolean(false)
    var saved = 0 // capture executor only
    // Some HALs deliver only one of the pair; never wait forever.
    val timeout = Runnable { captureExecutor.execute { finishRaw(finished, jpeg, dng, request, started, listener, null) } }
    capture.takePicture(fileOptions(dng, mirror), fileOptions(jpeg, mirror), captureExecutor, object : ImageCapture.OnImageSavedCallback {
      override fun onCaptureStarted() {
        listener.onShutter(request.captureId)
        main.postDelayed(timeout, RAW_PAIR_TIMEOUT_MS)
      }
      override fun onImageSaved(output: ImageCapture.OutputFileResults) {
        if (++saved >= 2) {
          main.removeCallbacks(timeout)
          finishRaw(finished, jpeg, dng, request, started, listener, null)
        }
      }
      override fun onError(exception: ImageCaptureException) {
        main.removeCallbacks(timeout)
        finishRaw(finished, jpeg, dng, request, started, listener, exception)
      }
    })
  }

  private fun finishRaw(
    finished: AtomicBoolean,
    jpeg: File,
    dng: File,
    request: CaptureOptions,
    started: Long,
    listener: Listener,
    error: ImageCaptureException?,
  ) {
    if (!finished.compareAndSet(false, true)) return
    val hasJpeg = jpeg.isFile && jpeg.length() > 0
    val hasRaw = dng.isFile && dng.length() > 0
    if (!hasJpeg) {
      jpeg.delete()
      dng.delete()
      listener.onFailure(request.captureId, "CAPTURE_FAILED", error?.let { describe(it) } ?: "The camera did not deliver the photo")
      return
    }
    if (!hasRaw) dng.delete()
    listener.onSuccess(request.captureId, photoResult(jpeg, request.mode, started) + mapOf(
      "rawPath" to if (hasRaw) dng.absolutePath else null,
      "rawSizeBytes" to if (hasRaw) dng.length().toDouble() else null,
    ))
  }

  // ------------------------------------------------------------- processed

  /** Shared by the frames of one burst; touched only on the capture executor. */
  private class Burst(
    val request: CaptureOptions,
    val plan: ModePlan,
    val mirror: Boolean,
    val started: Long,
    /** Spotlight taps in the upright photo. */
    val points: List<Pair<Float, Float>>,
    val propFrame: PropFrame,
    val light: LightDecision?,
    val capture: ImageCapture,
  ) {
    val frames = ArrayList<Bitmap>(plan.frames)
    /** Natural Flash: the flash frame, taken after the ambient burst. */
    var flashFrame: Bitmap? = null
    var flashRequested = false
    val wantsFlashFrame: Boolean get() = light?.flash == LightFlash.NATURAL
    /** Frames to wait for: the burst, plus the flash frame. */
    val expected: Int get() = plan.frames + if (wantsFlashFrame) 1 else 0
    var received = 0
    var failed = false
    var bufferSize: Pair<Int, Int>? = null
    var geometry: CaptureGeometry.DecodePlan? = null
    var exif: Map<String, String> = emptyMap()

    fun release() {
      frames.forEach { it.recycle() }
      frames.clear()
      flashFrame?.recycle()
      flashFrame = null
    }
  }

  private fun captureProcessed(
    capture: ImageCapture,
    request: CaptureOptions,
    mirror: Boolean,
    points: List<Pair<Float, Float>>,
    propFrame: PropFrame,
    listener: Listener,
    light: LightDecision?,
  ) {
    if (!NativeEngine.ensureLoaded()) {
      listener.onFailure(request.captureId, "ENGINE_UNAVAILABLE", "The processing engine is unavailable on this device")
      return
    }
    // One processed photo at a time bounds memory; standard shots stay available.
    if (!processing.compareAndSet(false, true)) {
      listener.onFailure(request.captureId, "BUSY", "Still processing the previous photo")
      return
    }
    // A look on a plain photo is still decoded: size it like Clarity so it is
    // bounded and can step down on low memory (never a full-sensor decode).
    val base = CapturePolicy.plan(if (request.mode.processed) request.mode else CaptureMode.ENHANCE, tier)
    // Auto Light sets the burst length (a longer one is decoded smaller).
    val plan = if (light != null) AutoLight.burstPlan(base, light.frames) else base
    val burst = Burst(request, plan, mirror, SystemClock.elapsedRealtime(), points, propFrame, light, capture)
    cancelledBursts.remove(request.captureId)
    listener.onProgress(request.captureId, "capturing", 0.0)
    // Queue every frame at once; CameraX pipelines them back to back, which
    // keeps the burst short (less subject motion) and the hand-shake small.
    repeat(plan.frames) { index ->
      capture.takePicture(captureExecutor, object : ImageCapture.OnImageCapturedCallback() {
        override fun onCaptureStarted() {
          if (index == 0) listener.onShutter(request.captureId)
        }
        override fun onCaptureSuccess(image: ImageProxy) {
          try {
            if (cancelledBursts.contains(request.captureId)) {
              failBurst(burst, listener, EngineException.CANCELLED, "Capture was cancelled")
            } else if (!burst.failed) {
              acceptFrame(burst, image)
            }
          } catch (e: EngineException) {
            failBurst(burst, listener, e.code, e.message ?: "Unable to read the photo")
          } catch (e: OutOfMemoryError) {
            failBurst(burst, listener, EngineException.OUT_OF_MEMORY, "Not enough memory for this burst")
          } finally {
            image.close()
          }
          frameDone(burst, listener)
        }
        override fun onError(exception: ImageCaptureException) {
          // A dropped frame is tolerable as long as the reference survives.
          if (burst.request.mode.burst && exception.imageCaptureError != ImageCapture.ERROR_CAMERA_CLOSED) {
            frameDone(burst, listener)
          } else {
            failBurst(burst, listener, "CAPTURE_FAILED", describe(exception))
            frameDone(burst, listener)
          }
        }
      })
    }
  }

  private fun acceptFrame(burst: Burst, image: ImageProxy) {
    val size = image.width to image.height
    val expected = burst.bufferSize
    if (expected != null && expected != size) return // resolution changed mid-burst: skip
    val buffer = image.planes.firstOrNull()?.buffer
      ?: throw EngineException(EngineException.UNSUPPORTED_FORMAT, "Capture has no image data")
    if (expected == null) {
      val exif = readExif(buffer)
      val exifDegrees = CaptureGeometry.exifRotation(exif?.getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL) ?: 0)
      val crop = image.cropRect
      burst.bufferSize = size
      burst.geometry = CaptureGeometry.decodePlan(
        image.width, image.height, IntRect(crop.left, crop.top, crop.right, crop.bottom),
        exifDegrees, image.imageInfo.rotationDegrees, burst.plan.decodeDimension,
      )
      burst.exif = exif?.let { e -> COPIED_TAGS.mapNotNull { tag -> e.getAttribute(tag)?.let { tag to it } }.toMap() } ?: emptyMap()
    }
    burst.frames += decodeFrame(buffer, burst.geometry!!)
  }

  private fun frameDone(burst: Burst, listener: Listener) {
    burst.received++
    if (!burst.failed) {
      listener.onProgress(burst.request.captureId, "capturing", burst.received.toDouble() / burst.expected)
    }
    if (burst.received < burst.plan.frames) return
    if (burst.failed) return
    if (burst.wantsFlashFrame && !burst.flashRequested && burst.frames.isNotEmpty()) {
      burst.flashRequested = true
      main.post { takeFlashFrame(burst, listener) }
      return
    }
    if (burst.flashRequested && burst.received < burst.expected) return
    if (burst.frames.isEmpty()) {
      failBurst(burst, listener, "CAPTURE_FAILED", "No frames were captured")
      return
    }
    listener.onProgress(burst.request.captureId, "processing", 0.0)
    process(burst, listener)
  }

  /**
   * Natural Flash: one frame with the flash, after the ambient burst (main
   * thread). Without it the photo is still the Moonlight merge.
   */
  private fun takeFlashFrame(burst: Burst, listener: Listener) {
    val capture = burst.capture
    try {
      capture.flashMode = ImageCapture.FLASH_MODE_ON
    } catch (e: Exception) {
      captureExecutor.execute { frameDone(burst, listener) }
      return
    }
    capture.takePicture(captureExecutor, object : ImageCapture.OnImageCapturedCallback() {
      override fun onCaptureSuccess(image: ImageProxy) {
        try {
          if (!burst.failed && !cancelledBursts.contains(burst.request.captureId)) acceptFlashFrame(burst, image)
        } catch (e: Exception) {
          android.util.Log.w("PhotoPlusCapture", "flash frame unusable", e)
        } catch (e: OutOfMemoryError) {
          android.util.Log.w("PhotoPlusCapture", "no memory for the flash frame", e)
        } finally {
          image.close()
          main.post { restoreFlash(capture) }
        }
        frameDone(burst, listener)
      }
      override fun onError(exception: ImageCaptureException) {
        main.post { restoreFlash(capture) }
        frameDone(burst, listener) // the ambient merge alone
      }
    })
  }

  private fun restoreFlash(capture: ImageCapture) {
    try { capture.flashMode = ImageCapture.FLASH_MODE_OFF } catch (_: Exception) {}
  }

  private fun acceptFlashFrame(burst: Burst, image: ImageProxy) {
    if (burst.bufferSize != (image.width to image.height)) return // a different size cannot be lined up
    val buffer = image.planes.firstOrNull()?.buffer ?: return
    burst.flashFrame = decodeFrame(buffer, burst.geometry ?: return)
  }

  private fun failBurst(burst: Burst, listener: Listener, code: String, message: String) {
    if (burst.failed) return
    burst.failed = true
    burst.release()
    processing.set(false)
    val cancelled = cancelledBursts.remove(burst.request.captureId)
    listener.onFailure(burst.request.captureId, if (cancelled) EngineException.CANCELLED else code, message)
  }

  /** Decodes straight from the camera's direct buffer, letting the decoder crop and scale. */
  private fun decodeFrame(buffer: ByteBuffer, geometry: CaptureGeometry.DecodePlan): Bitmap {
    val bitmap = try {
      ImageDecoder.decodeBitmap(ImageDecoder.createSource(buffer.duplicate())) { decoder, _, _ ->
        decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
        decoder.isUnpremultipliedRequired = true
        decoder.isMutableRequired = true // looks and Spotlight may work in place
        decoder.setTargetSize(geometry.targetWidth, geometry.targetHeight)
        geometry.crop?.let { decoder.crop = Rect(it.left, it.top, it.right, it.bottom) }
      }
    } catch (e: IOException) {
      throw EngineException(EngineException.UNSUPPORTED_FORMAT, "Unable to decode the capture: ${e.message}")
    }
    if (bitmap.config == Bitmap.Config.ARGB_8888) return bitmap
    return try { ImageCodec.toStraightArgb8888(bitmap) } finally { bitmap.recycle() }
  }

  private fun process(burst: Burst, listener: Listener) {
    val id = burst.request.captureId
    jobs.submit(id, object : JobListener<Map<String, Any?>> {
      override fun onSuccess(jobId: String, result: Map<String, Any?>) {
        processing.set(false)
        cancelledBursts.remove(id)
        listener.onSuccess(id, result)
      }
      override fun onFailure(jobId: String, error: EngineException) {
        burst.release()
        processing.set(false)
        cancelledBursts.remove(id)
        listener.onFailure(id, error.code, error.message ?: "Processing failed")
      }
    }) { token ->
      try {
        enhanceWithFallback(burst, token, listener)
      } finally {
        burst.release()
      }
    }
  }

  private fun enhanceWithFallback(burst: Burst, token: Long, listener: Listener): Map<String, Any?> {
    var plan = burst.plan
    var frames: List<Bitmap> = burst.frames
    val processStarted = SystemClock.elapsedRealtime()
    try {
      while (true) {
        try {
          return enhanceAndSave(burst, frames, token, listener, processStarted)
        } catch (e: EngineException) {
          if (e.code != EngineException.BUDGET_EXCEEDED) throw e
          val smaller = CapturePolicy.reduced(plan) ?: throw e
          // Rare path: shrink the frames we already have rather than fail the shot.
          val factor = smaller.decodeDimension.toDouble() / plan.decodeDimension
          val scaled = frames.map { f -> f.scale(max(16, (f.width * factor).roundToInt()), max(16, (f.height * factor).roundToInt())) }
          if (frames !== burst.frames) frames.forEach { it.recycle() }
          frames = scaled
          plan = smaller
        }
      }
    } finally {
      if (frames !== burst.frames) frames.forEach { it.recycle() }
    }
  }

  private fun enhanceAndSave(
    burst: Burst,
    frames: List<Bitmap>,
    token: Long,
    listener: Listener,
    processStarted: Long,
  ): Map<String, Any?> {
    val request = burst.request
    val first = frames.first()
    val upscale = request.mode.upscale
    val enhance = request.mode.processed
    val geometry = burst.geometry!!
    val target = newFile("jpg")
    val original = File(target.parentFile, target.nameWithoutExtension + "_original.jpg")
    val report = FloatArray(7)
    // Enhanced modes write into a fresh bitmap; a look on its own grades the
    // decoded frame in place (after its untouched copy is saved).
    var output: Bitmap? = null
    try {
      val work: Bitmap
      if (enhance) {
        output = createBitmap(first.width * upscale, first.height * upscale, Bitmap.Config.ARGB_8888).apply {
          isPremultiplied = false // the engine writes straight alpha
          setHasAlpha(false)
        }
        NativeEngine.nativeEnhance(
          frames.toTypedArray(), upscale,
          floatArrayOf(request.denoise, request.sharpen, request.detail, request.vibrance),
          request.autoTone, output, token, report,
        )
        work = output
      } else {
        work = first
      }
      // Auto Light: the flash frame fused in (Natural Flash), then tone for the
      // light the scene had, before any beauty or look (they see good light).
      val lightReport = burst.light?.takeIf { enhance }?.let { applyLight(it, burst, frames.size, work, token, listener) }
      val reference = frames[report[2].toInt().coerceIn(0, frames.lastIndex)]
      val (ow, oh) = CaptureGeometry.displaySize(reference.width, reference.height, geometry.remainingRotation)
      if (!enhance) {
        ImageCodec.encode(reference, original, OutputFormat.JPEG, ORIGINAL_QUALITY)
        writeExif(original, burst.exif, geometry.remainingRotation, burst.mirror)
      }
      var portrait: PortraitReport? = null
      val recipe = request.beautyRecipe?.takeIf { it.active }
      val backdrop = request.backdrop?.takeIf { it.active }
      if (recipe != null || backdrop != null) {
        // Beauty preset (with Portrait mode's own controls in Spotlight), then the backdrop.
        listener.onProgress(request.captureId, "styling", 0.6)
        val points = if (request.mode == CaptureMode.PORTRAIT) burst.points else emptyList()
        portrait = portraitProcessor.applyBeauty(work, geometry.remainingRotation, points, recipe, token, backdrop)
      } else if (request.mode == CaptureMode.PORTRAIT) {
        listener.onProgress(request.captureId, "styling", 0.6)
        portrait = portraitProcessor.apply(work, geometry.remainingRotation, burst.points, request.portrait, token, request.beauty)
      } else if (request.beautyActive) {
        // Camera beauty: Glow and its face touches, no background change.
        listener.onProgress(request.captureId, "styling", 0.6)
        val params = PortraitParams(blur = 0f, retouch = request.beautyRetouch, brighten = request.beautyBrighten, pop = 0f)
        portrait = portraitProcessor.apply(work, geometry.remainingRotation, emptyList(), params, token, request.beauty)
      }
      request.look?.let { NativeEngine.nativeStyleApply(work, it, request.lookStrength, token) }
      val processMs = SystemClock.elapsedRealtime() - processStarted
      listener.onProgress(request.captureId, "saving", 0.9)
      ImageCodec.encode(work, target, OutputFormat.JPEG, OUTPUT_QUALITY)
      writeExif(target, burst.exif, geometry.remainingRotation, burst.mirror)
      applyProps(target, request, burst.propFrame)
      val clean = markForPlan(target, request)
      if (enhance) {
        // Keep the reference frame so the user can compare before and after.
        ImageCodec.encode(reference, original, OutputFormat.JPEG, ORIGINAL_QUALITY)
        writeExif(original, burst.exif, geometry.remainingRotation, burst.mirror)
      }

      val (w, h) = CaptureGeometry.displaySize(work.width, work.height, geometry.remainingRotation)
      return mapOf(
        "path" to target.absolutePath,
        "cleanPath" to clean,
        "width" to w,
        "height" to h,
        "sizeBytes" to target.length().toDouble(),
        "capturedAt" to System.currentTimeMillis().toDouble(),
        "mode" to request.mode.id,
        "originalPath" to original.absolutePath,
        "originalWidth" to ow,
        "originalHeight" to oh,
        "look" to (request.look != null),
        "light" to lightReport,
        "report" to if (enhance) mapOf(
          "framesInput" to report[0].toInt(),
          "framesUsed" to report[1].toInt(),
          "referenceIndex" to report[2].toInt(),
          "noiseBefore" to report[3].toDouble(),
          "noiseAfter" to report[4].toDouble(),
          "blurBefore" to report[5].toDouble(),
          "deblurSigma" to report[6].toDouble(),
          "upscale" to upscale,
        ) else null,
        "portrait" to portrait?.let {
          mapOf("people" to it.people, "spots" to it.spots, "skin" to it.skinCoverage.toDouble(), "subject" to it.subjectCoverage.toDouble())
        },
        // What Spotlight used, in the upright photo, so the effect can be
        // re-applied exactly later (e.g. after Transform restores the original).
        "spotlight" to portrait?.takeIf { request.mode == CaptureMode.PORTRAIT }?.let {
          mapOf(
            "points" to burst.points.flatMap { (x, y) -> listOf(x.toDouble(), y.toDouble()) },
            "blur" to request.portrait.blur.toDouble(),
            "retouch" to request.portrait.retouch.toDouble(),
            "brighten" to request.portrait.brighten.toDouble(),
            "pop" to request.portrait.pop.toDouble(),
          )
        },
        // The beauty preset used, so it can be re-applied (e.g. after Transform).
        "beautyPreset" to request.beautyPreset?.takeIf { recipe != null },
        "backdrop" to backdrop?.let { mapOf("id" to it.design.id, "strength" to it.strength.toDouble()) },
        // Camera beauty used, so it can be re-applied (e.g. after Transform).
        "beauty" to portrait?.takeIf { request.mode != CaptureMode.PORTRAIT && recipe == null && backdrop == null }?.let {
          mapOf(
            "retouch" to request.beautyRetouch.toDouble(),
            "brighten" to request.beautyBrighten.toDouble(),
            "eyes" to request.beauty.eyes.toDouble(),
            "underEye" to request.beauty.underEye.toDouble(),
            "shine" to request.beauty.shine.toDouble(),
            "faceLight" to request.beauty.faceLight.toDouble(),
          )
        },
        "timings" to mapOf(
          "captureMs" to (processStarted - burst.started).toDouble(),
          "processMs" to processMs.toDouble(),
          "totalMs" to (SystemClock.elapsedRealtime() - burst.started).toDouble(),
        ),
      )
    } catch (e: Exception) {
      target.delete()
      original.delete()
      throw e
    } finally {
      output?.recycle()
    }
  }

  // ---------------------------------------------------------------- helpers

  /**
   * Free plan: the photo carries the Photo Plus mark (signature style by
   * default); the untouched photo is kept as its clean companion. A failure
   * here never loses the photo: it is kept as captured (exports still apply
   * the mark).
   */
  /** Props go into the photo itself, before the watermark; a failure keeps the photo as captured. */
  private fun applyLight(light: LightDecision, burst: Burst, frameCount: Int, work: Bitmap, token: Long, listener: Listener): Map<String, Any?> {
    listener.onProgress(burst.request.captureId, "lighting", 0.5)
    var fuse: FloatArray? = null
    val flash = burst.flashFrame
    if (light.flash == LightFlash.NATURAL && flash != null) {
      // After a low-memory retry the burst is smaller than the flash frame.
      val matched = if (flash.width == work.width && flash.height == work.height) flash else flash.scale(work.width, work.height)
      try {
        val r = FloatArray(4)
        NativeEngine.nativeFlashFuse(work, matched, work, NATURAL_FLASH_FILL, 1f, token, r)
        fuse = r
      } finally {
        if (matched !== flash) matched.recycle()
      }
    }
    val tone = light.tone?.let { t -> FloatArray(6).also { r -> NativeEngine.nativeLightTone(work, t.toParams(), token, r) } }
    return mapOf(
      "plan" to light.plan.id,
      "label" to light.label,
      "frames" to frameCount,
      "flash" to when (light.flash) {
        LightFlash.NONE -> null
        LightFlash.SCREEN -> "screen"
        LightFlash.NATURAL -> if (fuse != null) "natural" else null
      },
      "flashAligned" to fuse?.let { it[0] > 0.5f },
      "keyBefore" to tone?.get(0)?.toDouble(),
      "keyAfter" to tone?.get(1)?.toDouble(),
    )
  }

  private fun applyProps(file: File, request: CaptureOptions, frame: PropFrame) {
    val art = propArt ?: return
    if (request.props.isEmpty()) return
    try {
      CameraProps.applyToFile(file, request.props, art, frame.deviceDegrees, frame.mirrored)
    } catch (e: Exception) {
      android.util.Log.w("PhotoPlusCapture", "props failed; photo kept as captured", e)
    }
  }

  private fun markForPlan(file: File, request: CaptureOptions): String? = try {
    PhotoWatermark.applyToFile(file, request.watermark, Entitlements.plan)
  } catch (e: Exception) {
    android.util.Log.w("PhotoPlusCapture", "watermark failed; photo kept as captured", e)
    null
  }

  private fun fileOptions(file: File, mirror: Boolean): ImageCapture.OutputFileOptions {
    file.parentFile?.mkdirs()
    val metadata = ImageCapture.Metadata().apply { isReversedHorizontal = mirror }
    return ImageCapture.OutputFileOptions.Builder(file).setMetadata(metadata).build()
  }

  private fun newFile(extension: String): File {
    capturesDir.mkdirs()
    val stamp = SimpleDateFormat("yyyyMMdd_HHmmss_SSS", Locale.US).format(Date())
    var file = File(capturesDir, "PP_$stamp.$extension")
    var n = 1
    while (file.exists()) file = File(capturesDir, "PP_${stamp}_${n++}.$extension")
    return file
  }

  private fun photoResult(file: File, mode: CaptureMode, started: Long): Map<String, Any?> {
    val bounds = ImageCodec.readDisplayBounds(file)
    return mapOf(
      "path" to file.absolutePath,
      "width" to bounds.width,
      "height" to bounds.height,
      "sizeBytes" to file.length().toDouble(),
      "capturedAt" to System.currentTimeMillis().toDouble(),
      "mode" to mode.id,
      "timings" to mapOf("totalMs" to (SystemClock.elapsedRealtime() - started).toDouble()),
    )
  }

  private fun readExif(buffer: ByteBuffer): ExifInterface? = try {
    ExifInterface(ByteBufferInputStream(buffer.duplicate()))
  } catch (e: IOException) {
    null
  }

  private fun writeExif(file: File, tags: Map<String, String>, rotation: Int, mirror: Boolean) {
    try {
      val exif = ExifInterface(file)
      tags.forEach { (tag, value) -> exif.setAttribute(tag, value) }
      exif.setAttribute(ExifInterface.TAG_SOFTWARE, "Photo Plus")
      exif.setAttribute(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL.toString())
      if (rotation != 0) exif.rotate(rotation)
      if (mirror) exif.flipHorizontally()
      exif.saveAttributes()
    } catch (e: IOException) {
      // Metadata is best-effort; the photo itself is already safely written.
    }
  }

  private fun describe(e: ImageCaptureException) = e.message ?: "Capture failed (code ${e.imageCaptureError})"

  /** Reads a ByteBuffer in place (no copy into a byte array). */
  private class ByteBufferInputStream(private val buffer: ByteBuffer) : InputStream() {
    override fun read(): Int = if (buffer.hasRemaining()) buffer.get().toInt() and 0xFF else -1
    override fun read(b: ByteArray, off: Int, len: Int): Int {
      if (!buffer.hasRemaining()) return -1
      val n = minOf(len, buffer.remaining())
      buffer.get(b, off, n)
      return n
    }
    override fun available(): Int = buffer.remaining()
  }

  companion object {
    private const val RAW_PAIR_TIMEOUT_MS = 15_000L
    private const val OUTPUT_QUALITY = 95
    private const val ORIGINAL_QUALITY = 90
    /** How much of the flash's own light Natural Flash keeps (the rest is the room's light). */
    private const val NATURAL_FLASH_FILL = 0.3f

    private val COPIED_TAGS = listOf(
      ExifInterface.TAG_MAKE,
      ExifInterface.TAG_MODEL,
      ExifInterface.TAG_DATETIME,
      ExifInterface.TAG_DATETIME_ORIGINAL,
      ExifInterface.TAG_DATETIME_DIGITIZED,
      ExifInterface.TAG_OFFSET_TIME_ORIGINAL,
      ExifInterface.TAG_PHOTOGRAPHIC_SENSITIVITY,
      ExifInterface.TAG_EXPOSURE_TIME,
      ExifInterface.TAG_F_NUMBER,
      ExifInterface.TAG_APERTURE_VALUE,
      ExifInterface.TAG_FOCAL_LENGTH,
      ExifInterface.TAG_FOCAL_LENGTH_IN_35MM_FILM,
      ExifInterface.TAG_FLASH,
      ExifInterface.TAG_WHITE_BALANCE,
      ExifInterface.TAG_LENS_MAKE,
      ExifInterface.TAG_LENS_MODEL,
    )
  }
}
