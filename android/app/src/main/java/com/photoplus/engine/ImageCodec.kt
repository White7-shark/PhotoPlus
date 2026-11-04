package com.photoplus.engine

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.ImageDecoder
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Shader
import android.os.Build
import androidx.core.graphics.createBitmap
import androidx.exifinterface.media.ExifInterface
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import kotlin.math.max

/** Decoding/encoding at the Kotlin layer; pixel work happens in C++. */
object ImageCodec {

  data class Decoded(val bitmap: Bitmap, val sourceWidth: Int, val sourceHeight: Int) {
    /** Factor that maps source coordinates to decoded-bitmap coordinates. */
    val scale: Double get() = bitmap.width.toDouble() / sourceWidth.toDouble()
  }

  data class Bounds(val width: Int, val height: Int, val mime: String?)

  fun readBounds(file: File): Bounds {
    val opts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeFile(file.absolutePath, opts)
    return Bounds(opts.outWidth, opts.outHeight, opts.outMimeType)
  }

  /** Bounds as displayed (EXIF orientation applied), matching what ImageDecoder produces. */
  fun readDisplayBounds(file: File): Bounds {
    val raw = readBounds(file)
    val orientation = try {
      ExifInterface(file.absolutePath).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL)
    } catch (e: IOException) {
      ExifInterface.ORIENTATION_NORMAL
    }
    return if (swapsAxes(orientation)) raw.copy(width = raw.height, height = raw.width) else raw
  }

  fun swapsAxes(orientation: Int): Boolean = orientation == ExifInterface.ORIENTATION_ROTATE_90 ||
    orientation == ExifInterface.ORIENTATION_ROTATE_270 ||
    orientation == ExifInterface.ORIENTATION_TRANSPOSE ||
    orientation == ExifInterface.ORIENTATION_TRANSVERSE

  /**
   * Decodes into a software ARGB_8888 bitmap (required for pixel locking),
   * downsampling so the longest edge is at most [maxDimension]. EXIF
   * orientation is applied by ImageDecoder, so engine coordinates are always
   * in display orientation.
   */
  fun decode(file: File, maxDimension: Int): Decoded {
    if (!file.isFile) throw EngineException(EngineException.IO_ERROR, "Input file not found")
    var srcW = 0
    var srcH = 0
    val bitmap = try {
      ImageDecoder.decodeBitmap(ImageDecoder.createSource(file)) { decoder, info, _ ->
        srcW = info.size.width
        srcH = info.size.height
        decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
        decoder.isMutableRequired = false
        // The engine works in straight alpha (see pp_engine.h).
        decoder.isUnpremultipliedRequired = true
        val longest = max(srcW, srcH)
        if (longest > maxDimension) {
          val ratio = maxDimension.toDouble() / longest
          decoder.setTargetSize(max(1, (srcW * ratio).toInt()), max(1, (srcH * ratio).toInt()))
        }
      }
    } catch (e: IOException) {
      throw EngineException(EngineException.UNSUPPORTED_FORMAT, "Unable to decode image: ${e.message}")
    }
    val argb = if (bitmap.config == Bitmap.Config.ARGB_8888) bitmap else {
      try { toStraightArgb8888(bitmap) } finally { bitmap.recycle() }
    }
    return Decoded(argb, srcW, srcH)
  }

  /**
   * Converts other configs (e.g. RGBA_F16 wide-gamut decodes) to straight-alpha
   * ARGB_8888. getPixels/setPixels always use non-premultiplied colours, so a
   * row-at-a-time copy is correct and keeps the extra memory to one row.
   */
  fun toStraightArgb8888(src: Bitmap): Bitmap {
    val out = try {
      createBitmap(src.width, src.height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = false }
    } catch (e: OutOfMemoryError) {
      throw EngineException(EngineException.OUT_OF_MEMORY, "Unable to convert bitmap")
    }
    val row = IntArray(src.width)
    for (y in 0 until src.height) {
      src.getPixels(row, 0, src.width, 0, y, src.width, 1)
      out.setPixels(row, 0, src.width, 0, y, src.width, 1)
    }
    return out
  }

  /** Writes atomically: encode to a sibling temp file, then rename. */
  fun encode(bitmap: Bitmap, target: File, format: OutputFormat, quality: Int) {
    target.parentFile?.mkdirs()
    val tmp = File(target.parentFile, ".${target.name}.tmp")
    val compressFormat = when (format) {
      OutputFormat.JPEG -> Bitmap.CompressFormat.JPEG
      OutputFormat.PNG -> Bitmap.CompressFormat.PNG
      OutputFormat.WEBP -> {
        // WEBP_LOSSY arrived in Android 11; the pre-11 constant is deprecated,
        // so WebP export is offered on Android 11+ only (see CapabilityProbe).
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
          throw EngineException(EngineException.UNSUPPORTED_FORMAT, "WebP export requires Android 11 or newer")
        }
        Bitmap.CompressFormat.WEBP_LOSSY
      }
    }
    try {
      FileOutputStream(tmp).use { out ->
        if (!bitmap.compress(compressFormat, quality, out)) {
          throw EngineException(EngineException.IO_ERROR, "Encoder rejected the image")
        }
        out.fd.sync()
      }
      if (!tmp.renameTo(target)) throw EngineException(EngineException.IO_ERROR, "Unable to move output into place")
    } catch (e: IOException) {
      throw EngineException(EngineException.IO_ERROR, "Unable to write output: ${e.message}")
    } finally {
      if (tmp.exists()) tmp.delete()
    }
  }

  /** Synthetic photo-like image used by diagnostics and self-tests. */
  fun generateTestImage(target: File, width: Int, height: Int) {
    val bitmap = createBitmap(width, height, Bitmap.Config.ARGB_8888)
    try {
      val canvas = Canvas(bitmap)
      val sky = Paint().apply {
        shader = LinearGradient(0f, 0f, 0f, height.toFloat(),
          intArrayOf(0xFF1B1035.toInt(), 0xFFE8467C.toInt(), 0xFFFF9A52.toInt()), null, Shader.TileMode.CLAMP)
      }
      canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), sky)
      val sun = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        shader = RadialGradient(width * 0.7f, height * 0.62f, width * 0.22f,
          intArrayOf(0xFFFFF1C1.toInt(), 0x00FFF1C1), null, Shader.TileMode.CLAMP)
      }
      canvas.drawCircle(width * 0.7f, height * 0.62f, width * 0.22f, sun)
      val ground = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF0B0814.toInt() }
      canvas.drawRect(0f, height * 0.78f, width.toFloat(), height.toFloat(), ground)
      encode(bitmap, target, OutputFormat.JPEG, 90)
    } finally {
      bitmap.recycle()
    }
  }

  fun readMetadata(file: File): Map<String, Any?> {
    val bounds = readDisplayBounds(file)
    val exif = try { ExifInterface(file.absolutePath) } catch (e: IOException) { null }
    return mapOf(
      "path" to file.absolutePath,
      "sizeBytes" to file.length().toDouble(),
      "width" to bounds.width,
      "height" to bounds.height,
      "mimeType" to bounds.mime,
      "orientation" to (exif?.getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_UNDEFINED) ?: 0),
      "dateTaken" to exif?.getAttribute(ExifInterface.TAG_DATETIME_ORIGINAL),
      "make" to exif?.getAttribute(ExifInterface.TAG_MAKE),
      "model" to exif?.getAttribute(ExifInterface.TAG_MODEL),
      "iso" to exif?.getAttributeInt(ExifInterface.TAG_PHOTOGRAPHIC_SENSITIVITY, 0)?.takeIf { it > 0 },
      "exposureTime" to exif?.getAttributeDouble(ExifInterface.TAG_EXPOSURE_TIME, 0.0)?.takeIf { it > 0 },
      "fNumber" to exif?.getAttributeDouble(ExifInterface.TAG_F_NUMBER, 0.0)?.takeIf { it > 0 },
      "focalLength" to exif?.getAttributeDouble(ExifInterface.TAG_FOCAL_LENGTH, 0.0)?.takeIf { it > 0 },
    )
  }
}
