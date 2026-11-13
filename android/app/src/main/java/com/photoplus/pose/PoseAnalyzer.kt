package com.photoplus.pose

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Matrix
import android.os.SystemClock
import android.util.Log
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import com.google.mediapipe.framework.image.BitmapImageBuilder
import com.google.mediapipe.framework.image.MPImage
import com.google.mediapipe.tasks.vision.poselandmarker.PoseLandmarker
import com.google.mediapipe.tasks.vision.poselandmarker.PoseLandmarkerResult
import com.photoplus.engine.NativeEngine
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The live pose assistant: about 15 times a second, the person's pose in
 * what the preview shows (the viewport crop, upright, mirrored like the
 * preview for the front camera), steadied through brief occlusions and
 * compared with the chosen reference pose natively; each answer goes to the
 * app as a PhotoPlusPose event.
 *
 * It can share the analysis stream with another analyser (live beauty,
 * props, focus peaking): [capture] copies the frame and leaves the image
 * open for the other one.
 */
object PoseAnalyzer : ImageAnalysis.Analyzer {
  private const val TAG = "PhotoPlusPose"
  private const val MIN_FRAME_INTERVAL_MS = 66L // ≈ 15 fps

  fun interface Sink {
    fun onPose(payload: Map<String, Any?>)
  }

  @Volatile var sink: Sink? = null
  @Volatile var mirrored = false
  @Volatile var guide: PoseGuide? = null

  private val lock = Any()
  private var landmarker: PoseLandmarker? = null
  private var tracker = 0L
  private var appContext: Context? = null
  private var onGpu = true
  private val busy = AtomicBoolean(false)
  private var lastSubmit = 0L
  private var lastTimestamp = 0L
  private class Pending(val mirrored: Boolean, val bitmap: Bitmap)
  private val pending = ConcurrentHashMap<Long, Pending>()
  /** Loads and closes landmarkers (never on MediaPipe's own thread). */
  val worker = Executors.newSingleThreadExecutor { r -> Thread(r, "pp-pose") }

  val running: Boolean get() = synchronized(lock) { landmarker != null }

  /** Starts the landmarker (off the main thread is best: it loads a model). Returns false if it cannot run. */
  fun start(context: Context): Boolean = synchronized(lock) {
    if (landmarker != null) return true
    appContext = context.applicationContext
    if (!NativeEngine.ensureLoaded() || !PoseEstimator.available(context)) return false
    return try {
      landmarker = PoseEstimator.live(context.applicationContext, onGpu, ::onResult, ::onError)
      if (tracker == 0L) tracker = NativeEngine.nativePoseTrackerCreate()
      true
    } catch (e: Exception) {
      Log.w(TAG, "pose assistant unavailable", e)
      false
    }
  }

  fun stop() = synchronized(lock) {
    try { landmarker?.close() } catch (e: Exception) { Log.w(TAG, "closing the landmarker", e) }
    landmarker = null
    if (tracker != 0L) NativeEngine.nativePoseTrackerDestroy(tracker)
    tracker = 0L
    pending.values.forEach { it.bitmap.recycle() }
    pending.clear()
    busy.set(false)
  }

  /** A new camera session: forget the last person. */
  fun reset() = synchronized(lock) {
    if (tracker != 0L) {
      NativeEngine.nativePoseTrackerDestroy(tracker)
      tracker = NativeEngine.nativePoseTrackerCreate()
    }
  }

  override fun analyze(image: ImageProxy) {
    try { capture(image) } finally { image.close() }
  }

  /** Copies the frame for the landmarker (throttled); never closes [image]. */
  fun capture(image: ImageProxy) {
    val now = SystemClock.uptimeMillis()
    val lm = synchronized(lock) { landmarker } ?: return
    if (sink == null || now - lastSubmit < MIN_FRAME_INTERVAL_MS || !busy.compareAndSet(false, true)) return
    lastSubmit = now
    val frame = try {
      upright(image)
    } catch (e: Exception) {
      Log.w(TAG, "pose frame skipped", e)
      busy.set(false)
      return
    }
    val ts = maxOf(now, lastTimestamp + 1)
    lastTimestamp = ts
    pending[ts] = Pending(mirrored, frame)
    try {
      lm.detectAsync(BitmapImageBuilder(frame).build(), ts)
    } catch (e: Exception) {
      pending.remove(ts)?.bitmap?.recycle()
      busy.set(false)
      Log.w(TAG, "pose frame not taken", e)
    }
  }

  /** The analysis frame as the preview shows it: viewport crop, upright. */
  private fun upright(image: ImageProxy): Bitmap {
    val full = image.toBitmap()
    val crop = image.cropRect
    val rotation = image.imageInfo.rotationDegrees
    val m = Matrix().apply { postRotate(rotation.toFloat()) }
    val out = Bitmap.createBitmap(full, crop.left, crop.top, crop.width(), crop.height(), m, true)
    if (out !== full) full.recycle()
    return out
  }

  private fun onResult(result: PoseLandmarkerResult, input: MPImage) {
    val meta = pending.remove(result.timestampMs())
    busy.set(false)
    try {
      val mirror = meta?.mirrored ?: mirrored
      val aspect = input.width.toFloat() / input.height.coerceAtLeast(1)
      val live = result.landmarks().firstOrNull()?.let(PoseEstimator::flatten)
      val g = guide
      val out = FloatArray(PoseStep.OUT)
      synchronized(lock) {
        if (tracker == 0L) return
        NativeEngine.nativePoseStep(
          tracker, live, aspect, result.timestampMs(), mirror,
          g?.reference, g?.referenceAspect ?: 1f, g?.mirrorReference ?: false, g?.framing ?: true, g?.strictness ?: 0.5f,
          out,
        )
      }
      sink?.onPose(PoseStep.toMap(out, mirror))
    } catch (e: Exception) {
      Log.w(TAG, "pose result dropped", e)
    } finally {
      meta?.bitmap?.recycle()
    }
  }

  /** A GPU that fails at run time: carry on on the CPU. */
  private fun onError(e: RuntimeException) {
    Log.w(TAG, "pose landmarker error", e)
    busy.set(false)
    val context = appContext ?: return
    if (onGpu) {
      onGpu = false
      worker.execute {
        stop()
        start(context)
      }
    }
  }
}
