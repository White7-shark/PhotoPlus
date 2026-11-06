package com.photoplus.camera

import android.graphics.Bitmap
import com.photoplus.edit.PhotoWatermark
import com.photoplus.edit.PropArt
import com.photoplus.edit.PropFit
import com.photoplus.engine.ImageCodec
import com.photoplus.engine.NativeEngine
import com.photoplus.engine.OutputFormat
import com.photoplus.studio.FaceFinder
import com.photoplus.studio.MaskGeometry
import java.io.File

/**
 * Props worn in the camera (like a live lens): the person sees a prop
 * following their face in the preview, and the photo gets it composited into
 * its pixels by the engine (light, colour, softness, grain and a contact
 * shadow matched to the scene), before any watermark.
 *
 * The preview position is what the person saw. When the prop was following a
 * face, it is fitted again to that face in the photo itself (the person may
 * have moved since the frame they saw), keeping their own adjustment.
 */
object CameraProps {
  const val MAX_PROPS = 6

  /** One prop as placed in the preview (preview fractions, rotation clockwise). */
  data class Placed(
    val id: String,
    val cx: Float,
    val cy: Float,
    val width: Float,
    val rotation: Float,
    val flip: Boolean,
    val spec: PropFit.Spec,
    /** Present when the prop was following a face. */
    val adjust: PropFit.Adjust?,
  )

  fun parse(raw: Any?): List<Placed> {
    val list = raw as? List<*> ?: return emptyList()
    return list.take(MAX_PROPS).mapNotNull { item ->
      val m = item as? Map<*, *> ?: return@mapNotNull null
      val id = m["id"] as? String ?: return@mapNotNull null
      if (!com.photoplus.edit.EditDocument.isKnownProp(id)) return@mapNotNull null
      fun num(key: String, min: Float, max: Float, default: Float): Float {
        val v = (m[key] as? Number)?.toFloat() ?: return default
        return if (v.isFinite()) v.coerceIn(min, max) else default
      }
      val anchor = when (m["anchor"]) {
        "head" -> PropFit.Anchor.HEAD
        "eyes" -> PropFit.Anchor.EYES
        "neck" -> PropFit.Anchor.NECK
        else -> PropFit.Anchor.FREE
      }
      val spec = PropFit.Spec(
        anchor, num("ax", 0f, 1f, 0.5f), num("ay", 0f, 1f, 0.5f), num("fit", 0.1f, 10f, 1.3f),
        num("drop", -2f, 2f, 0f), num("aspect", 0.05f, 5f, 0.5f),
      )
      val adjust = if (m["tracked"] == true) {
        PropFit.Adjust(num("du", -5f, 5f, 0f), num("dv", -5f, 5f, 0f), num("scale", 0.2f, 5f, 1f), num("turn", -180f, 180f, 0f))
      } else {
        null
      }
      Placed(
        id, num("cx", -0.5f, 1.5f, 0.5f), num("cy", -0.5f, 1.5f, 0.5f), num("width", 0.01f, 3f, 0.35f),
        num("rotation", -180f, 180f, 0f), m["flip"] == true, spec, adjust,
      )
    }
  }

  /**
   * Where a prop placed on the preview goes on the upright photo, in pixels.
   * The preview is portrait-locked (and mirrored for the front camera); the
   * photo follows the phone ([deviceDegrees] clockwise) and is mirrored only
   * when the front-camera photo is saved like the preview.
   */
  fun toPhoto(p: Placed, photoW: Int, photoH: Int, deviceDegrees: Int, mirrored: Boolean): Triple<PropFit.Placement, Boolean, Float> {
    val (ux, uy) = MaskGeometry.previewToUpright(p.cx, p.cy, deviceDegrees, mirrored)
    val turned = MaskGeometry.normalise(deviceDegrees) % 180 != 0
    // The preview's horizontal axis is the photo's width, or its height when the phone was turned.
    val widthPx = p.width * if (turned) photoH else photoW
    val baseRotation = if (mirrored) -p.rotation else p.rotation
    val rotation = baseRotation + MaskGeometry.normalise(deviceDegrees)
    return Triple(PropFit.Placement(ux * photoW, uy * photoH, widthPx, wrap(rotation)), p.flip != mirrored, rotation)
  }

  private fun wrap(deg: Float): Float {
    var d = deg % 360f
    if (d > 180f) d -= 360f
    if (d < -180f) d += 360f
    return d
  }

  /**
   * Composites [props] into [file] (re-encoded in place, camera details kept).
   * Returns how many were placed.
   */
  fun applyToFile(file: File, props: List<Placed>, art: PropArt, deviceDegrees: Int, mirrored: Boolean, maxDimension: Int = 8192): Int {
    if (props.isEmpty()) return 0
    val decoded = ImageCodec.decode(file, maxDimension)
    val bmp = if (decoded.bitmap.isMutable) decoded.bitmap else decoded.bitmap.copy(Bitmap.Config.ARGB_8888, true).also { decoded.bitmap.recycle() }
    try {
      bmp.isPremultiplied = false
      val faces = if (props.any { it.adjust != null }) FaceFinder.findGeometry(bmp) else emptyList()
      var placed = 0
      for (p in props) {
        val (fromPreview, flip, _) = toPhoto(p, bmp.width, bmp.height, deviceDegrees, mirrored)
        val face = p.adjust?.let { PropFit.nearest(faces, fromPreview.cx, fromPreview.cy) }
        val at = if (face != null && p.adjust != null) PropFit.adjusted(PropFit.fit(p.spec, face), face, p.adjust) else fromPreview
        val params = floatArrayOf(at.cx, at.cy, at.width, at.rotation, if (flip) 1f else 0f, 1f, 1f, 1f)
        NativeEngine.nativePlaceProp(bmp, art.load(p.id), params, 0L)
        placed++
      }
      val tmp = File(file.parentFile, "${file.nameWithoutExtension}.props.tmp")
      ImageCodec.encode(bmp, tmp, OutputFormat.JPEG, 95)
      PhotoWatermark.copyExif(file, tmp)
      if (!tmp.renameTo(file)) {
        tmp.copyTo(file, overwrite = true)
        tmp.delete()
      }
      return placed
    } finally {
      bmp.recycle()
    }
  }
}
