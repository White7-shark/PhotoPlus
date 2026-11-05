package com.photoplus.storage

import android.content.ClipData
import android.content.Intent
import android.net.Uri
import android.os.StatFs
import androidx.activity.ComponentActivity
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.FileProvider
import com.facebook.react.bridge.ReadableArray
import com.facebook.react.bridge.UiThreadUtil
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.engine.EngineException
import com.photoplus.specs.NativePhotoPlusStorageSpec
import com.photoplus.util.toWritableMap
import java.io.File
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

@ReactModule(name = StorageModule.NAME)
class StorageModule(reactContext: ReactApplicationContext) : NativePhotoPlusStorageSpec(reactContext) {
  private val paths = AppPaths(reactContext)
  private val executor = Executors.newSingleThreadExecutor { r -> Thread(r, "pp-storage") }

  override fun getName() = NAME

  private fun run(promise: Promise, block: () -> Any?) {
    executor.execute {
      try {
        promise.resolve(block())
      } catch (e: EngineException) {
        promise.reject(e.code, e.message, e)
      } catch (e: Exception) {
        promise.reject(EngineException.IO_ERROR, e.message, e)
      }
    }
  }

  override fun getPaths(promise: Promise) = run(promise) {
    paths.ensureCreated()
    paths.all().mapValues { it.value.absolutePath }.toWritableMap()
  }

  override fun getStorageUsage(promise: Promise) = run(promise) {
    val stat = StatFs(reactApplicationContext.filesDir.absolutePath)
    val areas = paths.all().filterKeys { it != "cache" }.mapValues { StorageUsage.directorySize(it.value).toDouble() }
    val cacheBytes = StorageUsage.directorySize(paths.cacheRoot)
    val databaseBytes = StorageUsage.directorySize(paths.databases)
    mapOf(
      "areas" to areas,
      "cacheBytes" to cacheBytes.toDouble(),
      "databaseBytes" to databaseBytes.toDouble(),
      "appTotalBytes" to (StorageUsage.directorySize(reactApplicationContext.filesDir) + cacheBytes + databaseBytes).toDouble(),
      "deviceAvailableBytes" to stat.availableBytes.toDouble(),
      "deviceTotalBytes" to stat.totalBytes.toDouble(),
    ).toWritableMap()
  }

  override fun clearCache(promise: Promise) = run(promise) {
    StorageUsage.clearDirectoryContents(paths.cacheRoot).toDouble()
  }

  override fun deleteFile(path: String, promise: Promise) = run(promise) {
    val file = paths.resolve(path)
    file.isFile && file.delete()
  }

  override fun fileExists(path: String, promise: Promise) = run(promise) {
    paths.resolve(path).exists()
  }

  override fun pickImage(area: String, promise: Promise) {
    val target = when (area) {
      "looks" -> paths.looks
      else -> paths.imports
    }
    val activity = reactApplicationContext.currentActivity as? ComponentActivity
    if (activity == null) {
      promise.reject("NO_ACTIVITY", "Photo picker needs a foreground activity")
      return
    }
    UiThreadUtil.runOnUiThread {
      // Registered without a lifecycle owner so it survives configuration
      // changes; it unregisters itself after one result.
      val key = "pp-pick-${pickCounter.incrementAndGet()}"
      var launcher: androidx.activity.result.ActivityResultLauncher<PickVisualMediaRequest>? = null
      launcher = activity.activityResultRegistry.register(key, ActivityResultContracts.PickVisualMedia()) { uri: Uri? ->
        launcher?.unregister()
        if (uri == null) {
          promise.resolve(null)
          return@register
        }
        run(promise) { copyIn(uri, target).absolutePath }
      }
      launcher.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
    }
  }

  private fun copyIn(uri: Uri, dir: File): File {
    dir.mkdirs()
    val resolver = reactApplicationContext.contentResolver
    val ext = when (resolver.getType(uri)) {
      "image/png" -> "png"
      "image/webp" -> "webp"
      "image/heic", "image/heif" -> "heic"
      else -> "jpg"
    }
    val out = File(dir, "PP_IMPORT_${UUID.randomUUID().toString().take(8)}.$ext")
    resolver.openInputStream(uri).use { input ->
      if (input == null) throw EngineException(EngineException.IO_ERROR, "That photo could not be opened")
      out.outputStream().use { input.copyTo(it) }
    }
    return out
  }

  override fun shareImages(paths: ReadableArray, title: String, promise: Promise) {
    val activity = reactApplicationContext.currentActivity
    if (activity == null) {
      promise.reject("NO_ACTIVITY", "Sharing needs a foreground activity")
      return
    }
    try {
      val authority = "${reactApplicationContext.packageName}.files"
      val uris = (0 until paths.size()).mapNotNull { i -> paths.getString(i) }.map { p ->
        val file = this.paths.resolve(p)
        if (!file.isFile) throw EngineException(EngineException.IO_ERROR, "Photo not found")
        FileProvider.getUriForFile(reactApplicationContext, authority, file)
      }
      if (uris.isEmpty()) throw EngineException(EngineException.INVALID_ARGUMENT, "Nothing to share")
      val send = if (uris.size == 1) {
        Intent(Intent.ACTION_SEND).putExtra(Intent.EXTRA_STREAM, uris[0])
      } else {
        Intent(Intent.ACTION_SEND_MULTIPLE).putParcelableArrayListExtra(Intent.EXTRA_STREAM, ArrayList(uris))
      }
      send.type = "image/*"
      send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
      // ClipData carries the read grant to whichever app the user picks.
      send.clipData = ClipData.newUri(reactApplicationContext.contentResolver, "photo", uris[0]).apply {
        uris.drop(1).forEach { addItem(ClipData.Item(it)) }
      }
      activity.startActivity(Intent.createChooser(send, title))
      promise.resolve(true)
    } catch (e: EngineException) {
      promise.reject(e.code, e.message, e)
    } catch (e: Exception) {
      promise.reject("SHARE_FAILED", e.message, e)
    }
  }

  override fun invalidate() {
    executor.shutdown()
    super.invalidate()
  }

  companion object {
    const val NAME = "PhotoPlusStorage"
    private val pickCounter = AtomicInteger()
  }
}

object StorageUsage {
  /** Recursive size without following symlinks out of the tree. */
  fun directorySize(dir: File): Long {
    if (!dir.exists()) return 0
    return dir.walkTopDown()
      .onEnter { d -> d.canonicalPath.startsWith(dir.canonicalPath) }
      .filter { it.isFile }
      .sumOf { it.length() }
  }

  /** Deletes everything inside [dir] (not the dir itself). Returns bytes freed. */
  fun clearDirectoryContents(dir: File): Long {
    var freed = 0L
    dir.listFiles()?.forEach { child ->
      val size = if (child.isDirectory) directorySize(child) else child.length()
      if (child.deleteRecursively()) freed += size
    }
    return freed
  }
}
