package com.photoplus.camera

/**
 * Camera configuration and capture requests as plain data, parsed from the
 * JavaScript bridge maps. Free of Android types so it is JVM-testable.
 */

enum class AspectRatioMode(val id: String, val width: Int, val height: Int) {
  RATIO_4_3("4:3", 4, 3),
  RATIO_16_9("16:9", 16, 9),
  RATIO_1_1("1:1", 1, 1);

  /** CameraX streams are 4:3 or 16:9; 1:1 is produced by a viewport crop of 4:3. */
  val streamRatio: AspectRatioMode get() = if (this == RATIO_1_1) RATIO_4_3 else this

  companion object {
    fun parse(value: String?): AspectRatioMode = entries.firstOrNull { it.id == value } ?: RATIO_4_3
  }
}

/** Target capture size by megapixels; MAX = the sensor's largest for the ratio. */
enum class ResolutionPreset(val id: String, val megapixels: Double?) {
  MAX("max", null),
  MP12("12mp", 12.0),
  MP8("8mp", 8.0),
  MP5("5mp", 5.0),
  MP2("2mp", 2.0);

  companion object {
    fun parse(value: String?): ResolutionPreset = entries.firstOrNull { it.id == value } ?: MAX
  }
}

enum class FlashSetting(val id: String) {
  OFF("off"),
  ON("on"),
  AUTO("auto");

  companion object {
    fun parse(value: String?): FlashSetting = entries.firstOrNull { it.id == value } ?: OFF
  }
}

/**
 * How a photo is produced:
 *  - STANDARD: the camera's own JPEG, saved directly.
 *  - RAW: DNG + JPEG pair (where the sensor supports RAW).
 *  - ENHANCE: one frame restored by the engine (denoise, deblur, tone).
 *  - NIGHT: a burst fused to remove noise, then restored.
 *  - SUPER_RES: a burst fused onto a 2x grid (multi-frame super-resolution).
 *  - PORTRAIT: one frame enhanced, then the subject made to stand out
 *    (background blur, subject clarity, natural skin retouching).
 *
 * Frame counts and working sizes depend on the device tier (CapturePolicy).
 */
enum class CaptureMode(val id: String, val upscale: Int, val processed: Boolean, val burst: Boolean) {
  STANDARD("standard", 1, false, false),
  RAW("raw", 1, false, false),
  ENHANCE("enhance", 1, true, false),
  NIGHT("night", 1, true, true),
  SUPER_RES("superres", 2, true, true),
  PORTRAIT("spotlight", 1, true, false);

  companion object {
    fun parse(value: String?): CaptureMode = entries.firstOrNull { it.id == value } ?: STANDARD
  }
}

data class SessionConfig(
  val aspectRatio: AspectRatioMode = AspectRatioMode.RATIO_4_3,
  val resolution: ResolutionPreset = ResolutionPreset.MAX,
  val rawEnabled: Boolean = false,
  /** Glide: the session records video (preview + video stream) instead of photos. */
  val video: Boolean = false,
  /** Live beauty on the preview (GPU effect + live analysis); photos are finished with the recipe either way. */
  val beauty: Boolean = false,
) {
  companion object {
    fun fromMap(map: Map<String, Any?>, current: SessionConfig = SessionConfig()) = SessionConfig(
      aspectRatio = if (map.containsKey("aspectRatio")) AspectRatioMode.parse(map["aspectRatio"] as? String) else current.aspectRatio,
      resolution = if (map.containsKey("resolution")) ResolutionPreset.parse(map["resolution"] as? String) else current.resolution,
      rawEnabled = (map["raw"] as? Boolean) ?: current.rawEnabled,
      video = (map["video"] as? Boolean) ?: current.video,
      beauty = (map["beauty"] as? Boolean) ?: current.beauty,
    )
  }
}

data class CaptureOptions(
  val captureId: String,
  val mode: CaptureMode,
  val flash: FlashSetting,
  /** Engine strength multipliers, 0..1 (defaults are the tuned values). */
  val denoise: Float = 1f,
  val sharpen: Float = 1f,
  val detail: Float = 0.35f,
  val vibrance: Float = 0.25f,
  val autoTone: Boolean = true,
  /** Save front-camera photos as the preview shows them (mirrored), like a mirror. */
  val mirrorFront: Boolean = true,
  /** Spotlight settings (PORTRAIT mode). */
  val portrait: com.photoplus.studio.PortraitParams = com.photoplus.studio.PortraitParams(),
  /** What to make stand out: taps on the preview (0..1), as the user saw them. */
  val previewPoints: List<Pair<Float, Float>> = emptyList(),
  /** A look to apply ("Vibe Match"), as engine look numbers; null = none. */
  val look: FloatArray? = null,
  val lookStrength: Float = 0.8f,
  /** The watermark free-plan photos carry (the user's chosen style). */
  val watermark: com.photoplus.edit.WatermarkSpec = com.photoplus.edit.WatermarkSpec(),
  /** Props worn in the preview, composited into the photo (not RAW). */
  val props: List<CameraProps.Placed> = emptyList(),
  /** Camera beauty (Glow) for photos taken outside Spotlight: skin retouch strength and its lift (0..1). */
  val beautyRetouch: Float = 0f,
  val beautyBrighten: Float = 0f,
  /** Face-precise touches that come with the beauty level (eyes, under-eye, shine, face light). */
  val beauty: com.photoplus.studio.FaceTuning = com.photoplus.studio.FaceTuning(),
  /** A beauty preset's resolved recipe (the camera's Beauty mode); it supersedes the Glow levels above. */
  val beautyRecipe: com.photoplus.beauty.BeautyRecipe? = null,
  /** The preset's id, kept with the photo so it can be re-applied. */
  val beautyPreset: String? = null,
) {
  val beautyRecipeActive: Boolean get() = beautyRecipe?.active == true
  val beautyActive: Boolean get() = beautyRecipeActive || beautyRetouch > 0f || beauty.any

  /** RAW stays untouched; every other capture with a look or beauty is decoded and processed. */
  val needsProcessing: Boolean get() = mode != CaptureMode.RAW && (mode.processed || look != null || beautyActive)
  /** Bursts never fire the flash: repeated flashes would differ frame to frame and break the merge. */
  val effectiveFlash: FlashSetting get() = if (mode.burst) FlashSetting.OFF else flash

  companion object {
    /** The face touches of a beauty level (finite, 0..1; anything else is ignored). */
    fun beautyTuning(m: Map<*, *>?): com.photoplus.studio.FaceTuning {
      if (m == null) return com.photoplus.studio.FaceTuning()
      fun v(k: String) = (m[k] as? Number)?.toFloat()?.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f
      return com.photoplus.studio.FaceTuning(eyes = v("eyes"), underEye = v("underEye"), shine = v("shine"), faceLight = v("faceLight"))
    }

    fun fromMap(map: Map<String, Any?>): CaptureOptions {
      fun unit(key: String, default: Float) = ((map[key] as? Number)?.toFloat() ?: default).coerceIn(0f, 1f)
      return CaptureOptions(
        captureId = (map["captureId"] as? String)?.takeIf { it.isNotBlank() } ?: "capture-${System.nanoTime()}",
        mode = CaptureMode.parse(map["mode"] as? String),
        flash = FlashSetting.parse(map["flash"] as? String),
        denoise = unit("denoise", 1f),
        sharpen = unit("sharpen", 1f),
        detail = unit("detail", 0.35f),
        vibrance = unit("vibrance", 0.25f),
        autoTone = map["autoTone"] as? Boolean ?: true,
        mirrorFront = map["mirrorFront"] as? Boolean ?: true,
        portrait = com.photoplus.studio.PortraitParams.fromMap(map),
        previewPoints = com.photoplus.studio.PortraitParams.points(map["points"]),
        look = (map["look"] as? List<*>)?.mapNotNull { (it as? Number)?.toFloat() }
          ?.takeIf { it.size == com.photoplus.engine.NativeEngine.STYLE_FLOATS && it.all { v -> v.isFinite() } }
          ?.toFloatArray(),
        lookStrength = unit("lookStrength", 0.8f),
        beautyRetouch = (map["beauty"] as? Map<*, *>)?.let { (it["retouch"] as? Number)?.toFloat()?.coerceIn(0f, 1f) } ?: 0f,
        beautyBrighten = (map["beauty"] as? Map<*, *>)?.let { (it["brighten"] as? Number)?.toFloat()?.coerceIn(0f, 1f) } ?: 0f,
        beauty = beautyTuning(map["beauty"] as? Map<*, *>),
        beautyRecipe = com.photoplus.beauty.BeautyRecipe.fromMap(map["beautyRecipe"] as? Map<*, *>),
        beautyPreset = (map["beautyPreset"] as? String)?.takeIf { it.length in 1..64 },
        watermark = runCatching {
          @Suppress("UNCHECKED_CAST")
          com.photoplus.edit.WatermarkSpec.fromMap(map["watermark"] as? Map<String, Any?>)
        }.getOrDefault(com.photoplus.edit.WatermarkSpec()),
        props = CameraProps.parse(map["props"]),
      )
    }
  }
}
