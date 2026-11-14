package com.photoplus.video

import com.photoplus.engine.EngineException
import com.photoplus.studio.SceneBackground
import com.photoplus.studio.SceneCrop
import com.photoplus.studio.SceneLook
import com.photoplus.studio.ScenePlacement

/**
 * Glide in a new scene (Premium): the person is cut out of every frame and
 * placed in [background] as the app framed them (true to the scene's size,
 * from the clip's [box]). The box is fixed for the whole clip, so the person
 * keeps one size and place while they move. Pure Kotlin (unit-tested).
 */
data class GlideScene(
  val background: SceneBackground,
  val crop: SceneCrop,
  val placement: ScenePlacement,
  val look: SceneLook,
  /** The person's usual box in the clip (left, top, right, bottom, 0..1), from [GlideSubject]. */
  val box: FloatArray,
) {
  init {
    require(box.size == 4 && box[2] - box[0] > 0.01f && box[3] - box[1] > 0.01f) { "bad box" }
  }

  override fun equals(other: Any?) =
    other is GlideScene && background == other.background && crop == other.crop && placement == other.placement &&
      look == other.look && box.contentEquals(other.box)

  override fun hashCode() = listOf(background, crop, placement, look, box.contentHashCode()).hashCode()

  companion object {
    @Suppress("UNCHECKED_CAST")
    fun fromMap(m: Map<*, *>?): GlideScene? {
      if (m == null) return null
      val map = m as Map<String, Any?>
      val bg = SceneBackground.fromMap(map["background"] as? Map<String, Any?>)
      if (bg is SceneBackground.Transparent) throw EngineException(EngineException.INVALID_ARGUMENT, "A video needs a background")
      val raw = (map["box"] as? List<*>)?.map { (it as? Number)?.toFloat() ?: Float.NaN }
      if (raw == null || raw.size != 4 || raw.any { !it.isFinite() }) {
        throw EngineException(EngineException.INVALID_ARGUMENT, "The person's place in the clip is missing")
      }
      val box = floatArrayOf(raw[0].coerceIn(0f, 1f), raw[1].coerceIn(0f, 1f), raw[2].coerceIn(0f, 1f), raw[3].coerceIn(0f, 1f))
      if (box[2] - box[0] <= 0.01f || box[3] - box[1] <= 0.01f) {
        throw EngineException(EngineException.INVALID_ARGUMENT, "The person's place in the clip is missing")
      }
      return GlideScene(
        background = bg,
        crop = SceneCrop.fromMap(map["crop"] as? Map<String, Any?>),
        placement = ScenePlacement.fromMap(map["placement"] as? Map<String, Any?>),
        look = SceneLook.fromMap(map["look"] as? Map<String, Any?>),
        box = box,
      )
    }
  }
}

/**
 * Where the person usually is in a clip, from a person mask per sampled
 * frame: the median of each side of their box (a raised arm or a step to
 * the side in one frame does not count), and whether the frame cuts them
 * off at an edge in most frames. Pure (unit-tested).
 */
object GlideSubject {
  data class Summary(
    val box: FloatArray,
    val cutLeft: Boolean,
    val cutTop: Boolean,
    val cutRight: Boolean,
    val cutBottom: Boolean,
    val frames: Int,
  ) {
    fun toMap(aspect: Float): Map<String, Any?> = mapOf(
      "box" to box.map { it.toDouble() },
      "cutLeft" to cutLeft,
      "cutTop" to cutTop,
      "cutRight" to cutRight,
      "cutBottom" to cutBottom,
      "aspect" to aspect.toDouble(),
      "frames" to frames,
      "found" to (frames > 0),
    )

    override fun equals(other: Any?) =
      other is Summary && box.contentEquals(other.box) && cutLeft == other.cutLeft && cutTop == other.cutTop &&
        cutRight == other.cutRight && cutBottom == other.cutBottom && frames == other.frames

    override fun hashCode() = listOf(box.contentHashCode(), cutLeft, cutTop, cutRight, cutBottom, frames).hashCode()
  }

  /** One frame's box (left, top, right, bottom, 0..1) and cut edges (left, top, right, bottom), or null when nobody is in it. */
  fun frameBox(mask: ByteArray, width: Int, height: Int): Pair<FloatArray, BooleanArray>? {
    var l = width
    var r = -1
    var t = height
    var b = -1
    var area = 0
    for (y in 0 until height) {
      for (x in 0 until width) {
        if ((mask[y * width + x].toInt() and 0xFF) > 127) {
          if (x < l) l = x
          if (x > r) r = x
          if (y < t) t = y
          if (y > b) b = y
          area++
        }
      }
    }
    if (r < 0 || area < width * height / 400) return null
    fun run(horizontal: Boolean, at: Int): Boolean {
      val len = if (horizontal) width else height
      var on = 0
      for (k in 0 until len) {
        val i = if (horizontal) at * width + k else k * width + at
        if ((mask[i].toInt() and 0xFF) > 127) on++
      }
      return on.toFloat() / len > 0.04f
    }
    val box = floatArrayOf(l.toFloat() / width, t.toFloat() / height, (r + 1).toFloat() / width, (b + 1).toFloat() / height)
    return box to booleanArrayOf(run(false, 0), run(true, 0), run(false, width - 1), run(true, height - 1))
  }

  fun summarise(frames: List<Pair<FloatArray, BooleanArray>>): Summary? {
    if (frames.isEmpty()) return null
    fun median(k: Int) = frames.map { it.first[k] }.sorted().let { it[it.size / 2] }
    fun most(k: Int) = frames.count { it.second[k] } * 2 > frames.size
    return Summary(floatArrayOf(median(0), median(1), median(2), median(3)), most(0), most(1), most(2), most(3), frames.size)
  }
}
