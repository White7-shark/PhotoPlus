package com.photoplus.i18n

import android.annotation.SuppressLint
import android.content.res.Configuration
import android.content.res.Resources
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.UiThreadUtil
import com.facebook.react.bridge.WritableMap
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.specs.NativePhotoPlusI18nSpec
import java.util.Locale

/**
 * The app's words and language. Every string the screens show is an
 * Android resource (the strings.xml of each res/values folder, made by
 * tools/i18n/build-strings.mjs), read here for any language so the JS side
 * shows exactly what Android would, including the translations Google Play
 * adds to the app bundle. The language is Android's per-app language
 * (AppCompat stores it before Android 13; the system does from 13 on).
 */
@ReactModule(name = I18nModule.NAME)
class I18nModule(reactContext: ReactApplicationContext) : NativePhotoPlusI18nSpec(reactContext) {

  override fun getName() = NAME

  override fun getStrings(tag: String): WritableMap {
    val res = resourcesFor(tag.ifEmpty { currentTag() })
    return Arguments.createMap().apply {
      for ((key, id) in STRING_TABLE) putString(key, res.getString(id))
    }
  }

  override fun getLanguage(): String = AppCompatDelegate.getApplicationLocales().toLanguageTags()

  override fun getSystemLanguage(): String =
    Resources.getSystem().configuration.locales.get(0)?.toLanguageTag() ?: Locale.getDefault().toLanguageTag()

  override fun setLanguage(tag: String) {
    if (!TAG.matches(tag)) return
    val locales = if (tag.isEmpty()) LocaleListCompat.getEmptyLocaleList() else LocaleListCompat.forLanguageTags(tag)
    // AppCompat applies it to the activity (it is recreated in the new language).
    UiThreadUtil.runOnUiThread { AppCompatDelegate.setApplicationLocales(locales) }
  }

  private fun currentTag(): String = getLanguage().ifEmpty { getSystemLanguage() }

  /** The app's resources as Android resolves them for [tag] (its fallbacks included). */
  // Every language ships in the app (build.gradle: bundle.language.enableSplit
  // = false), so no language has to be downloaded before it is read.
  @SuppressLint("AppBundleLocaleChanges")
  private fun resourcesFor(tag: String): Resources {
    val ctx = reactApplicationContext
    val config = Configuration(ctx.resources.configuration)
    config.setLocale(Locale.forLanguageTag(tag))
    return ctx.createConfigurationContext(config).resources
  }

  companion object {
    const val NAME = "PhotoPlusI18n"
    private val TAG = Regex("^([A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*)?$")
  }
}
