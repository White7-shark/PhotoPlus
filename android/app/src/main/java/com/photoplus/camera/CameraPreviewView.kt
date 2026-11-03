package com.photoplus.camera

import android.annotation.SuppressLint
import android.view.Choreographer
import android.widget.FrameLayout
import androidx.camera.view.PreviewView
import androidx.lifecycle.LifecycleOwner
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.WritableMap
import com.facebook.react.uimanager.ThemedReactContext
import com.facebook.react.uimanager.UIManagerHelper
import com.facebook.react.uimanager.events.Event

/**
 * Native view backing <PhotoPlusCameraPreview />. Only ever created by
 * CameraPreviewManager (never inflated from XML), so the XML constructors
 * Lint asks for would be unusable: the view needs a ThemedReactContext.
 */
@SuppressLint("ViewConstructor")
class CameraPreviewView(private val reactContext: ThemedReactContext) : FrameLayout(reactContext), CameraSession.Callbacks {

  private val previewView = PreviewView(reactContext).apply {
    implementationMode = PreviewView.ImplementationMode.COMPATIBLE
    scaleType = PreviewView.ScaleType.FILL_CENTER
  }

  var lensFacing: String = "back"
    set(value) {
      if (field != value) {
        field = value
        rebind()
      }
    }

  var active: Boolean = true
    set(value) {
      if (field != value) {
        field = value
        rebind()
      }
    }

  init {
    addView(previewView, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
  }

  override fun onAttachedToWindow() {
    super.onAttachedToWindow()
    rebind()
  }

  override fun onDetachedFromWindow() {
    CameraSession.unbind(context, this)
    super.onDetachedFromWindow()
  }

  private fun rebind() {
    if (!isAttachedToWindow) return
    if (!active) {
      CameraSession.unbind(context, this)
      return
    }
    val owner = reactContext.currentActivity as? LifecycleOwner
    if (owner == null) {
      onError("NO_ACTIVITY", "Camera preview requires a foreground activity")
      return
    }
    CameraSession.bind(context, owner, previewView, lensFacing, this, this)
  }

  // React Native lays out views itself and does not re-layout native children
  // that request it; without this the PreviewView's surface stays 0x0.
  private var layoutScheduled = false
  private val layoutCallback = Choreographer.FrameCallback {
    layoutScheduled = false
    measure(
      MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY),
      MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY),
    )
    layout(left, top, right, bottom)
  }

  override fun requestLayout() {
    super.requestLayout()
    if (!layoutScheduled) {
      layoutScheduled = true
      Choreographer.getInstance().postFrameCallback(layoutCallback)
    }
  }

  override fun onReady(lensFacing: String) {
    dispatch(CameraEvent(UIManagerHelper.getSurfaceId(this), id, CameraEvent.READY, Arguments.createMap().apply {
      putString("lensFacing", lensFacing)
    }))
  }

  override fun onError(code: String, message: String) {
    dispatch(CameraEvent(UIManagerHelper.getSurfaceId(this), id, CameraEvent.ERROR, Arguments.createMap().apply {
      putString("code", code)
      putString("message", message)
    }))
  }

  private fun dispatch(event: CameraEvent) {
    UIManagerHelper.getEventDispatcher(reactContext)?.dispatchEvent(event)
  }
}

class CameraEvent(surfaceId: Int, viewId: Int, private val name: String, private val payload: WritableMap) :
  Event<CameraEvent>(surfaceId, viewId) {
  override fun getEventName() = name
  override fun getEventData(): WritableMap = payload

  companion object {
    const val READY = "topCameraReady"
    const val ERROR = "topCameraError"
  }
}
