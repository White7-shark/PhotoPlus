package com.photoplus.ml

/**
 * Everything the app needs to know to run an on-device model, in one place:
 * where it comes from, its licence, how its input is prepared (done natively
 * by the engine), and which accelerators to try.
 */
data class ModelSpec(
  val id: String,
  /** Asset file name (fetched and checksum-verified at build time). */
  val asset: String,
  val purpose: String,
  val license: String,
  val source: String,
  val input: TensorInput,
  /** Tried in order; the first that loads and passes its warm-up check is used. */
  val accelerators: List<Accel> = listOf(Accel.GPU, Accel.CPU),
  /**
   * Working memory while loaded (weights as unpacked by the runtime +
   * activations), measured; a model is not loaded when the device cannot
   * spare it ([AcceleratorPlan.canLoad]).
   */
  val memoryBytes: Long,
)

enum class Accel { GPU, CPU }

data class TensorInput(
  val width: Int,
  val height: Int,
  val layout: Layout,
  val fit: Fit,
  val mean: FloatArray,
  val std: FloatArray,
) {
  enum class Layout(val id: Int) { NHWC(0), NCHW(1) }
  enum class Fit(val id: Int) { STRETCH(0), LETTERBOX(1), CROP_CENTER(2) }

  val floats: Int get() = width * height * 3

  init {
    require(width in 1..4096 && height in 1..4096) { "bad model input size" }
    require(mean.size == 3 && std.size == 3 && std.all { it != 0f && it.isFinite() } && mean.all { it.isFinite() }) {
      "bad normalisation"
    }
  }

  override fun equals(other: Any?) = other is TensorInput && width == other.width && height == other.height &&
    layout == other.layout && fit == other.fit && mean.contentEquals(other.mean) && std.contentEquals(other.std)

  override fun hashCode() = listOf(width, height, layout, fit, mean.contentHashCode(), std.contentHashCode()).hashCode()
}

/** The models the app ships. Candidates evaluated but not shipped are in docs/MODEL_EVALUATION.md. */
object ModelRegistry {
  private val IMAGENET_MEAN = floatArrayOf(0.485f, 0.456f, 0.406f)
  private val IMAGENET_STD = floatArrayOf(0.229f, 0.224f, 0.225f)

  /** Relative depth for Spotlight (Depth Anything V2 Small, Apache-2.0). */
  val DEPTH = ModelSpec(
    id = "depth",
    asset = "spotlight_depth.tflite",
    purpose = "Relative depth for Spotlight's distance-aware background blur",
    license = "Apache-2.0",
    source = "Depth Anything V2 Small (LiteRT 8-bit weights)",
    input = TensorInput(686, 518, TensorInput.Layout.NCHW, TensorInput.Fit.STRETCH, IMAGENET_MEAN, IMAGENET_STD),
    // Measured: +234 MB peak on the CPU path (int8 weights unpacked to float);
    // see docs/MODEL_EVALUATION.md.
    memoryBytes = 240L * 1024 * 1024,
  )

  val ALL: List<ModelSpec> = listOf(DEPTH)

  fun byId(id: String): ModelSpec? = ALL.firstOrNull { it.id == id }
}
