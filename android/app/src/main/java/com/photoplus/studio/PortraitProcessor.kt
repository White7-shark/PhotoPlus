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
import com.google.mlkit.vision.face.FaceLandmark
import com.photoplus.edit.PropFit.Pt
import com.google.mlkit.vision.segmentation.Segmentation
import com.google.mlkit.vision.segmentation.selfie.SelfieSegmenterOptions
import com.google.mlkit.vision.segmentation.subject.SubjectSegmentation
import com.google.mlkit.vision.segmentation.subject.SubjectSegmentationResult
import com.google.mlkit.vision.segmentation.subject.SubjectSegmenterOptions
import com.photoplus.engine.NativeEngine
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.concurrent.TimeUnit
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Spotlight and Glow settings. blur, retouch, brighten, pop are 0..1 (the
 * free presets). The advanced (Premium) controls: smooth, blemish, even
 * (0..1; negative = follow retouch), background light and colour (-1..1)
 * and edge refinement (0..1).
 */
data class PortraitParams(
  val blur: Float = 0.6f,
  val retouch: Float = 0.75f,
  val brighten: Float = 0.4f,
  val pop: Float = 0.5f,
  val smooth: Float = -1f,
  val blemish: Float = -1f,
  val even: Float = -1f,
  val bgLight: Float = 0f,
  val bgColor: Float = 0f,
  val edge: Float = 0f,
) {
  fun toArray() = floatArrayOf(blur, retouch, brighten, pop, smooth, blemish, even, bgLight, bgColor, edge)

  private fun follow(v: Float) = if (v < 0f) retouch else v

  /** Whether the engine's subject/skin pass has anything to do. */
  val needsPortrait: Boolean
    get() = blur > 0f || pop > 0f || brighten > 0f || follow(smooth) > 0f || follow(blemish) > 0f ||
      follow(even) > 0f || bgLight != 0f || bgColor != 0f

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

/** Face-precise adjustments (all 0..1 except [lightAngle], -1 picture left .. 1 right). */
data class FaceTuning(
  val eyes: Float = 0f,
  val teeth: Float = 0f,
  val lips: Float = 0f,
  val underEye: Float = 0f,
  val shine: Float = 0f,
  val slim: Float = 0f,
  val faceLight: Float = 0f,
  val studio: Float = 0f,
  val lightAngle: Float = 0f,
) {
  val any: Boolean get() = listOf(eyes, teeth, lips, underEye, shine, slim, faceLight, studio).any { it > 0f }
  fun toArray() = floatArrayOf(eyes, teeth, lips, underEye, shine, slim, faceLight, studio, lightAngle)
}

data class PortraitReport(
  val subjectCoverage: Float,
  val skinCoverage: Float,
  val spots: Int,
  val people: Int,
  val source: String,
  /** True when depth came from the photo's depth estimate (not the built-in ground-plane guess). */
  val depthFromPhoto: Boolean = false,
  /** Faces given precise adjustments (0 when none were asked for). */
  val facesAdjusted: Int = 0,
)

/**
 * Spotlight: finds what should stand out and hands masks to the engine.
 *
 *  - Subject: on-device subject segmentation (people and objects); taps pick
 *    which subjects, otherwise people (or all main subjects) are chosen.
 *    Falls back to the bundled person model, then to the engine's own
 *    tap-seeded cut-out, so it works fully offline.
 *  - Faces: face contours give face areas (retouching, calibrated to each
 *    person's own skin) and the eyes, brows, lips and nostrils, which are
 *    protected from smoothing. Faces too small to find in the whole photo
 *    (full-length shots) are looked for again in an enlarged crop of the
 *    top of each person.
 *  - Depth: an on-device depth estimate, so the blur grows with distance
 *    behind the subject while the ground they stand on stays sharp (the
 *    engine estimates a ground plane itself when it is unavailable).
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
        .setLandmarkMode(FaceDetectorOptions.LANDMARK_MODE_ALL)
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

  fun apply(
    bitmap: Bitmap,
    rotation: Int,
    points: List<Pair<Float, Float>>,
    params: PortraitParams,
    token: Long,
    tuning: FaceTuning = FaceTuning(),
  ): PortraitReport {
    var report = PortraitReport(0f, 0f, 0, 0, "", false)
    var landmarks: List<FaceLandmarks> = emptyList()
    if (params.needsPortrait) {
      val masks = buildMasks(bitmap, rotation, points, wantDepth = params.blur > 0f)
      try {
        val out = FloatArray(NativeEngine.PORTRAIT_REPORT_SIZE)
        NativeEngine.nativePortrait(bitmap, masks.subject, masks.faces, masks.protect, masks.depth, params.toArray(), token, out)
        report = PortraitReport(out[0], out[1], out[2].toInt(), masks.people, masks.source, depthFromPhoto = out[5] < 0.5f && masks.depth != null)
        landmarks = masks.landmarks
      } finally {
        masks.subject.recycle()
        masks.faces?.recycle()
        masks.protect?.recycle()
        masks.depth?.recycle()
      }
    } else if (tuning.any) {
      landmarks = findLandmarks(bitmap, rotation)
      report = report.copy(people = landmarks.size)
    }
    if (tuning.any && landmarks.isNotEmpty()) {
      val (pts, detailed) = FaceLandmarks.pack(landmarks.take(MAX_FACES))
      val out = FloatArray(NativeEngine.FACE_REPORT_SIZE)
      NativeEngine.nativeFaceAdjust(bitmap, pts, detailed, tuning.toArray(), token, out)
      report = report.copy(facesAdjusted = out[1].toInt())
    }
    return report
  }

  /**
   * A beauty preset (with Portrait mode's controls when it is a Spotlight
   * photo): masks and landmarks as for Spotlight, then the engine's beauty
   * compositor — skin, face, lighting, background and colour in one pass.
   */
  fun applyBeauty(
    bitmap: Bitmap,
    rotation: Int,
    points: List<Pair<Float, Float>>,
    recipe: com.photoplus.beauty.BeautyRecipe,
    token: Long,
  ): PortraitReport {
    val masks = buildMasks(bitmap, rotation, points, wantDepth = recipe.blur > 0f)
    try {
      val (pts, detailed) = FaceLandmarks.pack(masks.landmarks.take(MAX_FACES))
      val out = FloatArray(NativeEngine.BEAUTY_REPORT_SIZE)
      NativeEngine.nativeBeauty(bitmap, masks.subject, masks.faces, masks.protect, masks.depth, pts, detailed, recipe.toArray(), token, out)
      return PortraitReport(
        subjectCoverage = out[1],
        skinCoverage = out[2],
        spots = out[5].toInt(),
        people = masks.people,
        source = masks.source,
        depthFromPhoto = masks.depth != null && recipe.blur > 0f,
        facesAdjusted = out[6].toInt(),
      )
    } finally {
      masks.subject.recycle()
      masks.faces?.recycle()
      masks.protect?.recycle()
      masks.depth?.recycle()
    }
  }

  /** Landmarks of every face, in the bitmap's own orientation (no segmentation needed). */
  fun findLandmarks(bitmap: Bitmap, rotation: Int): List<FaceLandmarks> {
    val rot = MaskGeometry.normalise(rotation)
    val upright = scaledUpright(bitmap, rot, ANALYSIS_EDGE)
    try {
      val faces = runCatching { Tasks.await(faceDetector.process(InputImage.fromBitmap(upright, 0)), ML_TIMEOUT_S, TimeUnit.SECONDS) }
        .onFailure { Log.w(TAG, "face detection unavailable", it) }.getOrNull() ?: return emptyList()
      return toBitmapLandmarks(withContours(upright, faces.map { FaceShape.of(it) }), upright.width, upright.height, rot)
    } finally {
      upright.recycle()
    }
  }

  /**
   * Face detection measures full contours only for the most prominent face.
   * Each other face is looked at again on its own (an enlarged crop where it
   * is the main face), so everyone in a group photo gets precise outlines.
   */
  private fun withContours(upright: Bitmap, faces: List<FaceShape>): List<FaceShape> {
    var extra = 0
    return faces.map { face ->
      if (face.contour(FaceContour.FACE) != null || extra >= MAX_FACES) return@map face
      extra++
      val b = face.box
      val side = max(b.width(), b.height()) * 1.8f
      val x0 = (b.centerX() - side / 2).roundToInt().coerceIn(0, upright.width - 1)
      val y0 = (b.centerY() - side / 2).roundToInt().coerceIn(0, upright.height - 1)
      val cw = side.roundToInt().coerceAtMost(upright.width - x0)
      val ch = side.roundToInt().coerceAtMost(upright.height - y0)
      if (cw < 16 || ch < 16) return@map face
      val scale = (CROP_EDGE.toFloat() / max(cw, ch)).coerceIn(1f, MAX_CROP_ZOOM)
      val crop = Bitmap.createBitmap(upright, x0, y0, cw, ch, Matrix().apply { postScale(scale, scale) }, true)
      try {
        val found = runCatching { Tasks.await(faceDetector.process(InputImage.fromBitmap(crop, 0)), ML_TIMEOUT_S, TimeUnit.SECONDS) }
          .getOrNull().orEmpty()
        val s = crop.width.toFloat() / cw
        found.map { FaceShape.of(it, s, x0.toFloat(), y0.toFloat()) }
          .filter { it.contour(FaceContour.FACE) != null }
          .minByOrNull { hypot(it.box.centerX() - b.centerX(), it.box.centerY() - b.centerY()) }
          ?.takeIf { hypot(it.box.centerX() - b.centerX(), it.box.centerY() - b.centerY()) < 0.5f * b.width() }
          ?: face
      } finally {
        crop.recycle()
      }
    }
  }

  private fun toBitmapLandmarks(faces: List<FaceShape>, w: Int, h: Int, rot: Int): List<FaceLandmarks> =
    faces.mapNotNull { it.landmarks(w.toFloat(), h.toFloat()) }
      .map { lm -> lm.mapped { x, y -> MaskGeometry.uprightToBitmap(x, y, rot) } }

  private data class Masks(
    val subject: Bitmap,
    val faces: Bitmap?,
    val protect: Bitmap?,
    val depth: Bitmap?,
    val people: Int,
    val source: String,
    val landmarks: List<FaceLandmarks> = emptyList(),
  )

  /** A detected face, in upright analysis-image coordinates. */
  private class FaceShape(val box: RectF, val contours: Map<Int, List<PointF>>, val marks: Map<Int, PointF>) {
    fun contour(type: Int) = contours[type]

    /** Landmarks normalised to the [w] x [h] analysis image: measured contours, else approximated. */
    fun landmarks(w: Float, h: Float): FaceLandmarks? {
      val measured = LAYOUT.mapNotNull { (type, c) -> contours[type]?.let { pts -> c to pts.map { Pt(it.x, it.y) } } }.toMap()
      if (measured.size == LAYOUT.size) FaceLandmarks.fromContours(measured, w, h)?.let { return it }
      fun mark(type: Int) = marks[type]?.let { Pt(it.x, it.y) }
      return FaceLandmarks.approximate(
        box.left, box.top, box.right, box.bottom,
        mark(FaceLandmark.LEFT_EYE), mark(FaceLandmark.RIGHT_EYE),
        mark(FaceLandmark.MOUTH_LEFT), mark(FaceLandmark.MOUTH_RIGHT), mark(FaceLandmark.MOUTH_BOTTOM), w, h,
      )
    }

    companion object {
      /** [face] found in a crop scaled by [scale] whose origin is ([dx], [dy]) in the analysis image. */
      fun of(face: Face, scale: Float = 1f, dx: Float = 0f, dy: Float = 0f): FaceShape {
        val inv = 1f / scale
        val b = face.boundingBox
        val box = RectF(b.left * inv + dx, b.top * inv + dy, b.right * inv + dx, b.bottom * inv + dy)
        val contours = LAYOUT.mapNotNull { (type, _) ->
          face.getContour(type)?.points?.takeIf { it.isNotEmpty() }
            ?.let { pts -> type to pts.map { PointF(it.x * inv + dx, it.y * inv + dy) } }
        }.toMap()
        val marks = MARKS.mapNotNull { type ->
          face.getLandmark(type)?.position?.let { type to PointF(it.x * inv + dx, it.y * inv + dy) }
        }.toMap()
        return FaceShape(box, contours, marks)
      }

      /** Face contour types → the engine's landmark layout. */
      val LAYOUT = listOf(
        FaceContour.FACE to FaceLandmarks.OVAL,
        FaceContour.LEFT_EYEBROW_TOP to FaceLandmarks.LEFT_BROW_TOP,
        FaceContour.LEFT_EYEBROW_BOTTOM to FaceLandmarks.LEFT_BROW_BOTTOM,
        FaceContour.RIGHT_EYEBROW_TOP to FaceLandmarks.RIGHT_BROW_TOP,
        FaceContour.RIGHT_EYEBROW_BOTTOM to FaceLandmarks.RIGHT_BROW_BOTTOM,
        FaceContour.LEFT_EYE to FaceLandmarks.LEFT_EYE,
        FaceContour.RIGHT_EYE to FaceLandmarks.RIGHT_EYE,
        FaceContour.UPPER_LIP_TOP to FaceLandmarks.UPPER_LIP_TOP,
        FaceContour.UPPER_LIP_BOTTOM to FaceLandmarks.UPPER_LIP_BOTTOM,
        FaceContour.LOWER_LIP_TOP to FaceLandmarks.LOWER_LIP_TOP,
        FaceContour.LOWER_LIP_BOTTOM to FaceLandmarks.LOWER_LIP_BOTTOM,
        FaceContour.NOSE_BRIDGE to FaceLandmarks.NOSE_BRIDGE,
        FaceContour.NOSE_BOTTOM to FaceLandmarks.NOSE_BOTTOM,
      )
      private val MARKS = listOf(
        FaceLandmark.LEFT_EYE, FaceLandmark.RIGHT_EYE, FaceLandmark.MOUTH_LEFT, FaceLandmark.MOUTH_RIGHT, FaceLandmark.MOUTH_BOTTOM,
      )
    }
  }

  /**
   * A cut-out of what the taps chose in an upright photo (for custom props):
   * subject segmentation first (people and objects), then the engine's tap
   * cut-out. Returns mask bytes (0..255) at the photo's size, or null.
   */
  fun cutout(bitmap: Bitmap, points: List<Pair<Float, Float>>): ByteArray? {
    val w = bitmap.width
    val h = bitmap.height
    val image = InputImage.fromBitmap(bitmap, 0)
    val subjects = runCatching { Tasks.await(subjectSegmenter.process(image), ML_TIMEOUT_S, TimeUnit.SECONDS) }
      .onFailure { Log.i(TAG, "subject segmentation unavailable, using the tap cut-out", it) }.getOrNull()
    subjectMask(image, w, h, points, emptyList(), subjects)?.let { return it.first }
    val size = IntArray(2)
    val bytes = NativeEngine.nativeSubjectMask(bitmap, points.flatMap { listOf(it.first, it.second) }.toFloatArray(), size)
    if (size[0] <= 0 || size[1] <= 0) return null
    return MaskGeometry.resizeBilinear(bytes, size[0], size[1], w, h)
  }

  private fun buildMasks(bitmap: Bitmap, rotation: Int, points: List<Pair<Float, Float>>, wantDepth: Boolean): Masks {
    val rot = MaskGeometry.normalise(rotation)
    // Small upright copy for the models (one scaled copy; the photo itself is untouched).
    val upright = scaledUpright(bitmap, rot, ANALYSIS_EDGE)
    try {
      val image = InputImage.fromBitmap(upright, 0)
      // Both models start at once; each wait is bounded.
      val faceTask = runCatching { faceDetector.process(image) }.getOrNull()
      val subjectTask = runCatching { subjectSegmenter.process(image) }.getOrNull()
      // Depth runs on this thread while the other models work.
      val depthMap = if (wantDepth) DepthEstimator.estimate(upright) else null
      var faces = (
        runCatching { faceTask?.let { Tasks.await(it, ML_TIMEOUT_S, TimeUnit.SECONDS) } }
          .onFailure { Log.w(TAG, "face detection unavailable", it) }.getOrNull() ?: emptyList()
        ).map { FaceShape.of(it) }
      val subjects = runCatching { subjectTask?.let { Tasks.await(it, ML_TIMEOUT_S, TimeUnit.SECONDS) } }
        .onFailure { Log.i(TAG, "subject segmentation unavailable, using fallbacks", it) }.getOrNull()
      val (subjectBytes, source) = subjectMask(image, upright.width, upright.height, points, faces, subjects)
        ?: (null to "")
      if (faces.isEmpty() && subjectBytes != null) faces = smallFaces(upright, subjectBytes)
      faces = withContours(upright, faces)
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
      val depth = depthMap?.let {
        val (b, w, h) = MaskGeometry.rotateMask(it.bytes, it.width, it.height, -rot)
        alphaBitmap(b, w, h)
      }
      return Masks(
        subject, faceMask, protectMask, depth, faces.size, if (subjectBytes != null) source else "taps",
        toBitmapLandmarks(faces, upright.width, upright.height, rot),
      )
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
    faces: List<FaceShape>,
    result: SubjectSegmentationResult?,
  ): Pair<ByteArray, String>? {
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
          faces.isNotEmpty() -> faces.any { f -> at(f.box.centerX() / w, f.box.centerY() / h) >= 0.5f }
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

  /**
   * In full-length or group shots a face can be too small to find in the
   * whole photo: look again in an enlarged crop of the top of the person.
   */
  private fun smallFaces(upright: Bitmap, subject: ByteArray): List<FaceShape> {
    val w = upright.width
    val h = upright.height
    val crop = MaskGeometry.headCrop(subject, w, h) ?: return emptyList()
    val (x0, y0, cw, ch) = crop
    val scale = minOf(MAX_CROP_ZOOM, SMALL_FACE_EDGE.toFloat() / max(cw, ch))
    if (scale < 1.5f) return emptyList() // big enough already: nothing more to find
    val zoomed = Bitmap.createBitmap(upright, x0, y0, cw, ch, Matrix().apply { postScale(scale, scale) }, true)
    return try {
      val found = runCatching { Tasks.await(faceDetector.process(InputImage.fromBitmap(zoomed, 0)), ML_TIMEOUT_S, TimeUnit.SECONDS) }
        .onFailure { Log.i(TAG, "small-face search unavailable", it) }.getOrNull() ?: emptyList()
      // The real scale of the crop (bitmap sizes are rounded).
      val s = zoomed.width.toFloat() / cw
      found.map { FaceShape.of(it, s, x0.toFloat(), y0.toFloat()) }
    } finally {
      zoomed.recycle()
    }
  }

  /** Face areas and protected features, drawn in the bitmap's own orientation. */
  private fun drawFaces(faces: List<FaceShape>, w: Int, h: Int, rot: Int): Pair<Bitmap, Bitmap> {
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
      val oval = face.contour(FaceContour.FACE)
      if (oval != null && oval.size >= 8) {
        fc.drawPath(polygon(oval), fill)
      } else {
        fc.drawOval(face.box, fill) // faces beyond the first come without contours
      }
      val size = max(face.box.width(), face.box.height())
      val pad = Paint(fill).apply { style = Paint.Style.FILL_AND_STROKE; strokeWidth = size * 0.04f; strokeJoin = Paint.Join.ROUND }
      for (part in listOf(FaceContour.LEFT_EYE, FaceContour.RIGHT_EYE)) {
        face.contour(part)?.takeIf { it.size >= 3 }?.let { pc.drawPath(polygon(it), Paint(pad).apply { strokeWidth = size * 0.06f }) }
      }
      for ((top, bottom) in listOf(
        FaceContour.LEFT_EYEBROW_TOP to FaceContour.LEFT_EYEBROW_BOTTOM,
        FaceContour.RIGHT_EYEBROW_TOP to FaceContour.RIGHT_EYEBROW_BOTTOM,
        FaceContour.UPPER_LIP_TOP to FaceContour.LOWER_LIP_BOTTOM,
      )) {
        val a = face.contour(top)
        val b = face.contour(bottom)
        if (a != null && b != null && a.size >= 2 && b.size >= 2) pc.drawPath(polygon(a + b.reversed()), pad)
      }
      face.contour(FaceContour.NOSE_BOTTOM)?.takeIf { it.size >= 2 }?.let {
        pc.drawPath(polygon(it), Paint(pad).apply { strokeWidth = size * 0.07f; style = Paint.Style.STROKE })
      }
    }
    return faceMask to protect
  }

  companion object {
    private const val TAG = "PhotoPlusSpotlight"

    /** A detected face's landmarks, normalised to the [w] x [h] image it was found in (contours, else approximated). */
    fun landmarksOf(face: Face, w: Float, h: Float): FaceLandmarks? = FaceShape.of(face).landmarks(w, h)
    private const val ANALYSIS_EDGE = 1024
    private const val ML_TIMEOUT_S = 10L
    private const val SMALL_FACE_EDGE = 720
    private const val MAX_CROP_ZOOM = 4f
    /** Faces given precise outlines and adjustments (larger groups: the biggest faces). */
    private const val MAX_FACES = 8
    private const val CROP_EDGE = 480

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
