package com.photoplus.edit

import com.photoplus.studio.FaceFinder
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.sin

/**
 * Fits a prop to a face so it looks worn (the same rules as
 * src/services/edit/placement.ts, in pixels): hats sit where the crown meets
 * the head, glasses centre between the eyes, outfit pieces sit below the
 * chin, sized to the face and turned with the head.
 */
object PropFit {
  /** A point in pixels (plain Kotlin, so the geometry is unit-tested on the JVM). */
  data class Pt(val x: Float, val y: Float)

  enum class Anchor { HEAD, EYES, NECK, FREE }

  /** Where a prop meets the body (from the catalogue, or chosen for a custom prop). */
  data class Spec(val anchor: Anchor, val ax: Float, val ay: Float, val fit: Float, val drop: Float, val aspect: Float)

  /** Centre and width in pixels, rotation in degrees clockwise. */
  data class Placement(val cx: Float, val cy: Float, val width: Float, val rotation: Float)

  /**
   * The person's adjustment on top of the fitted placement, so it follows the
   * face: a shift along the face's own axes (in face widths), a size factor
   * and an extra turn.
   */
  data class Adjust(val du: Float = 0f, val dv: Float = 0f, val scale: Float = 1f, val turn: Float = 0f)

  private class Axes(val rx: Float, val ry: Float, val faceW: Float)

  private fun axes(face: FaceFinder.Geometry): Axes {
    var rx = face.eyeR.x - face.eyeL.x
    var ry = face.eyeR.y - face.eyeL.y
    val d = hypot(rx, ry).coerceAtLeast(1e-3f)
    rx /= d
    ry /= d
    return Axes(rx, ry, face.width)
  }

  fun fit(spec: Spec, face: FaceFinder.Geometry): Placement {
    val a = axes(face)
    val dx = -a.ry
    val dy = a.rx
    val eyeDist = hypot(face.eyeR.x - face.eyeL.x, face.eyeR.y - face.eyeL.y).coerceAtLeast(1e-3f)
    val faceH = max(1e-3f, (face.chin.x - face.top.x) * dx + (face.chin.y - face.top.y) * dy)
    val (anchor, width) = when (spec.anchor) {
      Anchor.EYES -> Pt((face.eyeL.x + face.eyeR.x) / 2f, (face.eyeL.y + face.eyeR.y) / 2f) to spec.fit * eyeDist
      Anchor.HEAD -> face.top to spec.fit * a.faceW
      Anchor.NECK -> face.chin to spec.fit * a.faceW
      Anchor.FREE -> Pt((face.top.x + face.chin.x) / 2f, (face.top.y + face.chin.y) / 2f) to spec.fit * a.faceW
    }
    val ax = anchor.x + dx * spec.drop * faceH
    val ay = anchor.y + dy * spec.drop * faceH
    val height = width * spec.aspect
    val ox = (0.5f - spec.ax) * width
    val oy = (0.5f - spec.ay) * height
    return Placement(
      ax + a.rx * ox + dx * oy,
      ay + a.ry * ox + dy * oy,
      width,
      Math.toDegrees(atan2(a.ry, a.rx).toDouble()).toFloat(),
    )
  }

  /** The fitted placement with the person's adjustment applied. */
  fun adjusted(base: Placement, face: FaceFinder.Geometry, adjust: Adjust): Placement {
    val a = axes(face)
    val dx = -a.ry
    val dy = a.rx
    return Placement(
      base.cx + (a.rx * adjust.du + dx * adjust.dv) * a.faceW,
      base.cy + (a.ry * adjust.du + dy * adjust.dv) * a.faceW,
      base.width * adjust.scale.coerceIn(0.2f, 5f),
      base.rotation + adjust.turn,
    )
  }

  /** The face nearest a point (pixels), e.g. where the prop was in the preview. */
  fun nearest(faces: List<FaceFinder.Geometry>, x: Float, y: Float): FaceFinder.Geometry? = faces.minByOrNull { f ->
    val cx = (f.top.x + f.chin.x) / 2f
    val cy = (f.top.y + f.chin.y) / 2f
    hypot(cx - x, cy - y) / max(1f, f.width)
  }

  /** Rotates a vector by [degrees] clockwise (image axes, y down). */
  fun rotate(x: Float, y: Float, degrees: Float): Pt {
    val t = Math.toRadians(degrees.toDouble())
    return Pt((x * cos(t) - y * sin(t)).toFloat(), (x * sin(t) + y * cos(t)).toFloat())
  }
}
