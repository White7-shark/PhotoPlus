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
 * strength) and the seed.
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
    const val PARAMS = 10
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
 * A learned image-to-image cartoon model (an AnimeGANv2- or White-box
 * Cartoonization-like generator) run with LiteRT on the GPU, else the CPU —
 * behind the [BuildConfig.TOON_MODEL_ENGINE] flag, off by default.
 *
 * TODO(model): no weights are bundled. Before shipping one:
 *  - check its licence: the published AnimeGANv2 and White-box Cartoonization
 *    weights are, as far as we know, for non-commercial use only (verify with
 *    their authors); prefer a model trained on licensed data;
 *  - convert it to LiteRT (NHWC, 512 x 512, inputs and outputs in [-1, 1]),
 *    add it to the asset fetch with its checksum, and measure [SPEC]'s memory;
 *  - check faces of every skin tone against the classic engine's acceptance tests.
 * Until then [isAvailable] is false and the classic engine draws every picture.
 */
class ModelToonEngine(private val assets: AssetManager?) : ToonEngine {
  override val id = "model"

  override fun isAvailable(): Boolean = assets?.list("")?.contains(SPEC.asset) == true

  override fun render(input: ToonInput, token: Long) {
    val size = SPEC.input.width
    val out = ModelRuntime.use(SPEC) { session -> session.run(input.bitmap) }
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

  companion object {
    /** The model's place in the runtime (not in ModelRegistry.ALL: nothing is shipped yet). */
    val SPEC = ModelSpec(
      id = "toon",
      asset = "toon_anime.tflite",
      purpose = "A learned anime look for Toon Studio (behind a flag; not shipped)",
      license = "TODO: verify before shipping (see ModelToonEngine)",
      source = "TODO",
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
