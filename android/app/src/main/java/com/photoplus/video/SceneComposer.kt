package com.photoplus.video

import android.graphics.Bitmap
import androidx.core.graphics.createBitmap
import androidx.core.graphics.scale
import com.photoplus.engine.NativeEngine
import com.photoplus.studio.CutoutSettings
import com.photoplus.studio.StudioStroke
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * One Glide frame in a new scene: the studio cut-out of the person (from the
 * frame and its steadied mask), the steadying warp applied to the cut-out
 * (so the person is steady, and the scene never moves), then the studio
 * compose at the clip's fixed box. The colour match is measured on the first
 * frame and kept, and the grain changes every frame as a camera's does.
 */
class SceneComposer(private val scene: GlideScene, private val width: Int, private val height: Int, background: Bitmap) {
  private val bgPixels = IntArray(width * height).also { background.getPixels(it, 0, width, 0, 0, width, height) }
  private val k = minOf(1f, CUT_EDGE.toFloat() / max(width, height))
  private val cw = max(16, (width * k).roundToInt())
  private val ch = max(16, (height * k).roundToInt())
  private val cut = createBitmap(cw, ch).apply { isPremultiplied = false }
  private val steadyCut = createBitmap(cw, ch).apply { isPremultiplied = false }
  private val strokes = StudioStroke.encode(emptyList())
  private val settings = CutoutSettings().toArray()
  private val cutReport = FloatArray(16)
  private val report = FloatArray(10)
  private var harmony: FloatArray? = null

  init {
    background.recycle()
  }

  /** [frame] (clip size, enhanced) and its person [mask] (any size, or null when nobody was found) → [out]. */
  fun compose(frame: Bitmap, mask: Bitmap?, warp: FloatArray, zoom: Float, out: Bitmap, token: Long, index: Int) {
    out.setPixels(bgPixels, 0, width, 0, 0, width, height)
    if (mask == null) return
    val small = if (cw == frame.width && ch == frame.height) frame else frame.scale(cw, ch)
    try {
      NativeEngine.nativeStudioCutout(small, mask, null, strokes, settings, cut, token, cutReport)
    } finally {
      if (small !== frame) small.recycle()
    }
    NativeEngine.nativeVideoWarp(cut, warpFor(warp, k), steadyCut, token)
    val look = scene.look.copy(seed = 11 + index, harmony = harmony)
    NativeEngine.nativeStudioCompose(out, steadyCut, scene.placement.toArray(boxIn(scene.box, zoom)), look.toArray(), token, report)
    if (harmony == null && scene.look.colorMatch > 0f) {
      // gain, contrast, balance r g b
      harmony = floatArrayOf(report[4], report[9], report[5], report[6], report[7])
    }
  }

  fun release() {
    cut.recycle()
    steadyCut.recycle()
  }

  companion object {
    /** The cut-out is made at this size (the person is resized into the scene anyway). */
    const val CUT_EDGE = 960

    /** The steadying warp (output → source pixels) for frames scaled by [k]: translations scale, the rest stays. */
    fun warpFor(warp: FloatArray, k: Float) = floatArrayOf(warp[0], warp[1], warp[2] * k, warp[3], warp[4], warp[5] * k)

    /**
     * The clip's box after the steadying crop: the warp zooms about the
     * centre by [zoom], so the person (and their box) grow by it too; the
     * box grows with them, keeping the person the size the app framed.
     */
    fun boxIn(box: FloatArray, zoom: Float): FloatArray {
      val z = zoom.coerceIn(1f, 1.5f)
      return FloatArray(4) { 0.5f + z * (box[it] - 0.5f) }
    }
  }
}
