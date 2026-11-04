package com.photoplus.camera

import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.core.graphics.createBitmap
import com.photoplus.engine.EngineException
import com.photoplus.engine.NativeEngine
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Advanced focus assistance (premium): highlights in-focus edges over the
 * live preview and reports a centre sharpness score.
 *
 * Zero-copy: the camera's Y plane (a direct ByteBuffer) is read in place by
 * the native engine, which writes straight into a small overlay bitmap.
 * Two overlay bitmaps alternate so the one on screen is never written to.
 */
object FocusPeakingAnalyzer : ImageAnalysis.Analyzer {
  private const val TAG = "PhotoPlusPeaking"
  private const val MIN_FRAME_INTERVAL_MS = 100L // ≤ 10 fps of overlay work
  private const val SHARPNESS_INTERVAL_MS = 250L
  private const val OVERLAY_DOWNSCALE = 4

  /** Receives overlay frames on the main thread (implemented by CameraPreviewView). */
  interface Overlay {
    fun showPeaking(bitmap: Bitmap, transform: FloatArray)
    fun clearPeaking()
  }

  @Volatile var overlay: Overlay? = null
  @Volatile var mirrored = false

  /** Edge-strength threshold (0..1); lower marks more edges. */
  @Volatile var threshold = 0.16f
  /** Peaking colour as 0xAARRGGBB. */
  @Volatile var color: Int = 0xFFFF3B6B.toInt()

  private val main = Handler(Looper.getMainLooper())
  private val posting = AtomicBoolean(false)
  private val buffers = arrayOfNulls<Bitmap>(2)
  private var next = 0
  private var lastFrame = 0L
  private var lastSharpness = 0L

  override fun analyze(image: ImageProxy) {
    image.use { frame ->
      val now = SystemClock.elapsedRealtime()
      if (now - lastFrame < MIN_FRAME_INTERVAL_MS || posting.get()) return
      lastFrame = now
      val target = overlay ?: return
      if (!NativeEngine.ensureLoaded()) return
      val plane = frame.planes.firstOrNull() ?: return
      val w = frame.width
      val h = frame.height
      val bitmap = buffer(next, maxOf(1, w / OVERLAY_DOWNSCALE), maxOf(1, h / OVERLAY_DOWNSCALE))
      val sharpness = try {
        NativeEngine.nativeFocusPeaking(plane.buffer, w, h, plane.rowStride, plane.pixelStride, threshold, OverlayGeometry.toRgbaMemory(color), bitmap)
      } catch (e: EngineException) {
        Log.w(TAG, "Focus peaking failed", e)
        return
      }
      val crop = frame.cropRect
      val transform = OverlayGeometry.transform(
        overlayWidth = bitmap.width,
        sourceWidth = w,
        cropLeft = crop.left, cropTop = crop.top, cropWidth = crop.width(), cropHeight = crop.height(),
        rotationDegrees = frame.imageInfo.rotationDegrees,
        mirror = mirrored,
      )
      next = 1 - next
      posting.set(true)
      main.post {
        posting.set(false)
        // The overlay may have been detached while this frame was in flight.
        if (overlay === target) target.showPeaking(bitmap, transform)
      }
      if (now - lastSharpness >= SHARPNESS_INTERVAL_MS) {
        lastSharpness = now
        CameraController.events?.emit("PhotoPlusCameraPeaking", mapOf("sharpness" to sharpness.toDouble()))
      }
    }
  }

  private fun buffer(index: Int, width: Int, height: Int): Bitmap {
    val existing = buffers[index]
    if (existing != null && existing.width == width && existing.height == height) return existing
    // Old bitmaps are left to the GC: the other buffer may still be on screen.
    return createBitmap(width, height, Bitmap.Config.ARGB_8888).also { buffers[index] = it }
  }

  fun clearOverlay() {
    main.post { overlay?.clearPeaking() }
  }
}

/**
 * Maps overlay pixels (the analysis frame downscaled, in sensor orientation)
 * to view-normalised coordinates (0..1 in the upright, cropped preview).
 * Pure maths so it is unit-testable; the view scales the result to its size.
 */
object OverlayGeometry {
  /**
   * Returns an affine [a, b, c, d, e, f] with x' = a·x + b·y + c and
   * y' = d·x + e·y + f.
   */
  fun transform(
    overlayWidth: Int,
    sourceWidth: Int,
    cropLeft: Int,
    cropTop: Int,
    cropWidth: Int,
    cropHeight: Int,
    rotationDegrees: Int,
    mirror: Boolean,
  ): FloatArray {
    val s = overlayWidth.toFloat() / sourceWidth // overlay px per source px
    val cw = cropWidth * s
    val ch = cropHeight * s
    val ox = cropLeft * s
    val oy = cropTop * s
    // 1. Translate so the crop starts at the origin: (x - ox, y - oy).
    // 2. Rotate clockwise into upright orientation and normalise to 0..1.
    var m = when (((rotationDegrees % 360) + 360) % 360) {
      90 -> floatArrayOf(0f, -1f / ch, (ch + oy) / ch, 1f / cw, 0f, -ox / cw)
      180 -> floatArrayOf(-1f / cw, 0f, (cw + ox) / cw, 0f, -1f / ch, (ch + oy) / ch)
      270 -> floatArrayOf(0f, 1f / ch, -oy / ch, -1f / cw, 0f, (cw + ox) / cw)
      else -> floatArrayOf(1f / cw, 0f, -ox / cw, 0f, 1f / ch, -oy / ch)
    }
    // 3. Front camera previews are mirrored; analysis frames are not.
    if (mirror) m = floatArrayOf(-m[0], -m[1], 1f - m[2], m[3], m[4], m[5])
    return m
  }

  /** 0xAARRGGBB → bytes R,G,B,A in memory (little-endian int with R lowest). */
  fun toRgbaMemory(argb: Int): Int {
    val a = (argb ushr 24) and 0xFF
    val r = (argb ushr 16) and 0xFF
    val g = (argb ushr 8) and 0xFF
    val b = argb and 0xFF
    return r or (g shl 8) or (b shl 16) or (a shl 24)
  }

  fun apply(m: FloatArray, x: Float, y: Float): Pair<Float, Float> =
    (m[0] * x + m[1] * y + m[2]) to (m[3] * x + m[4] * y + m[5])
}
