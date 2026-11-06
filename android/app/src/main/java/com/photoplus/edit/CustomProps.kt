package com.photoplus.edit

import android.graphics.Bitmap
import androidx.core.graphics.createBitmap
import androidx.core.graphics.scale
import com.photoplus.engine.EngineException
import com.photoplus.engine.ImageCodec
import com.photoplus.engine.OutputFormat
import com.photoplus.studio.MaskGeometry
import java.io.File
import java.security.SecureRandom

/**
 * Props people make from their own photos (Premium): the chosen object is cut
 * out (soft edges kept), trimmed to its outline with a little room, and saved
 * as a transparent PNG in app storage, named by [EditDocument.CUSTOM_PROP].
 */
object CustomProps {
  /** Longest side of a saved prop: plenty for any photo, small on disk. */
  const val MAX_EDGE = 1024
  private const val ALPHABET = "abcdefghijklmnopqrstuvwxyz0123456789"
  private val random = SecureRandom()

  fun newId(): String = "custom-" + (1..16).map { ALPHABET[random.nextInt(ALPHABET.length)] }.joinToString("")

  data class Saved(val id: String, val file: File, val width: Int, val height: Int)

  /**
   * Cuts [mask] (bytes 0..255 at the photo's size) out of [photo] and saves it.
   * Fails with INVALID_ARGUMENT when the chosen area is too small to use.
   */
  fun save(photo: Bitmap, mask: ByteArray, dir: File, id: String = newId()): Saved {
    require(EditDocument.CUSTOM_PROP.matches(id)) { "bad custom prop id" }
    val w = photo.width
    val h = photo.height
    val box = MaskGeometry.bounds(mask, w, h) ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Nothing was found where you tapped")
    if (box.width < 12 || box.height < 12) throw EngineException(EngineException.INVALID_ARGUMENT, "That object is too small to use")
    val margin = (0.03f * maxOf(box.width, box.height)).toInt() + 2
    val x0 = (box.x - margin).coerceAtLeast(0)
    val y0 = (box.y - margin).coerceAtLeast(0)
    val x1 = (box.x + box.width + margin).coerceAtMost(w)
    val y1 = (box.y + box.height + margin).coerceAtMost(h)
    val cw = x1 - x0
    val ch = y1 - y0
    val factor = minOf(1f, MAX_EDGE.toFloat() / maxOf(cw, ch))
    val ow = maxOf(1, (cw * factor).toInt())
    val oh = maxOf(1, (ch * factor).toInt())
    val cut = createBitmap(cw, ch, Bitmap.Config.ARGB_8888)
    try {
      val row = IntArray(cw)
      for (y in 0 until ch) {
        photo.getPixels(row, 0, cw, x0, y0 + y, cw, 1)
        for (x in 0 until cw) {
          val a = mask[(y0 + y) * w + x0 + x].toInt() and 0xFF
          row[x] = (row[x] and 0x00FFFFFF) or (a shl 24)
        }
        cut.setPixels(row, 0, cw, 0, y, cw, 1)
      }
      val out = if (ow == cw && oh == ch) cut else cut.scale(ow, oh)
      try {
        dir.mkdirs()
        val file = File(dir, "$id.png")
        ImageCodec.encode(out, file, OutputFormat.PNG, 100)
        return Saved(id, file, ow, oh)
      } finally {
        if (out !== cut) out.recycle()
      }
    } finally {
      cut.recycle()
    }
  }

  fun delete(dir: File, id: String): Boolean {
    if (!EditDocument.CUSTOM_PROP.matches(id)) return false
    return File(dir, "$id.png").delete()
  }
}
