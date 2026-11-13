package com.photoplus.camera

import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * Auto Light: how the camera handles dark rooms, night and harsh light.
 * Plain Kotlin (no Android types) so every rule is JVM-testable.
 *
 * The engine's meter (pp_light_meter) reads the scene from the camera's
 * own exposure and a small preview frame, and names a plan. This file turns
 * a steady reading into what the shutter actually does, the way the best
 * phone cameras do (see docs/PHASE9_LIGHTING_REPORT.md):
 *
 *  - Moonlight (dim, below ~40 lux): a burst merged to remove noise (longer
 *    as the light fades), then night tone: clear, but night still looks
 *    like night.
 *  - Natural Flash (very dark, rear camera, something near, flash allowed):
 *    the room's light from an ambient burst, the detail from one flash frame.
 *  - Selfie Light (very dark, front camera, flash allowed): the screen lights
 *    the face (tinted toward the room's light), then tone.
 *  - Bright Balance (harsh or backlit): a burst held 1–2 stops darker to
 *    keep the sky and highlights, then the shadows opened by local tone.
 */
enum class LightPlan(val id: String) {
  NORMAL("normal"),
  MOONLIGHT("moonlight"),
  FLASH("flash"),
  BRIGHT_BALANCE("brightBalance");

  companion object {
    fun fromEngine(value: Int): LightPlan = when (value) {
      1 -> MOONLIGHT
      2 -> FLASH
      3 -> BRIGHT_BALANCE
      else -> NORMAL
    }
  }
}

/** One reading of the scene (nativeLightMeter's output). */
data class LightReading(
  val lux: Float?,
  val key: Float,
  val highlights: Float,
  val rangeStops: Float,
  val backlit: Boolean,
  val plan: LightPlan,
  val frames: Int,
  val exposureBias: Float,
  val night: Float,
  /** The screen light's colour, 0xRRGGBB. */
  val screenColor: Int,
) {
  companion object {
    const val METER_IN = 7
    const val METER_OUT = 15

    /** The meter's input: ISO, exposure seconds, f-number, front, has flash, face luma, focus. */
    fun meterInput(iso: Int?, exposureNs: Long?, aperture: Float?, front: Boolean, hasFlash: Boolean, faceLuma: Float?, focusDiopters: Float?) =
      floatArrayOf(
        (iso ?: 0).toFloat(),
        (exposureNs ?: 0L) / 1e9f,
        aperture ?: 0f,
        if (front) 1f else 0f,
        if (hasFlash) 1f else 0f,
        faceLuma ?: -1f,
        focusDiopters ?: -1f,
      )

    fun fromMeter(out: FloatArray): LightReading {
      require(out.size >= METER_OUT)
      fun finite(v: Float, d: Float) = if (v.isFinite()) v else d
      val c = { i: Int -> finite(out[i], 255f).roundToInt().coerceIn(0, 255) }
      return LightReading(
        lux = if (out[2] > 0.5f && out[1].isFinite()) max(0f, out[1]) else null,
        key = finite(out[3], 0.18f),
        highlights = finite(out[4], 0f),
        rangeStops = finite(out[6], 0f),
        backlit = out[7] > 0.5f,
        plan = LightPlan.fromEngine(out[8].roundToInt()),
        frames = finite(out[9], 1f).roundToInt().coerceIn(1, 16),
        exposureBias = finite(out[10], 0f).coerceIn(-3f, 0f),
        night = finite(out[11], 0f).coerceIn(0f, 1f),
        screenColor = (c(12) shl 16) or (c(13) shl 8) or c(14),
      )
    }
  }
}

/**
 * Keeps the plan from flickering as the preview changes (a face turning, a
 * hand in front of a lamp): a new plan is taken only once it has been read
 * [hold] times in a row. Within the current plan, the latest numbers are used.
 */
class LightSteadier(private val hold: Int = 3) {
  var current: LightReading? = null
    private set
  private var candidate: LightPlan? = null
  private var count = 0

  /** Returns true when the steady reading changed in a way the user would see (the plan). */
  fun feed(reading: LightReading): Boolean {
    val now = current
    if (now == null) {
      current = reading
      return true
    }
    if (reading.plan == now.plan) {
      candidate = null
      count = 0
      current = reading
      return false
    }
    if (reading.plan == candidate) count++ else {
      candidate = reading.plan
      count = 1
    }
    if (count >= hold) {
      current = reading
      candidate = null
      count = 0
      return true
    }
    return false
  }

  fun reset() {
    current = null
    candidate = null
    count = 0
  }
}

/** What the camera can do right now. */
data class LightContext(
  val front: Boolean,
  val hasFlash: Boolean,
  /** Exposure compensation: step in EV, the allowed index range and the index the user set. */
  val exposureStep: Float,
  val exposureMin: Int,
  val exposureMax: Int,
  val exposureIndex: Int,
  /** The screen light can be used (front camera, the window is known). */
  val screenFlashReady: Boolean,
)

enum class LightFlash { NONE, SCREEN, NATURAL }

/** Tone after the merge (nativeLightTone's parameters). */
data class LightTone(
  val night: Float,
  val captureBias: Float,
  val whiteBalance: Float,
  val shadows: Float = -1f,
  val highlights: Float = -1f,
  val exposure: Float = 0f,
  val detail: Float = 0.35f,
  val strength: Float = 1f,
) {
  fun toParams() = floatArrayOf(night, shadows, highlights, exposure, detail, whiteBalance, captureBias, strength)
}

/** What the shutter does under Auto Light. */
data class LightDecision(
  val plan: LightPlan,
  /** The capture mode actually used (a burst for Moonlight and Bright Balance). */
  val mode: CaptureMode,
  /** Ambient frames (the flash frame of Natural Flash is extra). */
  val frames: Int,
  /** Exposure compensation index for the capture, or null to leave it as it is. */
  val exposureIndex: Int?,
  val flash: LightFlash,
  /** The screen light's colour (0xRRGGBB), for Selfie Light. */
  val screenColor: Int,
  val tone: LightTone?,
  /** What the user is told ("Moonlight", "Natural Flash", ...); null = nothing special. */
  val label: String?,
)

object AutoLight {
  /** The longest burst each phone class handles quickly (memory and time). */
  fun maxFrames(tier: DeviceTier): Int = when (tier) {
    DeviceTier.ENTRY -> 6
    DeviceTier.STANDARD -> 8
    DeviceTier.PERFORMANCE -> 10
  }

  /**
   * A burst longer than the tier's usual NIGHT plan is decoded smaller, so
   * the pixels processed (frames × size²) stay within the same budget.
   */
  fun burstPlan(base: ModePlan, frames: Int): ModePlan {
    if (frames <= base.frames || base.decodeDimension <= 0) return base.copy(frames = max(1, frames))
    val scale = sqrt(base.frames.toDouble() / frames)
    return base.copy(frames = frames, decodeDimension = max(640, (base.decodeDimension * scale).roundToInt()))
  }

  /** Modes Auto Light may change: the everyday ones (not RAW, Super Res or Spotlight). */
  fun applies(mode: CaptureMode): Boolean = mode == CaptureMode.STANDARD || mode == CaptureMode.ENHANCE || mode == CaptureMode.NIGHT

  /** The label shown in the camera for a steady reading. */
  fun label(reading: LightReading, context: LightContext, flash: FlashSetting): String? = when (reading.plan) {
    LightPlan.NORMAL -> null
    LightPlan.MOONLIGHT -> "Moonlight"
    LightPlan.BRIGHT_BALANCE -> "Bright Balance"
    LightPlan.FLASH -> when {
      flash == FlashSetting.OFF -> "Moonlight"
      context.front && context.screenFlashReady -> "Selfie Light"
      !context.front && context.hasFlash -> "Natural Flash"
      else -> "Moonlight"
    }
  }

  /**
   * Turns a steady reading and the user's choices into the capture. Never
   * overrides the user: flash OFF never fires a flash (the screen included),
   * and a manual exposure compensation is left alone.
   */
  fun decide(request: CaptureOptions, reading: LightReading?, context: LightContext, tier: DeviceTier): LightDecision? {
    if (!request.autoLight || !applies(request.mode)) return null
    // Flash ON on the front camera: the screen light, whatever the scene.
    if (context.front && request.flash == FlashSetting.ON && context.screenFlashReady) {
      val r = reading
      return LightDecision(
        LightPlan.FLASH, CaptureMode.ENHANCE, 1, null, LightFlash.SCREEN, r?.screenColor ?: 0xFFFFFF,
        LightTone(night = 0f, captureBias = 0f, whiteBalance = 0f), "Selfie Light",
      )
    }
    reading ?: return null
    // Without multi-shot: one frame and tone (the burst modes are a plan feature).
    val multi = request.autoLightMultiShot
    val cap = if (multi) maxFrames(tier) else 1
    val burstMode = if (multi) CaptureMode.NIGHT else CaptureMode.ENHANCE
    val userExposure = context.exposureIndex != 0
    fun biasIndex(ev: Float): Int? {
      if (userExposure || ev >= -0.05f || context.exposureStep <= 0f) return null
      val idx = (ev / context.exposureStep).roundToInt().coerceIn(context.exposureMin, context.exposureMax)
      return idx.takeIf { it != context.exposureIndex }
    }
    fun appliedBias(index: Int?) = (index ?: context.exposureIndex) * context.exposureStep
    fun moonlight(frames: Int, night: Float, label: String): LightDecision {
      val idx = biasIndex(reading.exposureBias)
      val bias = if (idx != null) appliedBias(idx) else 0f
      return LightDecision(
        LightPlan.MOONLIGHT, burstMode, frames.coerceIn(min(2, cap), cap), idx, LightFlash.NONE, reading.screenColor,
        LightTone(night = night, captureBias = min(0f, bias), whiteBalance = 0.6f * night), label,
      )
    }
    fun natural(frames: Int) = LightDecision(
      LightPlan.FLASH, burstMode, frames.coerceIn(min(2, cap), cap), null, LightFlash.NATURAL,
      reading.screenColor, LightTone(night = 0.6f, captureBias = 0f, whiteBalance = 0.4f), "Natural Flash",
    )
    val rearFlashForced = !context.front && context.hasFlash && request.flash == FlashSetting.ON
    return when (reading.plan) {
      LightPlan.NORMAL -> null
      // Flash asked for in a dim room: the flash's detail with the room's light.
      LightPlan.MOONLIGHT -> if (rearFlashForced) natural(reading.frames) else moonlight(reading.frames, reading.night, "Moonlight")
      LightPlan.BRIGHT_BALANCE -> {
        // One frame cannot hold two stops of shadow back without noise: one stop at most.
        val idx = biasIndex(if (multi) reading.exposureBias else max(reading.exposureBias, -1f))
        val bias = if (idx != null) appliedBias(idx) else 0f
        LightDecision(
          LightPlan.BRIGHT_BALANCE, burstMode, reading.frames.coerceIn(min(3, cap), cap), idx, LightFlash.NONE,
          reading.screenColor, LightTone(night = 0f, captureBias = min(0f, bias), whiteBalance = 0f), "Bright Balance",
        )
      }
      LightPlan.FLASH -> {
        val flashAllowed = request.flash != FlashSetting.OFF
        when {
          flashAllowed && context.front && context.screenFlashReady -> LightDecision(
            LightPlan.FLASH, CaptureMode.ENHANCE, 1, null, LightFlash.SCREEN, reading.screenColor,
            // Lit by the screen, the face is close and bright: a portrait, not a night scene.
            LightTone(night = 0.3f, captureBias = 0f, whiteBalance = 0.3f), "Selfie Light",
          )
          flashAllowed && !context.front && context.hasFlash -> natural(reading.frames)
          // No flash (or not allowed): the longest burst, full night look.
          else -> moonlight(cap, max(reading.night, 0.8f), "Moonlight")
        }
      }
    }
  }

  /**
   * Low Light Boost (Android 15+, where the camera supports it) brightens
   * the live view in the dark. It turns on in a steady dark reading and off
   * only once the scene is clearly brighter, so it never flickers.
   */
  fun wantLowLightBoost(on: Boolean, reading: LightReading?): Boolean {
    reading ?: return on
    val lux = reading.lux
    return if (!on) {
      (reading.plan == LightPlan.MOONLIGHT || reading.plan == LightPlan.FLASH) && (lux == null || lux < 10f)
    } else {
      !(reading.plan == LightPlan.NORMAL || reading.plan == LightPlan.BRIGHT_BALANCE || (lux != null && lux > 60f))
    }
  }

  /** Whether two readings differ enough to tell JavaScript again. */
  fun worthReporting(previous: LightReading?, next: LightReading): Boolean {
    previous ?: return true
    if (previous.plan != next.plan || previous.frames != next.frames || previous.backlit != next.backlit) return true
    val a = previous.lux
    val b = next.lux
    if ((a == null) != (b == null)) return true
    if (a != null && b != null && abs(kotlin.math.ln((b + 0.1f) / (a + 0.1f))) > 0.35f) return true
    return false
  }
}
