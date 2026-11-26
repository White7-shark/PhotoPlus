package com.photoplus.video

import android.content.Context
import android.media.MediaPlayer
import android.net.Uri
import android.view.Gravity
import android.widget.FrameLayout
import android.widget.VideoView
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.ReactContext
import com.facebook.react.uimanager.UIManagerHelper
import com.facebook.react.uimanager.events.Event
import java.io.File

/**
 * A looping player for local clips (Glide results), letterboxed inside the
 * view — or, with [cover], filling it (the clip's edges cropped), as for the
 * Lucid theme's background clip.
 */
class VideoPlayerView(context: Context) : FrameLayout(context) {
  private val video = VideoView(context)
  private var player: MediaPlayer? = null
  private var source: String? = null
  var loop = true
    set(v) {
      field = v
      player?.isLooping = v
    }
  var muted = false
    set(v) {
      field = v
      val vol = if (v) 0f else 1f
      runCatching { player?.setVolume(vol, vol) }
    }
  var paused = false
    set(v) {
      field = v
      applyPaused()
    }
  var cover = false
    set(v) {
      field = v
      fitVideo()
    }
  private var clipW = 0
  private var clipH = 0

  init {
    clipChildren = true
    addView(video, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT, Gravity.CENTER))
    video.setOnPreparedListener { mp ->
      player = mp
      clipW = mp.videoWidth
      clipH = mp.videoHeight
      fitVideo()
      mp.isLooping = loop
      val vol = if (muted) 0f else 1f
      mp.setVolume(vol, vol)
      dispatch(VideoEvent.READY) {
        putDouble("durationMs", mp.duration.toDouble())
        putInt("width", mp.videoWidth)
        putInt("height", mp.videoHeight)
      }
      applyPaused()
    }
    video.setOnErrorListener { _, what, extra ->
      dispatch(VideoEvent.ERROR) { putString("message", "Playback failed ($what/$extra)") }
      true
    }
  }

  fun setSource(path: String?) {
    if (path == source) return
    source = path
    player = null
    if (path.isNullOrEmpty() || !File(path).isFile) {
      video.stopPlayback()
      return
    }
    video.setVideoURI(Uri.fromFile(File(path)))
  }

  /** Letterboxed (VideoView's own fit), or scaled to cover the view and centred (the parent clips the rest). */
  private fun fitVideo() {
    val lp = video.layoutParams as LayoutParams
    if (cover && clipW > 0 && clipH > 0 && width > 0 && height > 0) {
      val k = maxOf(width.toFloat() / clipW, height.toFloat() / clipH)
      lp.width = Math.ceil((clipW * k).toDouble()).toInt()
      lp.height = Math.ceil((clipH * k).toDouble()).toInt()
    } else {
      lp.width = LayoutParams.MATCH_PARENT
      lp.height = LayoutParams.MATCH_PARENT
    }
    lp.gravity = Gravity.CENTER
    video.layoutParams = lp
  }

  override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
    super.onSizeChanged(w, h, oldw, oldh)
    if (cover) post { fitVideo() }
  }

  private fun applyPaused() {
    if (paused) video.pause() else video.start()
  }

  override fun onDetachedFromWindow() {
    video.stopPlayback()
    player = null
    super.onDetachedFromWindow()
  }

  // React Native lays children out itself; re-run the native pass when the
  // VideoView learns the clip's size (otherwise it keeps a zero size).
  private val relayout = Runnable {
    measure(MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY))
    layout(left, top, right, bottom)
  }

  override fun requestLayout() {
    super.requestLayout()
    post(relayout)
  }

  private fun dispatch(name: String, fill: com.facebook.react.bridge.WritableMap.() -> Unit) {
    val ctx = context as? ReactContext ?: return
    val surface = UIManagerHelper.getSurfaceId(this)
    val payload = Arguments.createMap().apply(fill)
    UIManagerHelper.getEventDispatcherForReactTag(ctx, id)?.dispatchEvent(VideoEvent(surface, id, name, payload))
  }

  private class VideoEvent(surfaceId: Int, viewId: Int, private val name: String, private val payload: com.facebook.react.bridge.WritableMap) :
    Event<VideoEvent>(surfaceId, viewId) {
    override fun getEventName() = name
    override fun getEventData() = payload

    companion object {
      const val READY = "topVideoReady"
      const val ERROR = "topVideoError"
    }
  }

  companion object {
    const val EVENT_READY = VideoEvent.READY
    const val EVENT_ERROR = VideoEvent.ERROR
  }
}
