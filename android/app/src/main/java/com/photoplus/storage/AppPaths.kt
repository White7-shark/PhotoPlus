package com.photoplus.storage

import android.content.Context
import com.photoplus.engine.EngineException
import java.io.File

/**
 * Single source of truth for where Photo Plus keeps files. Everything lives in
 * app-private storage, so no storage permission is needed for app data, and
 * all paths received from JavaScript are confined to these roots.
 */
class AppPaths(context: Context) {
  private val files: File = context.filesDir
  private val cache: File = context.cacheDir

  val projects = File(files, "projects")
  val captures = File(files, "captures")
  val exports = File(files, "exports")
  val packs = File(files, "packs")
  val models = File(files, "models")
  val databases: File = context.getDatabasePath("x").parentFile ?: File(files.parentFile, "databases")
  val processingCache = File(cache, "processing")
  val testImages = File(cache, "test-images")
  val cacheRoot: File = cache

  fun all(): Map<String, File> = mapOf(
    "projects" to projects,
    "captures" to captures,
    "exports" to exports,
    "packs" to packs,
    "models" to models,
    "processingCache" to processingCache,
    "testImages" to testImages,
    "cache" to cache,
  )

  fun ensureCreated() {
    all().values.forEach { it.mkdirs() }
  }

  /** Resolves a JS-supplied path and rejects anything outside app storage. */
  fun resolve(path: String): File = resolveWithin(path, listOf(files, cache))

  companion object {
    fun resolveWithin(path: String, roots: List<File>): File {
      val raw = path.removePrefix("file://")
      if (raw.isBlank()) throw EngineException(EngineException.INVALID_ARGUMENT, "Path is empty")
      val file = File(raw).canonicalFile
      val allowed = roots.any { root ->
        val canonicalRoot = root.canonicalFile
        file == canonicalRoot || file.path.startsWith(canonicalRoot.path + File.separator)
      }
      if (!allowed) throw EngineException(EngineException.INVALID_ARGUMENT, "Path is outside app storage")
      return file
    }
  }
}
