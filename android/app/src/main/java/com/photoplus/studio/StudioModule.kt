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
import com.photoplus.R
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
import com.photoplus.entitlement.ArtsAllowance
import com.photoplus.entitlement.Entitlements
import com.photoplus.entitlement.MotionAllowance
import com.photoplus.entitlement.ToonAllowance
import com.photoplus.i18n.Words
import com.photoplus.specs.NativePhotoPlusStudioSpec
import com.photoplus.storage.AppPaths
import com.photoplus.studio.toon.ModelToonEngine
import com.photoplus.studio.toon.ToonEngines
import com.photoplus.studio.toon.ToonInput
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

  // ------------------------------------------------------------ Masters

  /** Faces and person masks of the photos painted lately, by path (each artist reuses them). */
  private val artSubjects = object : LinkedHashMap<String, Pair<Bitmap?, FloatArray>>(4, 0.75f, true) {
    override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, Pair<Bitmap?, FloatArray>>?) = size > 3
  }

  /**
   * Masters: the photo painted as a great artist worked. request: { inputPath,
   * artist (0..19), level (0 study, 1 painting, 2 masterpiece), quality
   * ('thumb' | 'preview' | 'full'), seed }. Thumbs and previews are free and
   * unlimited; a finished painting is Premium, or one a day on the free plan.
   */
  override fun renderMasters(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val artist = (map["artist"] as? Number)?.toInt() ?: -1
    val level = (map["level"] as? Number)?.toInt() ?: 1
    val quality = map["quality"] as? String ?: "preview"
    val seed = (map["seed"] as? Number)?.toInt() ?: 7
    val pencil = map["mode"] == "pencil"
    val controls = artControls(map["controls"])
    val pencilOptions = pencilOptions(map["pencil"])
    val frame = (map["frame"] as? Number)?.toInt() ?: 0
    // The live studio: a preview of its own (never replaced by a newer preview while it plays), with its blank sheet.
    val live = map["live"] == true
    if (controls == null || pencilOptions == null || frame !in 0 until ART_FRAMES) {
      promise.reject("INVALID_ARGUMENT", Words.get(reactApplicationContext, R.string.native_masters_unknown_artist))
      return
    }
    if (artist !in 0 until ARTISTS || level !in 0..2 || quality !in setOf("thumb", "preview", "full")) {
      promise.reject("INVALID_ARGUMENT", Words.get(reactApplicationContext, R.string.native_masters_unknown_artist))
      return
    }
    val full = quality == "full"
    if (full && !ArtsAllowance.allowed(reactApplicationContext)) {
      // Photo Arts on the free plan: five works a day (paintings and drawings), each after an ad.
      val left = (ArtsAllowance.state(reactApplicationContext)["remaining"] as? Int) ?: 0
      if (left > 0) promise.reject("AD_REQUIRED", Words.get(reactApplicationContext, R.string.native_arts_ad_required))
      else promise.reject("PREMIUM_REQUIRED", Words.get(reactApplicationContext, R.string.native_arts_daily_used))
      return
    }
    // The signature is Premium's: on the free plan it is left off.
    val signature = ArtSignature.parse(map["signature"])?.takeIf { Entitlements.plan == Plan.PREMIUM && quality != "thumb" }
    val file = paths.resolve(map["inputPath"] as? String ?: "")
    jobs.submit(jobId, listener(promise)) { token ->
      // Big enough for the level's brushes (a drawing: HD and above), within the phone's memory.
      val edge = when (quality) {
        "thumb" -> MASTERS_THUMB
        "preview" -> MASTERS_PREVIEW
        else -> min(budget.maxDecodeDimension, if (pencil) min(PENCIL_FULL[level], pencilEdgeFor(budget.engineBytes)) else MASTERS_FULL[level])
      }
      val decoded = ImageCodec.decode(file, edge).bitmap
      val bitmap = if (decoded.isMutable && decoded.config == Bitmap.Config.ARGB_8888) decoded
      else decoded.copy(Bitmap.Config.ARGB_8888, true).also { decoded.recycle() }
      try {
        val (mask, faces) = synchronized(artSubjects) { artSubjects[file.absolutePath] } ?: portraitProcessor.personAndFaces(bitmap).also { found ->
          synchronized(artSubjects) { artSubjects[file.absolutePath] = found }
        }
        val blank = if (live) bitmap.copy(Bitmap.Config.ARGB_8888, true) else null
        // Cards show each artist's own way; the preview and the work follow the person's controls.
        if (pencil) {
          NativeEngine.nativeArtPencil(bitmap, artist, if (quality == "thumb") 0 else level, mask, faces, seed, pencilOptions, token)
        } else {
          NativeEngine.nativeArtPaint(bitmap, artist, if (quality == "thumb") 0 else level, mask, faces, seed, if (quality == "thumb") null else controls, token)
        }
        signature?.let { ArtSignature.apply(bitmap, it, if (pencil) pencilOptions[0].roundToInt() else -1, java.time.Year.now().value) }
        // The frame goes on the preview too, so what is seen is what is saved (cards stay unframed).
        val shown = if (quality == "thumb") bitmap else framed(bitmap, frame, token)
        // The picture inside the frame (normalised), for the live studio's strokes.
        val ix = (shown.width - bitmap.width) / 2f / shown.width
        val iy = (shown.height - bitmap.height) / 2f / shown.height
        val inner = listOf(ix.toDouble(), iy.toDouble(), (1 - ix).toDouble(), (1 - iy).toDouble())
        val name = when {
          quality == "thumb" -> "${if (pencil) "PencilThumb" else "MastersThumb"}$artist"
          live -> "MastersLive"
          pencil -> "PencilArt"
          else -> "Masters"
        }
        val out = try {
          encodeStudio(shown, full, name)
        } finally {
          if (shown !== bitmap) shown.recycle()
        }
        // The blank sheet (or primed ground) the work starts from, in the same frame.
        val blankPath = blank?.let { b ->
          try {
            NativeEngine.nativeArtBlank(b, if (pencil) 1 else 0, artist, seed, pencilOptions)
            val bs = framed(b, frame, token)
            try {
              encodeStudio(bs, false, "MastersLiveBlank")["outputPath"]
            } finally {
              if (bs !== b) bs.recycle()
            }
          } finally {
            b.recycle()
          }
        }
        if (full) ArtsAllowance.use(reactApplicationContext)
        val remaining = ArtsAllowance.state(reactApplicationContext)["remaining"] as Int
        out + mapOf("remaining" to remaining, "inner" to inner) + (if (blankPath != null) mapOf("blankPath" to blankPath) else emptyMap())
      } finally {
        bitmap.recycle()
      }
    }
  }

  /** The largest drawing the engine's memory allows (about 64 bytes a pixel at its peak, a 4:3 sheet). */
  private fun pencilEdgeFor(engineBytes: Long): Int = kotlin.math.sqrt(engineBytes / 64.0 / 0.75).toInt().coerceIn(1200, 4096)

  /** Toon's working memory is about 120 bytes a pixel at its peak (measured at 9 MP): the long side that fits. */
  private fun toonEdgeFor(engineBytes: Long): Int = kotlin.math.sqrt(engineBytes / 128.0 / 0.75).toInt().coerceIn(1600, 6144)

  /** Toon's subjects (mask, faces, contours) of the photos drawn lately, by path. */
  private val toonSubjects = object : LinkedHashMap<String, PortraitProcessor.Subjects>(4, 0.75f, true) {
    override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, PortraitProcessor.Subjects>?) = size > 3
  }

  /**
   * Anime & Cartoon: { inputPath, quality 'thumb' | 'preview' | 'full', style 0..5,
   * background 0..5, lines, shades 2..6, vivid, detail, eyes, skin, glow, strength 0..1,
   * outline, backdrop, brightness -1..1, warmth -1..1, texture, faceDetail, hairDetail, ink 0..3, seed }
   * → { outputPath, cleanPath, width, height, remaining }. Previews are
   * free; the finished picture is Premium, or two a day on the free plan,
   * each after an ad.
   */
  override fun renderToon(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val quality = map["quality"] as? String ?: "preview"
    fun num(key: String, def: Float, lo: Float, hi: Float): Float? {
      val v = map[key] ?: return def
      val f = (v as? Number)?.toFloat() ?: return null
      return if (f.isFinite()) f.coerceIn(lo, hi) else null
    }
    val params = listOf(
      num("style", 0f, 0f, 5f)?.roundToInt()?.toFloat(), num("background", 0f, 0f, 5f)?.roundToInt()?.toFloat(),
      num("lines", 0.5f, 0f, 1f), num("shades", 3f, 2f, 6f)?.roundToInt()?.toFloat(), num("vivid", 0.3f, -1f, 1f),
      num("detail", 0.5f, 0f, 1f), num("eyes", 0.4f, 0f, 1f), num("skin", 0.6f, 0f, 1f), num("glow", 0.3f, 0f, 1f),
      num("strength", 1f, 0f, 1f),
      num("outline", 0.5f, 0f, 1f), num("backdrop", 0.5f, 0f, 1f), num("brightness", 0f, -1f, 1f), num("warmth", 0f, -1f, 1f),
      num("texture", 0.5f, 0f, 1f), num("faceDetail", 0.5f, 0f, 1f), num("hairDetail", 0.5f, 0f, 1f), num("ink", 0f, 0f, 3f)?.roundToInt()?.toFloat(),
    )
    if (params.any { it == null } || quality !in setOf("thumb", "preview", "full")) {
      promise.reject("INVALID_ARGUMENT", Words.get(reactApplicationContext, R.string.native_toon_bad_settings))
      return
    }
    val full = quality == "full"
    if (full && !ToonAllowance.allowed(reactApplicationContext)) {
      val left = (ToonAllowance.state(reactApplicationContext)["remaining"] as? Int) ?: 0
      if (left > 0) promise.reject("AD_REQUIRED", Words.get(reactApplicationContext, R.string.native_toon_ad_required))
      else promise.reject("PREMIUM_REQUIRED", Words.get(reactApplicationContext, R.string.native_toon_daily_used))
      return
    }
    val seed = (map["seed"] as? Number)?.toInt() ?: 7
    val file = paths.resolve(map["inputPath"] as? String ?: "")
    val p = params.map { it!! }.toFloatArray()
    jobs.submit(jobId, listener(promise)) { token ->
      // Previews at 1024 px; the finished picture at the photo's full size, as far as memory allows.
      val edge = when (quality) {
        "thumb" -> MASTERS_THUMB
        "preview" -> TOON_PREVIEW
        else -> min(budget.maxDecodeDimension, toonEdgeFor(budget.engineBytes))
      }
      val decoded = ImageCodec.decode(file, edge).bitmap
      val bitmap = if (decoded.isMutable && decoded.config == Bitmap.Config.ARGB_8888) decoded
      else decoded.copy(Bitmap.Config.ARGB_8888, true).also { decoded.recycle() }
      try {
        // The person, faces and their contours: found once per photo (every style and size reuses them).
        val subjects = synchronized(toonSubjects) { toonSubjects[file.absolutePath] }
          ?: portraitProcessor.personFacesAndContours(bitmap).also { found -> synchronized(toonSubjects) { toonSubjects[file.absolutePath] = found } }
        val input = ToonInput(bitmap, p, subjects.mask, subjects.faces, subjects.contours.takeIf { it.isNotEmpty() }, seed)
        ToonEngines.render(ToonEngines.pick(ToonEngines.modelFlag, ModelToonEngine(reactApplicationContext.assets)), input, token)
        val out = encodeStudio(bitmap, full, if (quality == "thumb") "ToonThumb${p[0].roundToInt()}" else "Toon", keep = if (full) 0 else TOON_PREVIEWS_KEPT)
        if (full) ToonAllowance.use(reactApplicationContext)
        out + mapOf("remaining" to (ToonAllowance.state(reactApplicationContext)["remaining"] as Int))
      } finally {
        bitmap.recycle()
      }
    }
  }

  /** { remaining (-1 unlimited), limit, credits, needsAd } — Anime & Cartoon on the free plan today. */
  override fun toonAllowance(promise: Promise) {
    promise.resolve(ToonAllowance.state(reactApplicationContext).toWritableMap())
  }

  /** Pencil Art shares Photo Arts' allowance: { remaining, limit, credits, needsAd, owed, maxOwed, frozen }. */
  override fun pencilAllowance(promise: Promise) {
    promise.resolve(ArtsAllowance.state(reactApplicationContext).toWritableMap())
  }

  /**
   * The drawing's options from { medium 0..2, paper 0..3, intensity 0..1,
   * lines 0.5..1.5, shading 0.5..1.5 } (each optional), clamped; null if a
   * value isn't a number.
   */
  private fun pencilOptions(raw: Any?): FloatArray? {
    val defaults = floatArrayOf(0f, 0f, 0.6f, 1f, 1f)
    val m = raw as? Map<*, *> ?: return defaults
    val names = listOf("medium" to (0f to 2f), "paper" to (0f to 3f), "intensity" to (0f to 1f), "lines" to (0.5f to 1.5f), "shading" to (0.5f to 1.5f))
    return FloatArray(5) { i ->
      val (name, range) = names[i]
      val v = m[name] ?: return@FloatArray defaults[i]
      val f = (v as? Number)?.toFloat() ?: return null
      if (!f.isFinite()) return null
      val c = f.coerceIn(range.first, range.second)
      if (i < 2) c.roundToInt().toFloat() else c
    }
  }

  /**
   * The person's painting controls from { brush, palette, vivid, likeness,
   * texture, age, background } (each optional, the artist's own way when
   * absent), clamped to the engine's ranges; null if a value isn't a number.
   */
  private fun artControls(raw: Any?): FloatArray? {
    val m = raw as? Map<*, *> ?: return ART_DEFAULTS.copyOf()
    return FloatArray(ART_CONTROLS.size) { i ->
      val (name, lo, hi) = ART_CONTROLS[i]
      val v = m[name] ?: return@FloatArray ART_DEFAULTS[i]
      val f = (v as? Number)?.toFloat() ?: return null
      if (!f.isFinite()) return null
      // Surface and backdrop are choices (whole numbers).
      if (i >= 8) f.roundToInt().toFloat().coerceIn(lo, hi) else f.coerceIn(lo, hi)
    }
  }

  /** The finished painting (or its preview) in a museum frame; the painting itself when [style] is 0. */
  private fun framed(painting: Bitmap, style: Int, token: Long): Bitmap {
    if (style <= 0) return painting
    val handle = NativeEngine.nativeArtFrame(painting, style, token)
    try {
      val out = Bitmap.createBitmap(NativeEngine.nativeResultWidth(handle), NativeEngine.nativeResultHeight(handle), Bitmap.Config.ARGB_8888)
      NativeEngine.nativeCopyResultToBitmap(handle, out)
      return out
    } finally {
      NativeEngine.nativeReleaseResult(handle)
    }
  }

  // ---- Photo Motion --------------------------------------------------------------

  /** A photo being animated: its person, faces and depth, found once. */
  private class MotionSession(val id: String, val file: java.io.File, val mask: Bitmap?, val faces: FloatArray, val depth: Bitmap?) {
    var lastUsed = android.os.SystemClock.elapsedRealtime()
    fun release() {
      mask?.recycle()
      depth?.recycle()
    }
  }
  private val motionSessions = java.util.concurrent.ConcurrentHashMap<String, MotionSession>()

  /**
   * Photo Motion: looks at the photo once — the person, their faces, the
   * depth of the scene, how much of it is sky. { inputPath } → { sessionId,
   * width, height, sky (0..1), depth (found), faces }.
   */
  override fun startMotion(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val file = try {
      paths.resolve(request.toHashMap()["inputPath"] as? String ?: "")
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
      return
    }
    jobs.submit(jobId, listener(promise)) { token ->
      val decoded = ImageCodec.decode(file, MOTION_ANALYSIS).bitmap
      val bmp = if (decoded.isMutable && decoded.config == Bitmap.Config.ARGB_8888) decoded
      else decoded.copy(Bitmap.Config.ARGB_8888, true).also { decoded.recycle() }
      try {
        val (mask, faces) = portraitProcessor.personAndFaces(bmp)
        val depth = runCatching { DepthEstimator.estimate(bmp) }.getOrNull()?.let { PortraitProcessor.alphaBitmap(it.bytes, it.width, it.height) }
        // How much is sky, measured on a small copy.
        val s = 360f / max(bmp.width, bmp.height)
        val small = Bitmap.createScaledBitmap(bmp, max(16, (bmp.width * s).roundToInt()), max(16, (bmp.height * s).roundToInt()), true)
        val sky = try {
          val h = NativeEngine.nativeMotionCreate(small, depth, mask, null, null, floatArrayOf(0f, 0f, 1f, 0f, 0.5f, 0f, 0.6f, 1f, 1f), token)
          try { NativeEngine.nativeMotionSkyShare(h) } finally { NativeEngine.nativeMotionDestroy(h) }
        } finally {
          if (small !== bmp) small.recycle()
        }
        while (motionSessions.size >= 2) motionSessions.values.minByOrNull { it.lastUsed }?.let { motionSessions.remove(it.id)?.release() } ?: break
        val id = "motion_${System.nanoTime()}"
        motionSessions[id] = MotionSession(id, file, mask, faces, depth)
        mapOf("sessionId" to id, "width" to bmp.width, "height" to bmp.height, "sky" to sky.toDouble(), "depth" to (depth != null), "faces" to faces.size / 12)
      } finally {
        bmp.recycle()
      }
    }
  }

  /**
   * Photo Motion: renders the loop as an MP4. { sessionId, quality 'preview' |
   * 'full', duration (s), camera, cameraAmount, flowSpeed, sky, skySpeed,
   * effect, effectAmount, loop, keepFaces, arrows [x0,y0,x1,y1,…],
   * anchors [x,y,r,…] } → { outputPath, posterPath, width, height,
   * durationMs, remaining }. Previews are free; a finished video is Premium,
   * or on the free plan two a day, each unlocked by an ad (and watermarked).
   */
  override fun renderMotion(jobId: String, request: ReadableMap, promise: Promise) {
    if (!requireEngine(promise)) return
    val map = request.toHashMap()
    val session = motionSessions[map["sessionId"] as? String ?: ""]
    if (session == null) {
      promise.reject("INVALID_ARGUMENT", Words.get(reactApplicationContext, R.string.native_motion_session_ended))
      return
    }
    session.lastUsed = android.os.SystemClock.elapsedRealtime()
    fun num(key: String, def: Float, lo: Float, hi: Float) = ((map[key] as? Number)?.toFloat() ?: def).let { if (it.isFinite()) it.coerceIn(lo, hi) else def }
    val full = map["quality"] == "full"
    val camera = num("camera", 6f, 0f, 7f).roundToInt()
    val effect = num("effect", 0f, 0f, 9f).roundToInt()
    val duration = num("duration", 5f, 2f, 10f)
    val loop = map["loop"] as? Boolean ?: true
    val keepFaces = map["keepFaces"] as? Boolean ?: true
    val sky = map["sky"] as? Boolean ?: false
    if (full && !MotionAllowance.allowed(reactApplicationContext)) {
      val left = (MotionAllowance.state(reactApplicationContext)["remaining"] as? Int) ?: 0
      if (left > 0) promise.reject("AD_REQUIRED", Words.get(reactApplicationContext, R.string.native_motion_ad_required))
      else promise.reject("PREMIUM_REQUIRED", Words.get(reactApplicationContext, R.string.native_motion_daily_used))
      return
    }
    val params = floatArrayOf(
      camera.toFloat(), num("cameraAmount", 0.5f, 0f, 1f), num("flowSpeed", 1f, 0f, 2f), if (sky) 1f else 0f,
      num("skySpeed", 0.5f, -1f, 1f), effect.toFloat(), num("effectAmount", 0.6f, 0f, 1f), if (loop) 1f else 0f, 7f,
    )
    fun floats(key: String, stride: Int): FloatArray? =
      (map[key] as? List<*>)?.mapNotNull { (it as? Number)?.toFloat() }?.takeIf { it.isNotEmpty() && it.size % stride == 0 && it.size <= stride * 32 }?.toFloatArray()
    val arrows = floats("arrows", 4)
    val userAnchors = floats("anchors", 3)
    jobs.submit(jobId, listener(promise)) { token ->
      val edge = if (full) MOTION_FULL else MOTION_PREVIEW
      val fps = if (full) 30f else 12f
      val decoded = ImageCodec.decode(session.file, edge).bitmap
      // Encoders want sizes in whole blocks: crop to multiples of 16 (a few pixels at most).
      val w = max(16, decoded.width / 16 * 16)
      val h = max(16, decoded.height / 16 * 16)
      val photo = Bitmap.createBitmap(decoded, (decoded.width - w) / 2, (decoded.height - h) / 2, w, h).copy(Bitmap.Config.ARGB_8888, true)
      decoded.recycle()
      // Faces stay still (flow never warps a face): each face is an anchor.
      val anchors = buildList {
        userAnchors?.forEach { add(it) }
        if (keepFaces) {
          val g = min(w, h).toFloat()
          for (i in 0 until session.faces.size / 12) {
            val fx = session.faces[i * 12]; val fy = session.faces[i * 12 + 1]; val fw = session.faces[i * 12 + 2]; val fh = session.faces[i * 12 + 3]
            add(fx + fw / 2); add(fy + fh / 2); add((max(fw * w, fh * h) / g * 0.62f).coerceIn(0.01f, 0.9f))
          }
        }
      }.take(3 * 64).toFloatArray()
      val handle = try {
        NativeEngine.nativeMotionCreate(photo, session.depth, session.mask, arrows, anchors.takeIf { it.isNotEmpty() }, params, token)
      } finally {
        photo.recycle()
      }
      val frame = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
      val target = if (full) {
        paths.exports.mkdirs()
        java.io.File(paths.exports, "PhotoMotion_${stamp()}.mp4")
      } else {
        java.io.File(java.io.File(paths.processingCache, "motion-preview").apply { mkdirs() }, "preview_${System.nanoTime()}.mp4")
      }
      val poster = java.io.File(target.parentFile, target.nameWithoutExtension + "_poster.jpg")
      val mark = if (full) com.photoplus.edit.ExportPolicy.resolve(com.photoplus.edit.WatermarkSpec(), Entitlements.plan) else null
      val frames = (duration * fps).roundToInt()
      try {
        com.photoplus.video.GlideEncoder(target, w, h, fps, null).use { enc ->
          for (i in 0 until frames) {
            NativeEngine.nativeMotionRender(handle, i.toFloat() / frames, frame, token)
            if (mark != null) com.photoplus.edit.WatermarkRenderer.apply(frame, mark)
            if (i == 0) ImageCodec.encode(frame, poster, OutputFormat.JPEG, 88)
            enc.encode(frame, (i * 1_000_000L / fps).toLong())
          }
          enc.finish((frames * 1_000_000L / fps).toLong())
        }
      } catch (e: Exception) {
        target.delete()
        throw e
      } finally {
        frame.recycle()
        NativeEngine.nativeMotionDestroy(handle)
      }
      if (full) {
        MotionAllowance.use(reactApplicationContext)
      } else {
        // Older previews go once this one is written (the one on screen plays until it is replaced).
        val keep = setOf(target.name, poster.name)
        target.parentFile?.listFiles()?.filter { it.name !in keep }?.sortedBy { it.lastModified() }?.dropLast(2)?.forEach { it.delete() }
      }
      mapOf(
        "outputPath" to target.absolutePath, "posterPath" to poster.absolutePath, "width" to w, "height" to h,
        "durationMs" to (frames * 1000.0 / fps), "remaining" to (MotionAllowance.state(reactApplicationContext)["remaining"] as Int),
      )
    }
  }

  override fun endMotion(sessionId: String) {
    motionSessions.remove(sessionId)?.release()
  }

  /** { remaining (-1 unlimited), limit, credits, needsAd } — the free plan's videos today. */
  override fun motionAllowance(promise: Promise) {
    promise.resolve(MotionAllowance.state(reactApplicationContext).toWritableMap())
  }

  /** { remaining (-1 unlimited), limit, credits, needsAd, owed, maxOwed, frozen } — Photo Arts on the free plan today. */
  override fun mastersAllowance(promise: Promise) {
    promise.resolve(ArtsAllowance.state(reactApplicationContext).toWritableMap())
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

  /** [keep]: previews of this kind kept besides the new one (a screen may show them again from its cache). */
  private fun encodeStudio(out: Bitmap, full: Boolean, name: String, keep: Int = 0): Map<String, Any?> {
    val target = if (full) {
      paths.studio.mkdirs()
      File(paths.studio, "${name}_${stamp()}.jpg")
    } else {
      val dir = File(paths.processingCache, "studio-${name.lowercase()}").apply { mkdirs() }
      dir.listFiles()?.sortedBy { it.lastModified() }?.dropLast(keep)?.forEach { it.delete() }
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
    private const val ARTISTS = 20
    private const val MASTERS_THUMB = 300
    private const val MASTERS_PREVIEW = 760
    private val MASTERS_FULL = intArrayOf(1100, 1500, 1800)
    // A drawing's lines stay crisp at full size: HD and above (a study 1600 px, a drawing 2048, a masterpiece 2560).
    private val PENCIL_FULL = intArrayOf(1600, 2048, 2560)
    // Anime & Cartoon: a quick preview; the finished picture in HD.
    private const val TOON_PREVIEW = 1024
    /** Matches the Toon screen's preview cache (src/screens/studio/ToonScreen.tsx PREVIEW_CACHE). */
    private const val TOON_PREVIEWS_KEPT = 12
    private const val MOTION_ANALYSIS = 1280
    private const val MOTION_PREVIEW = 480
    private const val MOTION_FULL = 1080
    /** The painting controls, in the engine's order, with their ranges (pp_art_controls). */
    private val ART_CONTROLS = listOf(
      Triple("brush", 0.6f, 1.6f),
      Triple("palette", 0f, 1f),
      Triple("vivid", -1f, 1f),
      Triple("likeness", 0f, 1f),
      Triple("texture", 0f, 1.5f),
      Triple("age", 0f, 1.5f),
      Triple("background", 0f, 1f),
      Triple("intensity", 0f, 1f),
      Triple("surface", 0f, 6f),
      Triple("backdrop", 0f, 6f),
    )
    private val ART_DEFAULTS = floatArrayOf(1f, 1f, 0f, 0.5f, 0.4f, 1f, 1f, 0.6f, 0f, 0f)  // a smooth texture, a balanced transformation
    private const val ART_FRAMES = 6
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
