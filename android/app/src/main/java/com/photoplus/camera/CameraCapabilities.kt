package com.photoplus.camera

/**
 * What the *active* camera can do, assembled from raw CameraX/Camera2 facts.
 * The UI renders a control only if it appears here, so features a device
 * lacks are never offered (Phase 2, step 2.4).
 *
 * Kept pure: CameraController gathers [Inputs] from the hardware; [build]
 * makes every product decision (e.g. distrusting manual sensor control on
 * LEGACY hardware) and is unit-tested on the JVM.
 */
object CameraCapabilities {

  data class Inputs(
    val lensFacing: String,
    val hardwareLevel: String,
    val zoomMin: Float,
    val zoomMax: Float,
    val exposureSupported: Boolean,
    val exposureMinIndex: Int,
    val exposureMaxIndex: Int,
    val exposureStep: Float,
    val hasFlash: Boolean,
    val focusMeteringSupported: Boolean,
    val manualSensor: Boolean,
    val manualPostProcessing: Boolean,
    val isoRange: Pair<Int, Int>?,
    val exposureRangeNs: Pair<Long, Long>?,
    val minFocusDiopters: Float,
    val awbModes: List<String>,
    val aeLockAvailable: Boolean,
    val awbLockAvailable: Boolean,
    val rawSupported: Boolean,
    val jpegSizes: List<PixelSize>,
  )

  fun manualRanges(inputs: Inputs): ManualRanges? {
    val iso = inputs.isoRange ?: return null
    val exposure = inputs.exposureRangeNs ?: return null
    return ManualRanges(iso.first, iso.second, exposure.first, exposure.second, inputs.minFocusDiopters)
  }

  fun build(inputs: Inputs): Map<String, Any?> {
    // LEGACY devices often advertise controls that silently do nothing.
    val trustworthy = inputs.hardwareLevel != "legacy"
    val manualExposure = trustworthy && inputs.manualSensor && inputs.isoRange != null && inputs.exposureRangeNs != null
    val manualFocus = trustworthy && inputs.manualSensor && inputs.minFocusDiopters > 0f
    val ranges = manualRanges(inputs)

    val resolutions = AspectRatioMode.entries.associate { ratio ->
      ratio.id to ResolutionPolicy.availablePresets(inputs.jpegSizes, ratio).map { (preset, size) ->
        val shown = if (ratio == AspectRatioMode.RATIO_1_1) PixelSize(minOf(size.width, size.height), minOf(size.width, size.height)) else size
        mapOf(
          "id" to preset.id,
          "width" to maxOf(shown.width, shown.height),
          "height" to minOf(shown.width, shown.height),
          "megapixels" to Math.round(shown.megapixels * 10) / 10.0,
        )
      }
    }

    val modes = buildList {
      add(CaptureMode.STANDARD.id)
      add(CaptureMode.ENHANCE.id)
      add(CaptureMode.PORTRAIT.id)
      add(CaptureMode.NIGHT.id)
      add(CaptureMode.SUPER_RES.id)
      if (inputs.rawSupported) add(CaptureMode.RAW.id)
    }

    return mapOf(
      "lensFacing" to inputs.lensFacing,
      "hardwareLevel" to inputs.hardwareLevel,
      "zoom" to mapOf("min" to inputs.zoomMin.toDouble(), "max" to inputs.zoomMax.toDouble()),
      "exposureCompensation" to if (inputs.exposureSupported && inputs.exposureMaxIndex > inputs.exposureMinIndex) mapOf(
        "minIndex" to inputs.exposureMinIndex,
        "maxIndex" to inputs.exposureMaxIndex,
        "step" to inputs.exposureStep.toDouble(),
      ) else null,
      "flash" to inputs.hasFlash,
      "torch" to inputs.hasFlash,
      "tapToFocus" to inputs.focusMeteringSupported,
      "manual" to mapOf(
        "exposure" to manualExposure,
        "iso" to if (manualExposure) inputs.isoRange.toList() else null,
        "exposureTimeNs" to if (manualExposure) inputs.exposureRangeNs.let { listOf(it.first.toDouble(), it.second.toDouble()) } else null,
        "isoStops" to if (manualExposure && ranges != null) ManualMath.isoStops(ranges) else emptyList<Int>(),
        "shutterStops" to if (manualExposure && ranges != null) ManualMath.shutterStops(ranges).map { it.toDouble() } else emptyList<Double>(),
        "focus" to manualFocus,
        "minFocusDiopters" to if (manualFocus) inputs.minFocusDiopters.toDouble() else null,
        "whiteBalanceModes" to (listOf("auto") + inputs.awbModes.filter { it != "auto" }).distinct(),
        "kelvin" to (trustworthy && inputs.manualPostProcessing),
        "aeLock" to inputs.aeLockAvailable,
        "awbLock" to inputs.awbLockAvailable,
      ),
      "raw" to inputs.rawSupported,
      "aspectRatios" to AspectRatioMode.entries.map { it.id },
      "resolutions" to resolutions,
      "captureModes" to modes,
      "focusPeaking" to true,
    )
  }

  fun awbModeName(mode: Int): String? = when (mode) {
    1 -> "auto"         // CONTROL_AWB_MODE_AUTO
    2 -> "incandescent" // CONTROL_AWB_MODE_INCANDESCENT
    3 -> "fluorescent"
    5 -> "daylight"
    6 -> "cloudy"       // CLOUDY_DAYLIGHT
    8 -> "shade"
    else -> null
  }
}
