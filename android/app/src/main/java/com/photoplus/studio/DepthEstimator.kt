package com.photoplus.studio

import android.content.Context
import android.content.res.AssetManager
import android.graphics.Bitmap
import android.graphics.Matrix
import android.os.SystemClock
import android.util.Log
import androidx.core.graphics.scale
import com.google.ai.edge.litert.Accelerator
import com.google.ai.edge.litert.CompiledModel
import com.google.ai.edge.litert.TensorBuffer
import com.photoplus.device.MemoryPressureMonitor
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

/**
 * Spotlight's sense of depth: how far every part of the photo is from the
 * camera, estimated on-device from a single photo (no depth sensor needed).
 * With it the background blurs progressively with distance like a real lens,
 * while the ground the subject stands on stays sharp.
 *
 * The model loads on first use and is released under memory pressure. If it
 * is missing or fails, [estimate] returns null and the engine falls back to
 * its own ground-plane estimate, so Spotlight always works.
 */
object DepthEstimator {
  private const val TAG = "PhotoPlusDepth"
  const val ASSET = "spotlight_depth.tflite"

  @Volatile private var assets: AssetManager? = null
  private val lock = ReentrantLock()
  @Volatile private var releaseRequested = false
  private var model: CompiledModel? = null
  private var inputs: List<TensorBuffer> = emptyList()
  private var outputs: List<TensorBuffer> = emptyList()
  private var unavailable = false
  private var input: FloatArray? = null

  /** Called once from the Application. */
  fun install(context: Context) {
    if (assets != null) return
    assets = context.applicationContext.assets
    MemoryPressureMonitor.addListener { level ->
      if (level != MemoryPressureMonitor.Level.NORMAL) release()
    }
  }

  /** A depth map, 0 far … 255 near, row-major [width] x [height]. */
  class DepthMap(val bytes: ByteArray, val width: Int, val height: Int)

  /** Depth of [upright] (the photo the right way up), in the same orientation, or null. */
  fun estimate(upright: Bitmap): DepthMap? = lock.withLock {
    try {
      estimateLocked(upright)
    } finally {
      if (releaseRequested) releaseLocked()
    }
  }

  private fun estimateLocked(upright: Bitmap): DepthMap? {
    val m = load() ?: return null
    val started = SystemClock.elapsedRealtime()
    return runCatching {
      // The model expects landscape: turn portrait photos so nothing is squeezed.
      val portrait = upright.height > upright.width
      val turned = if (portrait) {
        Bitmap.createBitmap(upright, 0, 0, upright.width, upright.height, Matrix().apply { postRotate(-90f) }, true)
      } else {
        upright
      }
      val sized = turned.scale(DepthMath.IN_W, DepthMath.IN_H)
      if (turned !== upright && turned !== sized) turned.recycle()
      val argb = IntArray(DepthMath.IN_W * DepthMath.IN_H)
      sized.getPixels(argb, 0, DepthMath.IN_W, 0, 0, DepthMath.IN_W, DepthMath.IN_H)
      if (sized !== upright) sized.recycle()
      val data = DepthMath.toInput(argb, input ?: FloatArray(3 * argb.size).also { input = it })
      inputs[0].writeFloat(data)
      m.run(inputs, outputs)
      val raw = outputs[0].readFloat()
      if (raw.size < DepthMath.IN_W * DepthMath.IN_H) error("unexpected depth output size ${raw.size}")
      val bytes = DepthMath.toBytes(raw, DepthMath.IN_W * DepthMath.IN_H) ?: return@runCatching null
      // Back to upright (undo the landscape turn). The engine samples masks
      // at any size, so the map stays at the model's resolution.
      val (b, w, h) = if (portrait) {
        MaskGeometry.rotateMask(bytes, DepthMath.IN_W, DepthMath.IN_H, 90)
      } else {
        Triple(bytes, DepthMath.IN_W, DepthMath.IN_H)
      }
      Log.i(TAG, "depth in ${SystemClock.elapsedRealtime() - started} ms")
      DepthMap(b, w, h)
    }.onFailure { Log.w(TAG, "depth estimate failed; using the built-in estimate", it) }.getOrNull()
  }

  /**
   * Frees the model. Never blocks (memory callbacks arrive on the main
   * thread): while an estimate is running, it is freed right after it.
   */
  fun release() {
    releaseRequested = true
    if (lock.tryLock()) {
      try {
        releaseLocked()
      } finally {
        lock.unlock()
      }
    }
  }

  private fun releaseLocked() {
    releaseRequested = false
    inputs.forEach { it.close() }
    outputs.forEach { it.close() }
    inputs = emptyList()
    outputs = emptyList()
    model?.close()
    model = null
    input = null
  }

  private fun load(): CompiledModel? {
    model?.let { return it }
    if (unavailable) return null
    val am = assets ?: return null
    return runCatching {
      if (am.list("")?.contains(ASSET) != true) {
        unavailable = true
        Log.i(TAG, "depth model not bundled; using the built-in estimate")
        return null
      }
      val threads = Runtime.getRuntime().availableProcessors().coerceIn(1, 4)
      val options = CompiledModel.Options(Accelerator.CPU).apply { cpuOptions = CompiledModel.CpuOptions(numThreads = threads) }
      val created = CompiledModel.create(am, ASSET, options)
      inputs = created.createInputBuffers()
      outputs = created.createOutputBuffers()
      model = created
      created
    }.onFailure {
      unavailable = true
      releaseLocked()
      Log.w(TAG, "depth model failed to load; using the built-in estimate", it)
    }.getOrNull()
  }
}
