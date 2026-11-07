package com.photoplus.beauty

import android.graphics.Bitmap
import android.graphics.Matrix
import android.os.SystemClock
import android.util.Log
import androidx.annotation.OptIn
import androidx.camera.core.ExperimentalGetImage
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.core.graphics.createBitmap
import androidx.core.graphics.scale
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.face.FaceDetection
import com.google.mlkit.vision.face.FaceDetectorOptions
import com.google.mlkit.vision.segmentation.Segmentation
import com.google.mlkit.vision.segmentation.selfie.SelfieSegmenterOptions
import com.photoplus.camera.FaceTracker
import com.photoplus.engine.NativeEngine
import com.photoplus.studio.FaceFinder
import com.photoplus.studio.FaceLandmarks
import com.photoplus.studio.PortraitProcessor
import com.photoplus.video.FaceMasks
import java.util.concurrent.TimeUnit
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Live analysis for the beauty preview, on the camera's analysis stream
 * (~640 px, about 12 times a second):
 *  - the subject mask (on-device selfie segmentation, stream mode) every
 *    other frame — the shader keeps the last one in between;
 *  - face contours every frame (the largest face drives the face touches);
 *    faces are also passed on to the props tracker when props are worn;
 *  - the scene (the person's own skin colour, subject extent, light side)
 *    twice a second — it changes slowly.
 * Everything is expressed in preview space (what the viewfinder shows).
 */
object BeautyAnalyzer : ImageAnalysis.Analyzer {
  private const val TAG = "BeautyAnalyzer"
  private const val MIN_FRAME_INTERVAL_MS = 80L
  private const val SCENE_INTERVAL_MS = 500L
  private const val MASK_EDGE = 256

  @Volatile var mirrored = false

  private val detector by lazy {
    FaceDetection.getClient(
      FaceDetectorOptions.Builder()
        .setPerformanceMode(FaceDetectorOptions.PERFORMANCE_MODE_FAST)
        .setContourMode(FaceDetectorOptions.CONTOUR_MODE_ALL)
        .setMinFaceSize(0.15f)
        .build(),
    )
  }
  private val segmenter by lazy {
    Segmentation.getClient(SelfieSegmenterOptions.Builder().setDetectorMode(SelfieSegmenterOptions.STREAM_MODE).build())
  }

  private var lastFrame = 0L
  private var lastScene = 0L
  private var frameCount = 0L
  private var mask: ByteArray? = null
  private var maskSize = 0 to 0
  private var scene = LiveBeautyState.NO_SCENE

  @OptIn(markerClass = [ExperimentalGetImage::class])
  override fun analyze(image: ImageProxy) {
    val now = SystemClock.elapsedRealtime()
    val media = image.image
    if (media == null || LiveBeautyState.recipe?.active != true || now - lastFrame < MIN_FRAME_INTERVAL_MS) {
      image.close()
      return
    }
    lastFrame = now
    try {
      analyzeFrame(image, InputImage.fromMediaImage(media, image.imageInfo.rotationDegrees), now)
    } catch (e: Exception) {
      Log.w(TAG, "live analysis failed", e)
    } finally {
      image.close()
    }
  }

  private fun analyzeFrame(image: ImageProxy, input: InputImage, now: Long) {
    val rotation = image.imageInfo.rotationDegrees
    val c = image.cropRect
    val crop = PreviewSpace.Crop(c.left, c.top, c.right, c.bottom)
    val mirror = mirrored
    val (uw, uh) = PreviewSpace.uprightSize(crop, rotation)
    val toPreview = PreviewSpace.fromBuffer(crop, rotation, mirror)
    val sensor = image.imageInfo.sensorToBufferTransformMatrix.let { m -> FloatArray(9).also { m.getValues(it) } }

    // Faces (every frame) and the mask (every other frame), in parallel.
    val faceTask = detector.process(input)
    val maskTask = if (frameCount++ % 2L == 0L || mask == null) segmenter.process(input) else null
    val faces = runCatching { Tasks.await(faceTask, 1, TimeUnit.SECONDS) }.getOrNull().orEmpty()
    // Upright analysis frame (ML Kit's coordinates) → preview fractions.
    val uprightW = if (rotation % 180 == 90) image.height else image.width
    val uprightH = if (rotation % 180 == 90) image.width else image.height
    val uprightCrop = FaceTracker.Mapping.uprightCrop(
      FaceTracker.Mapping.Box(c.left, c.top, c.right, c.bottom), image.width, image.height, rotation,
    )
    fun toPreviewFraction(x: Float, y: Float): Pair<Float, Float> {
      val px = (x * uprightW - uprightCrop.left) / max(1, uprightCrop.width)
      val py = (y * uprightH - uprightCrop.top) / max(1, uprightCrop.height)
      return (if (mirror) 1f - px else px) to py
    }
    val landmarks = faces.mapNotNull { PortraitProcessor.landmarksOf(it, uprightW.toFloat(), uprightH.toFloat()) }
      .map { it.mapped { x, y -> toPreviewFraction(x, y) } }
    // Props worn in the camera follow the same faces.
    FaceTracker.sink?.let { sink ->
      val bufferCrop = FaceTracker.Mapping.Box(c.left, c.top, c.right, c.bottom)
      sink.onFaces(faces.mapNotNull { FaceFinder.geometry(it) }.map { FaceTracker.Mapping.toPreview(it, bufferCrop, image.width, image.height, rotation, mirror) })
    }

    if (maskTask != null) {
      runCatching { Tasks.await(maskTask, 1, TimeUnit.SECONDS) }.getOrNull()?.let { seg ->
        val sw = seg.width
        val sh = seg.height
        val buf = seg.buffer.apply { rewind() }.asFloatBuffer()
        val scale = MASK_EDGE.toFloat() / max(uw, uh)
        val mw = max(1, (uw * scale).roundToInt())
        val mh = max(1, (uh * scale).roundToInt())
        val out = ByteArray(mw * mh)
        for (y in 0 until mh) {
          for (x in 0 until mw) {
            // Preview fraction → upright analysis pixel → segmentation cell.
            var fx = (x + 0.5f) / mw
            if (mirror) fx = 1f - fx
            val ux = uprightCrop.left + fx * uprightCrop.width
            val uy = uprightCrop.top + (y + 0.5f) / mh * uprightCrop.height
            val sx = (ux / uprightW * sw).toInt().coerceIn(0, sw - 1)
            val sy = (uy / uprightH * sh).toInt().coerceIn(0, sh - 1)
            out[y * mw + x] = (buf.get(sy * sw + sx).coerceIn(0f, 1f) * 255f).roundToInt().toByte()
          }
        }
        mask = out
        maskSize = mw to mh
      }
    }

    // The scene, twice a second: measured by the engine on a small preview-space frame.
    if (now - lastScene >= SCENE_INTERVAL_MS) {
      lastScene = now
      scene = measureScene(image, rotation, uprightCrop, mirror, landmarks) ?: scene
    }
    val largest = landmarks.maxByOrNull { lm -> (0 until 36).maxOf { lm.points[2 * it] } - (0 until 36).minOf { lm.points[2 * it] } }
    LiveBeautyState.analysis = LiveBeautyState.Analysis(
      mask = mask,
      maskW = maskSize.first,
      maskH = maskSize.second,
      previewW = uw,
      previewH = uh,
      face = largest?.let { LiveBeautyUniforms.face(it.points, uw, uh) },
      scene = scene,
      previewFromAnalysis = toPreview,
      analysisSensorToBuffer = Affine.fromValues(sensor),
      timestampMs = now,
    )
  }

  private fun measureScene(
    image: ImageProxy,
    rotation: Int,
    crop: FaceTracker.Mapping.Box,
    mirror: Boolean,
    landmarks: List<FaceLandmarks>,
  ): FloatArray? {
    val frame = runCatching { image.toBitmap() }.getOrNull() ?: return null
    var preview: Bitmap? = null
    var faceMask: Bitmap? = null
    var protect: Bitmap? = null
    var subject: Bitmap? = null
    try {
      // Upright, cropped to the viewport, mirrored like the viewfinder, ~256 px.
      val m = Matrix().apply {
        postRotate(rotation.toFloat())
        if (mirror) postScale(-1f, 1f)
      }
      val upright = Bitmap.createBitmap(frame, 0, 0, frame.width, frame.height, m, true)
      val ux = if (mirror) upright.width - crop.right else crop.left
      val cropped = Bitmap.createBitmap(
        upright, ux.coerceIn(0, upright.width - 1), crop.top.coerceIn(0, upright.height - 1),
        crop.width.coerceAtMost(upright.width - ux.coerceIn(0, upright.width - 1)),
        crop.height.coerceAtMost(upright.height - crop.top.coerceIn(0, upright.height - 1)),
      )
      if (upright !== frame && upright !== cropped) upright.recycle()
      val s = MASK_EDGE.toFloat() / max(cropped.width, cropped.height)
      preview = cropped.scale(max(1, (cropped.width * s).roundToInt()), max(1, (cropped.height * s).roundToInt()))
        .copy(Bitmap.Config.ARGB_8888, true)
      if (cropped !== frame) cropped.recycle()
      val p = preview ?: return null
      if (landmarks.isNotEmpty()) {
        val (fm, pm) = FaceMasks.draw(landmarks, p.width, p.height)
        faceMask = fm
        protect = pm
      }
      mask?.let { bytes ->
        val (mw, mh) = maskSize
        subject = createBitmap(mw, mh, Bitmap.Config.ALPHA_8).apply { copyPixelsFromBuffer(java.nio.ByteBuffer.wrap(bytes)) }
      }
      val (pts, detailed) = FaceLandmarks.pack(landmarks.take(4))
      val out = FloatArray(NativeEngine.BEAUTY_SCENE_SIZE)
      NativeEngine.nativeBeautyMeasure(p, subject, faceMask, protect, pts, detailed, out)
      return out
    } catch (e: Exception) {
      Log.w(TAG, "scene measure failed", e)
      return null
    } finally {
      frame.recycle()
      preview?.recycle()
      faceMask?.recycle()
      protect?.recycle()
      subject?.recycle()
    }
  }

  fun reset() {
    mask = null
    scene = LiveBeautyState.NO_SCENE
    lastScene = 0L
  }
}
