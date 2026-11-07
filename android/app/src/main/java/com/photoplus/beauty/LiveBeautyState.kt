package com.photoplus.beauty

/**
 * What the live beauty preview needs, shared between JS (the recipe), the
 * analyser (masks, the face, the scene) and the GL thread (which only
 * reads). Each field is replaced whole, so readers never see a half-update.
 */
object LiveBeautyState {
  /** The recipe shown live (null or inactive = the camera as it is). */
  @Volatile var recipe: BeautyRecipe? = null

  /** "Original" held down: show the camera untouched. */
  @Volatile var showOriginal: Boolean = false

  /** The latest analysis (null until the first frame is analysed). */
  @Volatile var analysis: Analysis? = null

  /** Set when the live preview cannot run on this phone (photos are still finished with the recipe). */
  @Volatile var unavailableReason: String? = null

  class Analysis(
    /** Subject mask in preview space, [maskW] x [maskH] bytes (row 0 = top). */
    val mask: ByteArray?,
    val maskW: Int,
    val maskH: Int,
    /** Preview size in analysis pixels (upright crop): the units of [face]. */
    val previewW: Int,
    val previewH: Int,
    /** The largest face's uniforms (preview pixels), or null. */
    val face: LiveBeautyUniforms.Face?,
    /** has skin, skin Y, Cb, Cr, spread, subject x, subject width, light angle, skin luma spread. */
    val scene: FloatArray,
    /** Analysis buffer pixels → preview fractions. */
    val previewFromAnalysis: Affine,
    /** Sensor → analysis buffer pixels (CameraX ImageInfo). */
    val analysisSensorToBuffer: Affine,
    val timestampMs: Long,
  )

  /** A scene with no measured skin (the grade then spares nothing). */
  val NO_SCENE = floatArrayOf(0f, 0.5f, -0.05f, 0.08f, 0.03f, 0.5f, 0.5f, 0.35f, 0.06f)

  fun reset() {
    analysis = null
    showOriginal = false
  }
}
