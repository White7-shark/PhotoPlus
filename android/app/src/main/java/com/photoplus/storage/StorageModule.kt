package com.photoplus.storage

import android.os.StatFs
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.engine.EngineException
import com.photoplus.specs.NativePhotoPlusStorageSpec
import com.photoplus.util.toWritableMap
import java.io.File
import java.util.concurrent.Executors

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

  override fun invalidate() {
    executor.shutdown()
    super.invalidate()
  }

  companion object {
    const val NAME = "PhotoPlusStorage"
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
