package com.photoplus

import android.os.Bundle
import androidx.activity.enableEdgeToEdge
import com.facebook.react.ReactActivity
import com.facebook.react.ReactActivityDelegate
import com.facebook.react.defaults.DefaultReactActivityDelegate

class MainActivity : ReactActivity() {

  /** Name of the root component registered from JavaScript (index.js). */
  override fun getMainComponentName(): String = "PhotoPlus"

  override fun createReactActivityDelegate(): ReactActivityDelegate =
    DefaultReactActivityDelegate(this, mainComponentName)

  /**
   * react-native-screens cannot restore its fragments after process death.
   * Passing null makes a restored activity start cleanly instead of crashing,
   * while JS state is re-hydrated from the local database during bootstrap.
   */
  override fun onCreate(savedInstanceState: Bundle?) {
    // Edge to edge on every Android version (required from Android 15, the
    // only mode from Android 16): the app draws behind the status and
    // navigation bars and keeps its content clear of them with the system's
    // insets (react-native-safe-area-context). AndroidX's API, not the
    // deprecated status/navigation bar colours Google Play warns about.
    enableEdgeToEdge()
    super.onCreate(null)
  }
}
