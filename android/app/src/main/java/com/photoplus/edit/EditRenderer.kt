package com.photoplus.edit

import android.content.res.AssetManager
import android.graphics.Bitmap
import androidx.core.graphics.createBitmap
import java.io.File
import com.photoplus.engine.EngineException
import com.photoplus.engine.EngineOp
import com.photoplus.engine.NativeEngine
import com.photoplus.studio.PortraitParams
import com.photoplus.studio.PortraitProcessor
import com.photoplus.studio.PortraitReport

/**
 * Replays an [EditDocument] on the untouched photo, always in the same order:
 * Transform → turn/mirror/straighten/crop → Spotlight and Glow → light,
 * colour, look, focus, sharpening → props → frame → watermark. The photo
 * passed in is never modified.
 *
 * The slow early stages (Transform, Spotlight) are cached for the last
 * document, so moving a slider in the preview only redoes the fast steps.
 */
class EditRenderer(
  private val assets: AssetManager,
  /** Where custom props live ([EditDocument.CUSTOM_PROP] ids → <id>.png). */
  private val customProps: File?,
  private val portrait: () -> PortraitProcessor,
) {
  data class Options(
    val skipProps: Set<Int> = emptySet(),
    val skipFrame: Boolean = false,
    val watermark: ResolvedWatermark? = null,
    val maxEdge: Int = 4096,
    /** Identifies the source photo for the stage cache (null: no caching). */
    val cacheKey: String? = null,
    /**
     * An on-screen preview: per-pixel steps may run on the GPU once it has
     * passed its self-test. Saves and exports always render on the CPU, so the
     * files are identical on every device.
     */
    val preview: Boolean = false,
  )

  data class Result(
    val bitmap: Bitmap,
    /** Where the photo sits in the result (x, y, w, h fractions): all of it unless framed. */
    val photoRect: FloatArray,
    val watermarkVisible: Long,
    val portrait: PortraitReport?,
  )

  private var stageKey: String? = null
  private var stage: Bitmap? = null
  private val propArt = PropArt(assets, customProps)

  @Synchronized
  fun render(source: Bitmap, doc: EditDocument, opts: Options, token: Long): Result {
    // 1-3: the slow stages, cached per document.
    val key = opts.cacheKey?.let { "$it|${source.width}x${source.height}|${heavyKey(doc)}" }
    var portraitReport: PortraitReport? = null
    var cur: Bitmap
    val cached = stage
    if (key != null && key == stageKey && cached != null && !cached.isRecycled) {
      cur = cached.copy(Bitmap.Config.ARGB_8888, true).also { mark(it) }
    } else {
      cur = heavy(source, doc, opts.preview, token) { portraitReport = it }
      if (key != null) {
        stage?.recycle()
        stage = cur.copy(Bitmap.Config.ARGB_8888, false)
        stageKey = key
      }
    }
    // 4: light, colour, look, focus, sharpening.
    cur = runOps(cur, doc.toneOps(maxOf(cur.width, cur.height)), opts.preview, token, owned = true)
    // 5: props.
    doc.props.forEachIndexed { i, p ->
      if (i in opts.skipProps) return@forEachIndexed
      val art = prop(p.id)
      val params = floatArrayOf(
        p.cx * cur.width, p.cy * cur.height, p.width * cur.width, p.rotation, if (p.flip) 1f else 0f, 1f, 1f, 1f,
      )
      NativeEngine.nativePlaceProp(cur, art, params, token)
    }
    // 6: frame.
    var rect = floatArrayOf(0f, 0f, 1f, 1f)
    val frame = doc.frame
    if (frame != null && !opts.skipFrame) {
      val style = FrameStyles.byId(frame.id) ?: throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown frame")
      val (cw, ch) = style.canvasSize(cur.width, cur.height, opts.maxEdge)
      val canvas = createBitmap(cw, ch, Bitmap.Config.ARGB_8888).also { mark(it) }
      NativeEngine.nativeFrame(cur, canvas, style.floats(frame.zoom, frame.x, frame.y), style.ints(), token)
      cur.recycle()
      cur = canvas
      rect = style.window.copyOf()
    }
    // 7: watermark, drawn into the pixels.
    val visible = opts.watermark?.let { WatermarkRenderer.apply(cur, it) } ?: 0L
    return Result(cur, rect, visible, portraitReport)
  }

  /** Transform, geometry and Spotlight/Glow: everything before the fast steps. */
  private fun heavy(source: Bitmap, doc: EditDocument, preview: Boolean, token: Long, report: (PortraitReport) -> Unit): Bitmap {
    var cur: Bitmap = source
    var owned = false
    if (doc.transform) {
      val out = createBitmap(source.width, source.height, Bitmap.Config.ARGB_8888).also { mark(it) }
      NativeEngine.nativeTransform(source, out, floatArrayOf(1f, 1f, 1f, 0.4f), token, FloatArray(NativeEngine.TRANSFORM_REPORT_SIZE))
      cur = out
      owned = true
    }
    cur = runOps(cur, doc.geometryOps(), preview, token, owned).also { if (it !== cur) owned = true }
    doc.cropOp(cur.width, cur.height)?.let { crop ->
      cur = runOps(cur, listOf(crop), preview, token, owned)
      owned = true
    }
    if (!owned) {
      cur = cur.copy(Bitmap.Config.ARGB_8888, true).also { mark(it) }
    }
    if (doc.hasPortraitWork) {
      val s = doc.spotlight
      val params = PortraitParams(
        blur = s?.blur ?: 0f,
        retouch = doc.retouch,
        brighten = if (doc.retouch > 0f) 0.35f else 0.2f,
        pop = if (s != null) 0.5f else 0f,
      )
      report(portrait().apply(cur, 0, s?.points ?: emptyList(), params, token))
    }
    return cur
  }

  /** Runs engine operations, returning a new bitmap (the input is recycled if [owned]). */
  private fun runOps(input: Bitmap, ops: List<EngineOp>, preview: Boolean, token: Long, owned: Boolean): Bitmap {
    if (ops.isEmpty()) return input
    val types = ops.map { it.typeId }.toIntArray()
    val params = FloatArray(ops.size * EngineOp.PARAMS).also { out ->
      ops.forEachIndexed { i, op -> op.params().copyInto(out, i * EngineOp.PARAMS) }
    }
    // A preview's input is already preview-sized: its own long edge keeps the
    // pixel settings as they are while allowing the verified GPU.
    val handle = if (preview) {
      NativeEngine.nativeProcessEx(input, types, params, maxOf(input.width, input.height), NativeEngine.BACKEND_AUTO, token)
    } else {
      NativeEngine.nativeProcessEx(input, types, params, 0, NativeEngine.BACKEND_CPU, token)
    }
    try {
      val out = createBitmap(NativeEngine.nativeResultWidth(handle), NativeEngine.nativeResultHeight(handle), Bitmap.Config.ARGB_8888)
      mark(out)
      NativeEngine.nativeCopyResultToBitmap(handle, out)
      if (owned) input.recycle()
      return out
    } finally {
      NativeEngine.nativeReleaseResult(handle)
    }
  }

  private fun prop(id: String): Bitmap = propArt.load(id)

  /** Engine-written photos: straight alpha, opaque. */
  private fun mark(b: Bitmap) {
    b.isPremultiplied = false
    b.setHasAlpha(false)
  }

  @Synchronized
  fun clear() {
    stage?.recycle()
    stage = null
    stageKey = null
    propArt.clear()
  }

  companion object {
    fun heavyKey(d: EditDocument) =
      listOf(d.transform, d.quarterTurns, d.flipH, d.perspectiveV, d.perspectiveH, d.straighten, d.crop, d.retouch, d.spotlight)
        .joinToString("|")
  }
}
