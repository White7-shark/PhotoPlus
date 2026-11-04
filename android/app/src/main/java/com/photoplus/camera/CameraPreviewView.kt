package com.photoplus.camera

import android.annotation.SuppressLint
import android.graphics.Bitmap
import android.graphics.Matrix
import android.view.Choreographer
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.widget.FrameLayout
import android.widget.ImageView
import androidx.camera.view.PreviewView
import androidx.lifecycle.LifecycleOwner
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.WritableMap
import com.facebook.react.uimanager.ThemedReactContext
import com.facebook.react.uimanager.UIManagerHelper
import com.facebook.react.uimanager.events.Event

/**
 * Native view backing <PhotoPlusCameraPreview />: the live preview, the
 * focus-peaking overlay, and the touch gestures that need camera latency
 * (pinch to zoom, tap to focus). Only ever created by CameraPreviewManager,
 * so the XML constructors Lint asks for would be unusable.
 */
@SuppressLint("ViewConstructor")
class CameraPreviewView(private val reactContext: ThemedReactContext) :
  FrameLayout(reactContext), CameraController.SessionCallbacks, FocusPeakingAnalyzer.Overlay {

  private val previewView = PreviewView(reactContext).apply {
    implementationMode = PreviewView.ImplementationMode.COMPATIBLE
    scaleType = PreviewView.ScaleType.FILL_CENTER
  }

  private val peakingView = ImageView(reactContext).apply {
    scaleType = ImageView.ScaleType.MATRIX
    importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
  }
  private val peakingMatrix = Matrix()
  private val matrixValues = FloatArray(9)

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

  var gesturesEnabled: Boolean = true

  init {
    addView(previewView, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
    addView(peakingView, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
  }

  override fun onAttachedToWindow() {
    super.onAttachedToWindow()
    FocusPeakingAnalyzer.overlay = this
    rebind()
  }

  override fun onDetachedFromWindow() {
    if (FocusPeakingAnalyzer.overlay === this) FocusPeakingAnalyzer.overlay = null
    CameraController.unbind(context, this)
    super.onDetachedFromWindow()
  }

  private fun rebind() {
    if (!isAttachedToWindow) return
    if (!active) {
      CameraController.unbind(context, this)
      clearPeaking()
      return
    }
    val owner = reactContext.currentActivity as? LifecycleOwner
    if (owner == null) {
      onError("NO_ACTIVITY", "Camera preview requires a foreground activity")
      return
    }
    CameraController.bind(context, owner, previewView, lensFacing, this, this)
  }

  // ------------------------------------------------------------- gestures

  private val scaleDetector = ScaleGestureDetector(reactContext, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
    private var startRatio = 1f
    private var accumulated = 1f
    override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
      startRatio = CameraController.currentZoomRatio()
      accumulated = 1f
      return true
    }
    override fun onScale(detector: ScaleGestureDetector): Boolean {
      accumulated *= detector.scaleFactor
      CameraController.setZoomRatio(startRatio * accumulated) {}
      return true
    }
  })

  private val tapDetector = GestureDetector(reactContext, object : GestureDetector.SimpleOnGestureListener() {
    override fun onDown(e: MotionEvent) = true
    override fun onSingleTapUp(e: MotionEvent): Boolean {
      if (width == 0 || height == 0) return false
      val x = (e.x / width).coerceIn(0f, 1f)
      val y = (e.y / height).coerceIn(0f, 1f)
      dispatch(CameraEvent(UIManagerHelper.getSurfaceId(this@CameraPreviewView), id, CameraEvent.FOCUS_TAP,
        Arguments.createMap().apply {
          putDouble("x", x.toDouble())
          putDouble("y", y.toDouble())
        }))
      CameraController.focusAt(x, y) {}
      return true
    }
  })

  @SuppressLint("ClickableViewAccessibility") // taps are mirrored by accessible JS controls
  override fun onTouchEvent(event: MotionEvent): Boolean {
    if (!gesturesEnabled || !active) return false
    scaleDetector.onTouchEvent(event)
    if (!scaleDetector.isInProgress) tapDetector.onTouchEvent(event)
    return true
  }

  // ------------------------------------------------------- focus peaking

  override fun showPeaking(bitmap: Bitmap, transform: FloatArray) {
    val w = peakingView.width.toFloat()
    val h = peakingView.height.toFloat()
    if (w == 0f || h == 0f) return
    matrixValues[0] = transform[0] * w; matrixValues[1] = transform[1] * w; matrixValues[2] = transform[2] * w
    matrixValues[3] = transform[3] * h; matrixValues[4] = transform[4] * h; matrixValues[5] = transform[5] * h
    matrixValues[6] = 0f; matrixValues[7] = 0f; matrixValues[8] = 1f
    peakingMatrix.setValues(matrixValues)
    peakingView.imageMatrix = peakingMatrix
    peakingView.setImageBitmap(bitmap)
    peakingView.invalidate() // the same bitmap object is reused with new pixels
  }

  override fun clearPeaking() {
    peakingView.setImageDrawable(null)
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

  // ---------------------------------------------------------------- events

  override fun onReady(lensFacing: String) {
    FocusPeakingAnalyzer.mirrored = lensFacing == "front"
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
    const val FOCUS_TAP = "topFocusTap"
  }
}
