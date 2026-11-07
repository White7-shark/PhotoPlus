package com.photoplus.video

import com.facebook.react.module.annotations.ReactModule
import com.facebook.react.uimanager.SimpleViewManager
import com.facebook.react.uimanager.ThemedReactContext
import com.facebook.react.uimanager.ViewManagerDelegate
import com.facebook.react.viewmanagers.PhotoPlusVideoViewManagerDelegate
import com.facebook.react.viewmanagers.PhotoPlusVideoViewManagerInterface

@ReactModule(name = VideoPlayerManager.NAME)
class VideoPlayerManager : SimpleViewManager<VideoPlayerView>(), PhotoPlusVideoViewManagerInterface<VideoPlayerView> {
  private val delegate = PhotoPlusVideoViewManagerDelegate(this)

  override fun getDelegate(): ViewManagerDelegate<VideoPlayerView> = delegate
  override fun getName() = NAME
  override fun createViewInstance(context: ThemedReactContext) = VideoPlayerView(context)

  override fun setSource(view: VideoPlayerView, value: String?) = view.setSource(value)
  override fun setPaused(view: VideoPlayerView, value: Boolean) {
    view.paused = value
  }
  override fun setLoop(view: VideoPlayerView, value: Boolean) {
    view.loop = value
  }
  override fun setMuted(view: VideoPlayerView, value: Boolean) {
    view.muted = value
  }

  override fun getExportedCustomDirectEventTypeConstants(): Map<String, Any> = mapOf(
    VideoPlayerView.EVENT_READY to mapOf("registrationName" to "onVideoReady"),
    VideoPlayerView.EVENT_ERROR to mapOf("registrationName" to "onVideoError"),
  )

  companion object {
    const val NAME = "PhotoPlusVideoView"
  }
}
