package com.photoplus.engine

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong

class JobManagerTest {
  private class FakeTokens : CancelTokens {
    val next = AtomicLong(1)
    val live: MutableSet<Long> = Collections.synchronizedSet(mutableSetOf())
    val cancelled: MutableSet<Long> = Collections.synchronizedSet(mutableSetOf())
    val useAfterDestroy = AtomicInteger()
    override fun create(): Long = next.getAndIncrement().also { live.add(it) }
    override fun cancel(handle: Long) {
      if (!live.contains(handle)) useAfterDestroy.incrementAndGet()
      cancelled.add(handle)
    }
    override fun destroy(handle: Long) {
      if (!live.remove(handle)) useAfterDestroy.incrementAndGet()
    }
  }

  private class Recorder<T> : JobListener<T> {
    val done = CountDownLatch(1)
    val states: MutableList<JobState> = Collections.synchronizedList(mutableListOf())
    @Volatile var result: T? = null
    @Volatile var error: EngineException? = null
    override fun onStateChanged(jobId: String, state: JobState) { states.add(state) }
    override fun onSuccess(jobId: String, result: T) { this.result = result; done.countDown() }
    override fun onFailure(jobId: String, error: EngineException) { this.error = error; done.countDown() }
    fun await() = assertTrue("job did not finish", done.await(5, TimeUnit.SECONDS))
  }

  private val executor = Executors.newFixedThreadPool(2)
  private val tokens = FakeTokens()
  private val manager = JobManager(executor, tokens)

  @After fun tearDown() { executor.shutdownNow() }

  @Test fun successfulJobReportsLifecycleAndReleasesToken() {
    val rec = Recorder<Int>()
    manager.submit("a", rec) { token -> assertTrue(tokens.live.contains(token)); 42 }
    rec.await()
    assertEquals(42, rec.result)
    assertEquals(listOf(JobState.QUEUED, JobState.RUNNING, JobState.SUCCEEDED), rec.states.toList())
    assertTrue(tokens.live.isEmpty())
    assertEquals(0, manager.activeJobCount) // cleaned up before the listener ran
  }

  @Test fun engineErrorsArePropagatedWithCode() {
    val rec = Recorder<Int>()
    manager.submit("b", rec) { throw EngineException(EngineException.BUDGET_EXCEEDED, "too big") }
    rec.await()
    assertEquals(EngineException.BUDGET_EXCEEDED, rec.error?.code)
    assertEquals(JobState.FAILED, rec.states.last())
    assertTrue(tokens.live.isEmpty())
  }

  @Test fun unexpectedExceptionsBecomeInternalErrors() {
    val rec = Recorder<Int>()
    manager.submit("c", rec) { throw IllegalStateException("boom") }
    rec.await()
    assertEquals(EngineException.INTERNAL, rec.error?.code)
  }

  @Test fun outOfMemoryIsReportedNotCrashing() {
    val rec = Recorder<Int>()
    manager.submit("oom", rec) { throw OutOfMemoryError() }
    rec.await()
    assertEquals(EngineException.OUT_OF_MEMORY, rec.error?.code)
  }

  @Test fun cancelSignalsRunningJobToken() {
    val started = CountDownLatch(1)
    val rec = Recorder<Int>()
    manager.submit("d", rec) { token ->
      started.countDown()
      // Simulate the engine polling its token.
      while (!tokens.cancelled.contains(token)) Thread.sleep(1)
      throw EngineException(EngineException.CANCELLED, "cancelled")
    }
    assertTrue(started.await(5, TimeUnit.SECONDS))
    assertTrue(manager.cancel("d"))
    rec.await()
    assertEquals(EngineException.CANCELLED, rec.error?.code)
    assertEquals(JobState.CANCELLED, rec.states.last())
    assertEquals(0, tokens.useAfterDestroy.get())
  }

  @Test fun cancelBeforeStartNeverRunsWork() {
    val single = Executors.newSingleThreadExecutor()
    val m = JobManager(single, tokens)
    val gate = CountDownLatch(1)
    val blocker = Recorder<Int>()
    m.submit("blocker", blocker) { gate.await(); 1 }
    val ran = AtomicInteger()
    val rec = Recorder<Int>()
    m.submit("queued", rec) { ran.incrementAndGet(); 2 }
    assertTrue(m.cancel("queued"))
    gate.countDown()
    rec.await()
    blocker.await()
    assertEquals(0, ran.get())
    assertEquals(EngineException.CANCELLED, rec.error?.code)
    single.shutdownNow()
  }

  @Test fun duplicateJobIdsAreRejected() {
    val gate = CountDownLatch(1)
    val first = Recorder<Int>()
    manager.submit("dup", first) { gate.await(); 1 }
    val second = Recorder<Int>()
    manager.submit("dup", second) { 2 }
    second.await()
    assertEquals(EngineException.INVALID_ARGUMENT, second.error?.code)
    gate.countDown()
    first.await()
    assertEquals(1, first.result)
  }

  @Test fun cancelUnknownJobReturnsFalse() {
    assertFalse(manager.cancel("missing"))
  }

  @Test fun cancelRacingCompletionNeverTouchesDestroyedToken() {
    repeat(300) { i ->
      val rec = Recorder<Int>()
      manager.submit("race-$i", rec) { i }
      manager.cancel("race-$i")
      rec.await()
    }
    assertEquals(0, tokens.useAfterDestroy.get())
    assertTrue(tokens.live.isEmpty())
  }

  @Test fun shutdownRejectsNewWork() {
    manager.shutdown()
    val rec = Recorder<Int>()
    manager.submit("late", rec) { 1 }
    rec.await()
    assertEquals(EngineException.INTERNAL, rec.error?.code)
    assertEquals(0, manager.activeJobCount)
  }

  private fun waitUntil(condition: () -> Boolean) {
    val deadline = System.currentTimeMillis() + 2000
    while (!condition()) {
      if (System.currentTimeMillis() > deadline) throw AssertionError("condition not met")
      Thread.sleep(5)
    }
  }
}
