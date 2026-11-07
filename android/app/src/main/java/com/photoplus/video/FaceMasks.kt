package com.photoplus.video

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import androidx.core.graphics.createBitmap
import com.photoplus.studio.FaceLandmarks

/** Face areas and protected features (eyes, brows, lips) drawn from landmarks, as ALPHA_8 masks of [w] x [h]. */
object FaceMasks {
  fun draw(faces: List<FaceLandmarks>, w: Int, h: Int): Pair<Bitmap, Bitmap> {
    val face = createBitmap(w, h, Bitmap.Config.ALPHA_8)
    val protect = createBitmap(w, h, Bitmap.Config.ALPHA_8)
    val fc = Canvas(face)
    val pc = Canvas(protect)
    val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    for (f in faces) {
      fun pts(c: FaceLandmarks.Contour) = (0 until c.count).map { f.point(c.first + it) }
      fun path(points: List<com.photoplus.edit.PropFit.Pt>) = Path().apply {
        points.forEachIndexed { i, p -> if (i == 0) moveTo(p.x * w, p.y * h) else lineTo(p.x * w, p.y * h) }
        close()
      }
      fc.drawPath(path(pts(FaceLandmarks.OVAL)), fill)
      val size = f.width() * w
      val pad = Paint(fill).apply { style = Paint.Style.FILL_AND_STROKE; strokeWidth = size * 0.05f; strokeJoin = Paint.Join.ROUND }
      pc.drawPath(path(pts(FaceLandmarks.LEFT_EYE)), pad)
      pc.drawPath(path(pts(FaceLandmarks.RIGHT_EYE)), pad)
      pc.drawPath(path(joined(pts(FaceLandmarks.LEFT_BROW_TOP), pts(FaceLandmarks.LEFT_BROW_BOTTOM))), pad)
      pc.drawPath(path(joined(pts(FaceLandmarks.RIGHT_BROW_TOP), pts(FaceLandmarks.RIGHT_BROW_BOTTOM))), pad)
      pc.drawPath(path(joined(pts(FaceLandmarks.UPPER_LIP_TOP), pts(FaceLandmarks.LOWER_LIP_BOTTOM))), pad)
    }
    return face to protect
  }

  /** Two contours as one closed outline, whatever direction each runs in. */
  fun joined(a: List<com.photoplus.edit.PropFit.Pt>, b: List<com.photoplus.edit.PropFit.Pt>): List<com.photoplus.edit.PropFit.Pt> {
    fun d2(p: com.photoplus.edit.PropFit.Pt, q: com.photoplus.edit.PropFit.Pt) = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y)
    if (a.isEmpty() || b.isEmpty()) return a + b
    return a + if (d2(a.last(), b.last()) < d2(a.last(), b.first())) b.reversed() else b
  }
}
