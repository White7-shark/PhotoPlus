package com.photoplus.studio

import android.content.ContentValues
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.provider.MediaStore
import androidx.core.graphics.createBitmap
import androidx.core.graphics.scale
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.device.DeviceInfo
import com.photoplus.device.MemoryPressureMonitor
import com.photoplus.edit.Plan
import com.photoplus.engine.EngineException
import com.photoplus.engine.ImageCodec
import com.photoplus.engine.JobListener
import com.photoplus.engine.JobManager
import com.photoplus.engine.MemoryBudgetPolicy
import com.photoplus.engine.NativeCancelTokens
import com.photoplus.engine.NativeEngine
import com.photoplus.engine.OutputFormat
import com.photoplus.entitlement.Entitlements
import com.photoplus.specs.NativePhotoPlusStudioSpec
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
 * The photoshoot studio for the app (see SceneStudio and the engine's
 * studio module):
 *
 *  1. startSession — the photo's person mask (ML Kit subject segmentation,
 *     then the bundled person model, then the engine's tap cut-out; taps pick
 *     who), the person-parts map (hair, skin, clothes), and a first cut-out
 *     preview;
 *  2. refineCutout — the preview again with the person's corrections;
 *  3. renderScene — the person placed in a background: a quick preview, or
 *     the full-quality photo (Premium), sized so the person keeps their own
 *     detail; or a transparent PNG.
 *
 * One worker: studio work is heavy, and a newer request for the same session
 * simply follows the previous one.
 */
@ReactModule(name = StudioModule.NAME)
class StudioModule(reactContext: ReactApplicationContext) :
  NativePhotoPlusStudioSpec(reactContext), MemoryPressureMonitor.Listener {

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
    /** The person mask and the parts map (any size: the engine fits them to the photo). */
    val mask: Bitmap,
    val zones: Bitmap?,
    /** The photo, upright, at preview size. */
    val preview: Bitmap,
    val source: String,
  ) {
    @Volatile var strokes: List<StudioStroke> = emptyList()
    @Volatile var settings = CutoutSettings()
    @Volatile var previewCutout: Bitmap? = null
    var fullCutout: Bitmap? = null
    var fullKey: Any? = null
    var serial = 0
    val lastUsed = System.currentTimeMillis()

    fun release() {
      previewCutout?.recycle()
      fullCutout?.recycle()
      mask.recycle()
      zones?.recycle()
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
    // The full-size cut-out is rebuilt when needed.
    sessions.values.forEach { s ->
      synchronized(s) {
        s.fullCutout?.recycle()
        s.fullCutout = null
        s.fullKey = null
      }
    }
  }

  private fun requireEngine(promise: Promise): Boolean {
    if (engineAvailable) return true
    promise.reject(EngineException.INTERNAL, "Processing engine is unavailable on this device")
    return false
  }

  private fun listener(promise: Promise) = object : JobListener<Map<String, Any?>> {
    override fun onSuccess(jobId: String, result: Map<String, Any?>) = promise.resolve(result.toWritableMap())
    override fun onFailure(jobId: String, error: EngineException) = promise.reject(error.code, error.message, error)
  }

  private fun session(map: Map<String, Any?>): Session =
    sessions[map["sessionId"] as? String ?: ""] ?: throw EngineException(EngineException.INVALID_ARGUMENT, "This studio session has ended")

  override fun startSession(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val file = try {
      paths.resolve(map["inputPath"] as? String ?: "")
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val points = PortraitParams.points(map["points"])
    jobs.submit(jobId, listener(promise)) { token ->
      // At most two sessions: an older one is ended.
      if (sessions.size >= 2) sessions.values.minByOrNull { it.lastUsed }?.let { sessions.remove(it.id)?.release() }
      val analysis = ImageCodec.decode(file, ANALYSIS_EDGE).bitmap
      try {
        val maskBytes = portraitProcessor.cutout(analysis, points)
          ?: throw EngineException(NO_PERSON, "Nobody was found in this photo")
        val mask = PortraitProcessor.alphaBitmap(maskBytes, analysis.width, analysis.height)
        val zones = PersonParts.segment(reactApplicationContext, analysis)?.let { PortraitProcessor.alphaBitmap(it.bytes, it.width, it.height) }
        val k = min(1f, SceneStudio.PREVIEW_EDGE.toFloat() / max(analysis.width, analysis.height))
        val preview = if (k < 1f) analysis.scale(max(1, (analysis.width * k).roundToInt()), max(1, (analysis.height * k).roundToInt())) else analysis.copy(Bitmap.Config.ARGB_8888, false)
        val id = UUID.randomUUID().toString()
        val dir = File(paths.processingCache, "studio-$id").apply { mkdirs() }
        val s = Session(id, file, dir, mask, zones, preview, if (points.isEmpty()) "auto" else "taps")
        sessions[id] = s
        val photoPath = File(dir, "photo.jpg").also { ImageCodec.encode(preview, it, OutputFormat.JPEG, 90) }
        val result = cutPreview(s, token)
        result + mapOf("sessionId" to id, "photoPath" to photoPath.absolutePath, "parts" to (zones != null))
      } finally {
        analysis.recycle()
      }
    }
  }

  override fun refineCutout(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    jobs.submit(jobId, listener(promise)) { token ->
      val s = session(map)
      s.strokes = StudioStroke.listFrom(map["strokes"])
      s.settings = CutoutSettings.fromMap(map)
      cutPreview(s, token)
    }
  }

  /** The cut-out of the preview photo with the session's corrections, saved as a PNG. */
  private fun cutPreview(s: Session, token: Long): Map<String, Any?> {
    val out = createBitmap(s.preview.width, s.preview.height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
    val report = FloatArray(16)
    NativeEngine.nativeStudioCutout(s.preview, s.mask, s.zones, StudioStroke.encode(s.strokes), s.settings.toArray(), out, token, report)
    synchronized(s) {
      s.previewCutout?.recycle()
      s.previewCutout = out
      s.serial++
    }
    // A new name each time, so the app's image cache shows the new one.
    s.dir.listFiles { f -> f.name.startsWith("cut_") }?.forEach { it.delete() }
    val png = File(s.dir, "cut_${s.serial}.png")
    ImageCodec.encode(out, png, OutputFormat.PNG, 100)
    return mapOf(
      "cutoutPath" to png.absolutePath,
      "width" to s.preview.width,
      "height" to s.preview.height,
      "report" to reportMap(report),
    )
  }

  private fun reportMap(r: FloatArray) = mapOf(
    "coverage" to r[0].toDouble(),
    "edge" to r[1].toDouble(),
    "hairEdge" to r[2].toDouble(),
    "box" to listOf(r[3].toDouble(), r[4].toDouble(), r[5].toDouble(), r[6].toDouble()),
    "cutLeft" to (r[7] > 0.5f),
    "cutTop" to (r[8] > 0.5f),
    "cutRight" to (r[9] > 0.5f),
    "cutBottom" to (r[10] > 0.5f),
    "found" to (r[0] > 0.002f),
    // The face's box: its size tells how tall the person really is.
    "face" to if (r[11] > 0.5f) listOf(r[12].toDouble(), r[13].toDouble(), r[14].toDouble(), r[15].toDouble()) else null,
  )

  /** The full-resolution cut-out for the session's current corrections (cached). */
  private fun fullCutout(s: Session, token: Long): Bitmap {
    val key = Triple(s.strokes, s.settings, s.file.lastModified())
    synchronized(s) {
      s.fullCutout?.let { if (s.fullKey == key) return it }
    }
    val maxDim = min(budget.maxDecodeDimension, MAX_EXPORT_EDGE)
    val photo = ImageCodec.decode(s.file, maxDim).bitmap
    try {
      val out = createBitmap(photo.width, photo.height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
      val src = if (photo.config == Bitmap.Config.ARGB_8888) photo else photo.copy(Bitmap.Config.ARGB_8888, false)
      try {
        NativeEngine.nativeStudioCutout(src, s.mask, s.zones, StudioStroke.encode(s.strokes), s.settings.toArray(), out, token, FloatArray(16))
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

  override fun renderScene(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val full = map["quality"] == "full"
    if (full && Entitlements.plan != Plan.PREMIUM) {
      promise.reject("PREMIUM_REQUIRED", "Saving studio photos is part of Premium")
      return
    }
    @Suppress("UNCHECKED_CAST")
    val parsed = try {
      Triple(
        SceneBackground.fromMap(map["background"] as? Map<String, Any?>),
        SceneCrop.fromMap(map["crop"] as? Map<String, Any?>),
        ScenePlacement.fromMap(map["placement"] as? Map<String, Any?>) to SceneLook.fromMap(map["look"] as? Map<String, Any?>),
      )
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val (background, crop, pl) = parsed
    val (placement, look) = pl
    val aspectHint = (map["aspect"] as? Number)?.toFloat()?.takeIf { it.isFinite() && it in 0.2f..5f }
    jobs.submit(jobId, listener(promise)) { token ->
      val s = session(map)
      val cutout = if (full) fullCutout(s, token) else synchronized(s) { s.previewCutout } ?: throw EngineException(NO_PERSON, "Nobody was found in this photo")
      if (background is SceneBackground.Transparent) {
        return@submit transparent(s, cutout, full)
      }
      val maxEdge = min(budget.maxDecodeDimension, MAX_EXPORT_EDGE)
      // Frame size: preview long edge, or as large as keeps the person's own detail.
      val (bgW, bgH) = backgroundSize(background, s)
      val cropW = bgW * crop.w
      val cropH = bgH * crop.h
      val aspect = if (background is SceneBackground.Plain) (aspectHint ?: (cutout.width.toFloat() / cutout.height)) else cropW / cropH
      val (fw, fh) = if (full) {
        val box = SceneStudio.alphaBox(cutout) ?: throw EngineException(NO_PERSON, "Nobody was found in this photo")
        SceneStudio.exportSize(aspect, box.height(), placement.height, maxEdge)
      } else {
        val k = SceneStudio.PREVIEW_EDGE / max(aspect, 1f)
        (k * aspect).roundToInt() to k.roundToInt()
      }
      val frame = when (background) {
        is SceneBackground.Plain -> SceneStudio.plain(background.color, background.vignette, fw, fh)
        else -> {
          val src = decodeBackground(background, max(fw, fh), crop)
          try {
            SceneStudio.frame(src, crop, fw, fh)
          } finally {
            src.recycle()
          }
        }
      }
      try {
        val report = FloatArray(9)
        // The scene alone (for moving the person over it smoothly in the app).
        val hidePerson = !full && map["hidePerson"] == true
        if (!hidePerson) {
          NativeEngine.nativeStudioCompose(frame, cutout, placement.toArray(), look.copy(seed = s.serial + 11).toArray(), token, report)
        }
        val target = if (full) {
          paths.studio.mkdirs()
          File(paths.studio, "Studio_${stamp()}.jpg")
        } else {
          val prefix = if (hidePerson) "empty_" else "scene_"
          s.dir.listFiles { f -> f.name.startsWith(prefix) }?.forEach { it.delete() }
          File(s.dir, "$prefix${System.nanoTime()}.jpg")
        }
        ImageCodec.encode(frame, target, OutputFormat.JPEG, if (full) 95 else 88)
        mapOf(
          "outputPath" to target.absolutePath,
          "width" to frame.width,
          "height" to frame.height,
          "sizeBytes" to target.length().toDouble(),
          "box" to listOf(report[0].toDouble(), report[1].toDouble(), report[2].toDouble(), report[3].toDouble()),
          "exposure" to report[4].toDouble(),
          "grounded" to (report[8] > 0.5f),
          "format" to "jpeg",
        )
      } finally {
        frame.recycle()
      }
    }
  }

  private fun transparent(s: Session, cutout: Bitmap, full: Boolean): Map<String, Any?> {
    val trimmed = SceneStudio.trimmed(cutout)
    try {
      val target = if (full) {
        paths.studio.mkdirs()
        File(paths.studio, "Studio_${stamp()}.png")
      } else {
        File(s.dir, "transparent_${System.nanoTime()}.png")
      }
      ImageCodec.encode(trimmed, target, OutputFormat.PNG, 100)
      return mapOf(
        "outputPath" to target.absolutePath,
        "width" to trimmed.width,
        "height" to trimmed.height,
        "sizeBytes" to target.length().toDouble(),
        "format" to "png",
      )
    } finally {
      trimmed.recycle()
    }
  }

  private fun backgroundSize(bg: SceneBackground, s: Session): Pair<Int, Int> = when (bg) {
    is SceneBackground.Plate -> reactApplicationContext.assets.open(plateAsset(bg.assetId)).use { input ->
      val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
      BitmapFactory.decodeStream(input, null, o)
      if (o.outWidth <= 0) throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown background")
      o.outWidth to o.outHeight
    }
    is SceneBackground.Photo -> {
      val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
      BitmapFactory.decodeFile(paths.resolve(bg.path).absolutePath, o)
      if (o.outWidth <= 0) throw EngineException(EngineException.IO_ERROR, "The background photo could not be read")
      // Orientation is applied on decode; a turned photo swaps its sides.
      val turned = ImageCodec.decode(paths.resolve(bg.path), 64).bitmap.let { b -> (b.width > b.height).also { b.recycle() } }
      if (turned == (o.outWidth > o.outHeight)) o.outWidth to o.outHeight else o.outHeight to o.outWidth
    }
    else -> s.preview.width to s.preview.height
  }

  /** The background, decoded so its crop is at least `edge` on the long side. */
  private fun decodeBackground(bg: SceneBackground, edge: Int, crop: SceneCrop): Bitmap {
    val need = (edge / max(crop.w, crop.h)).roundToInt()
    return when (bg) {
      is SceneBackground.Plate -> {
        val bounds = reactApplicationContext.assets.open(plateAsset(bg.assetId)).use { input ->
          BitmapFactory.Options().apply { inJustDecodeBounds = true }.also { BitmapFactory.decodeStream(input, null, it) }
        }
        reactApplicationContext.assets.open(plateAsset(bg.assetId)).use { input ->
          BitmapFactory.decodeStream(input, null, SceneStudio.decodeOptions(bounds.outWidth, bounds.outHeight, need))
            ?: throw EngineException(EngineException.IO_ERROR, "The background could not be read")
        }
      }
      is SceneBackground.Photo -> ImageCodec.decode(paths.resolve(bg.path), min(need, MAX_EXPORT_EDGE)).bitmap
      else -> throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown background")
    }
  }

  override fun saveToGallery(path: String, promise: Promise) {
    val file = try {
      paths.resolve(path)
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    if (!file.isFile || !file.canonicalPath.startsWith(paths.studio.canonicalPath + File.separator)) {
      promise.reject(EngineException.INVALID_ARGUMENT, "Only studio photos can be saved from here")
      return
    }
    try {
      val png = file.extension.equals("png", ignoreCase = true)
      val resolver = reactApplicationContext.contentResolver
      val values = ContentValues().apply {
        put(MediaStore.Images.Media.DISPLAY_NAME, file.name)
        put(MediaStore.Images.Media.MIME_TYPE, if (png) "image/png" else "image/jpeg")
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
      promise.resolve(uri.toString())
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
    } catch (e: Exception) {
      promise.reject(EngineException.IO_ERROR, e.message, e)
    }
  }

  override fun endSession(sessionId: String) {
    sessions.remove(sessionId)?.let { s -> synchronized(s) { s.release() } }
  }

  override fun cancel(jobId: String) {
    jobs.cancel(jobId)
  }

  private fun stamp() = java.text.SimpleDateFormat("yyyyMMdd_HHmmss_SSS", java.util.Locale.US).format(java.util.Date())

  companion object {
    const val NAME = "PhotoPlusStudio"
    const val NO_PERSON = "NO_PERSON"
    /** Person masks are found on a copy this size. */
    private const val ANALYSIS_EDGE = 2048
    /** Exports: up to 4096 px on the long edge (~12 MP at 4:3). */
    private const val MAX_EXPORT_EDGE = 4096

    fun plateAsset(id: String) = "studio/plates/$id.jpg"
  }
}
