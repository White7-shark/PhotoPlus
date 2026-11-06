package com.photoplus.studio

import com.photoplus.edit.PropFit.Pt
import kotlin.math.hypot

/**
 * One face's landmarks in the engine's layout (PP_FACE_* in pp_engine.h):
 * 131 points, x and y normalised to the image they were found in. The layout
 * follows on-device face contour detection, contour by contour.
 *
 * Pure Kotlin (unit-tested): building from contours (resampled to the exact
 * counts the engine expects), approximating from a few landmarks when a face
 * came without contours, and moving between image orientations.
 */
class FaceLandmarks(val points: FloatArray, val detailed: Boolean) {
  init {
    require(points.size == FLOATS) { "a face needs $FLOATS values" }
  }

  fun point(i: Int) = Pt(points[2 * i], points[2 * i + 1])

  /** The same face in another orientation of the image ([map] takes and returns normalised points). */
  fun mapped(map: (Float, Float) -> Pair<Float, Float>): FaceLandmarks {
    val out = FloatArray(FLOATS)
    for (i in 0 until POINTS) {
      val (x, y) = map(points[2 * i], points[2 * i + 1])
      out[2 * i] = x
      out[2 * i + 1] = y
    }
    return FaceLandmarks(out, detailed)
  }

  /** Width of the face outline as a fraction of the image width. */
  fun width(): Float {
    var lo = Float.MAX_VALUE
    var hi = -Float.MAX_VALUE
    for (i in OVAL.first until OVAL.first + OVAL.count) {
      lo = minOf(lo, points[2 * i])
      hi = maxOf(hi, points[2 * i])
    }
    return hi - lo
  }

  /** A contour of the layout: where it starts and how many points it has. */
  data class Contour(val first: Int, val count: Int)

  companion object {
    val OVAL = Contour(0, 36)
    val LEFT_BROW_TOP = Contour(36, 5)
    val LEFT_BROW_BOTTOM = Contour(41, 5)
    val RIGHT_BROW_TOP = Contour(46, 5)
    val RIGHT_BROW_BOTTOM = Contour(51, 5)
    val LEFT_EYE = Contour(56, 16)
    val RIGHT_EYE = Contour(72, 16)
    val UPPER_LIP_TOP = Contour(88, 11)
    val UPPER_LIP_BOTTOM = Contour(99, 9)
    val LOWER_LIP_TOP = Contour(108, 9)
    val LOWER_LIP_BOTTOM = Contour(117, 9)
    val NOSE_BRIDGE = Contour(126, 2)
    val NOSE_BOTTOM = Contour(128, 3)
    val ALL = listOf(
      OVAL, LEFT_BROW_TOP, LEFT_BROW_BOTTOM, RIGHT_BROW_TOP, RIGHT_BROW_BOTTOM, LEFT_EYE, RIGHT_EYE,
      UPPER_LIP_TOP, UPPER_LIP_BOTTOM, LOWER_LIP_TOP, LOWER_LIP_BOTTOM, NOSE_BRIDGE, NOSE_BOTTOM,
    )
    const val POINTS = 131
    const val FLOATS = POINTS * 2

    /** Faces packed for the engine (NativeEngine.nativeFaceAdjust). */
    fun pack(faces: List<FaceLandmarks>): Pair<FloatArray, IntArray> {
      val pts = FloatArray(faces.size * FLOATS)
      faces.forEachIndexed { i, f -> f.points.copyInto(pts, i * FLOATS) }
      return pts to IntArray(faces.size) { if (faces[it].detailed) 1 else 0 }
    }

    /**
     * From measured contours in pixels of a [w] x [h] image. Each contour is
     * resampled along its length to the layout's count (detectors may return
     * a slightly different number). Null if a contour is missing.
     */
    fun fromContours(contours: Map<Contour, List<Pt>>, w: Float, h: Float): FaceLandmarks? {
      val out = FloatArray(FLOATS)
      for (c in ALL) {
        val pts = contours[c]?.takeIf { it.isNotEmpty() } ?: return null
        val closed = c == OVAL || c == LEFT_EYE || c == RIGHT_EYE
        resample(pts, c.count, closed).forEachIndexed { k, p ->
          out[2 * (c.first + k)] = p.x / w
          out[2 * (c.first + k) + 1] = p.y / h
        }
      }
      return FaceLandmarks(out, detailed = true)
    }

    /**
     * Approximate outlines from a face box and a few landmarks (pixels),
     * for faces detected without contours. Shapes follow average face
     * proportions around the measured eyes and mouth; the engine treats them
     * more gently (no precise feature edits are promised from a guess).
     */
    fun approximate(
      left: Float, top: Float, right: Float, bottom: Float,
      eyeL: Pt?, eyeR: Pt?, mouthL: Pt?, mouthR: Pt?, mouthBottom: Pt?,
      w: Float, h: Float,
    ): FaceLandmarks {
      val bw = right - left
      val bh = bottom - top
      val cx = (left + right) / 2f
      val el = eyeL ?: Pt(left + 0.32f * bw, top + 0.40f * bh)
      val er = eyeR ?: Pt(left + 0.68f * bw, top + 0.40f * bh)
      val (a, b) = if (el.x <= er.x) el to er else er to el
      val eyeDist = hypot(b.x - a.x, b.y - a.y).coerceAtLeast(1f)
      val ml = mouthL ?: Pt(cx - 0.38f * eyeDist, top + 0.75f * bh)
      val mr = mouthR ?: Pt(cx + 0.38f * eyeDist, top + 0.75f * bh)
      val (mA, mB) = if (ml.x <= mr.x) ml to mr else mr to ml
      val mcx = (mA.x + mB.x) / 2f
      val mcy = (mA.y + mB.y) / 2f
      val mw = (mB.x - mA.x).coerceAtLeast(0.3f * eyeDist) / 2f
      val mh = ((mouthBottom?.y ?: (mcy + 0.18f * eyeDist)) - mcy).coerceIn(0.08f * eyeDist, 0.35f * eyeDist)
      val c = mutableMapOf<Contour, List<Pt>>()
      c[OVAL] = ellipse(cx, top + 0.5f * bh, bw * 0.5f, bh * 0.55f, 36, startTop = true)
      val ew = 0.55f * eyeDist / 2f
      val eh = 0.28f * ew
      c[LEFT_EYE] = ellipse(b.x, b.y, ew, eh, 16)
      c[RIGHT_EYE] = ellipse(a.x, a.y, ew, eh, 16)
      fun brow(e: Pt, topEdge: Boolean): List<Pt> {
        val y = e.y - 0.45f * eyeDist + if (topEdge) -0.05f * eyeDist else 0f
        return List(5) { k -> Pt(e.x - 1.1f * ew + 2.2f * ew * k / 4f, y) }
      }
      c[LEFT_BROW_TOP] = brow(b, true)
      c[LEFT_BROW_BOTTOM] = brow(b, false)
      c[RIGHT_BROW_TOP] = brow(a, true)
      c[RIGHT_BROW_BOTTOM] = brow(a, false)
      fun arc(ry: Float, n: Int, upper: Boolean, inner: Boolean): List<Pt> {
        val rx = if (inner) mw * 0.8f else mw
        val from = if (inner) 0.1 else 0.0
        val to = if (inner) 0.9 else 1.0
        return List(n) { k ->
          val t = Math.PI * (from + (to - from) * k / (n - 1))
          val sx = -kotlin.math.cos(t).toFloat()
          val sy = kotlin.math.sin(t).toFloat()
          Pt(mcx + rx * sx, mcy + (if (upper) -1f else 1f) * ry * sy)
        }
      }
      // A closed mouth: inner lip lines meet (no teeth are promised).
      c[UPPER_LIP_TOP] = arc(0.45f * mh, 11, upper = true, inner = false)
      c[UPPER_LIP_BOTTOM] = arc(0.02f * mh, 9, upper = true, inner = true)
      c[LOWER_LIP_TOP] = arc(0.02f * mh, 9, upper = false, inner = true)
      c[LOWER_LIP_BOTTOM] = arc(mh, 9, upper = false, inner = false)
      val noseY = (a.y + b.y) / 2f + 0.55f * eyeDist
      c[NOSE_BRIDGE] = listOf(Pt(cx, (a.y + b.y) / 2f), Pt(cx, noseY))
      c[NOSE_BOTTOM] = listOf(Pt(cx - 0.18f * eyeDist, noseY), Pt(cx, noseY + 0.05f * eyeDist), Pt(cx + 0.18f * eyeDist, noseY))
      return fromContours(c, w, h)!!.let { FaceLandmarks(it.points, detailed = false) }
    }

    private fun ellipse(cx: Float, cy: Float, rx: Float, ry: Float, n: Int, startTop: Boolean = false): List<Pt> =
      List(n) { k ->
        val t = 2.0 * Math.PI * k / n + if (startTop) -Math.PI / 2 else 0.0
        Pt(cx + rx * kotlin.math.cos(t).toFloat(), cy + ry * kotlin.math.sin(t).toFloat())
      }

    /** [n] points evenly spaced along the polyline (closed: around the loop). */
    fun resample(pts: List<Pt>, n: Int, closed: Boolean): List<Pt> {
      if (pts.size == n) return pts
      if (pts.size == 1) return List(n) { pts[0] }
      val path = if (closed) pts + pts[0] else pts
      val cum = FloatArray(path.size)
      for (i in 1 until path.size) cum[i] = cum[i - 1] + hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y)
      val total = cum.last()
      if (total <= 0f) return List(n) { pts[0] }
      val steps = if (closed) n else n - 1
      return List(n) { k ->
        val d = total * k / steps
        var j = 1
        while (j < path.size - 1 && cum[j] < d) j++
        val seg = (cum[j] - cum[j - 1]).coerceAtLeast(1e-6f)
        val t = ((d - cum[j - 1]) / seg).coerceIn(0f, 1f)
        Pt(path[j - 1].x + (path[j].x - path[j - 1].x) * t, path[j - 1].y + (path[j].y - path[j - 1].y) * t)
      }
    }
  }
}
