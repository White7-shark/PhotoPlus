package com.photoplus.studio.toon

import android.content.res.AssetManager
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import com.photoplus.BuildConfig
import com.photoplus.engine.EngineException
import com.photoplus.engine.NativeEngine
import com.photoplus.ml.Accel
import com.photoplus.ml.ModelRuntime
import com.photoplus.ml.ModelSpec
import com.photoplus.ml.TensorInput

/**
 * What a Toon picture is made from: the photo ([bitmap], drawn in place), the
 * person's mask, the faces (12 floats each: box, mouth, eyes; normalised),
 * their contour points ([ToonContours.FLOATS] each, ML Kit's order), the
 * settings (style, background, lines, shades, vivid, detail, eyes, skin, glow,
 * strength, outline, backdrop, brightness, warmth, texture, face detail, hair
 * detail, ink) and the seed.
 */
class ToonInput(
  val bitmap: Bitmap,
  val params: FloatArray,
  val mask: Bitmap?,
  val faces: FloatArray?,
  val contours: FloatArray?,
  val seed: Int,
) {
  init {
    require(params.size == PARAMS) { "toon settings: $PARAMS values" }
  }

  val strength: Float get() = params[9]

  companion object {
    const val PARAMS = 18
  }
}

/** One way of drawing a photo as a toon. */
interface ToonEngine {
  /** A short id, for logs and stats. */
  val id: String

  /** Whether it can run here and now (its library, model or connection is there). */
  fun isAvailable(): Boolean

  /** Draws [input]'s bitmap in place; throws [EngineException] (e.g. CANCELLED) on failure. */
  fun render(input: ToonInput, token: Long)
}

/**
 * The classic engine (native/engine/src/toon): clean-up, segmentation-aware
 * smoothing, Lab palettes, skin tones, the face drawn from its contours,
 * line art and each style's finish. Every style, every control; on every device.
 */
object ClassicToonEngine : ToonEngine {
  override val id = "classic"

  override fun isAvailable() = NativeEngine.ensureLoaded()

  override fun render(input: ToonInput, token: Long) =
    NativeEngine.nativeToonRenderEx(input.bitmap, input.params, input.mask, input.faces, input.contours, input.seed, token)
}

/**
 * The settings as ToonNet reads them (tools/toon_model/cond.py, which must
 * agree; both sides test the same example): style, background and ink
 * one-hot, the 14 sliders scaled to 0..1, then whether there is a person mask.
 * Strength is not in it: the picture is blended over the photo afterwards.
 */
object ToonCond {
  const val STYLES = 6
  const val BACKGROUNDS = 6
  const val INKS = 4

  /** (index in [ToonInput.params], low, high) of each slider, in the model's order. */
  private val SLIDERS = arrayOf(
    floatArrayOf(2f, 0f, 1f), floatArrayOf(3f, 2f, 6f), floatArrayOf(4f, -1f, 1f), floatArrayOf(5f, 0f, 1f),
    floatArrayOf(6f, 0f, 1f), floatArrayOf(7f, 0f, 1f), floatArrayOf(8f, 0f, 1f), floatArrayOf(10f, 0f, 1f),
    floatArrayOf(11f, 0f, 1f), floatArrayOf(12f, -1f, 1f), floatArrayOf(13f, -1f, 1f), floatArrayOf(14f, 0f, 1f),
    floatArrayOf(15f, 0f, 1f), floatArrayOf(16f, 0f, 1f),
  )
  val SIZE = STYLES + BACKGROUNDS + INKS + SLIDERS.size + 1

  fun encode(params: FloatArray, hasMask: Boolean): FloatArray {
    require(params.size == ToonInput.PARAMS) { "toon settings: ${ToonInput.PARAMS} values" }
    val out = FloatArray(SIZE)
    fun hot(at: Int, v: Float, n: Int) {
      out[at + Math.round(v).coerceIn(0, n - 1)] = 1f
    }
    hot(0, params[0], STYLES)
    hot(STYLES, params[1], BACKGROUNDS)
    hot(STYLES + BACKGROUNDS, params[17], INKS)
    var k = STYLES + BACKGROUNDS + INKS
    for ((i, lo, hi) in SLIDERS) out[k++] = (params[i.toInt()].coerceIn(lo, hi) - lo) / (hi - lo)
    out[k] = if (hasMask) 1f else 0f
    return out
  }
}

/**
 * Photo Plus's own learned cartoon model, ToonNet (tools/toon_model): one
 * network for every style, background, ink and slider, steered by [ToonCond],
 * that also reads the person's mask. Trained first on the classic engine's own
 * pictures (the app's look, no third-party rights), then optionally sharpened
 * on art the app has licensed. Run with LiteRT on the GPU, else the CPU.
 *
 * Behind the [BuildConfig.TOON_MODEL_ENGINE] flag, off by default. Until a
 * trained toon_photoplus.tflite is bundled (with its model card), [isAvailable]
 * is false and the classic engine draws every picture. Before turning it on:
 *  - train it on photos with model releases and art with written rights (see
 *    tools/toon_model/README.md), and keep the model card with the file;
 *  - check faces of every skin tone against the classic engine's acceptance tests;
 *  - measure [SPEC]'s memory and time on low-end phones.
 */
class ModelToonEngine(private val assets: AssetManager?) : ToonEngine {
  override val id = "model"

  override fun isAvailable(): Boolean = assets?.list("")?.contains(SPEC.asset) == true

  override fun render(input: ToonInput, token: Long) {
    val size = SPEC.input.width
    val mask = maskTensor(input.mask, size)
    val cond = ToonCond.encode(input.params, input.mask != null)
    val out = ModelRuntime.use(SPEC) { session -> session.run(input.bitmap, listOf(mask, cond)) }
      ?: throw EngineException(EngineException.INTERNAL, "The cartoon model isn't available")
    if (out.size < size * size * 3) throw EngineException(EngineException.INTERNAL, "The cartoon model gave an unexpected picture")
    // [-1, 1] NHWC to a bitmap, stretched back over the photo, blended by Strength.
    val pixels = IntArray(size * size) { i ->
      fun c(k: Int) = ((out[i * 3 + k] + 1f) * 127.5f).toInt().coerceIn(0, 255)
      (0xFF shl 24) or (c(0) shl 16) or (c(1) shl 8) or c(2)
    }
    val styled = Bitmap.createBitmap(pixels, size, size, Bitmap.Config.ARGB_8888)
    try {
      val paint = Paint(Paint.FILTER_BITMAP_FLAG).apply { alpha = (input.strength * 255).toInt().coerceIn(0, 255) }
      Canvas(input.bitmap).drawBitmap(styled, null, Rect(0, 0, input.bitmap.width, input.bitmap.height), paint)
    } finally {
      styled.recycle()
    }
  }

  /** The person's mask (ALPHA_8) stretched to the model's size, 0..1; zeros without one. */
  private fun maskTensor(mask: Bitmap?, size: Int): FloatArray {
    val out = FloatArray(size * size)
    if (mask == null) return out
    val scaled = Bitmap.createScaledBitmap(mask, size, size, true)
    try {
      val alpha = scaled.extractAlpha()
      try {
        val row = alpha.rowBytes
        val bytes = java.nio.ByteBuffer.allocate(row * size)
        alpha.copyPixelsToBuffer(bytes)
        val a = bytes.array()
        for (y in 0 until size) for (x in 0 until size) out[y * size + x] = (a[y * row + x].toInt() and 0xFF) / 255f
      } finally {
        alpha.recycle()
      }
    } finally {
      if (scaled !== mask) scaled.recycle()
    }
    return out
  }

  companion object {
    /** The model's place in the runtime (not in ModelRegistry.ALL: nothing is shipped yet). */
    val SPEC = ModelSpec(
      id = "toon",
      asset = "toon_photoplus.tflite",
      purpose = "ToonNet: Photo Plus's own learned cartoon look for Toon Studio (behind a flag; not shipped)",
      license = "Photo Plus's own model; its training data's licences are in toon_photoplus.json",
      source = "tools/toon_model (trained by Photo Plus)",
      input = TensorInput(512, 512, TensorInput.Layout.NHWC, TensorInput.Fit.STRETCH, floatArrayOf(0.5f, 0.5f, 0.5f), floatArrayOf(0.5f, 0.5f, 0.5f)),
      accelerators = listOf(Accel.GPU, Accel.CPU),
      memoryBytes = 160L * 1024 * 1024,
    )
  }
}

/**
 * A future server-side engine: img2img with a line-art ControlNet and identity
 * preservation, for looks a phone can't compute. Uploading a person's photo
 * needs their consent, a privacy notice and a reviewed backend first.
 */
interface CloudToonEngine : ToonEngine

/** No cloud engine yet. */
object NoCloudToonEngine : CloudToonEngine {
  override val id = "cloud"

  override fun isAvailable() = false

  override fun render(input: ToonInput, token: Long) =
    throw EngineException(EngineException.INTERNAL, "No cloud cartoon engine is configured")
}

/** Which engine draws a picture: the model when its flag is on and it is there, else the classic engine. */
object ToonEngines {
  fun pick(modelEnabled: Boolean, model: ToonEngine, classic: ToonEngine = ClassicToonEngine): ToonEngine =
    if (modelEnabled && model.isAvailable()) model else classic

  /** Renders with [engine], falling back to [classic] if it fails for any reason but cancellation. */
  fun render(engine: ToonEngine, input: ToonInput, token: Long, classic: ToonEngine = ClassicToonEngine) =
    withFallback(engine, classic) { it.render(input, token) }

  /** Runs [draw] with [engine]; on a failure other than cancellation, once more with [classic]. Returns the engine that drew. */
  fun withFallback(engine: ToonEngine, classic: ToonEngine, draw: (ToonEngine) -> Unit): ToonEngine {
    if (engine === classic) return classic.also(draw)
    return try {
      engine.also(draw)
    } catch (e: EngineException) {
      if (e.code == EngineException.CANCELLED) throw e
      classic.also(draw)
    }
  }

  val modelFlag: Boolean get() = BuildConfig.TOON_MODEL_ENGINE
}

/**
 * Faces' contour points for the engine, in ML Kit's order (pp_engine.h
 * PP_TOON_CONTOUR_POINTS): face oval 36, left brow top 5 and bottom 5, right
 * brow top 5 and bottom 5, left eye 16, right eye 16, upper lip top 11 and
 * bottom 9, lower lip top 9 and bottom 9, nose bridge 2, nose bottom 3.
 */
object ToonContours {
  /** Point counts in that order. */
  val COUNTS = intArrayOf(36, 5, 5, 5, 5, 16, 16, 11, 9, 9, 9, 2, 3)
  const val POINTS = 131
  const val FLOATS = POINTS * 2

  /**
   * One face's points ([parts] in the order above, each a list of x, y in
   * pixels of a [w] x [h] image), normalised; a part missing or of the wrong
   * size leaves the whole face unknown (-1), and the engine uses its box.
   */
  fun encode(parts: List<List<Pair<Float, Float>>?>, w: Float, h: Float): FloatArray {
    val out = FloatArray(FLOATS) { -1f }
    if (parts.size != COUNTS.size || w <= 0f || h <= 0f) return out
    if (parts.indices.any { parts[it]?.size != COUNTS[it] }) return out
    var k = 0
    for (part in parts) for ((x, y) in part!!) {
      out[k++] = x / w
      out[k++] = y / h
    }
    return out
  }
}
