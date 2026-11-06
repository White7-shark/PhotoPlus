package com.photoplus.camera

import android.os.SystemClock
import androidx.annotation.OptIn
import androidx.camera.core.ExperimentalGetImage
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.face.FaceDetection
import com.google.mlkit.vision.face.FaceDetectorOptions
import com.photoplus.edit.PropFit.Pt
import com.photoplus.studio.FaceFinder
import java.util.concurrent.Executor
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Live face tracking for props in the camera (like a lens): the faces in the
 * preview, as preview fractions, about 15 times a second. Props follow them
 * in the app; the photo gets its props fitted again natively at capture.
 *
 * Analysis frames carry the viewport crop, so positions are measured within
 * exactly what the preview shows, then mirrored for the front camera as the
 * preview is.
 */
object FaceTracker : ImageAnalysis.Analyzer {
  private const val MIN_FRAME_INTERVAL_MS = 66L // ≈ 15 fps

  /** Receives faces (preview fractions) on the analysis thread. */
  fun interface Sink {
    fun onFaces(faces: List<Map<String, Any?>>)
  }

  @Volatile var sink: Sink? = null
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
  private val busy = AtomicBoolean(false)
  private var lastFrame = 0L
  private var hadFaces = false
  private val direct = Executor { it.run() }

  @OptIn(markerClass = [ExperimentalGetImage::class])
  override fun analyze(image: ImageProxy) {
    val now = SystemClock.elapsedRealtime()
    val media = image.image
    if (media == null || sink == null || now - lastFrame < MIN_FRAME_INTERVAL_MS || !busy.compareAndSet(false, true)) {
      image.close()
      return
    }
    lastFrame = now
    val rotation = image.imageInfo.rotationDegrees
    val crop = image.cropRect.let { Mapping.Box(it.left, it.top, it.right, it.bottom) }
    val bufferW = image.width
    val bufferH = image.height
    val mirror = mirrored
    detector.process(InputImage.fromMediaImage(media, rotation))
      .addOnSuccessListener(direct) { faces ->
        val out = faces.mapNotNull { FaceFinder.geometry(it) }
          .map { Mapping.toPreview(it, crop, bufferW, bufferH, rotation, mirror) }
        if (out.isNotEmpty() || hadFaces) sink?.onFaces(out)
        hadFaces = out.isNotEmpty()
      }
      .addOnCompleteListener(direct) {
        image.close()
        busy.set(false)
      }
  }

  fun reset() {
    hadFaces = false
  }

  /** Pure geometry, unit-tested: analysis-frame pixels → preview fractions. */
  object Mapping {
    /** A pixel rectangle (left, top inclusive; right, bottom exclusive). */
    data class Box(val left: Int, val top: Int, val right: Int, val bottom: Int) {
      val width get() = right - left
      val height get() = bottom - top
    }

    /** The viewport crop, given in sensor-buffer pixels, in the upright (rotated) frame. */
    fun uprightCrop(crop: Box, bufferW: Int, bufferH: Int, rotation: Int): Box = when (((rotation % 360) + 360) % 360) {
      90 -> Box(bufferH - crop.bottom, crop.left, bufferH - crop.top, crop.right)
      180 -> Box(bufferW - crop.right, bufferH - crop.bottom, bufferW - crop.left, bufferH - crop.top)
      270 -> Box(crop.top, bufferW - crop.right, crop.bottom, bufferW - crop.left)
      else -> crop
    }

    fun point(p: Pt, crop: Box, mirror: Boolean): Pair<Double, Double> {
      val x = (p.x - crop.left) / crop.width.coerceAtLeast(1).toFloat()
      val y = (p.y - crop.top) / crop.height.coerceAtLeast(1).toFloat()
      return (if (mirror) 1.0 - x else x.toDouble()) to y.toDouble()
    }

    fun toPreview(face: FaceFinder.Geometry, crop: Box, bufferW: Int, bufferH: Int, rotation: Int, mirror: Boolean): Map<String, Any?> {
      val c = uprightCrop(crop, bufferW, bufferH, rotation)
      fun pt(p: Pt) = point(p, c, mirror).let { (x, y) -> mapOf("x" to x, "y" to y) }
      // Mirroring swaps which eye is on the left of the picture.
      val (left, right) = if (mirror) face.eyeR to face.eyeL else face.eyeL to face.eyeR
      return mapOf(
        "top" to pt(face.top),
        "chin" to pt(face.chin),
        "eyeL" to pt(left),
        "eyeR" to pt(right),
        "width" to (face.width / c.width.coerceAtLeast(1)).toDouble(),
      )
    }
  }
}
