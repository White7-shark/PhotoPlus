package com.photoplus.engine

import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ExecutorService
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

/** Abstraction over native cancel tokens so the manager is JVM-testable. */
interface CancelTokens {
  fun create(): Long
  fun cancel(handle: Long)
  fun destroy(handle: Long)
  /** How far the job using this token has got, 0..1. */
  fun progress(handle: Long): Float = 0f
}

object NativeCancelTokens : CancelTokens {
  override fun create() = NativeEngine.nativeCreateToken()
  override fun cancel(handle: Long) = NativeEngine.nativeCancelToken(handle)
  override fun destroy(handle: Long) = NativeEngine.nativeDestroyToken(handle)
  override fun progress(handle: Long) = NativeEngine.nativeTokenProgress(handle)
}

enum class JobState { QUEUED, RUNNING, SUCCEEDED, FAILED, CANCELLED }

interface JobListener<T> {
  fun onStateChanged(jobId: String, state: JobState) {}
  /** Progress while running (0..1, rising), reported at most every tick and only when it moves. */
  fun onProgress(jobId: String, fraction: Float) {}
  fun onSuccess(jobId: String, result: T)
  fun onFailure(jobId: String, error: EngineException)
}

/**
 * Coordinates background processing jobs.
 *
 * - Bounded concurrency comes from the injected executor (sized by device
 *   tier), so a burst of requests never multiplies peak memory.
 * - Each job owns a native cancel token. Token access is serialised per job
 *   so a cancel racing job completion can never touch a destroyed token.
 * - Duplicate job ids are rejected rather than silently replaced.
 * - With a [ticker], running jobs report progress read from their token.
 */
class JobManager(
  private val executor: ExecutorService,
  private val tokens: CancelTokens,
  private val ticker: ScheduledExecutorService? = null,
  private val tickMs: Long = 200,
) {
  private class Job(val id: String) {
    @Volatile var cancelled = false
    var token: Long = 0L // guarded by `this`
  }

  private val jobs = ConcurrentHashMap<String, Job>()

  val activeJobCount: Int get() = jobs.size

  fun <T> submit(jobId: String, listener: JobListener<T>, work: (token: Long) -> T) {
    val job = Job(jobId)
    if (jobs.putIfAbsent(jobId, job) != null) {
      listener.onFailure(jobId, EngineException(EngineException.INVALID_ARGUMENT, "Job '$jobId' is already running"))
      return
    }
    listener.onStateChanged(jobId, JobState.QUEUED)
    try {
      executor.execute { run(job, listener, work) }
    } catch (e: RejectedExecutionException) {
      jobs.remove(jobId)
      listener.onFailure(jobId, EngineException(EngineException.INTERNAL, "Processing queue is shut down"))
    }
  }

  private sealed class Outcome<out T> {
    data class Success<T>(val value: T) : Outcome<T>()
    data class Failure(val error: EngineException) : Outcome<Nothing>()
  }

  private fun <T> startProgress(job: Job, listener: JobListener<T>): ScheduledFuture<*>? {
    val t = ticker ?: return null
    var last = 0f
    return t.scheduleWithFixedDelay({
      // Reported under the job's lock, so no progress can follow the job's
      // final state (the token is cleared under the same lock first).
      synchronized(job) {
        if (job.token != 0L && !job.cancelled) {
          val p = tokens.progress(job.token)
          if (p - last >= 0.01f) {
            last = p
            listener.onProgress(job.id, p.coerceIn(0f, 1f))
          }
        }
      }
    }, tickMs, tickMs, TimeUnit.MILLISECONDS)
  }

  private fun <T> run(job: Job, listener: JobListener<T>, work: (Long) -> T) {
    var tick: ScheduledFuture<*>? = null
    val outcome: Outcome<T> = try {
      if (job.cancelled) throw cancelledError()
      synchronized(job) {
        if (job.cancelled) throw cancelledError()
        job.token = tokens.create()
      }
      listener.onStateChanged(job.id, JobState.RUNNING)
      tick = startProgress(job, listener)
      val value = work(job.token)
      if (job.cancelled) throw cancelledError()
      Outcome.Success(value)
    } catch (e: EngineException) {
      Outcome.Failure(e)
    } catch (e: OutOfMemoryError) {
      Outcome.Failure(EngineException(EngineException.OUT_OF_MEMORY, "Not enough memory to process this image"))
    } catch (e: Exception) {
      Outcome.Failure(EngineException(EngineException.INTERNAL, e.message ?: e.javaClass.simpleName))
    } finally {
      // Release native resources and deregister BEFORE notifying, so when
      // JavaScript sees the result the job no longer holds memory or a token.
      tick?.cancel(false)
      synchronized(job) {
        if (job.token != 0L) tokens.destroy(job.token)
        job.token = 0L
      }
      jobs.remove(job.id, job)
    }
    when (outcome) {
      is Outcome.Success -> {
        listener.onStateChanged(job.id, JobState.SUCCEEDED)
        listener.onSuccess(job.id, outcome.value)
      }
      is Outcome.Failure -> {
        val state = if (outcome.error.code == EngineException.CANCELLED) JobState.CANCELLED else JobState.FAILED
        listener.onStateChanged(job.id, state)
        listener.onFailure(job.id, outcome.error)
      }
    }
  }

  /** Returns true if a job with this id was found. */
  fun cancel(jobId: String): Boolean {
    val job = jobs[jobId] ?: return false
    synchronized(job) {
      job.cancelled = true
      if (job.token != 0L) tokens.cancel(job.token)
    }
    return true
  }

  fun cancelAll(): Int = jobs.keys.toList().count { cancel(it) }

  /** Cancels everything and stops accepting work (module teardown). */
  fun shutdown() {
    cancelAll()
    executor.shutdown()
    ticker?.shutdown()
  }

  private fun cancelledError() = EngineException(EngineException.CANCELLED, "Processing was cancelled")
}
