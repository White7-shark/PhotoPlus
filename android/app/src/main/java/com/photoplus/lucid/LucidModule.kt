package com.photoplus.lucid

import android.app.WallpaperColors
import android.app.WallpaperManager
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.ImageDecoder
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.graphics.drawable.toBitmap
import androidx.core.graphics.drawable.toDrawable
import androidx.core.graphics.scale
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.LifecycleEventListener
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.UiThreadUtil
import com.facebook.react.bridge.WritableMap
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.R
import com.photoplus.i18n.Words
import com.photoplus.specs.NativePhotoPlusLucidSpec
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/**
 * Lucid's native side (the see-through theme, from Dr. File): the phone's
 * wallpaper — as a picture when Android lets the app read it, else through
 * a see-through window that shows it (live wallpapers too) — and what a
 * picture or a clip is like (ImageStats), so text over it stays readable.
 * A photo or clip chosen as the background is copied into the app's own
 * storage (filesDir/lucid), so it stays if the original goes.
 */
@ReactModule(name = LucidModule.NAME)
class LucidModule(reactContext: ReactApplicationContext) :
  NativePhotoPlusLucidSpec(reactContext), LifecycleEventListener {

  /**
   * Whether Lucid wants the see-through window. Android forgets window flags
   * when it makes a new window (after Back, or after freeing memory while the
   * app was away), so it is applied again each time the app comes back.
   */
  @Volatile private var seeThrough = false
  private val dir get() = File(reactApplicationContext.filesDir, "lucid").apply { mkdirs() }

  init {
    reactContext.addLifecycleEventListener(this)
  }

  override fun getName() = NAME

  private fun applyWindow(on: Boolean): Boolean {
    val window = reactApplicationContext.currentActivity?.window ?: return false
    if (on) {
      window.addFlags(WindowManager.LayoutParams.FLAG_SHOW_WALLPAPER)
      window.setBackgroundDrawable(Color.TRANSPARENT.toDrawable())
    } else {
      window.clearFlags(WindowManager.LayoutParams.FLAG_SHOW_WALLPAPER)
      window.setBackgroundDrawableResource(R.color.pp_background)
    }
    return true
  }

  override fun onHostResume() {
    if (seeThrough) UiThreadUtil.runOnUiThread { applyWindow(true) }
  }

  override fun onHostPause() = Unit

  override fun onHostDestroy() = Unit

  private fun statsMap(s: ImageStats.Stats): WritableMap = Arguments.createMap().apply {
    putDouble("mean", s.mean)
    putDouble("p10", s.p10)
    putDouble("p50", s.p50)
    putDouble("p90", s.p90)
    putDouble("saturation", s.saturation)
    putArray("colors", Arguments.createArray().apply { s.colors.forEach { pushString(hex(it)) } })
  }

  private fun hex(argb: Int) = String.format("#%06X", argb and 0xFFFFFF)

  private fun statsOf(bitmap: Bitmap): ImageStats.Stats {
    val h = (48f * bitmap.height / bitmap.width).toInt().coerceIn(16, 160)
    val small = bitmap.scale(48, h)
    val px = IntArray(small.width * small.height)
    small.getPixels(px, 0, small.width, 0, 0, small.width, small.height)
    if (small !== bitmap) small.recycle()
    return ImageStats.of(px)
  }

  /** A picture, at most [max] px across, the right way up (ImageDecoder reads JPEG, HEIC, WebP, AVIF, PNG). */
  private fun decode(file: File, max: Int): Bitmap =
    ImageDecoder.decodeBitmap(ImageDecoder.createSource(file)) { decoder, info, _ ->
      decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
      val longest = maxOf(info.size.width, info.size.height)
      if (longest > max) {
        val k = max.toFloat() / longest
        decoder.setTargetSize((info.size.width * k).toInt().coerceAtLeast(1), (info.size.height * k).toInt().coerceAtLeast(1))
      }
    }

  /**
   * The home-screen wallpaper: { path, stats } when Android lets the app read
   * the picture, and always the colours Android shares with every app, whether
   * it is a live wallpaper and whether dark text suits it.
   */
  override fun wallpaper(promise: Promise) {
    Thread {
      try {
        val ctx = reactApplicationContext
        val wm = WallpaperManager.getInstance(ctx)
        val live = wm.wallpaperInfo != null
        val out = Arguments.createMap()
        out.putBoolean("live", live)
        out.putNull("path")
        out.putNull("stats")
        if (!live) {
          // Reading the picture needs a permission Photo Plus doesn't ask for on
          // newer Android; then the see-through window shows it instead.
          runCatching {
            @Suppress("MissingPermission")
            val d = wm.drawable
            if (d != null) {
              val metrics = ctx.resources.displayMetrics
              val w = minOf(d.intrinsicWidth.takeIf { it > 0 } ?: metrics.widthPixels, metrics.widthPixels)
              val h = minOf(d.intrinsicHeight.takeIf { it > 0 } ?: metrics.heightPixels, metrics.heightPixels)
              val bmp = d.toBitmap(w, h, Bitmap.Config.ARGB_8888)
              dir.listFiles()?.filter { it.name.startsWith("wallpaper-") }?.forEach { it.delete() }
              val file = File(dir, "wallpaper-${System.currentTimeMillis()}.jpg")
              file.outputStream().use { bmp.compress(Bitmap.CompressFormat.JPEG, 90, it) }
              out.putString("path", file.absolutePath)
              out.putMap("stats", statsMap(statsOf(bmp)))
              bmp.recycle()
            }
          }
        }
        val colors: WallpaperColors? = wm.getWallpaperColors(WallpaperManager.FLAG_SYSTEM)
        out.putArray(
          "colors",
          Arguments.createArray().apply {
            colors?.let { c -> listOfNotNull(c.primaryColor, c.secondaryColor, c.tertiaryColor).forEach { pushString(hex(it.toArgb())) } }
          },
        )
        out.putBoolean(
          "darkText",
          Build.VERSION.SDK_INT >= 31 && colors != null && (colors.colorHints and WallpaperColors.HINT_SUPPORTS_DARK_TEXT) != 0,
        )
        promise.resolve(out)
      } catch (e: Exception) {
        promise.reject("WALLPAPER", Words.get(reactApplicationContext, R.string.native_lucid_wallpaper_failed), e)
      }
    }.start()
  }

  override fun imageStats(path: String, promise: Promise) {
    Thread {
      runCatching {
        val bmp = decode(File(path), 400)
        statsMap(statsOf(bmp)).also { bmp.recycle() }
      }.onSuccess { promise.resolve(it) }.onFailure { promise.resolve(null) }
    }.start()
  }

  override fun showWallpaper(on: Boolean, promise: Promise) {
    seeThrough = on
    UiThreadUtil.runOnUiThread { promise.resolve(applyWindow(on)) }
  }

  /**
   * The system photo picker for a photo or a short clip; the choice is
   * copied into the app (a photo as a JPEG at most 2160 px, the right way up;
   * a clip as it is, up to 30 MB) and measured. { path, stats } or null when
   * nothing was chosen.
   */
  override fun pickBackground(kind: String, promise: Promise) {
    val activity = reactApplicationContext.currentActivity as? ComponentActivity
    if (activity == null) {
      promise.reject("NO_ACTIVITY", Words.get(reactApplicationContext, R.string.native_photo_plus_needs_to_be_open))
      return
    }
    val video = kind == "video"
    UiThreadUtil.runOnUiThread {
      val key = "pp-lucid-${counter.incrementAndGet()}"
      val settled = AtomicBoolean(false)
      var launcher: androidx.activity.result.ActivityResultLauncher<PickVisualMediaRequest>? = null
      var observer: LifecycleEventObserver? = null
      val finish = {
        launcher?.unregister()
        observer?.let { activity.lifecycle.removeObserver(it) }
      }
      launcher = activity.activityResultRegistry.register(key, ActivityResultContracts.PickVisualMedia()) { uri: Uri? ->
        if (!settled.compareAndSet(false, true)) return@register
        finish()
        if (uri == null) promise.resolve(null) else Thread { take(uri, video, promise) }.start()
      }
      observer = LifecycleEventObserver { _, event ->
        if (event == Lifecycle.Event.ON_DESTROY && settled.compareAndSet(false, true)) {
          finish()
          promise.resolve(null)
        }
      }
      activity.lifecycle.addObserver(observer)
      launcher.launch(
        PickVisualMediaRequest(if (video) ActivityResultContracts.PickVisualMedia.VideoOnly else ActivityResultContracts.PickVisualMedia.ImageOnly),
      )
    }
  }

  private fun take(uri: Uri, video: Boolean, promise: Promise) {
    val ctx = reactApplicationContext
    val tmp = File(ctx.cacheDir, "lucid-pick-${System.nanoTime()}")
    try {
      ctx.contentResolver.openInputStream(uri)?.use { input -> tmp.outputStream().use { input.copyTo(it) } }
        ?: throw IllegalStateException("unreadable")
      if (video && tmp.length() > MAX_VIDEO_BYTES) {
        promise.reject("TOO_BIG", Words.get(ctx, R.string.native_lucid_video_too_big))
        return
      }
      val stats: ImageStats.Stats
      val dest: File
      dir.listFiles()?.filter { it.name.startsWith("background-") }?.forEach { it.delete() }
      if (video) {
        dest = File(dir, "background-${System.currentTimeMillis()}.mp4")
        tmp.copyTo(dest, overwrite = true)
        stats = videoStats(dest) ?: run {
          dest.delete()
          promise.reject("UNREADABLE", Words.get(ctx, R.string.native_lucid_video_unreadable))
          return
        }
      } else {
        val bmp = runCatching { decode(tmp, 2160) }.getOrNull() ?: run {
          promise.reject("UNREADABLE", Words.get(ctx, R.string.native_lucid_photo_unreadable))
          return
        }
        dest = File(dir, "background-${System.currentTimeMillis()}.jpg")
        dest.outputStream().use { bmp.compress(Bitmap.CompressFormat.JPEG, 90, it) }
        stats = statsOf(bmp)
        bmp.recycle()
      }
      promise.resolve(Arguments.createMap().apply {
        putString("path", dest.absolutePath)
        putMap("stats", statsMap(stats))
      })
    } catch (e: Exception) {
      promise.reject("COPY_FAILED", Words.get(ctx, R.string.native_lucid_copy_failed), e)
    } finally {
      tmp.delete()
    }
  }

  /** Frames across the clip: the look must suit all of it. */
  private fun videoStats(file: File): ImageStats.Stats? {
    val r = MediaMetadataRetriever()
    return try {
      r.setDataSource(file.absolutePath)
      val frames = listOf(0L, 1_500L, 4_000L, 8_000L).mapNotNull { ms ->
        runCatching { r.getScaledFrameAtTime(ms * 1000, MediaMetadataRetriever.OPTION_CLOSEST_SYNC, 160, 160) }.getOrNull()
          ?.let { bmp -> statsOf(bmp).also { bmp.recycle() } }
      }
      ImageStats.combine(frames)
    } catch (_: Exception) {
      null
    } finally {
      r.release()
    }
  }

  override fun invalidate() {
    reactApplicationContext.removeLifecycleEventListener(this)
    super.invalidate()
  }

  companion object {
    const val NAME = "PhotoPlusLucid"
    const val MAX_VIDEO_BYTES = 30L * 1024 * 1024
    private val counter = AtomicInteger()
  }
}
