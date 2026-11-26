package com.photoplus.i18n

import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import android.os.LocaleList
import androidx.annotation.StringRes
import androidx.appcompat.app.AppCompatDelegate

/**
 * Words from the native side (errors the screens show, notifications) in
 * the app's language: the language chosen in Photo Plus, else the phone's.
 * They are string resources like every other word, so Google Play's
 * translation reaches them. {name} placeholders are filled from [args].
 */
object Words {
  fun get(context: Context, @StringRes id: Int, vararg args: Pair<String, Any?>): String {
    var text = resources(context).getString(id)
    for ((name, value) in args) text = text.replace("{$name}", value?.toString() ?: "")
    return text
  }

  /** The app's resources in its language. */
  // Every language ships in the app (build.gradle: bundle.language.enableSplit = false).
  @android.annotation.SuppressLint("AppBundleLocaleChanges")
  fun resources(context: Context): Resources {
    val tags = AppCompatDelegate.getApplicationLocales().toLanguageTags()
    if (tags.isEmpty()) return context.resources
    val config = Configuration(context.resources.configuration)
    config.setLocales(LocaleList.forLanguageTags(tags))
    return context.createConfigurationContext(config).resources
  }
}
