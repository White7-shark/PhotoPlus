package com.photoplus

import android.os.Bundle
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
    super.onCreate(null)
  }
}
