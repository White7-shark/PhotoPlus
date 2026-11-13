package com.photoplus.pose

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.SystemClock

/**
 * The level: the phone's tilt from gravity, smoothed, about 10 times a second.
 * Uses the gravity sensor (accelerometer as a fallback); nothing else.
 */
object LevelSensor : SensorEventListener {
  fun interface Sink {
    fun onLevel(level: LevelMath.Level)
  }

  @Volatile var sink: Sink? = null
  private var manager: SensorManager? = null
  private val g = FloatArray(3)
  private var primed = false
  private var lastEmit = 0L

  /** Returns false when the phone has no sensor for it. */
  fun start(context: Context): Boolean {
    if (manager != null) return true
    val m = context.getSystemService(Context.SENSOR_SERVICE) as? SensorManager ?: return false
    val sensor = m.getDefaultSensor(Sensor.TYPE_GRAVITY) ?: m.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) ?: return false
    primed = false
    if (!m.registerListener(this, sensor, SensorManager.SENSOR_DELAY_UI)) return false
    manager = m
    return true
  }

  fun stop() {
    manager?.unregisterListener(this)
    manager = null
  }

  override fun onSensorChanged(event: SensorEvent) {
    // Light smoothing (the accelerometer also sees hand movement).
    val k = if (primed) 0.25f else 1f
    for (i in 0 until 3) g[i] += k * (event.values[i] - g[i])
    primed = true
    val now = SystemClock.elapsedRealtime()
    if (now - lastEmit < 100L) return
    lastEmit = now
    LevelMath.fromGravity(g[0], g[1], g[2])?.let { sink?.onLevel(it) }
  }

  override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit
}
