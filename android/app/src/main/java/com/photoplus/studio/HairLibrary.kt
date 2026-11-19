package com.photoplus.studio

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import com.photoplus.engine.EngineException
import org.json.JSONObject
import java.io.FileNotFoundException

/**
 * The photographed hairstyles: for each style id, `hair/<id>.webp` (the hair
 * only, cut from an openly licensed photo: colour and cover, straight alpha)
 * and `hair/<id>.json` (`face`: the face box of the person it was cut from,
 * in the image's pixels; `natural`: the colour it is shown in until another
 * is chosen, "#RRGGBB"). Bald has none: the engine draws the scalp.
 */
class HairLibrary(private val context: Context) {
  /** A loaded style: [bitmap] not premultiplied, [face] (4) in its pixels, [natural] 0xRRGGBB. */
  class Photo(val id: String, val bitmap: Bitmap, val face: FloatArray, val natural: Int?)

  private var last: Photo? = null

  /** The style's photo, or null when the app has none for it. The last one is kept (choosing colours re-uses it). */
  @Synchronized
  fun load(id: String): Photo? {
    if (!ID.matches(id)) throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown hairstyle")
    last?.let { if (it.id == id) return it }
    val meta = try {
      context.assets.open("hair/$id.json").use { JSONObject(it.bufferedReader().readText()) }
    } catch (_: FileNotFoundException) {
      return null
    }
    val f = meta.getJSONArray("face")
    val face = FloatArray(4) { f.getDouble(it).toFloat() }
    if (!(face[2] - face[0] > 4f && face[3] - face[1] > 4f)) throw EngineException(EngineException.INVALID_ARGUMENT, "Bad hairstyle")
    val natural = meta.optString("natural").takeIf { HEX.matches(it) }?.substring(1)?.toInt(16)
    val opts = BitmapFactory.Options().apply {
      inPremultiplied = false
      inPreferredConfig = Bitmap.Config.ARGB_8888
    }
    val bmp = context.assets.open("hair/$id.webp").use { BitmapFactory.decodeStream(it, null, opts) }
      ?: throw EngineException(EngineException.IO_ERROR, "The hairstyle could not be read")
    // The previous one is left to the garbage collector: a job may still be drawing with it.
    return Photo(id, bmp, face, natural).also { last = it }
  }

  companion object {
    const val BALD = "bald"
    private val ID = Regex("^[a-z0-9-]{1,40}$")
    private val HEX = Regex("^#[0-9a-fA-F]{6}$")
  }
}
