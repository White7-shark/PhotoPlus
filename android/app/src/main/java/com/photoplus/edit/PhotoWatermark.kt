package com.photoplus.edit

import android.graphics.Bitmap
import androidx.exifinterface.media.ExifInterface
import com.photoplus.engine.ImageCodec
import com.photoplus.engine.OutputFormat
import java.io.File

/**
 * Free-plan photos carry the Photo Plus mark in their pixels. The untouched
 * photo is kept beside it as `<name>_clean.jpg` (app-private): editing,
 * Transform and every export start from the clean copy and draw the mark
 * once, so it is never doubled.
 */
object PhotoWatermark {
  /** Companion file holding the photo without the mark. */
  fun cleanFileFor(file: File) = File(file.parentFile, "${file.nameWithoutExtension}_clean.${file.extension}")

  /**
   * Free plan: moves the untouched photo to its clean companion and rewrites
   * [file] with the mark (verified visible). Returns the clean path, or null
   * when no mark applies (Premium photos stay clean; branding is applied at
   * export).
   */
  fun applyToFile(file: File, spec: WatermarkSpec, plan: Plan, maxDimension: Int = 8192): String? {
    if (plan == Plan.PREMIUM) return null
    val mark = ExportPolicy.resolve(spec, plan) ?: return null
    val clean = cleanFileFor(file)
    if (!file.renameTo(clean)) {
      file.copyTo(clean, overwrite = true)
    }
    try {
      val decoded = ImageCodec.decode(clean, maxDimension)
      val bmp = if (decoded.bitmap.isMutable) decoded.bitmap else decoded.bitmap.copy(Bitmap.Config.ARGB_8888, true).also { decoded.bitmap.recycle() }
      try {
        val visible = WatermarkRenderer.apply(bmp, mark)
        ExportPolicy.verify(mark, plan, visible, bmp.width, bmp.height)
        ImageCodec.encode(bmp, file, OutputFormat.JPEG, 95)
      } finally {
        bmp.recycle()
      }
      copyExif(clean, file)
      return clean.absolutePath
    } catch (e: Exception) {
      // Never lose the photo: put the original back, then report the failure.
      if (!file.exists()) clean.renameTo(file)
      throw e
    }
  }

  private val TAGS = listOf(
    ExifInterface.TAG_DATETIME, ExifInterface.TAG_DATETIME_ORIGINAL, ExifInterface.TAG_DATETIME_DIGITIZED,
    ExifInterface.TAG_OFFSET_TIME_ORIGINAL, ExifInterface.TAG_MAKE, ExifInterface.TAG_MODEL,
    ExifInterface.TAG_EXPOSURE_TIME, ExifInterface.TAG_F_NUMBER, ExifInterface.TAG_PHOTOGRAPHIC_SENSITIVITY,
    ExifInterface.TAG_FOCAL_LENGTH, ExifInterface.TAG_FOCAL_LENGTH_IN_35MM_FILM, ExifInterface.TAG_WHITE_BALANCE,
    ExifInterface.TAG_FLASH, ExifInterface.TAG_GPS_LATITUDE, ExifInterface.TAG_GPS_LATITUDE_REF,
    ExifInterface.TAG_GPS_LONGITUDE, ExifInterface.TAG_GPS_LONGITUDE_REF, ExifInterface.TAG_GPS_ALTITUDE,
    ExifInterface.TAG_GPS_ALTITUDE_REF,
  )

  /** Camera details travel with the marked photo; pixels are already upright. */
  fun copyExif(from: File, to: File) {
    runCatching {
      val src = ExifInterface(from)
      val dst = ExifInterface(to)
      for (tag in TAGS) src.getAttribute(tag)?.let { dst.setAttribute(tag, it) }
      dst.setAttribute(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL.toString())
      dst.setAttribute(ExifInterface.TAG_SOFTWARE, "Photo Plus")
      dst.saveAttributes()
    }
  }
}
