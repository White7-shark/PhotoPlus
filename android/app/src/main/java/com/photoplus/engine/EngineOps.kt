package com.photoplus.engine

/**
 * Typed operation list parsed from the JavaScript request. Kept free of
 * Android/React types so it can be unit tested on the JVM.
 *
 * Type ids mirror pp_op_type in native/engine/include/photoplus/pp_engine.h.
 */
sealed class EngineOp(val typeId: Int) {
  abstract fun params(): FloatArray

  data class Tone(val exposure: Float, val brightness: Float, val contrast: Float) : EngineOp(1) {
    override fun params() = floatArrayOf(exposure, brightness, contrast, 0f)
  }
  data object Grayscale : EngineOp(2) {
    override fun params() = floatArrayOf(0f, 0f, 0f, 0f)
  }
  data class Rotate(val quarterTurns: Int) : EngineOp(3) {
    override fun params() = floatArrayOf(quarterTurns.toFloat(), 0f, 0f, 0f)
  }
  data class Flip(val horizontal: Boolean, val vertical: Boolean) : EngineOp(4) {
    override fun params() =
      floatArrayOf(if (horizontal) 1f else 0f, if (vertical) 1f else 0f, 0f, 0f)
  }
  data class Crop(val x: Int, val y: Int, val width: Int, val height: Int) : EngineOp(5) {
    override fun params() = floatArrayOf(x.toFloat(), y.toFloat(), width.toFloat(), height.toFloat())
    fun scaled(factor: Double): Crop =
      if (factor == 1.0) this
      else Crop(
        (x * factor).toInt(),
        (y * factor).toInt(),
        maxOf(1, (width * factor).toInt()),
        maxOf(1, (height * factor).toInt()),
      )
  }
  data class Resize(val width: Int, val height: Int) : EngineOp(6) {
    override fun params() = floatArrayOf(width.toFloat(), height.toFloat(), 0f, 0f)
  }
  data class Sharpen(val amount: Float) : EngineOp(7) {
    override fun params() = floatArrayOf(amount, 0f, 0f, 0f)
  }
  data class Saturation(val amount: Float) : EngineOp(8) {
    override fun params() = floatArrayOf(amount, 0f, 0f, 0f)
  }
}

enum class OutputFormat(val extension: String, val mime: String) {
  JPEG("jpg", "image/jpeg"),
  PNG("png", "image/png"),
  WEBP("webp", "image/webp");

  companion object {
    fun parse(value: String?): OutputFormat =
      when (value?.lowercase()) {
        null, "jpeg", "jpg" -> JPEG
        "png" -> PNG
        "webp" -> WEBP
        else -> throw EngineException(EngineException.UNSUPPORTED_FORMAT, "Unsupported output format: $value")
      }
  }
}

data class ProcessRequest(
  val inputPath: String,
  val outputPath: String,
  val format: OutputFormat,
  val quality: Int,
  val maxDimension: Int,
  val operations: List<EngineOp>,
) {
  fun typeIds(ops: List<EngineOp> = operations): IntArray = ops.map { it.typeId }.toIntArray()

  fun packedParams(ops: List<EngineOp> = operations): FloatArray {
    val out = FloatArray(ops.size * 4)
    ops.forEachIndexed { i, op -> op.params().copyInto(out, i * 4) }
    return out
  }

  /** Re-expresses source-relative coordinates after decode-time downsampling. */
  fun operationsForDecodeScale(scale: Double): List<EngineOp> =
    operations.map { if (it is EngineOp.Crop) it.scaled(scale) else it }

  companion object {
    const val DEFAULT_MAX_DIMENSION = 4096
    const val MAX_OPERATIONS = 64

    /** Parses the bridge payload (ReadableMap.toHashMap()). */
    fun fromMap(map: Map<String, Any?>): ProcessRequest {
      val input = map.requireString("inputPath")
      val output = map.requireString("outputPath")
      val rawOps = map["operations"] as? List<*> ?: emptyList<Any?>()
      if (rawOps.size > MAX_OPERATIONS) invalid("Too many operations (max $MAX_OPERATIONS)")
      val ops = rawOps.mapIndexed { index, raw ->
        val op = raw as? Map<*, *> ?: invalid("Operation #$index must be an object")
        parseOp(index, op)
      }
      val quality = (map["quality"] as? Number)?.toInt() ?: 92
      if (quality !in 1..100) invalid("quality must be between 1 and 100")
      val maxDim = (map["maxDimension"] as? Number)?.toInt() ?: DEFAULT_MAX_DIMENSION
      if (maxDim !in 64..16384) invalid("maxDimension must be between 64 and 16384")
      return ProcessRequest(input, output, OutputFormat.parse(map["format"] as? String), quality, maxDim, ops)
    }

    private fun parseOp(index: Int, op: Map<*, *>): EngineOp {
      fun num(key: String, default: Double? = null): Double {
        val v = (op[key] as? Number)?.toDouble() ?: default ?: invalid("Operation #$index: '$key' is required")
        if (v.isNaN() || v.isInfinite()) invalid("Operation #$index: '$key' must be finite")
        return v
      }
      fun bool(key: String) = op[key] as? Boolean ?: false
      return when (val type = op["type"] as? String) {
        "tone" -> EngineOp.Tone(
          num("exposure", 0.0).toFloat(),
          num("brightness", 0.0).toFloat(),
          num("contrast", 0.0).toFloat(),
        )
        "grayscale" -> EngineOp.Grayscale
        "rotate" -> EngineOp.Rotate(num("quarterTurns").toInt())
        "flip" -> EngineOp.Flip(bool("horizontal"), bool("vertical"))
        "crop" -> EngineOp.Crop(num("x").toInt(), num("y").toInt(), num("width").toInt(), num("height").toInt())
        "resize" -> EngineOp.Resize(num("width").toInt(), num("height").toInt())
        "sharpen" -> EngineOp.Sharpen(num("amount").toFloat())
        "saturation" -> EngineOp.Saturation(num("amount").toFloat())
        else -> invalid("Operation #$index: unknown type '$type'")
      }
    }

    private fun Map<String, Any?>.requireString(key: String): String =
      (this[key] as? String)?.takeIf { it.isNotBlank() } ?: invalid("'$key' is required")

    private fun invalid(message: String): Nothing =
      throw EngineException(EngineException.INVALID_ARGUMENT, message)
  }
}
