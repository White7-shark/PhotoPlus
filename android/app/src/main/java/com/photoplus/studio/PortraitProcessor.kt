package com.photoplus.studio

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PointF
import android.graphics.RectF
import android.util.Log
import androidx.core.graphics.createBitmap
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.face.Face
import com.google.mlkit.vision.face.FaceContour
import com.google.mlkit.vision.face.FaceDetection
import com.google.mlkit.vision.face.FaceDetectorOptions
import com.google.mlkit.vision.segmentation.Segmentation
import com.google.mlkit.vision.segmentation.selfie.SelfieSegmenterOptions
import com.google.mlkit.vision.segmentation.subject.SubjectSegmentation
import com.google.mlkit.vision.segmentation.subject.SubjectSegmenterOptions
import com.photoplus.engine.NativeEngine
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.concurrent.TimeUnit
import kotlin.math.max
import kotlin.math.roundToInt

/** Spotlight settings (all 0..1). */
data class PortraitParams(
  val blur: Float = 0.6f,
  val retouch: Float = 0.75f,
  val brighten: Float = 0.4f,
  val pop: Float = 0.5f,
) {
  fun toArray() = floatArrayOf(blur, retouch, brighten, pop)

  companion object {
    fun fromMap(map: Map<String, Any?>): PortraitParams {
      fun unit(key: String, default: Float) = ((map[key] as? Number)?.toFloat() ?: default).coerceIn(0f, 1f)
      return PortraitParams(unit("blur", 0.6f), unit("retouch", 0.75f), unit("brighten", 0.4f), unit("pop", 0.5f))
    }

    /** "x0,y0,x1,y1…" from the bridge → points (0..1), at most 8. */
    fun points(raw: Any?): List<Pair<Float, Float>> {
      val list = (raw as? List<*>)?.mapNotNull { (it as? Number)?.toFloat() } ?: return emptyList()
      return list.chunked(2).filter { it.size == 2 && it.all { v -> v.isFinite() } }
        .map { it[0].coerceIn(0f, 1f) to it[1].coerceIn(0f, 1f) }.take(8)
    }
  }
}

data class PortraitReport(val subjectCoverage: Float, val skinCoverage: Float, val spots: Int, val people: Int, val source: String)

/**
 * Spotlight: finds what should stand out and hands masks to the engine.
 *
 *  - Subject: on-device subject segmentation (people and objects); taps pick
 *    which subjects, otherwise people (or all main subjects) are chosen.
 *    Falls back to the bundled person model, then to the engine's own
 *    tap-seeded cut-out, so it works fully offline.
 *  - Faces: face contours give face areas (retouching, calibrated to each
 *    person's own skin) and the eyes, brows, lips and nostrils, which are
 *    protected from smoothing.
 *
 * Runs on a background thread. Points are in the UPRIGHT photo (what the user
 * sees); [rotation] is the clockwise turn the bitmap still needs to be upright.
 */
class PortraitProcessor {
  private val faceDetector by lazy {
    FaceDetection.getClient(
      FaceDetectorOptions.Builder()
        .setPerformanceMode(FaceDetectorOptions.PERFORMANCE_MODE_ACCURATE)
        .setContourMode(FaceDetectorOptions.CONTOUR_MODE_ALL)
        .setMinFaceSize(0.06f)
        .build(),
    )
  }
  private val subjectSegmenter by lazy {
    SubjectSegmentation.getClient(
      SubjectSegmenterOptions.Builder()
        .enableForegroundConfidenceMask()
        .enableMultipleSubjects(SubjectSegmenterOptions.SubjectResultOptions.Builder().enableConfidenceMask().build())
        .build(),
    )
  }
  private val selfieSegmenter by lazy {
    Segmentation.getClient(SelfieSegmenterOptions.Builder().setDetectorMode(SelfieSegmenterOptions.SINGLE_IMAGE_MODE).build())
  }

  fun apply(bitmap: Bitmap, rotation: Int, points: List<Pair<Float, Float>>, params: PortraitParams, token: Long): PortraitReport {
    val masks = buildMasks(bitmap, rotation, points)
    try {
      val report = FloatArray(4)
      NativeEngine.nativePortrait(bitmap, masks.subject, masks.faces, masks.protect, params.toArray(), token, report)
      return PortraitReport(report[0], report[1], report[2].toInt(), masks.people, masks.source)
    } finally {
      masks.subject.recycle()
      masks.faces?.recycle()
      masks.protect?.recycle()
    }
  }

  private data class Masks(val subject: Bitmap, val faces: Bitmap?, val protect: Bitmap?, val people: Int, val source: String)

  private fun buildMasks(bitmap: Bitmap, rotation: Int, points: List<Pair<Float, Float>>): Masks {
    val rot = MaskGeometry.normalise(rotation)
    // Small upright copy for the models (one scaled copy; the photo itself is untouched).
    val upright = scaledUpright(bitmap, rot, ANALYSIS_EDGE)
    try {
      val image = InputImage.fromBitmap(upright, 0)
      val faces = runCatching { Tasks.await(faceDetector.process(image), ML_TIMEOUT_S, TimeUnit.SECONDS) }
        .onFailure { Log.w(TAG, "face detection unavailable", it) }.getOrDefault(emptyList())
      val (subjectBytes, source) = subjectMask(image, upright.width, upright.height, points, faces)
        ?: (null to "")
      val subject = if (subjectBytes != null) {
        val (b, w, h) = MaskGeometry.rotateMask(subjectBytes, upright.width, upright.height, -rot)
        alphaBitmap(b, w, h)
      } else {
        // Engine cut-out works on the bitmap itself: map taps into its orientation.
        val pts = points.flatMap { (x, y) -> MaskGeometry.uprightToBitmap(x, y, rot).toList() }.toFloatArray()
        val size = IntArray(2)
        val bytes = NativeEngine.nativeSubjectMask(bitmap, pts, size)
        alphaBitmap(bytes, size[0], size[1])
      }
      val (faceMask, protectMask) = if (faces.isEmpty()) null to null else drawFaces(faces, upright.width, upright.height, rot)
      return Masks(subject, faceMask, protectMask, faces.size, if (subjectBytes != null) source else "taps")
    } finally {
      upright.recycle()
    }
  }

  /** Subject mask in the upright analysis image, or null to use the engine's tap cut-out. */
  private fun subjectMask(
    image: InputImage,
    w: Int,
    h: Int,
    points: List<Pair<Float, Float>>,
    faces: List<Face>,
  ): Pair<ByteArray, String>? {
    val result = runCatching { Tasks.await(subjectSegmenter.process(image), ML_TIMEOUT_S, TimeUnit.SECONDS) }
      .onFailure { Log.i(TAG, "subject segmentation unavailable, using fallbacks", it) }.getOrNull()
    if (result != null) {
      val subjects = result.subjects
      val chosen = subjects.indices.filter { i ->
        val s = subjects[i]
        val mask = s.confidenceMask ?: return@filter false
        fun at(px: Float, py: Float): Float {
          val x = (px * w).toInt() - s.startX
          val y = (py * h).toInt() - s.startY
          return if (x in 0 until s.width && y in 0 until s.height) mask.get(y * s.width + x) else 0f
        }
        when {
          points.isNotEmpty() -> points.any { (px, py) -> at(px, py) >= 0.5f }
          faces.isNotEmpty() -> faces.any { f -> at(f.boundingBox.exactCenterX() / w, f.boundingBox.exactCenterY() / h) >= 0.5f }
          else -> true
        }
      }
      if (chosen.isNotEmpty()) {
        val out = ByteArray(w * h)
        for (i in chosen) {
          val s = subjects[i]
          val mask = s.confidenceMask ?: continue
          for (y in 0 until s.height) for (x in 0 until s.width) {
            val gx = s.startX + x
            val gy = s.startY + y
            if (gx !in 0 until w || gy !in 0 until h) continue
            val v = (mask.get(y * s.width + x).coerceIn(0f, 1f) * 255f).roundToInt()
            val idx = gy * w + gx
            if (v > (out[idx].toInt() and 0xFF)) out[idx] = v.toByte()
          }
        }
        return out to "subjects"
      }
      // A tap that hit no detected subject: let the tap cut-out decide.
      if (points.isNotEmpty()) return null
      result.foregroundConfidenceMask?.let { return floatsToBytes(it, w * h) to "foreground" }
    }
    // Person model (bundled): good whenever people are involved.
    val wantsPeople = faces.isNotEmpty() || points.isEmpty()
    if (!wantsPeople) return null
    val mask = runCatching { Tasks.await(selfieSegmenter.process(image), ML_TIMEOUT_S, TimeUnit.SECONDS) }
      .onFailure { Log.w(TAG, "person segmentation unavailable", it) }.getOrNull() ?: return null
    val floats = mask.buffer.order(ByteOrder.nativeOrder()).asFloatBuffer()
    val bytes = floatsToBytes(floats, mask.width * mask.height)
    val scaled = if (mask.width == w && mask.height == h) bytes else resizeNearest(bytes, mask.width, mask.height, w, h)
    // Taps on non-people (e.g. a cake) are better served by the tap cut-out.
    if (points.isNotEmpty() && points.none { (px, py) -> (scaled[(py * (h - 1)).toInt() * w + (px * (w - 1)).toInt()].toInt() and 0xFF) > 127 }) {
      return null
    }
    return scaled to "people"
  }

  /** Face areas and protected features, drawn in the bitmap's own orientation. */
  private fun drawFaces(faces: List<Face>, w: Int, h: Int, rot: Int): Pair<Bitmap, Bitmap> {
    // Draw upright, then rotate the drawing into the bitmap orientation.
    val (bw, bh) = if (rot == 90 || rot == 270) h to w else w to h
    val faceMask = createBitmap(bw, bh, Bitmap.Config.ALPHA_8)
    val protect = createBitmap(bw, bh, Bitmap.Config.ALPHA_8)
    val toBitmap = Matrix().apply {
      // Upright → bitmap: rotate by -rot about the origin, then shift into view.
      setRotate(-rot.toFloat())
      when (rot) {
        90 -> postTranslate(0f, w.toFloat())
        180 -> postTranslate(w.toFloat(), h.toFloat())
        270 -> postTranslate(h.toFloat(), 0f)
      }
    }
    val fc = Canvas(faceMask).apply { setMatrix(toBitmap) }
    val pc = Canvas(protect).apply { setMatrix(toBitmap) }
    val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL; alpha = 255 }
    for (face in faces) {
      val oval = face.getContour(FaceContour.FACE)?.points
      if (oval != null && oval.size >= 8) {
        fc.drawPath(polygon(oval), fill)
      } else {
        fc.drawOval(RectF(face.boundingBox), fill) // faces beyond the first come without contours
      }
      val size = max(face.boundingBox.width(), face.boundingBox.height()).toFloat()
      val pad = Paint(fill).apply { style = Paint.Style.FILL_AND_STROKE; strokeWidth = size * 0.04f; strokeJoin = Paint.Join.ROUND }
      for (part in listOf(FaceContour.LEFT_EYE, FaceContour.RIGHT_EYE)) {
        face.getContour(part)?.points?.takeIf { it.size >= 3 }?.let { pc.drawPath(polygon(it), Paint(pad).apply { strokeWidth = size * 0.06f }) }
      }
      for ((top, bottom) in listOf(
        FaceContour.LEFT_EYEBROW_TOP to FaceContour.LEFT_EYEBROW_BOTTOM,
        FaceContour.RIGHT_EYEBROW_TOP to FaceContour.RIGHT_EYEBROW_BOTTOM,
        FaceContour.UPPER_LIP_TOP to FaceContour.LOWER_LIP_BOTTOM,
      )) {
        val a = face.getContour(top)?.points
        val b = face.getContour(bottom)?.points
        if (a != null && b != null && a.size >= 2 && b.size >= 2) pc.drawPath(polygon(a + b.reversed()), pad)
      }
      face.getContour(FaceContour.NOSE_BOTTOM)?.points?.takeIf { it.size >= 2 }?.let {
        pc.drawPath(polygon(it), Paint(pad).apply { strokeWidth = size * 0.07f; style = Paint.Style.STROKE })
      }
    }
    return faceMask to protect
  }

  companion object {
    private const val TAG = "PhotoPlusSpotlight"
    private const val ANALYSIS_EDGE = 1024
    private const val ML_TIMEOUT_S = 10L

    private fun polygon(points: List<PointF>) = Path().apply {
      moveTo(points[0].x, points[0].y)
      for (i in 1 until points.size) lineTo(points[i].x, points[i].y)
      close()
    }

    /** One scaled, upright copy of the photo for analysis. */
    fun scaledUpright(src: Bitmap, rotation: Int, edge: Int): Bitmap {
      val s = minOf(1f, edge.toFloat() / max(src.width, src.height))
      val m = Matrix().apply {
        postScale(s, s)
        postRotate(rotation.toFloat())
      }
      return Bitmap.createBitmap(src, 0, 0, src.width, src.height, m, true)
    }

    fun floatsToBytes(f: FloatBuffer, n: Int): ByteArray {
      f.rewind()
      val out = ByteArray(n)
      for (i in 0 until minOf(n, f.remaining())) out[i] = (f.get(i).coerceIn(0f, 1f) * 255f).roundToInt().toByte()
      return out
    }

    fun resizeNearest(src: ByteArray, sw: Int, sh: Int, dw: Int, dh: Int): ByteArray {
      val out = ByteArray(dw * dh)
      for (y in 0 until dh) {
        val sy = minOf(sh - 1, y * sh / dh)
        for (x in 0 until dw) out[y * dw + x] = src[sy * sw + minOf(sw - 1, x * sw / dw)]
      }
      return out
    }

    /** ALPHA_8 bitmap from mask bytes (handles padded rows). */
    fun alphaBitmap(bytes: ByteArray, w: Int, h: Int): Bitmap {
      val bmp = createBitmap(w, h, Bitmap.Config.ALPHA_8)
      val stride = bmp.rowBytes
      val buf = ByteBuffer.allocate(stride * h)
      for (y in 0 until h) {
        buf.position(y * stride)
        buf.put(bytes, y * w, w)
      }
      buf.rewind()
      bmp.copyPixelsFromBuffer(buf)
      return bmp
    }
  }
}
