package com.photoplus.studio

import com.facebook.react.bridge.ReadableArray
import com.facebook.react.module.annotations.ReactModule
import com.facebook.react.uimanager.SimpleViewManager
import com.facebook.react.uimanager.ThemedReactContext
import com.facebook.react.uimanager.ViewManagerDelegate
import com.facebook.react.viewmanagers.PhotoPlusArtRevealViewManagerDelegate
import com.facebook.react.viewmanagers.PhotoPlusArtRevealViewManagerInterface

/** The live studio's view (JS: src/native/specs/PhotoPlusArtRevealViewNativeComponent.ts). */
@ReactModule(name = ArtRevealManager.NAME)
class ArtRevealManager : SimpleViewManager<ArtRevealView>(), PhotoPlusArtRevealViewManagerInterface<ArtRevealView> {
  private val delegate = PhotoPlusArtRevealViewManagerDelegate(this)

  override fun getDelegate(): ViewManagerDelegate<ArtRevealView> = delegate
  override fun getName() = NAME
  override fun createViewInstance(context: ThemedReactContext) = ArtRevealView(context)

  override fun setWork(view: ArtRevealView, value: String?) {
    view.work = value
  }
  override fun setBlank(view: ArtRevealView, value: String?) {
    view.blank = value
  }
  override fun setTool(view: ArtRevealView, value: Int) {
    view.tool = value.coerceIn(0, 1)
  }
  override fun setArtist(view: ArtRevealView, value: Int) {
    view.artist = value.coerceIn(0, 19)
  }
  override fun setInner(view: ArtRevealView, value: ReadableArray?) {
    if (value == null || value.size() != 4) return
    view.inner = FloatArray(4) { i -> value.getDouble(i).toFloat().coerceIn(0f, 1f) }
  }
  override fun setDurationMs(view: ArtRevealView, value: Int) {
    view.durationMs = value.coerceIn(1000, 40000)
  }
  override fun setPlaying(view: ArtRevealView, value: Boolean) {
    view.playing = value
  }
  override fun setFinish(view: ArtRevealView, value: Boolean) {
    view.finish = value
  }

  override fun getExportedCustomDirectEventTypeConstants(): Map<String, Any> = mapOf(
    ArtRevealView.EVENT_READY to mapOf("registrationName" to "onRevealReady"),
    ArtRevealView.EVENT_DONE to mapOf("registrationName" to "onRevealDone"),
    ArtRevealView.EVENT_ERROR to mapOf("registrationName" to "onRevealError"),
  )

  companion object {
    const val NAME = "PhotoPlusArtRevealView"
  }
}
