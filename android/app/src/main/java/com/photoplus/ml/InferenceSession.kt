package com.photoplus.ml

import android.content.res.AssetManager
import android.graphics.Bitmap
import android.os.SystemClock
import android.util.Log
import com.google.ai.edge.litert.Accelerator
import com.google.ai.edge.litert.CompiledModel
import com.google.ai.edge.litert.TensorBuffer
import com.photoplus.engine.NativeEngine

/**
 * One loaded model (LiteRT CompiledModel) with its input and output buffers.
 *
 * [open] tries the spec's accelerators in order; each candidate must load and
 * produce a plausible warm-up output (finite, not constant), otherwise the
 * next is tried, so a GPU driver problem degrades to the CPU instead of
 * failing. Not thread-safe: [ModelRuntime] serialises use.
 */
class InferenceSession private constructor(
  val spec: ModelSpec,
  val accelerator: Accel,
  private val model: CompiledModel,
  private val inputs: List<TensorBuffer>,
  private val outputs: List<TensorBuffer>,
) : AutoCloseable {
  private val input = FloatArray(spec.input.floats)
  private val placement = FloatArray(4)

  /** Where the photo landed in the last input (scaleX, scaleY, offsetX, offsetY). */
  val lastPlacement: FloatArray get() = placement.copyOf()

  /** Prepares [bitmap] natively (resize, normalise, layout) and runs the model; returns output 0. */
  fun run(bitmap: Bitmap): FloatArray {
    val t = spec.input
    NativeEngine.nativeImageToTensor(bitmap, t.width, t.height, t.layout.id, t.fit.id, t.mean, t.std, input, placement)
    return runPrepared(input)
  }

  fun runPrepared(data: FloatArray): FloatArray {
    inputs[0].writeFloat(data)
    model.run(inputs, outputs)
    return outputs[0].readFloat()
  }

  override fun close() {
    inputs.forEach { it.close() }
    outputs.forEach { it.close() }
    model.close()
  }

  companion object {
    private const val TAG = "PhotoPlusML"

    fun open(assets: AssetManager, spec: ModelSpec, order: List<Accel>, onGpuFailure: () -> Unit): InferenceSession? {
      for (accel in order) {
        val started = SystemClock.elapsedRealtime()
        val session = runCatching { create(assets, spec, accel) }
          .onFailure { Log.i(TAG, "${spec.id}: ${accel.name} unavailable (${it.message})") }
          .getOrNull() ?: run {
          if (accel == Accel.GPU) onGpuFailure()
          null
        } ?: continue
        // Warm-up: a structured input must give a usable output on this accelerator.
        val ok = runCatching { AcceleratorPlan.plausible(session.runPrepared(AcceleratorPlan.warmupInput(spec.input))) }.getOrDefault(false)
        if (ok) {
          Log.i(TAG, "${spec.id}: running on ${accel.name} (loaded in ${SystemClock.elapsedRealtime() - started} ms)")
          return session
        }
        Log.w(TAG, "${spec.id}: ${accel.name} produced unusable output; trying the next accelerator")
        session.close()
        if (accel == Accel.GPU) onGpuFailure()
      }
      return null
    }

    private fun create(assets: AssetManager, spec: ModelSpec, accel: Accel): InferenceSession {
      val options = when (accel) {
        Accel.GPU -> CompiledModel.Options(Accelerator.GPU)
        Accel.CPU -> CompiledModel.Options(Accelerator.CPU).apply {
          cpuOptions = CompiledModel.CpuOptions(numThreads = Runtime.getRuntime().availableProcessors().coerceIn(1, 4))
        }
      }
      val model = CompiledModel.create(assets, spec.asset, options)
      return try {
        InferenceSession(spec, accel, model, model.createInputBuffers(), model.createOutputBuffers())
      } catch (e: Exception) {
        model.close()
        throw e
      }
    }
  }
}
