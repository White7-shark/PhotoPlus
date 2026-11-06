package com.photoplus.edit

import android.content.res.AssetManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import com.photoplus.engine.EngineException
import java.io.File

/**
 * Prop artwork: bundled props from assets/props/<id>.png, and props people
 * made (Premium) from <customDir>/<id>.png. Bitmaps are straight alpha, as
 * the engine expects, and a few are kept in memory.
 */
class PropArt(private val assets: AssetManager, private val customDir: File?, private val keep: Int = 6) {
  private val cache = LinkedHashMap<String, Bitmap>(8, 0.75f, true)

  @Synchronized
  fun load(id: String): Bitmap {
    cache[id]?.takeIf { !it.isRecycled }?.let { return it }
    val options = BitmapFactory.Options().apply {
      inPremultiplied = false // the engine works in straight alpha
      inPreferredConfig = Bitmap.Config.ARGB_8888
    }
    val bmp = when {
      id in EditDocument.PROPS -> assets.open("props/$id.png").use { BitmapFactory.decodeStream(it, null, options) }
      EditDocument.CUSTOM_PROP.matches(id) -> customDir?.let { File(it, "$id.png") }?.takeIf { it.isFile }
        ?.inputStream()?.use { BitmapFactory.decodeStream(it, null, options) }
      else -> throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown prop")
    } ?: throw EngineException(EngineException.IO_ERROR, "Prop artwork is missing")
    while (cache.size >= keep) cache.remove(cache.keys.first())?.recycle()
    cache[id] = bmp
    return bmp
  }

  @Synchronized
  fun clear() {
    cache.values.forEach { it.recycle() }
    cache.clear()
  }
}
