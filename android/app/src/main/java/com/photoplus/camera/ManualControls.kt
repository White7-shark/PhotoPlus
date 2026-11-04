package com.photoplus.camera

import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.roundToLong

/**
 * Premium manual controls, expressed independently of Camera2 so the maths is
 * unit-testable. CameraController translates these into CaptureRequest keys.
 *
 * Camera2 semantics: turning auto-exposure off requires BOTH sensitivity and
 * exposure time; turning autofocus off requires a focus distance; manual
 * white balance in Kelvin requires colour-correction gains (approximated
 * here from the colour temperature) on MANUAL_POST_PROCESSING devices.
 */
data class ManualSettings(
  val iso: Int? = null,
  val exposureTimeNs: Long? = null,
  /** Lens focus distance in diopters (0 = infinity, larger = closer). */
  val focusDiopters: Float? = null,
  val whiteBalance: WhiteBalance = WhiteBalance.Auto,
  val aeLock: Boolean = false,
  val awbLock: Boolean = false,
) {
  val manualExposure: Boolean get() = iso != null || exposureTimeNs != null
  val manualFocus: Boolean get() = focusDiopters != null

  companion object {
    fun fromMap(map: Map<String, Any?>): ManualSettings = ManualSettings(
      iso = (map["iso"] as? Number)?.toInt(),
      exposureTimeNs = (map["exposureTimeNs"] as? Number)?.toLong(),
      focusDiopters = (map["focusDiopters"] as? Number)?.toFloat(),
      whiteBalance = WhiteBalance.parse(map["whiteBalance"] as? String, (map["kelvin"] as? Number)?.toInt()),
      aeLock = map["aeLock"] as? Boolean ?: false,
      awbLock = map["awbLock"] as? Boolean ?: false,
    )
  }
}

sealed class WhiteBalance(val id: String) {
  data object Auto : WhiteBalance("auto")
  data object Incandescent : WhiteBalance("incandescent")
  data object Fluorescent : WhiteBalance("fluorescent")
  data object Daylight : WhiteBalance("daylight")
  data object Cloudy : WhiteBalance("cloudy")
  data object Shade : WhiteBalance("shade")
  data class Kelvin(val kelvin: Int) : WhiteBalance("kelvin")

  companion object {
    fun parse(id: String?, kelvin: Int?): WhiteBalance = when (id) {
      "incandescent" -> Incandescent
      "fluorescent" -> Fluorescent
      "daylight" -> Daylight
      "cloudy" -> Cloudy
      "shade" -> Shade
      "kelvin" -> Kelvin((kelvin ?: 5500).coerceIn(2000, 10000))
      else -> Auto
    }
  }
}

/** Sensor ranges for clamping (from CameraCharacteristics). */
data class ManualRanges(
  val isoMin: Int,
  val isoMax: Int,
  val exposureMinNs: Long,
  val exposureMaxNs: Long,
  val minFocusDiopters: Float,
)

object ManualMath {
  /** Keeps a frame-rate-friendly ceiling on shutter time for live preview. */
  const val PREVIEW_MAX_EXPOSURE_NS = 250_000_000L

  /**
   * Fills the missing half of a manual exposure pair from the live auto
   * values, and clamps everything to what the sensor supports.
   */
  fun resolveExposure(
    settings: ManualSettings,
    ranges: ManualRanges,
    liveIso: Int?,
    liveExposureNs: Long?,
  ): Pair<Int, Long>? {
    if (!settings.manualExposure) return null
    val iso = (settings.iso ?: liveIso ?: 100).coerceIn(ranges.isoMin, ranges.isoMax)
    val exposure = (settings.exposureTimeNs ?: liveExposureNs ?: 16_666_667L).coerceIn(ranges.exposureMinNs, ranges.exposureMaxNs)
    return iso to exposure
  }

  fun clampFocus(diopters: Float, ranges: ManualRanges): Float = diopters.coerceIn(0f, ranges.minFocusDiopters)

  /** Photographic shutter stops within the sensor range, for a dial UI. */
  fun shutterStops(ranges: ManualRanges): List<Long> {
    val stops = listOf(
      1.0 / 8000, 1.0 / 4000, 1.0 / 2000, 1.0 / 1000, 1.0 / 500, 1.0 / 250, 1.0 / 125, 1.0 / 60, 1.0 / 30,
      1.0 / 15, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 4.0, 8.0, 15.0, 30.0,
    )
    return stops.map { (it * 1e9).roundToLong() }.filter { it in ranges.exposureMinNs..ranges.exposureMaxNs }
  }

  /** ISO stops (1/3-stop spacing is overkill on phones; full stops + common values). */
  fun isoStops(ranges: ManualRanges): List<Int> =
    listOf(25, 50, 64, 100, 125, 200, 250, 400, 640, 800, 1250, 1600, 3200, 6400, 12800)
      .filter { it in ranges.isoMin..ranges.isoMax }

  /**
   * Approximate RGGB white-balance gains for a colour temperature, from the
   * blackbody locus (Tanner Helland's fit), normalised so green = 1. Good
   * enough for a creative Kelvin dial; presets use the camera's own modes.
   */
  fun kelvinToRggbGains(kelvin: Int): FloatArray {
    val t = kelvin.coerceIn(1000, 40000) / 100.0
    val r = if (t <= 66) 255.0 else (329.698727446 * (t - 60).pow(-0.1332047592)).coerceIn(0.0, 255.0)
    val g = if (t <= 66) (99.4708025861 * ln(t) - 161.1195681661).coerceIn(0.0, 255.0)
    else (288.1221695283 * (t - 60).pow(-0.0755148492)).coerceIn(0.0, 255.0)
    val b = when {
      t >= 66 -> 255.0
      t <= 19 -> 0.0
      else -> (138.5177312231 * ln(t - 10) - 305.0447927307).coerceIn(0.0, 255.0)
    }
    // Gains are the inverse of the illuminant colour.
    val rg = (g / r.coerceAtLeast(1.0)).toFloat()
    val bg = (g / b.coerceAtLeast(1.0)).toFloat()
    return floatArrayOf(rg.coerceIn(0.25f, 4f), 1f, 1f, bg.coerceIn(0.25f, 4f))
  }

  /** "1/250" or "2\"" style label for a shutter time. */
  fun shutterLabel(ns: Long): String =
    if (ns >= 1_000_000_000L) "${"%.1f".format(ns / 1e9).removeSuffix(".0")}\""
    else "1/${(1e9 / ns).roundToLong()}"
}
