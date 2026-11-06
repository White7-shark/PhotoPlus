package com.photoplus.ml

import android.app.ActivityManager
import android.content.Context
import android.content.res.AssetManager
import android.os.SystemClock
import android.util.Log
import com.photoplus.device.MemoryPressureMonitor
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

/**
 * The common interface to on-device models. Models load on first use, run
 * one at a time per model, and are released under memory pressure (never
 * blocking the caller of the pressure callback). Missing or failing models
 * return null so every feature can fall back to its non-ML path.
 */
object ModelRuntime {
  private const val TAG = "PhotoPlusML"

  @Volatile private var assets: AssetManager? = null
  @Volatile private var activity: ActivityManager? = null
  private class Slot {
    val lock = ReentrantLock()
    var session: InferenceSession? = null
    var unavailable = false
    @Volatile var releaseRequested = false
    val stats = InferenceStats()
  }
  private val slots = ConcurrentHashMap<String, Slot>()
  /** A GPU that failed once is not retried this session (drivers rarely recover). */
  @Volatile var gpuBlocked = false
    private set

  fun install(context: Context) {
    if (assets != null) return
    assets = context.applicationContext.assets
    activity = context.applicationContext.getSystemService(ActivityManager::class.java)
    MemoryPressureMonitor.addListener { level ->
      if (level != MemoryPressureMonitor.Level.NORMAL) releaseAll()
    }
  }

  private fun slot(spec: ModelSpec) = slots.getOrPut(spec.id) { Slot() }

  /** Runs [block] with the model's session, or returns null if the model is unavailable. */
  fun <T> use(spec: ModelSpec, block: (InferenceSession) -> T): T? {
    val s = slot(spec)
    return s.lock.withLock {
      try {
        val session = loadLocked(spec, s) ?: return@withLock null
        val started = SystemClock.elapsedRealtime()
        val result = runCatching { block(session) }
          .onFailure {
            s.stats.fail()
            Log.w(TAG, "${spec.id} failed", it)
          }
          .getOrNull()
        if (result != null) s.stats.record((SystemClock.elapsedRealtime() - started).toDouble())
        result
      } finally {
        if (s.releaseRequested) releaseLocked(s)
      }
    }
  }

  private fun loadLocked(spec: ModelSpec, s: Slot): InferenceSession? {
    s.session?.let { return it }
    if (s.unavailable) return null
    val am = assets ?: return null
    if (am.list("")?.contains(spec.asset) != true) {
      s.unavailable = true
      Log.i(TAG, "${spec.id}: not bundled; features use their built-in fallback")
      return null
    }
    if (!roomFor(spec)) {
      // Not now (other apps need the memory); tried again on next use.
      s.stats.skip()
      Log.i(TAG, "${spec.id}: skipped, not enough free memory; using the built-in fallback")
      return null
    }
    val started = SystemClock.elapsedRealtime()
    val session = InferenceSession.open(am, spec, AcceleratorPlan.order(spec, gpuBlocked)) { gpuBlocked = true }
    if (session == null) {
      s.unavailable = true
      return null
    }
    s.stats.accelerator = session.accelerator
    s.stats.loadMs = (SystemClock.elapsedRealtime() - started).toDouble()
    s.session = session
    return session
  }

  private fun roomFor(spec: ModelSpec): Boolean {
    val am = activity ?: return true
    val info = ActivityManager.MemoryInfo().also { am.getMemoryInfo(it) }
    return AcceleratorPlan.canLoad(
      spec.memoryBytes, info.availMem, info.lowMemory,
      MemoryPressureMonitor.lastLevel == MemoryPressureMonitor.Level.CRITICAL,
    )
  }

  /** Frees every loaded model; models in use are freed right after their run. */
  fun releaseAll() {
    for (s in slots.values) {
      s.releaseRequested = true
      if (s.lock.tryLock()) {
        try {
          releaseLocked(s)
        } finally {
          s.lock.unlock()
        }
      }
    }
  }

  private fun releaseLocked(s: Slot) {
    s.releaseRequested = false
    s.session?.close()
    s.session = null
  }

  /** For diagnostics: per model, which accelerator runs it and how fast. */
  fun stats(): Map<String, Any?> = ModelRegistry.ALL.associate { spec ->
    spec.id to (slots[spec.id]?.stats?.snapshot() ?: mapOf("loaded" to false)) + mapOf(
      "license" to spec.license,
      "source" to spec.source,
    )
  }
}
