package com.photoplus.ml

/** Running statistics for one model (thread-safe). Pure Kotlin, unit-tested. */
class InferenceStats {
  private var runs = 0L
  private var totalMs = 0.0
  private var worstMs = 0.0
  private var failures = 0L
  var accelerator: Accel? = null
    @Synchronized get
    @Synchronized set
  var loadMs = 0.0
    @Synchronized get
    @Synchronized set

  @Synchronized
  fun record(ms: Double) {
    runs++
    totalMs += ms
    if (ms > worstMs) worstMs = ms
  }

  @Synchronized
  fun fail() {
    failures++
  }

  @Synchronized
  fun snapshot(): Map<String, Any?> = mapOf(
    "accelerator" to accelerator?.name?.lowercase(),
    "runs" to runs.toDouble(),
    "averageMs" to if (runs == 0L) 0.0 else totalMs / runs,
    "worstMs" to worstMs,
    "failures" to failures.toDouble(),
    "loadMs" to loadMs,
  )
}

/**
 * Which accelerators to try for a model, in order. GPU is skipped when the
 * device's GPU failed before (remembered for the session) or when the model
 * asks for CPU only. Pure, unit-tested.
 */
object AcceleratorPlan {
  fun order(spec: ModelSpec, gpuBlocked: Boolean): List<Accel> =
    spec.accelerators.filter { it != Accel.GPU || !gpuBlocked }.ifEmpty { listOf(Accel.CPU) }

  /**
   * A warm-up output is usable when every value is finite and not all are
   * zero (broken GPU paths typically give NaN or zeros).
   */
  fun plausible(output: FloatArray): Boolean {
    if (output.isEmpty()) return false
    var nonZero = false
    for (v in output) {
      if (!v.isFinite()) return false
      if (v != 0f) nonZero = true
    }
    return nonZero
  }

  /** A structured warm-up input (a diagonal ramp, in normalised units). */
  fun warmupInput(input: TensorInput): FloatArray {
    val out = FloatArray(input.floats)
    val n = input.width * input.height
    for (y in 0 until input.height) {
      for (x in 0 until input.width) {
        val v = 2f * ((x + y).toFloat() / (input.width + input.height) - 0.5f)
        val i = y * input.width + x
        for (c in 0 until 3) {
          if (input.layout == TensorInput.Layout.NCHW) out[c * n + i] = v else out[i * 3 + c] = v
        }
      }
    }
    return out
  }
}
