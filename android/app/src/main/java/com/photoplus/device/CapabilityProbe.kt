package com.photoplus.device

import android.content.Context
import android.content.pm.PackageManager
import android.graphics.ImageFormat
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata
import android.media.MediaCodecList
import android.os.Build
import android.os.StatFs
import android.util.Log

/**
 * Collects a one-shot snapshot of what this device can do. The result is
 * plain maps/lists so it can be serialised to JS and persisted for support
 * diagnostics. Each section is isolated: one failing probe (e.g. a buggy
 * camera HAL) never prevents the others from reporting.
 */
object CapabilityProbe {
  private const val TAG = "PhotoPlusCapabilities"
  const val SCHEMA_VERSION = 1

  fun collect(context: Context): Map<String, Any?> = mapOf(
    "schemaVersion" to SCHEMA_VERSION,
    "collectedAt" to System.currentTimeMillis().toDouble(),
    "os" to safely("os") { os() },
    "memory" to safely("memory") { memory(context) },
    "cpu" to safely("cpu") { cpu() },
    "storage" to safely("storage") { storage(context) },
    "display" to safely("display") { display(context) },
    "cameras" to (safely("cameras") { cameras(context) } ?: emptyList<Any>()),
    "imageFormats" to safely("imageFormats") { imageFormats() },
    "acceleration" to safely("acceleration") { acceleration(context) },
  )

  private fun <T> safely(section: String, block: () -> T): T? = try {
    block()
  } catch (t: Throwable) {
    Log.w(TAG, "Capability probe '$section' failed", t)
    null
  }

  private fun os() = mapOf(
    "sdkInt" to Build.VERSION.SDK_INT,
    "release" to Build.VERSION.RELEASE,
    "securityPatch" to Build.VERSION.SECURITY_PATCH,
    "manufacturer" to Build.MANUFACTURER,
    "brand" to Build.BRAND,
    "model" to Build.MODEL,
    "device" to Build.DEVICE,
    "supportedAbis" to Build.SUPPORTED_ABIS.toList(),
    "is64Bit" to Build.SUPPORTED_64_BIT_ABIS.isNotEmpty(),
  )

  fun memory(context: Context): Map<String, Any?> {
    val am = DeviceInfo.activityManager(context)
    val info = DeviceInfo.memoryInfo(context)
    return mapOf(
      "totalBytes" to info.totalMem.toDouble(),
      "availableBytes" to info.availMem.toDouble(),
      "thresholdBytes" to info.threshold.toDouble(),
      "lowMemory" to info.lowMemory,
      "isLowRamDevice" to am.isLowRamDevice,
      "memoryClassMb" to am.memoryClass,
      "largeMemoryClassMb" to am.largeMemoryClass,
      "pressureLevel" to MemoryPressureMonitor.lastLevel.name.lowercase(),
    )
  }

  private fun cpu() = mapOf(
    "cores" to Runtime.getRuntime().availableProcessors(),
    "socModel" to if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MODEL else null,
    "socManufacturer" to if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MANUFACTURER else null,
  )

  private fun storage(context: Context): Map<String, Any?> {
    val stat = StatFs(context.filesDir.absolutePath)
    return mapOf(
      "totalBytes" to stat.totalBytes.toDouble(),
      "availableBytes" to stat.availableBytes.toDouble(),
    )
  }

  private fun display(context: Context): Map<String, Any?> {
    val m = context.resources.displayMetrics
    return mapOf(
      "widthPx" to m.widthPixels,
      "heightPx" to m.heightPixels,
      "density" to m.density.toDouble(),
      "densityDpi" to m.densityDpi,
    )
  }

  private fun cameras(context: Context): List<Map<String, Any?>> {
    if (!context.packageManager.hasSystemFeature(PackageManager.FEATURE_CAMERA_ANY)) return emptyList()
    val manager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    return manager.cameraIdList.mapNotNull { id ->
      safely("camera.$id") { describeCamera(id, manager.getCameraCharacteristics(id)) }
    }
  }

  private fun describeCamera(id: String, c: CameraCharacteristics): Map<String, Any?> {
    val caps = c.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)?.toSet() ?: emptySet()
    fun has(cap: Int) = caps.contains(cap)
    val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
    val outputFormats = map?.outputFormats?.toSet() ?: emptySet()
    val jpegSizes = map?.getOutputSizes(ImageFormat.JPEG)
    val maxJpeg = jpegSizes?.maxByOrNull { it.width.toLong() * it.height }
    val pixelArray = c.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE)
    val iso = c.get(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE)
    val exposure = c.get(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE)
    val aeRange = c.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_RANGE)
    val aeStep = c.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_STEP)
    val afModes = c.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES)?.toList() ?: emptyList()
    val ois = c.get(CameraCharacteristics.LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION)?.toList() ?: emptyList()
    val zoomRange = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) c.get(CameraCharacteristics.CONTROL_ZOOM_RATIO_RANGE) else null

    return mapOf(
      "id" to id,
      "lensFacing" to when (c.get(CameraCharacteristics.LENS_FACING)) {
        CameraMetadata.LENS_FACING_BACK -> "back"
        CameraMetadata.LENS_FACING_FRONT -> "front"
        CameraMetadata.LENS_FACING_EXTERNAL -> "external"
        else -> "unknown"
      },
      "hardwareLevel" to when (c.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL)) {
        CameraMetadata.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY -> "legacy"
        CameraMetadata.INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED -> "limited"
        CameraMetadata.INFO_SUPPORTED_HARDWARE_LEVEL_FULL -> "full"
        CameraMetadata.INFO_SUPPORTED_HARDWARE_LEVEL_3 -> "level3"
        CameraMetadata.INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL -> "external"
        else -> "unknown"
      },
      "sensorMegapixels" to pixelArray?.let { it.width.toDouble() * it.height / 1_000_000.0 },
      "maxJpegSize" to maxJpeg?.let { mapOf("width" to it.width, "height" to it.height) },
      "controls" to mapOf(
        "manualSensor" to has(CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR),
        "manualPostProcessing" to has(CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING),
        "raw" to has(CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_RAW),
        "burstCapture" to has(CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_BURST_CAPTURE),
        "depthOutput" to has(CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_DEPTH_OUTPUT),
        "logicalMultiCamera" to has(CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA),
        "manualFocus" to ((c.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE) ?: 0f) > 0f),
        "autoFocus" to afModes.any { it != CameraMetadata.CONTROL_AF_MODE_OFF },
        "flash" to (c.get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true),
        "opticalStabilization" to ois.contains(CameraMetadata.LENS_OPTICAL_STABILIZATION_MODE_ON),
        "exposureCompensation" to (aeRange != null && aeRange.upper > aeRange.lower),
      ),
      "ranges" to mapOf(
        "iso" to iso?.let { listOf(it.lower, it.upper) },
        "exposureTimeNs" to exposure?.let { listOf(it.lower.toDouble(), it.upper.toDouble()) },
        "exposureCompensation" to aeRange?.let { listOf(it.lower, it.upper) },
        "exposureCompensationStep" to aeStep?.toDouble(),
        "zoomRatio" to zoomRange?.let { listOf(it.lower.toDouble(), it.upper.toDouble()) },
        "maxDigitalZoom" to c.get(CameraCharacteristics.SCALER_AVAILABLE_MAX_DIGITAL_ZOOM)?.toDouble(),
        "minFocusDistanceDiopters" to c.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE)?.toDouble(),
      ),
      "focalLengthsMm" to (c.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS)?.map { it.toDouble() } ?: emptyList()),
      "apertures" to (c.get(CameraCharacteristics.LENS_INFO_AVAILABLE_APERTURES)?.map { it.toDouble() } ?: emptyList()),
      "outputFormats" to listOfNotNull(
        "jpeg".takeIf { outputFormats.contains(ImageFormat.JPEG) },
        "yuv".takeIf { outputFormats.contains(ImageFormat.YUV_420_888) },
        "raw".takeIf { outputFormats.contains(ImageFormat.RAW_SENSOR) },
        "heic".takeIf { outputFormats.contains(ImageFormat.HEIC) },
        "depth".takeIf { outputFormats.contains(ImageFormat.DEPTH16) },
      ),
    )
  }

  private fun imageFormats(): Map<String, Any?> {
    val codecs = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos
    val hevcEncoder = codecs.any { info -> info.isEncoder && info.supportedTypes.any { it.equals("video/hevc", true) } }
    return mapOf(
      "decode" to listOfNotNull(
        "jpeg", "png", "webp", "gif", "bmp", "heif", "dng",
        "avif".takeIf { Build.VERSION.SDK_INT >= Build.VERSION_CODES.S },
      ),
      "encode" to listOfNotNull(
        "jpeg", "png", "webp",
        // HEIC export needs a hardware HEVC encoder (HeifWriter).
        "heic".takeIf { hevcEncoder },
      ),
      "hevcEncoder" to hevcEncoder,
      "ultraHdr" to (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE),
    )
  }

  private fun acceleration(context: Context): Map<String, Any?> {
    val pm = context.packageManager
    val glEs = DeviceInfo.activityManager(context).deviceConfigurationInfo.reqGlEsVersion
    val glMajor = glEs shr 16
    val glMinor = glEs and 0xFFFF
    val vulkanLevel = pm.systemAvailableFeatures
      .firstOrNull { it.name == PackageManager.FEATURE_VULKAN_HARDWARE_LEVEL }?.version
    val vulkanVersion = pm.systemAvailableFeatures
      .firstOrNull { it.name == PackageManager.FEATURE_VULKAN_HARDWARE_VERSION }?.version
    return mapOf(
      "glEsVersion" to "$glMajor.$glMinor",
      "glEs31" to (glMajor > 3 || (glMajor == 3 && glMinor >= 1)),
      "vulkan" to (vulkanLevel != null),
      "vulkanVersion" to vulkanVersion?.let { "${it shr 22}.${(it shr 12) and 0x3FF}" },
      // NNAPI is present on API 27+ but deprecated from API 35; inference
      // runtimes should prefer GPU delegates when available.
      "nnapi" to true,
      "nnapiDeprecated" to (Build.VERSION.SDK_INT >= 35),
      "renderEffectBlur" to (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S),
    )
  }
}
