package com.photoplus.ads

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import java.io.File
import java.security.MessageDigest

/**
 * The free plan's watermark-free unlock: watching [REQUIRED] rewarded ads for
 * a photo lets that photo be exported, saved or shared without the mark.
 *
 * Progress is counted here, natively, only from the ad SDK's reward callback,
 * so the app's screens cannot grant it; [ExportPolicy] checks [isUnlocked]
 * at export time. Each photo keeps its own count (people can stop and come
 * back), and only recent photos are remembered.
 */
class AdRewards(private val store: Store) {
  /** Minimal key-value storage (SharedPreferences in the app, a map in tests). */
  interface Store {
    fun getInt(key: String): Int
    fun putInt(key: String, value: Int)
    fun remove(key: String)
    fun keys(): Set<String>
  }

  data class Progress(val watched: Int, val required: Int, val unlocked: Boolean) {
    fun toMap(): Map<String, Any?> = mapOf("watched" to watched, "required" to required, "unlocked" to unlocked)
  }

  fun progress(photo: File): Progress {
    val n = store.getInt(keyFor(photo)).coerceIn(0, REQUIRED)
    return Progress(n, REQUIRED, n >= REQUIRED)
  }

  fun isUnlocked(photo: File): Boolean = progress(photo).unlocked

  /** One ad watched to the end for [photo]. */
  @Synchronized
  fun reward(photo: File): Progress {
    val key = keyFor(photo)
    val n = (store.getInt(key) + 1).coerceAtMost(REQUIRED)
    store.putInt(key, n)
    store.putInt(ORDER_PREFIX + key.removePrefix(PREFIX), (store.getInt(COUNTER) + 1).also { store.putInt(COUNTER, it) })
    trim()
    return Progress(n, REQUIRED, n >= REQUIRED)
  }

  /** Keeps the most recently rewarded [MAX_PHOTOS] photos. */
  private fun trim() {
    val photos = store.keys().filter { it.startsWith(PREFIX) }
    if (photos.size <= MAX_PHOTOS) return
    photos.sortedBy { store.getInt(ORDER_PREFIX + it.removePrefix(PREFIX)) }
      .take(photos.size - MAX_PHOTOS)
      .forEach {
        store.remove(it)
        store.remove(ORDER_PREFIX + it.removePrefix(PREFIX))
      }
  }

  companion object {
    const val REQUIRED = 4
    const val MAX_PHOTOS = 200
    private const val PREFIX = "photo:"
    private const val ORDER_PREFIX = "order:"
    private const val COUNTER = "counter"

    /**
     * A photo is identified by its untouched copy's path: edits and exports
     * of the same photo share one unlock.
     */
    fun keyFor(photo: File): String {
      val canonical = runCatching { photo.canonicalPath }.getOrDefault(photo.absolutePath)
      val base = canonical.removeSuffix("_clean.jpg").removeSuffix(".jpg")
      val digest = MessageDigest.getInstance("SHA-256").digest(base.toByteArray())
      return PREFIX + digest.take(12).joinToString("") { "%02x".format(it) }
    }

    fun preferences(context: Context): AdRewards {
      val prefs: SharedPreferences = context.applicationContext.getSharedPreferences("pp_ad_rewards", Context.MODE_PRIVATE)
      return AdRewards(object : Store {
        override fun getInt(key: String) = prefs.getInt(key, 0)
        override fun putInt(key: String, value: Int) = prefs.edit { putInt(key, value) }
        override fun remove(key: String) = prefs.edit { remove(key) }
        override fun keys(): Set<String> = prefs.all.keys
      })
    }
  }
}
