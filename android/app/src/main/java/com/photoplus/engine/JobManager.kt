package com.photoplus.engine

import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ExecutorService
import java.util.concurrent.RejectedExecutionException

/** Abstraction over native cancel tokens so the manager is JVM-testable. */
interface CancelTokens {
  fun create(): Long
  fun cancel(handle: Long)
  fun destroy(handle: Long)
}

object NativeCancelTokens : CancelTokens {
  override fun create() = NativeEngine.nativeCreateToken()
  override fun cancel(handle: Long) = NativeEngine.nativeCancelToken(handle)
  override fun destroy(handle: Long) = NativeEngine.nativeDestroyToken(handle)
}

enum class JobState { QUEUED, RUNNING, SUCCEEDED, FAILED, CANCELLED }

interface JobListener<T> {
  fun onStateChanged(jobId: String, state: JobState) {}
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
 */
class JobManager(
  private val executor: ExecutorService,
  private val tokens: CancelTokens,
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

  private fun <T> run(job: Job, listener: JobListener<T>, work: (Long) -> T) {
    try {
      if (job.cancelled) throw cancelledError()
      synchronized(job) {
        if (job.cancelled) throw cancelledError()
        job.token = tokens.create()
      }
      listener.onStateChanged(job.id, JobState.RUNNING)
      val result = work(job.token)
      if (job.cancelled) throw cancelledError()
      listener.onStateChanged(job.id, JobState.SUCCEEDED)
      listener.onSuccess(job.id, result)
    } catch (e: EngineException) {
      val state = if (e.code == EngineException.CANCELLED) JobState.CANCELLED else JobState.FAILED
      listener.onStateChanged(job.id, state)
      listener.onFailure(job.id, e)
    } catch (e: OutOfMemoryError) {
      listener.onStateChanged(job.id, JobState.FAILED)
      listener.onFailure(job.id, EngineException(EngineException.OUT_OF_MEMORY, "Not enough memory to process this image"))
    } catch (e: Exception) {
      listener.onStateChanged(job.id, JobState.FAILED)
      listener.onFailure(job.id, EngineException(EngineException.INTERNAL, e.message ?: e.javaClass.simpleName))
    } finally {
      synchronized(job) {
        if (job.token != 0L) tokens.destroy(job.token)
        job.token = 0L
      }
      jobs.remove(job.id, job)
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
  }

  private fun cancelledError() = EngineException(EngineException.CANCELLED, "Processing was cancelled")
}
