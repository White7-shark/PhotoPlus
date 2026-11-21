package com.photoplus.studio

import android.graphics.Bitmap
import androidx.core.graphics.createBitmap
import com.photoplus.engine.EngineException
import com.photoplus.engine.ImageCodec
import com.photoplus.engine.NativeEngine
import java.io.File

/**
 * Film & Collage (Photoshoot Studio): photos as frames on a film strip, or
 * in a collage, each frame with its own transformation (a look, a zoom, a
 * pan and a mirror). Crops and zooms centre on the photo's face.
 */
object FilmCollage {
  const val MAX_PHOTOS = 12
  const val MAX_FRAMES = 12
  /** Engine frame looks: 0 original .. 9 noir (pp_frame_look). */
  const val LOOKS = 10
  /** Floats per frame for the engine: photo, look, zoom, panX, panY, flip, focusX, focusY. */
  const val FRAME_FLOATS = 8
  private val HEX = Regex("^#[0-9a-fA-F]{6}$")

  data class Frame(val photo: Int, val look: Int = 0, val zoom: Float = 1f, val panX: Float = 0f, val panY: Float = 0f, val flip: Boolean = false) {
    fun floats(focus: Pair<Float, Float>?): FloatArray = floatArrayOf(
      photo.toFloat(), look.toFloat(), zoom, panX, panY, if (flip) 1f else 0f, focus?.first ?: -1f, focus?.second ?: -1f,
    )

    companion object {
      fun fromMap(m: Map<*, *>, photos: Int): Frame {
        val photo = (m["photo"] as? Number)?.toInt() ?: 0
        if (photo !in 0 until photos) throw EngineException(EngineException.INVALID_ARGUMENT, "A frame names a photo that isn't there")
        val look = (m["look"] as? Number)?.toInt() ?: 0
        if (look !in 0 until LOOKS) throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown look")
        fun f(k: String, d: Float, lo: Float, hi: Float) = ((m[k] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: d).coerceIn(lo, hi)
        return Frame(photo, look, f("zoom", 1f, 1f, 4f), f("panX", 0f, -1f, 1f), f("panY", 0f, -1f, 1f), m["flip"] == true)
      }
    }
  }

  enum class Style(val id: String, val engine: Int, val premium: Boolean) {
    CLASSIC("classic", 0, false), AMBER("amber", 1, true), BOOTH("booth", 2, false), CINEMA("cinema", 3, true);

    companion object {
      fun parse(id: Any?): Style = entries.firstOrNull { it.id == id } ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown film style")
    }
  }

  data class FilmSpec(val photos: List<String>, val frames: List<Frame>, val style: Style, val vertical: Boolean, val portrait: Boolean, val markings: Boolean)

  data class Cell(val x: Float, val y: Float, val w: Float, val h: Float, val frame: Frame)

  data class CollageSpec(
    val photos: List<String>, val cells: List<Cell>, val aspect: Float, val gap: Float, val margin: Float, val radius: Float, val background: Int,
  )

  private fun photosOf(m: Map<String, Any?>): List<String> {
    val photos = (m["photos"] as? List<*>)?.map { it as? String ?: "" }.orEmpty()
    if (photos.isEmpty() || photos.size > MAX_PHOTOS || photos.any { it.isBlank() }) {
      throw EngineException(EngineException.INVALID_ARGUMENT, "Choose 1 to $MAX_PHOTOS photos")
    }
    return photos
  }

  fun filmFromMap(m: Map<String, Any?>): FilmSpec {
    val photos = photosOf(m)
    val frames = (m["frames"] as? List<*>)?.map { Frame.fromMap(it as? Map<*, *> ?: emptyMap<String, Any?>(), photos.size) }.orEmpty()
    if (frames.isEmpty() || frames.size > MAX_FRAMES) throw EngineException(EngineException.INVALID_ARGUMENT, "A strip has 1 to $MAX_FRAMES frames")
    return FilmSpec(photos, frames, Style.parse(m["style"] ?: "classic"), m["vertical"] != false, m["portrait"] == true, m["markings"] != false)
  }

  fun collageFromMap(m: Map<String, Any?>): CollageSpec {
    val photos = photosOf(m)
    val cells = (m["cells"] as? List<*>)?.map { raw ->
      val c = raw as? Map<*, *> ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Bad collage cell")
      fun f(k: String) = (c[k] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Bad collage cell")
      val cell = Cell(f("x"), f("y"), f("w"), f("h"), Frame.fromMap(c, photos.size))
      if (cell.x < 0f || cell.y < 0f || cell.w <= 0f || cell.h <= 0f || cell.x + cell.w > 1.0001f || cell.y + cell.h > 1.0001f) {
        throw EngineException(EngineException.INVALID_ARGUMENT, "A cell lies outside the collage")
      }
      cell
    }.orEmpty()
    if (cells.isEmpty() || cells.size > MAX_FRAMES) throw EngineException(EngineException.INVALID_ARGUMENT, "A collage has 1 to $MAX_FRAMES cells")
    fun f(k: String, d: Float, lo: Float, hi: Float) = ((m[k] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: d).coerceIn(lo, hi)
    val bg = (m["background"] as? String)?.takeIf { HEX.matches(it) }?.substring(1)?.toInt(16) ?: 0xFFFFFF
    return CollageSpec(photos, cells, f("aspect", 0.8f, 0.4f, 2.5f), f("gap", 0.015f, 0f, 0.1f), f("margin", 0.02f, 0f, 0.2f), f("radius", 0f, 0f, 0.5f), bg)
  }

  /** The face to centre crops on: the largest face's middle, as 0..1 of the photo; null without a face. */
  fun focusOf(bitmap: Bitmap): Pair<Float, Float>? = runCatching {
    val face = FaceFinder.findGeometry(bitmap).maxByOrNull { it.width } ?: return null
    val cx = ((face.eyeL.x + face.eyeR.x) * 0.5f + face.chin.x) * 0.5f
    val cy = ((face.eyeL.y + face.eyeR.y) * 0.5f + face.chin.y) * 0.5f
    (cx / bitmap.width).coerceIn(0f, 1f) to (cy / bitmap.height).coerceIn(0f, 1f)
  }.getOrNull()

  class Decoded(val bitmaps: List<Bitmap>, val focus: List<Pair<Float, Float>?>) : AutoCloseable {
    override fun close() = bitmaps.forEach { it.recycle() }
  }

  /** Decodes the photos (at most [maxEdge]) and finds each one's face ([focusCache] keeps them by path). */
  fun decode(files: List<File>, maxEdge: Int, focusCache: MutableMap<String, Pair<Float, Float>?>): Decoded {
    val bitmaps = ArrayList<Bitmap>()
    try {
      for (f in files) {
        val d = ImageCodec.decode(f, maxEdge).bitmap
        bitmaps += if (d.config == Bitmap.Config.ARGB_8888 && d.isMutable) d else d.copy(Bitmap.Config.ARGB_8888, true).also { d.recycle() }
      }
      val focus = files.mapIndexed { i, f ->
        val key = f.absolutePath + ":" + f.lastModified()
        synchronized(focusCache) { if (focusCache.containsKey(key)) return@mapIndexed focusCache[key] }
        focusOf(bitmaps[i]).also { synchronized(focusCache) { focusCache[key] = it } }
      }
      return Decoded(bitmaps, focus)
    } catch (e: Throwable) {
      bitmaps.forEach { it.recycle() }
      throw e
    }
  }

  private fun fromHandle(handle: Long): Bitmap {
    try {
      val out = createBitmap(NativeEngine.nativeResultWidth(handle), NativeEngine.nativeResultHeight(handle), Bitmap.Config.ARGB_8888)
      NativeEngine.nativeCopyResultToBitmap(handle, out)
      return out
    } finally {
      NativeEngine.nativeReleaseResult(handle)
    }
  }

  /** The film strip; [frameSize] is a frame's short edge in pixels. */
  fun renderFilm(spec: FilmSpec, photos: Decoded, frameSize: Int, token: Long): Bitmap {
    val frames = FloatArray(spec.frames.size * FRAME_FLOATS)
    spec.frames.forEachIndexed { i, f -> f.floats(photos.focus[f.photo]).copyInto(frames, i * FRAME_FLOATS) }
    val params = intArrayOf(spec.style.engine, if (spec.vertical) 1 else 0, if (spec.portrait) 1 else 0, frameSize, if (spec.markings) 1 else 0, 7)
    return fromHandle(NativeEngine.nativeStudioFilm(photos.bitmaps.toTypedArray(), frames, params, token))
  }

  /** The collage, [longEdge] pixels on its long side. */
  fun renderCollage(spec: CollageSpec, photos: Decoded, longEdge: Int, token: Long): Bitmap {
    val w: Int
    val h: Int
    if (spec.aspect >= 1f) {
      w = longEdge
      h = (longEdge / spec.aspect).toInt()
    } else {
      h = longEdge
      w = (longEdge * spec.aspect).toInt()
    }
    val cells = FloatArray(spec.cells.size * (4 + FRAME_FLOATS))
    spec.cells.forEachIndexed { i, c ->
      val o = i * (4 + FRAME_FLOATS)
      cells[o] = c.x
      cells[o + 1] = c.y
      cells[o + 2] = c.w
      cells[o + 3] = c.h
      c.frame.floats(photos.focus[c.frame.photo]).copyInto(cells, o + 4)
    }
    val bg = spec.background
    val params = floatArrayOf(
      w.toFloat(), h.toFloat(), spec.gap, spec.margin, spec.radius,
      (bg shr 16 and 0xFF).toFloat(), (bg shr 8 and 0xFF).toFloat(), (bg and 0xFF).toFloat(),
    )
    return fromHandle(NativeEngine.nativeStudioCollage(photos.bitmaps.toTypedArray(), cells, params, token))
  }
}
