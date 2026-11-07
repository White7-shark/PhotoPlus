package com.photoplus.video

import com.photoplus.studio.FaceTuning

/**
 * Glide: a short (≤ 7 s) clip made steady, clean and sharp, with optional
 * Spotlight and Glow. Pure Kotlin (unit-tested).
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
) {
  val spotlight: Boolean get() = blur > 0f
  val portrait: Boolean get() = spotlight || retouch > 0f || brighten > 0f
  val faces: Boolean get() = beauty.any

  companion object {
    /** The longest clip Glide records and processes. */
    const val MAX_DURATION_MS = 7_000L
    /** Frames processed at most (7 s at 30 fps, with a little room for 31 fps sensors). */
    const val MAX_FRAMES = 220

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
