package com.photoplus.studio

import com.photoplus.R
import com.photoplus.i18n.Words
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
    /** The face box from the last cut-out report (normalised), for hairstyles. */
    @Volatile var face: FloatArray? = null
    /** The last preview cut-out with hair, by what made it. */
    var hairPreview: Bitmap? = null
    var hairKey: Any? = null
    /** The faces' lip outlines (normalised, 262 floats each), found once per session. */
    @Volatile var lipMarks: List<FloatArray>? = null
    /** The last preview cut-out with pink lips, by what made it. */
    var lipsPreview: Bitmap? = null
    var lipsKey: Any? = null
    /** The last preview cut-out with studio light, by what made it. */
    var lightPreview: Bitmap? = null
    var lightKey: Any? = null
    var fullCutout: Bitmap? = null
    var fullKey: Any? = null
    var serial = 0
    val lastUsed = System.currentTimeMillis()

    fun release() {
      previewCutout?.recycle()
      fullCutout?.recycle()
      hairPreview?.recycle()
      lipsPreview?.recycle()
      lightPreview?.recycle()
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
    val report = FloatArray(26)
    NativeEngine.nativeStudioCutout(s.preview, s.mask, s.zones, StudioStroke.encode(s.strokes), s.settings.toArray(), out, token, report)
    synchronized(s) {
      s.previewCutout?.recycle()
      s.previewCutout = out
      s.serial++
      s.face = if (report[11] > 0.5f) floatArrayOf(report[12], report[13], report[14], report[15]) else null
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
    "faces" to r[16].toInt(),
    // The outfit's main colours (for backdrop colours that suit it).
    "outfit" to SceneStudio.outfit(r),
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
    if (full && !Entitlements.allows("sceneSwap")) {
      promise.reject("PREMIUM_REQUIRED", Words.get(reactApplicationContext, R.string.native_saving_studio_photos_is_part_of))
      return
    }
    val resolution = try {
      ExportSize.parse(map["resolution"])
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
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
    @Suppress("UNCHECKED_CAST")
    val hair = try {
      HairSpec.fromMap(map["hair"] as? Map<String, Any?>)
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    if (hair != null && !Entitlements.allows("hair")) {
      promise.reject("PREMIUM_REQUIRED", Words.get(reactApplicationContext, R.string.native_hair_colours_and_hairstyles_are_part))
      return
    }
    @Suppress("UNCHECKED_CAST")
    val lips = try {
      LipsSpec.fromMap(map["lips"] as? Map<String, Any?>)
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    @Suppress("UNCHECKED_CAST")
    val lightAndFinish = try {
      SceneLight.fromMap(map["light"] as? Map<String, Any?>) to SceneFinish.fromMap(map["finish"] as? Map<String, Any?>)
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val (light, finish) = lightAndFinish
    if (!Entitlements.allows("studioLight") && (light?.premium == true || finish?.premium == true)) {
      promise.reject("PREMIUM_REQUIRED", Words.get(reactApplicationContext, R.string.native_this_light_or_film_look_is))
      return
    }
    jobs.submit(jobId, listener(promise)) { token ->
      val s = session(map)
      val base = if (full) fullCutout(s, token) else synchronized(s) { s.previewCutout } ?: throw EngineException(NO_PERSON, "Nobody was found in this photo")
      val hidden = !full && map["hidePerson"] == true
      val haired = if (hair != null && !hidden) withHair(s, base, hair, full, token) else base
      val lipped = if (lips != null && !hidden) withLips(s, haired, lips, full, token) else haired
      val cutout = if (light != null && !hidden) withLight(s, lipped, light, full, token) else lipped
      if (background is SceneBackground.Transparent) {
        // `aligned`: the whole photo's frame (for painting hair brushes over the photo), not trimmed.
        return@submit transparent(s, cutout, full, aligned = !full && map["aligned"] == true, resolution = resolution)
      }
      val maxEdge = min(budget.maxDecodeDimension, MAX_EXPORT_EDGE)
      // Frame size: preview long edge, or as large as keeps the person's own detail.
      val (bgW, bgH) = backgroundSize(background, s)
      val cropW = bgW * crop.w
      val cropH = bgH * crop.h
      val drawn = background is SceneBackground.Plain || background is SceneBackground.Gradient
      val aspect = if (drawn) (aspectHint ?: (cutout.width.toFloat() / cutout.height)) else cropW / cropH
      val (fw, fh) = if (full) {
        val box = SceneStudio.alphaBox(cutout) ?: throw EngineException(NO_PERSON, "Nobody was found in this photo")
        SceneStudio.exportSize(aspect, box.height(), placement.height, maxEdge)
      } else {
        val k = SceneStudio.PREVIEW_EDGE / max(aspect, 1f)
        (k * aspect).roundToInt() to k.roundToInt()
      }
      val frame = when (background) {
        is SceneBackground.Plain -> SceneStudio.plain(background.color, background.vignette, fw, fh, background.light)
        is SceneBackground.Gradient -> SceneStudio.gradient(background, fw, fh)
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
        // The finished photo's grade (over the scene too, as a lens and film would).
        if (finish != null) {
          frame.isPremultiplied = false
          NativeEngine.nativeStudioFinish(frame, finish.toArray(), 3, token)
          frame.isPremultiplied = true
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
        // The download size asked for (a saved photo only).
        val (ow, oh) = if (full && resolution != null) {
          ExportSize.applyToFile(target, resolution)
          ExportSize.target(frame.width, frame.height, resolution)
        } else {
          frame.width to frame.height
        }
        mapOf(
          "outputPath" to target.absolutePath,
          "width" to ow,
          "height" to oh,
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

  /**
   * The cut-out with new hair: a copy (the session's own cut-out stays as it
   * is). Previews keep the last one, so dragging the person doesn't redraw
   * the hair; a full save draws it afresh (and the caller's frame is freed
   * by GC, as it is not the session's).
   */
  private fun withHair(s: Session, base: Bitmap, hair: HairSpec, full: Boolean, token: Long): Bitmap {
    val zones = s.zones ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Hair can't be found in this photo")
    val face = s.face
    if (hair.style != null && face == null) throw EngineException(NO_FACE, "A hairstyle needs a face in the photo")
    val key = Triple(hair, s.serial, base.generationId)
    if (!full) synchronized(s) { s.hairPreview?.let { if (s.hairKey == key) return it } }
    // A raw copy (the cut-out is not premultiplied; a Bitmap copy could convert it).
    val out = createBitmap(base.width, base.height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
    val raw = java.nio.ByteBuffer.allocateDirect(base.byteCount)
    base.copyPixelsToBuffer(raw)
    raw.rewind()
    out.copyPixelsFromBuffer(raw)
    val c = hair.color
    NativeEngine.nativeStudioHair(
      out, zones, hair.style, c != null, (c ?: 0) shr 16 and 0xFF, (c ?: 0) shr 8 and 0xFF, (c ?: 0) and 0xFF, hair.strength,
      face ?: FloatArray(4), hair.edit.toArray(), StudioStroke.encode(hair.strokes), 5, token,
    )
    if (!full) {
      synchronized(s) {
        if (s.hairPreview !== out) s.hairPreview?.recycle()
        s.hairPreview = out
        s.hairKey = key
      }
    }
    return out
  }

  /**
   * The cut-out with natural pink lips or lipstick: a copy (with hair already on it, if any).
   * The lips are found once per session on the photo (ML Kit face outlines);
   * previews keep the last result, so moving the person doesn't repaint.
   */
  private fun withLips(s: Session, base: Bitmap, lips: LipsSpec, full: Boolean, token: Long): Bitmap {
    val marks = s.lipMarks ?: FaceFinder.lipLandmarks(s.preview).also { s.lipMarks = it }
    if (marks.isEmpty()) throw EngineException(NO_FACE, "Lips need a face in the photo")
    val key = Triple(lips, s.serial, base.generationId)
    if (!full) synchronized(s) { s.lipsPreview?.let { if (s.lipsKey == key) return it } }
    val out = createBitmap(base.width, base.height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
    val raw = java.nio.ByteBuffer.allocateDirect(base.byteCount)
    base.copyPixelsToBuffer(raw)
    raw.rewind()
    out.copyPixelsFromBuffer(raw)
    val flat = FloatArray(marks.size * FaceLandmarks.FLOATS)
    marks.forEachIndexed { i, m -> m.copyInto(flat, i * FaceLandmarks.FLOATS) }
    val c = lips.color
    NativeEngine.nativeStudioLips(out, flat, marks.size, c shr 16 and 0xFF, c shr 8 and 0xFF, c and 0xFF, lips.strength, lips.natural, token, null)
    if (!full) {
      synchronized(s) {
        if (s.lipsPreview !== out) s.lipsPreview?.recycle()
        s.lipsPreview = out
        s.lipsKey = key
      }
    }
    return out
  }

  /**
   * The cut-out lit with studio light: a copy (with hair already on it, if
   * any). Previews keep the last one, so moving the person doesn't relight.
   */
  private fun withLight(s: Session, base: Bitmap, light: SceneLight, full: Boolean, token: Long): Bitmap {
    val key = Triple(light, s.serial, base.generationId)
    if (!full) synchronized(s) { s.lightPreview?.let { if (s.lightKey == key) return it } }
    val out = createBitmap(base.width, base.height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
    val raw = java.nio.ByteBuffer.allocateDirect(base.byteCount)
    base.copyPixelsToBuffer(raw)
    raw.rewind()
    out.copyPixelsFromBuffer(raw)
    NativeEngine.nativeStudioRelight(out, light.toArray(s.face), token)
    if (!full) {
      synchronized(s) {
        if (s.lightPreview !== out) s.lightPreview?.recycle()
        s.lightPreview = out
        s.lightKey = key
      }
    }
    return out
  }

  private fun transparent(s: Session, cutout: Bitmap, full: Boolean, aligned: Boolean = false, resolution: Int? = null): Map<String, Any?> {
    val trimmed = if (aligned) cutout.copy(Bitmap.Config.ARGB_8888, false) else SceneStudio.trimmed(cutout)
    try {
      val target = if (full) {
        paths.studio.mkdirs()
        File(paths.studio, "Studio_${stamp()}.png")
      } else {
        s.dir.listFiles { f -> f.name.startsWith("transparent_") }?.forEach { it.delete() }
        File(s.dir, "transparent_${System.nanoTime()}.png")
      }
      ImageCodec.encode(trimmed, target, OutputFormat.PNG, 100)
      val (ow, oh) = if (full && resolution != null) {
        ExportSize.applyToFile(target, resolution)
        ExportSize.target(trimmed.width, trimmed.height, resolution)
      } else {
        trimmed.width to trimmed.height
      }
      return mapOf(
        "outputPath" to target.absolutePath,
        "width" to ow,
        "height" to oh,
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
  private fun decodeBackground(bg: SceneBackground, edge: Int, crop: SceneCrop): Bitmap =
    SceneBackgrounds.decode(reactApplicationContext, { paths.resolve(it) }, bg, edge, crop, MAX_EXPORT_EDGE)

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

  // ------------------------------------------------------------------ Relight

  /**
   * Relight: the person, lit with studio light (and natural pink lips or a
   * lipstick, if chosen), back in their own photo; the background can be
   * darkened or brightened, softened and made more or less colourful, and the
   * whole photo finished with a grade, vignette, warmth and tint.
   */
  override fun renderRelight(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val full = map["quality"] == "full"
    @Suppress("UNCHECKED_CAST")
    val parsed = try {
      Triple(
        SceneLight.fromMap(map["light"] as? Map<String, Any?>),
        SceneFinish.fromMap(map["finish"] as? Map<String, Any?>),
        LipsSpec.fromMap(map["lips"] as? Map<String, Any?>),
      )
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val (light, finish, lips) = parsed
    if (full && !Entitlements.allows("relight")) {
      promise.reject("PREMIUM_REQUIRED", Words.get(reactApplicationContext, R.string.native_saving_relit_photos_is_part_of))
      return
    }
    if (!Entitlements.allows("studioLight") && (light?.premium == true || finish?.premium == true)) {
      promise.reject("PREMIUM_REQUIRED", Words.get(reactApplicationContext, R.string.native_this_light_or_film_look_is))
      return
    }
    val resolution = resolutionFor(map, full, promise) ?: return
    val bg = map["background"] as? Map<*, *>
    fun bgValue(k: String, lo: Float) = ((bg?.get(k) as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: 0f).coerceIn(lo, 1f)
    val brightness = bgValue("brightness", -1f)
    val colour = bgValue("colour", -1f)
    val blur = bgValue("blur", 0f)
    jobs.submit(jobId, listener(promise)) { token ->
      val s = session(map)
      val base = if (full) fullCutout(s, token) else synchronized(s) { s.previewCutout } ?: throw EngineException(NO_PERSON, "Nobody was found in this photo")
      val lipped = if (lips != null) withLips(s, base, lips, full, token) else base
      val lit = if (light != null) withLight(s, lipped, light, full, token) else lipped
      // The photo itself, at the cut-out's size (they share one frame).
      val photo = if (full) ImageCodec.decode(s.file, max(base.width, base.height)).bitmap else s.preview
      val frame = createBitmap(base.width, base.height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
      try {
        val sized = if (photo.width == base.width && photo.height == base.height) photo else photo.scale(base.width, base.height, true)
        try {
          android.graphics.Canvas(frame).drawBitmap(sized, 0f, 0f, null)
        } finally {
          if (sized !== photo) sized.recycle()
        }
        NativeEngine.nativeStudioRelightCompose(frame, lit, brightness, colour, blur, token)
        if (finish != null) NativeEngine.nativeStudioFinish(frame, finish.toArray(), 3, token)
        finishFilmCollage(frame, full, "Relight", resolution.px)
      } finally {
        frame.recycle()
        if (full) photo.recycle()
      }
    }
  }

  // ------------------------------------------------------------ Film & Collage

  /** Faces found in Film & Collage photos, by path (crops and zooms centre on them). */
  private val filmFocus = object : LinkedHashMap<String, Pair<Float, Float>?>(16, 0.75f, true) {
    override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, Pair<Float, Float>?>?) = size > 48
  }

  override fun renderFilm(jobId: String, request: ReadableMap, promise: Promise) {
    val map = request.toHashMap()
    val spec = try {
      FilmCollage.filmFromMap(map)
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    if (spec.style.premium && Entitlements.plan != Plan.PREMIUM) {
      promise.reject("PREMIUM_REQUIRED", Words.get(reactApplicationContext, R.string.native_this_film_is_part_of_premium))
      return
    }
    val full = map["quality"] == "full"
    val resolution = resolutionFor(map, full, promise) ?: return
    jobs.submit(jobId, listener(promise)) { token ->
      val files = spec.photos.map { paths.resolve(it) }
      // Preview frames are small; a saved strip keeps each frame sharp, within the memory budget.
      val frameSize = if (full) min(FILM_FRAME_FULL, budget.maxDecodeDimension / 3) else FILM_FRAME_PREVIEW
      FilmCollage.decode(files, if (full) min(budget.maxDecodeDimension, FILM_DECODE_FULL) else FILM_DECODE_PREVIEW, filmFocus).use { photos ->
        val out = FilmCollage.renderFilm(spec, photos, frameSize, token)
        try {
          finishFilmCollage(out, full, "Film", resolution.px)
        } finally {
          out.recycle()
        }
      }
    }
  }

  override fun renderCollage(jobId: String, request: ReadableMap, promise: Promise) {
    val map = request.toHashMap()
    val spec = try {
      FilmCollage.collageFromMap(map)
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    val full = map["quality"] == "full"
    val resolution = resolutionFor(map, full, promise) ?: return
    jobs.submit(jobId, listener(promise)) { token ->
      val files = spec.photos.map { paths.resolve(it) }
      val edge = if (full) min(budget.maxDecodeDimension, COLLAGE_EDGE_FULL) else COLLAGE_EDGE_PREVIEW
      FilmCollage.decode(files, if (full) min(budget.maxDecodeDimension, FILM_DECODE_FULL) else FILM_DECODE_PREVIEW, filmFocus).use { photos ->
        val out = FilmCollage.renderCollage(spec, photos, edge, token)
        try {
          finishFilmCollage(out, full, "Collage", resolution.px)
        } finally {
          out.recycle()
        }
      }
    }
  }

  /**
   * A preview goes to the cache; a saved one to the studio folder, with the
   * Photo Plus mark on the free plan (the clean copy is kept beside it).
   */
  /** The download size asked for (wrapped, so "as made" is not mistaken for a rejected request); null when rejected. */
  private class Resolution(val px: Int?)

  private fun resolutionFor(map: Map<String, Any?>, full: Boolean, promise: Promise): Resolution? {
    val px = try {
      if (full) ExportSize.parse(map["resolution"]) else null
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return null
    }
    if (ExportSize.needsPremium(px) && !Entitlements.allows("exportHd")) {
      promise.reject("PREMIUM_REQUIRED", Words.get(reactApplicationContext, R.string.native_720p_1080p_hd_and_4k_downloads))
      return null
    }
    return Resolution(px)
  }

  private fun finishFilmCollage(source: Bitmap, full: Boolean, name: String, resolution: Int? = null): Map<String, Any?> {
    val out = if (full) ExportSize.scaled(source, resolution) else source
    try {
      return encodeStudio(out, full, name)
    } finally {
      if (out !== source) out.recycle()
    }
  }

  private fun encodeStudio(out: Bitmap, full: Boolean, name: String): Map<String, Any?> {
    val target = if (full) {
      paths.studio.mkdirs()
      File(paths.studio, "${name}_${stamp()}.jpg")
    } else {
      val dir = File(paths.processingCache, "studio-${name.lowercase()}").apply { mkdirs() }
      dir.listFiles()?.forEach { it.delete() }
      File(dir, "${name.lowercase()}_${System.nanoTime()}.jpg")
    }
    ImageCodec.encode(out, target, OutputFormat.JPEG, if (full) 95 else 88)
    val clean = if (full) com.photoplus.edit.PhotoWatermark.applyToFile(target, com.photoplus.edit.WatermarkSpec(), Entitlements.plan) else null
    return mapOf(
      "outputPath" to target.absolutePath,
      "cleanPath" to clean,
      "width" to out.width,
      "height" to out.height,
      "sizeBytes" to target.length().toDouble(),
      "format" to "jpeg",
    )
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
    const val NO_FACE = "NO_FACE"
    /** Person masks are found on a copy this size. */
    private const val ANALYSIS_EDGE = 2048
    /** Exports: up to 4096 px on the long edge (~12 MP at 4:3). */
    private const val MAX_EXPORT_EDGE = 4096
    private const val FILM_FRAME_PREVIEW = 300
    private const val FILM_FRAME_FULL = 1080
    private const val FILM_DECODE_PREVIEW = 900
    private const val FILM_DECODE_FULL = 2400
    private const val COLLAGE_EDGE_PREVIEW = 1100
    private const val COLLAGE_EDGE_FULL = 3200

    fun plateAsset(id: String) = "studio/plates/$id.jpg"
  }
}
