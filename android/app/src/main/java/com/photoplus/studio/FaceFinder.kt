package com.photoplus.studio

import android.graphics.Bitmap
import android.graphics.PointF
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.face.Face
import com.google.mlkit.vision.face.FaceContour
import com.google.mlkit.vision.face.FaceDetection
import com.google.mlkit.vision.face.FaceDetectorOptions
import java.util.concurrent.TimeUnit
import kotlin.math.hypot

/**
 * Face geometry for fitting props (src/services/edit/placement.ts): the top
 * of the face outline, the chin, both eyes and the face width, in photo
 * fractions. Eyes are reported left and right as seen in the picture.
 */
object FaceFinder {
  private val detector by lazy {
    FaceDetection.getClient(
      FaceDetectorOptions.Builder()
        .setPerformanceMode(FaceDetectorOptions.PERFORMANCE_MODE_ACCURATE)
        .setContourMode(FaceDetectorOptions.CONTOUR_MODE_ALL)
        .setMinFaceSize(0.05f)
        .build(),
    )
  }

  fun find(bitmap: Bitmap): List<Map<String, Any?>> {
    val faces = runCatching { Tasks.await(detector.process(InputImage.fromBitmap(bitmap, 0)), 10, TimeUnit.SECONDS) }.getOrNull()
      ?: return emptyList()
    val w = bitmap.width.toFloat()
    val h = bitmap.height.toFloat()
    return faces.mapNotNull { geometry(it, w, h) }
  }

  private fun centroid(points: List<PointF>?): PointF? {
    if (points.isNullOrEmpty()) return null
    return PointF(points.map { it.x }.average().toFloat(), points.map { it.y }.average().toFloat())
  }

  private fun geometry(face: Face, w: Float, h: Float): Map<String, Any?>? {
    val oval = face.getContour(FaceContour.FACE)?.points
    val e1 = centroid(face.getContour(FaceContour.LEFT_EYE)?.points)
    val e2 = centroid(face.getContour(FaceContour.RIGHT_EYE)?.points)
    val box = face.boundingBox
    if (oval == null || oval.size < 8 || e1 == null || e2 == null) return null
    val (eyeL, eyeR) = if (e1.x <= e2.x) e1 to e2 else e2 to e1
    // The face's down direction is perpendicular to the eye line.
    val ex = eyeR.x - eyeL.x
    val ey = eyeR.y - eyeL.y
    val d = hypot(ex, ey).coerceAtLeast(1e-3f)
    val dx = -ey / d
    val dy = ex / d
    val top = oval.minBy { it.x * dx + it.y * dy }
    val chin = oval.maxBy { it.x * dx + it.y * dy }
    fun pt(p: PointF) = mapOf("x" to (p.x / w).toDouble(), "y" to (p.y / h).toDouble())
    return mapOf(
      "top" to pt(top),
      "chin" to pt(chin),
      "eyeL" to pt(eyeL),
      "eyeR" to pt(eyeR),
      "width" to (box.width() / w).toDouble(),
    )
  }
}
