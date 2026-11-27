package com.photoplus.studio

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Choreographer
import android.view.View
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.ReactContext
import com.facebook.react.bridge.WritableMap
import com.facebook.react.uimanager.UIManagerHelper
import com.facebook.react.uimanager.events.Event
import com.photoplus.engine.ImageCodec
import com.photoplus.engine.NativeEngine
import java.io.File
import java.util.concurrent.Executors
import kotlin.math.min

/**
 * The live studio: the blank sheet, and a pencil (or a brush) moving over
 * it, each movement revealing the finished work beneath, until the work is
 * complete (com.photoplus.engine art reveal). Frames are drawn by the
 * engine as the display refreshes — a few milliseconds each, forward only —
 * so the drawing runs smoothly at the phone's own frame rate.
 */
class ArtRevealView(context: Context) : View(context), Choreographer.FrameCallback {
  var work: String? = null
    set(v) {
      if (field != v) {
        field = v
        scheduleLoad()
      }
    }
  var blank: String? = null
    set(v) {
      if (field != v) {
        field = v
        scheduleLoad()
      }
    }
  var tool = 0
  var artist = 0
  var inner = floatArrayOf(0f, 0f, 1f, 1f)
  var durationMs = 7000
  var playing = true
    set(v) {
      field = v
      if (v) resume() else pause()
    }
  /** Jump to the end (the finished work) at once. */
  var finish = false
    set(v) {
      field = v
      if (v && handle != 0L) {
        elapsedBefore = durationMs.toLong()
        startedAt = SystemClock.uptimeMillis()
        Choreographer.getInstance().postFrameCallback(this)
      }
    }

  private val paint = Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG)
  private val main = Handler(Looper.getMainLooper())
  private var handle = 0L
  private var frame: Bitmap? = null
  private var startedAt = 0L
  private var elapsedBefore = 0L
  private var running = false
  private var done = false
  private var loadToken = 0
  private val dest = RectF()

  private fun scheduleLoad() {
    val w = work ?: return
    val b = blank ?: return
    val token = ++loadToken
    release()
    loader.execute {
      try {
        // On-screen size: sharp on any phone, a few milliseconds a frame.
        val workBmp = decode(w)
        val blankBmp = decode(b).let { bb ->
          if (bb.width == workBmp.width && bb.height == workBmp.height) bb
          else Bitmap.createScaledBitmap(bb, workBmp.width, workBmp.height, true).also { if (it !== bb) bb.recycle() }
        }
        val h = try {
          NativeEngine.nativeArtRevealCreate(workBmp, blankBmp, tool, artist, inner, 7)
        } finally {
          workBmp.recycle()
          blankBmp.recycle()
        }
        val strokes = NativeEngine.nativeArtRevealStrokes(h)
        val f = Bitmap.createBitmap(workBmp.width, workBmp.height, Bitmap.Config.ARGB_8888)
        main.post {
          if (token != loadToken || !isAttachedToWindow) {
            NativeEngine.nativeArtRevealDestroy(h)
            f.recycle()
            return@post
          }
          handle = h
          frame = f
          done = false
          elapsedBefore = 0
          NativeEngine.nativeArtRevealFrame(h, 0f, f)
          dispatch(EVENT_READY) { putInt("strokes", strokes) }
          invalidate()
          if (playing) resume()
        }
      } catch (e: Throwable) {
        main.post { if (token == loadToken) dispatch(EVENT_ERROR) { putString("message", e.message ?: "The studio couldn't start") } }
      }
    }
  }

  private fun decode(path: String): Bitmap {
    val d = ImageCodec.decode(File(path), MAX_EDGE).bitmap
    return if (d.config == Bitmap.Config.ARGB_8888 && d.isMutable) d else d.copy(Bitmap.Config.ARGB_8888, true).also { d.recycle() }
  }

  private fun resume() {
    if (handle == 0L || running || done) return
    running = true
    startedAt = SystemClock.uptimeMillis()
    Choreographer.getInstance().postFrameCallback(this)
  }

  private fun pause() {
    if (!running) return
    elapsedBefore += SystemClock.uptimeMillis() - startedAt
    running = false
    Choreographer.getInstance().removeFrameCallback(this)
  }

  override fun doFrame(frameTimeNanos: Long) {
    val h = handle
    val f = frame ?: return
    if (h == 0L) return
    val elapsed = elapsedBefore + (if (running) SystemClock.uptimeMillis() - startedAt else 0L)
    val t = min(1f, elapsed.toFloat() / durationMs.coerceAtLeast(500))
    NativeEngine.nativeArtRevealFrame(h, t, f)
    invalidate()
    if (t >= 1f) {
      running = false
      if (!done) {
        done = true
        dispatch(EVENT_DONE) { putBoolean("done", true) }
      }
    } else if (running || finish) {
      Choreographer.getInstance().postFrameCallback(this)
    }
  }

  override fun onDraw(canvas: Canvas) {
    super.onDraw(canvas)
    val f = frame ?: return
    // Fitted inside the view, centred.
    val s = min(width.toFloat() / f.width, height.toFloat() / f.height)
    val dw = f.width * s
    val dh = f.height * s
    dest.set((width - dw) / 2, (height - dh) / 2, (width + dw) / 2, (height + dh) / 2)
    canvas.drawBitmap(f, null, dest, paint)
  }

  override fun onAttachedToWindow() {
    super.onAttachedToWindow()
    if (handle == 0L) scheduleLoad()
  }

  override fun onDetachedFromWindow() {
    super.onDetachedFromWindow()
    loadToken++
    release()
  }

  private fun release() {
    Choreographer.getInstance().removeFrameCallback(this)
    running = false
    if (handle != 0L) NativeEngine.nativeArtRevealDestroy(handle)
    handle = 0L
    frame?.recycle()
    frame = null
  }

  private fun dispatch(name: String, fill: WritableMap.() -> Unit) {
    val ctx = context as? ReactContext ?: return
    val surface = UIManagerHelper.getSurfaceId(this)
    val payload = Arguments.createMap().apply(fill)
    UIManagerHelper.getEventDispatcherForReactTag(ctx, id)?.dispatchEvent(RevealEvent(surface, id, name, payload))
  }

  private class RevealEvent(surfaceId: Int, viewId: Int, private val name: String, private val payload: WritableMap) :
    Event<RevealEvent>(surfaceId, viewId) {
    override fun getEventName() = name
    override fun getEventData() = payload
  }

  companion object {
    const val EVENT_READY = "topRevealReady"
    const val EVENT_DONE = "topRevealDone"
    const val EVENT_ERROR = "topRevealError"
    private const val MAX_EDGE = 1080
    private val loader = Executors.newSingleThreadExecutor { r -> Thread(r, "art-reveal").apply { isDaemon = true } }
  }
}
