package com.photoplus.studio

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.core.graphics.createBitmap
import androidx.core.graphics.scale
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.device.DeviceInfo
import com.photoplus.device.MemoryPressureMonitor
import com.photoplus.edit.ExportPolicy
import com.photoplus.edit.PhotoWatermark
import com.photoplus.edit.Plan
import com.photoplus.edit.WatermarkRenderer
import com.photoplus.edit.WatermarkSpec
import com.photoplus.engine.EngineException
import com.photoplus.engine.ImageCodec
import com.photoplus.engine.JobListener
import com.photoplus.engine.JobManager
import com.photoplus.engine.MemoryBudgetPolicy
import com.photoplus.engine.NativeCancelTokens
import com.photoplus.engine.NativeEngine
import com.photoplus.engine.OutputFormat
import com.photoplus.entitlement.Entitlements
import com.photoplus.specs.NativePhotoPlusProductSpec
import com.photoplus.storage.AppPaths
import com.photoplus.util.toWritableMap
import java.io.File
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Product photography (Phase 12; the engine's product module):
 *
 *  1. openProduct — the object in a photo: its mask from subject
 *     segmentation (taps choose which objects), the engine's cut-out by the
 *     backdrop's colours, a preview cut-out and what it found (the old
 *     backdrop's colour, a plain or busy backdrop, how many objects);
 *  2. refineProduct — the cut-out again with keep and remove strokes and
 *     edge settings;
 *  3. renderProduct — the catalogue shot: a preview, or the saved photo at
 *     an export profile's shape, size and format.
 *
 * The photo itself is only read: originals are never changed. One worker,
 * so a batch of photos runs one at a time within the memory budget.
 */
@ReactModule(name = ProductModule.NAME)
class ProductModule(reactContext: ReactApplicationContext) :
  NativePhotoPlusProductSpec(reactContext), MemoryPressureMonitor.Listener {

  private val paths = AppPaths(reactContext)
  private val budget = MemoryBudgetPolicy.forTotalRam(DeviceInfo.totalRamBytes(reactContext), DeviceInfo.isLowRamDevice(reactContext))
  private val engineAvailable = NativeEngine.ensureLoaded()
  private val portraitProcessor by lazy { PortraitProcessor() }
  private val jobs = JobManager(
    ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS, LinkedBlockingQueue()).apply { allowCoreThreadTimeOut(true) },
    NativeCancelTokens,
  )
  private val sessions = ConcurrentHashMap<String, Session>()

  private class Session(
    val id: String,
    val file: File,
    val dir: File,
    /** The object mask (ALPHA_8, analysis size), or null: the backdrop's colours alone. */
    val hint: Bitmap?,
    /** The photo, upright, at preview size. */
    val preview: Bitmap,
  ) {
    @Volatile var strokes: List<StudioStroke> = emptyList()
    @Volatile var matte = ProductMatte()
    @Volatile var previewCutout: Bitmap? = null
    /** The photographed backdrop (sRGB) when it was plain, for colour correction. */
    @Volatile var reference: FloatArray? = null
    var fullCutout: Bitmap? = null
    var fullKey: Any? = null
    var serial = 0
    val opened = System.currentTimeMillis()

    fun release() {
      previewCutout?.recycle()
      fullCutout?.recycle()
      hint?.recycle()
      preview.recycle()
      dir.deleteRecursively()
    }
  }

  init {
    MemoryPressureMonitor.addListener(this)
  }

  override fun getName() = NAME

  override fun invalidate() {
    MemoryPressureMonitor.removeListener(this)
    jobs.shutdown()
    sessions.values.forEach { it.release() }
    sessions.clear()
    super.invalidate()
  }

  override fun onMemoryPressure(level: MemoryPressureMonitor.Level) {
    sessions.values.forEach { s ->
      synchronized(s) {
        s.fullCutout?.recycle()
        s.fullCutout = null
        s.fullKey = null
      }
    }
  }

  private fun listener(promise: Promise) = object : JobListener<Map<String, Any?>> {
    override fun onSuccess(jobId: String, result: Map<String, Any?>) = promise.resolve(result.toWritableMap())
    override fun onFailure(jobId: String, error: EngineException) = promise.reject(error.code, error.message, error)
  }

  private fun session(map: Map<String, Any?>): Session =
    sessions[map["sessionId"] as? String ?: ""] ?: throw EngineException(EngineException.INVALID_ARGUMENT, "This product session has ended")

  override fun openProduct(jobId: String, request: ReadableMap, promise: Promise) {
    if (!engineAvailable) {
      promise.reject(EngineException.INTERNAL, "Processing engine is unavailable on this device")
      return
    }
    val map = request.toHashMap()
    val file = try {
      paths.resolve(map["inputPath"] as? String ?: "")
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val points = PortraitParams.points(map["points"])
    jobs.submit(jobId, listener(promise)) { token ->
      // A batch opens one photo after another: at most two at once.
      while (sessions.size >= 2) sessions.values.minByOrNull { it.opened }?.let { sessions.remove(it.id)?.release() } ?: break
      val analysis = ImageCodec.decode(file, ANALYSIS_EDGE).bitmap
      try {
        val hint = portraitProcessor.objectMask(analysis, points)?.let { PortraitProcessor.alphaBitmap(it, analysis.width, analysis.height) }
        val k = min(1f, PREVIEW_EDGE.toFloat() / max(analysis.width, analysis.height))
        val preview = if (k < 1f) {
          analysis.scale(max(1, (analysis.width * k).roundToInt()), max(1, (analysis.height * k).roundToInt()))
        } else {
          analysis.copy(Bitmap.Config.ARGB_8888, false)
        }
        val id = UUID.randomUUID().toString()
        val dir = File(paths.processingCache, "product-$id").apply { mkdirs() }
        val s = Session(id, file, dir, hint, preview)
        sessions[id] = s
        val photoPath = File(dir, "photo.jpg").also { ImageCodec.encode(preview, it, OutputFormat.JPEG, 90) }
        cutPreview(s, token) + mapOf("sessionId" to id, "photoPath" to photoPath.absolutePath, "model" to (hint != null))
      } finally {
        analysis.recycle()
      }
    }
  }

  override fun refineProduct(jobId: String, request: ReadableMap, promise: Promise) {
    val map = request.toHashMap()
    jobs.submit(jobId, listener(promise)) { token ->
      val s = session(map)
      s.strokes = StudioStroke.listFrom(map["strokes"])
      s.matte = ProductMatte.fromMap(map)
      cutPreview(s, token)
    }
  }

  private fun cutPreview(s: Session, token: Long): Map<String, Any?> {
    val out = createBitmap(s.preview.width, s.preview.height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
    val report = FloatArray(11)
    NativeEngine.nativeProductCutout(s.preview, s.hint, StudioStroke.encode(s.strokes), s.matte.toArray(), out, token, report)
    val plain = report[9] > 0.5f
    synchronized(s) {
      s.previewCutout?.recycle()
      s.previewCutout = out
      s.reference = if (plain) floatArrayOf(report[5], report[6], report[7]) else null
      s.serial++
    }
    s.dir.listFiles { f -> f.name.startsWith("cut_") }?.forEach { it.delete() }
    val png = File(s.dir, "cut_${s.serial}.png")
    ImageCodec.encode(out, png, OutputFormat.PNG, 100)
    return mapOf(
      "cutoutPath" to png.absolutePath,
      "width" to s.preview.width,
      "height" to s.preview.height,
      "report" to mapOf(
        "found" to (report[0] > 0.001f),
        "coverage" to report[0].toDouble(),
        "box" to listOf(report[1].toDouble(), report[2].toDouble(), report[3].toDouble(), report[4].toDouble()),
        "backdrop" to String.format(java.util.Locale.US, "#%02X%02X%02X", report[5].roundToInt().coerceIn(0, 255), report[6].roundToInt().coerceIn(0, 255), report[7].roundToInt().coerceIn(0, 255)),
        "plain" to plain,
        "objects" to report[10].toInt(),
      ),
    )
  }

  /** The full-resolution cut-out for the session's current corrections (cached). */
  private fun fullCutout(s: Session, token: Long): Bitmap {
    val key = Triple(s.strokes, s.matte, s.file.lastModified())
    synchronized(s) { s.fullCutout?.let { if (s.fullKey == key) return it } }
    val photo = ImageCodec.decode(s.file, min(budget.maxDecodeDimension, MAX_DECODE_EDGE)).bitmap
    try {
      val src = if (photo.config == Bitmap.Config.ARGB_8888) photo else photo.copy(Bitmap.Config.ARGB_8888, false)
      val out = createBitmap(src.width, src.height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
      try {
        NativeEngine.nativeProductCutout(src, s.hint, StudioStroke.encode(s.strokes), s.matte.toArray(), out, token, FloatArray(11))
      } finally {
        if (src !== photo) src.recycle()
      }
      synchronized(s) {
        s.fullCutout?.recycle()
        s.fullCutout = out
        s.fullKey = key
      }
      return out
    } finally {
      photo.recycle()
    }
  }

  override fun renderProduct(jobId: String, request: ReadableMap, promise: Promise) {
    val map = request.toHashMap()
    val full = map["quality"] == "full"
    @Suppress("UNCHECKED_CAST")
    val parsed = try {
      val setup = ProductSetup.fromMap(map["setup"] as? Map<String, Any?>)
      val profile = ExportProfile.fromMap(map["profile"] as? Map<String, Any?>, setup.backdrop.kind == ProductBackdropKind.TRANSPARENT)
      Triple(setup, profile, WatermarkSpec.fromMap(map["watermark"] as? Map<String, Any?>))
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val (setup, profile, watermark) = parsed
    if (full && Entitlements.plan != Plan.PREMIUM) {
      val reason = setup.premiumReason() ?: profile.premiumReason()
      if (reason != null) {
        promise.reject("PREMIUM_REQUIRED", reason)
        return
      }
    }
    jobs.submit(jobId, listener(promise)) { token ->
      val s = session(map)
      val cutout = if (full) fullCutout(s, token) else synchronized(s) { s.previewCutout }
        ?: throw EngineException(NO_PRODUCT, "No product was found in this photo")
      val (w, h) = if (full) profile.frame() else profile.previewFrame(PREVIEW_FRAME)
      val scene = setup.backdrop.plate?.takeIf { setup.backdrop.kind == ProductBackdropKind.SCENE }?.let { plate(it, max(w, h)) }
      val report = FloatArray(7)
      val handle = try {
        NativeEngine.nativeProductRender(cutout, scene, setup.ints(w, h), setup.floats(s.reference), token, report)
      } finally {
        scene?.recycle()
      }
      val shot = fromHandle(handle, transparent = profile.png)
      try {
        val box = listOf(report[0].toDouble(), report[1].toDouble(), report[2].toDouble(), report[3].toDouble())
        save(shot, full, profile, watermark) + mapOf("box" to box)
      } finally {
        shot.recycle()
      }
    }
  }

  private fun plate(id: String, edge: Int): Bitmap {
    val opts = BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 }
    val bmp = reactApplicationContext.assets.open(StudioModule.plateAsset(id)).use { BitmapFactory.decodeStream(it, null, opts) }
      ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown scene")
    val k = edge.toFloat() / max(bmp.width, bmp.height)
    if (k >= 1f) return bmp
    return bmp.scale(max(1, (bmp.width * k).roundToInt()), max(1, (bmp.height * k).roundToInt())).also { if (it !== bmp) bmp.recycle() }
  }

  private fun fromHandle(handle: Long, transparent: Boolean): Bitmap {
    try {
      // Straight alpha for a transparent PNG; an opaque shot can be drawn on (the watermark).
      val out = createBitmap(NativeEngine.nativeResultWidth(handle), NativeEngine.nativeResultHeight(handle), Bitmap.Config.ARGB_8888)
      if (transparent) out.isPremultiplied = false
      NativeEngine.nativeCopyResultToBitmap(handle, out)
      return out
    } finally {
      NativeEngine.nativeReleaseResult(handle)
    }
  }

  /**
   * A preview to the cache; a saved shot to the studio folder. Free plan:
   * the Photo Plus mark (the clean copy kept beside it). Premium: the
   * person's own brand mark when they chose one (not on a transparent PNG).
   */
  private fun save(shot: Bitmap, full: Boolean, profile: ExportProfile, watermark: WatermarkSpec): Map<String, Any?> {
    val ext = if (profile.png) "png" else "jpg"
    val target = if (full) {
      paths.studio.mkdirs()
      File(paths.studio, "Product_${stamp()}.$ext")
    } else {
      val dir = File(paths.processingCache, "product-previews").apply { mkdirs() }
      dir.listFiles()?.forEach { it.delete() }
      File(dir, "product_${System.nanoTime()}.$ext")
    }
    val plan = Entitlements.plan
    if (full && plan == Plan.PREMIUM && !profile.png) {
      ExportPolicy.resolve(watermark, plan)?.takeIf { watermark.custom != null }?.let { WatermarkRenderer.apply(shot, it) }
    }
    ImageCodec.encode(shot, target, if (profile.png) OutputFormat.PNG else OutputFormat.JPEG, if (full) profile.quality else 85)
    val clean = if (full && plan != Plan.PREMIUM) PhotoWatermark.applyToFile(target, WatermarkSpec(), plan) else null
    return mapOf(
      "outputPath" to target.absolutePath,
      "cleanPath" to clean,
      "width" to shot.width,
      "height" to shot.height,
      "sizeBytes" to target.length().toDouble(),
      "format" to if (profile.png) "png" else "jpeg",
    )
  }

  override fun closeProduct(sessionId: String) {
    sessions.remove(sessionId)?.let { s -> synchronized(s) { s.release() } }
  }

  override fun cancel(jobId: String) {
    jobs.cancel(jobId)
  }

  private fun stamp() = java.text.SimpleDateFormat("yyyyMMdd_HHmmss_SSS", java.util.Locale.US).format(java.util.Date())

  companion object {
    const val NAME = "PhotoPlusProduct"
    const val NO_PRODUCT = "NO_PRODUCT"
    private const val ANALYSIS_EDGE = 1536
    private const val PREVIEW_EDGE = 1280
    private const val PREVIEW_FRAME = 1000
    /** Product photos are cut out from up to this size (a 4K shot from a ~12 MP photo). */
    private const val MAX_DECODE_EDGE = 4096
  }
}
