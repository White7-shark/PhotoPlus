package com.photoplus.video

import com.photoplus.studio.FaceTuning

/**
 * Glide: a clip (up to 10 s on the free plan, 30 s on Premium) made steady,
 * clean and sharp, with optional Spotlight and Glow, or (Premium) the person
 * moved into a new scene. Pure Kotlin (unit-tested).
 */
data class GlideOptions(
  /** 0..1: how smooth the camera path becomes (1 ≈ a gimbal-like glide). */
  val steady: Float = 1f,
  /** Largest crop the steadying may use (1.0..1.25). */
  val maxZoom: Float = 1.12f,
  val denoise: Float = 1f,
  val sharpen: Float = 1f,
  /** Spotlight background blur 0..1 (0 = off) and subject pop. */
  val blur: Float = 0f,
  val pop: Float = 0.4f,
  /** Glow (skin) and its lift, 0..1 (0 = off). */
  val retouch: Float = 0f,
  val brighten: Float = 0f,
  /** Face touches that come with Glow. */
  val beauty: FaceTuning = FaceTuning(),
  /** A beauty preset's resolved recipe (with Spotlight's blur when on); it replaces Glow and the blur above. */
  val recipe: com.photoplus.beauty.BeautyRecipe? = null,
  /** Premium: the person cut out of every frame and placed in this scene. */
  val scene: GlideScene? = null,
) {
  val beautyLook: Boolean get() = recipe?.active == true
  val spotlight: Boolean get() = blur > 0f
  val portrait: Boolean get() = spotlight || retouch > 0f || brighten > 0f || beautyLook
  /** The person's mask is needed every frame. */
  val subject: Boolean get() = portrait || scene != null
  val faces: Boolean get() = beauty.any

  companion object {
    /** The longest clip on the free plan (always with the Photo Plus mark). */
    const val FREE_MAX_DURATION_MS = 10_000L
    /** The longest clip on Premium. */
    const val PREMIUM_MAX_DURATION_MS = 30_000L
    /** The longest clip Glide records and processes on any plan. */
    const val MAX_DURATION_MS = PREMIUM_MAX_DURATION_MS
    /** Frames processed at most (30 s at 30 fps, with room for 31 fps sensors). */
    const val MAX_FRAMES = 960

    fun maxDurationMs(plan: com.photoplus.edit.Plan): Long =
      if (plan == com.photoplus.edit.Plan.PREMIUM) PREMIUM_MAX_DURATION_MS else FREE_MAX_DURATION_MS

    /** A recording's length, kept within the plan's limit (and at least a second). */
    fun clampDurationMs(requested: Long?, plan: com.photoplus.edit.Plan): Long =
      (requested ?: maxDurationMs(plan)).coerceIn(1_000L, maxDurationMs(plan))

    /** Frames of a clip at [fps] the plan lets Glide process (a longer import is cut at the limit). */
    fun maxFrames(fps: Float, plan: com.photoplus.edit.Plan): Int =
      minOf(MAX_FRAMES, (fps.coerceIn(10f, 60f) * maxDurationMs(plan) / 1000f).toInt() + 1)

    fun fromMap(map: Map<String, Any?>): GlideOptions {
      fun unit(key: String, default: Float) =
        ((map[key] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: default).coerceIn(0f, 1f)
      val beauty = map["beauty"] as? Map<*, *>
      fun b(key: String) = ((beauty?.get(key) as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: 0f).coerceIn(0f, 1f)
      return GlideOptions(
        steady = unit("steady", 1f),
        maxZoom = ((map["maxZoom"] as? Number)?.toFloat()?.takeIf { it.isFinite() } ?: 1.12f).coerceIn(1f, 1.25f),
        denoise = unit("denoise", 1f),
        sharpen = unit("sharpen", 1f),
        blur = unit("blur", 0f),
        pop = unit("pop", 0.4f),
        retouch = b("retouch"),
        brighten = b("brighten"),
        beauty = FaceTuning(eyes = b("eyes"), underEye = b("underEye"), shine = b("shine"), faceLight = b("faceLight")),
        recipe = com.photoplus.beauty.BeautyRecipe.fromMap(map["beautyRecipe"] as? Map<*, *>),
        scene = GlideScene.fromMap(map["scene"] as? Map<*, *>),
      )
    }

    /**
     * H.264 bitrate for a size and frame rate: ~0.2 bits per pixel per frame,
     * so a 1080p30 Glide clip is ~12 Mbit/s (phone cameras record 15–20).
     */
    fun bitrate(width: Int, height: Int, fps: Float): Int =
      (width.toLong() * height * fps.coerceIn(15f, 60f) * 0.2f).toLong().coerceIn(2_000_000L, 40_000_000L).toInt()

    /** Presentation time of frame [index] at [fps], in microseconds (exact, no drift). */
    fun frameTimeUs(index: Int, fps: Float): Long = (index * 1_000_000.0 / fps.coerceIn(1f, 240f)).toLong()
  }
}

/**
 * Face landmarks steadied over time: each frame's points move part of the
 * way toward the new detection (detections jitter by a pixel or two, which
 * makes eye and under-eye touches shimmer); a large jump (the head turned,
 * or another face was found) is taken at once.
 */
class LandmarkSmoother(private val follow: Float = 0.45f, private val jump: Float = 0.04f) {
  private var last: FloatArray? = null

  fun next(points: FloatArray?): FloatArray? {
    if (points == null) {
      last = null
      return null
    }
    val prev = last
    val out = if (prev == null || prev.size != points.size || maxShift(prev, points) > jump) {
      points.copyOf()
    } else {
      FloatArray(points.size) { prev[it] + follow * (points[it] - prev[it]) }
    }
    last = out
    return out.copyOf()
  }

  private fun maxShift(a: FloatArray, b: FloatArray): Float {
    var m = 0f
    for (i in a.indices) m = maxOf(m, kotlin.math.abs(a[i] - b[i]))
    return m
  }
}
