package com.photoplus.engine

import org.junit.jupiter.api.AfterEach
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong

/** Progress reporting: read from the job's token on a ticker, only while it runs. */
class JobProgressTest {
  private class ProgressTokens : CancelTokens {
    private val next = AtomicLong(1)
    val live: MutableSet<Long> = ConcurrentHashMap.newKeySet()
    val value = ConcurrentHashMap<Long, Float>()
    override fun create(): Long = next.getAndIncrement().also { live.add(it); value[it] = 0f }
    override fun cancel(handle: Long) = Unit
    override fun destroy(handle: Long) {
      check(live.remove(handle)) { "destroyed twice" }
    }
    override fun progress(handle: Long): Float {
      check(handle in live) { "progress read from a destroyed token" }
      return value[handle] ?: 0f
    }
  }

  /** Every callback in order, as text ("p:0.5", "SUCCEEDED", "done"). */
  private class Events : JobListener<Unit> {
    // Read while the ticker appends: a copy-on-write list iterates safely.
    val log = CopyOnWriteArrayList<String>()
    val finished = CountDownLatch(1)
    override fun onStateChanged(jobId: String, state: JobState) { log.add(state.name) }
    override fun onProgress(jobId: String, fraction: Float) { log.add("p:$fraction") }
    override fun onSuccess(jobId: String, result: Unit) { log.add("done"); finished.countDown() }
    override fun onFailure(jobId: String, error: EngineException) { log.add("error:${error.code}"); finished.countDown() }
    fun progress() = log.filter { it.startsWith("p:") }.map { it.removePrefix("p:").toFloat() }
  }

  private val executor = Executors.newSingleThreadExecutor()
  private val ticker = Executors.newSingleThreadScheduledExecutor()
  private val tokens = ProgressTokens()
  private val manager = JobManager(executor, tokens, ticker, tickMs = 2)

  @AfterEach fun tearDown() {
    executor.shutdownNow()
    ticker.shutdownNow()
  }

  /** Waits until [n] progress events arrived (the ticker runs every 2 ms). */
  private fun awaitProgress(events: Events, n: Int) {
    val deadline = System.currentTimeMillis() + 5000
    while (events.progress().size < n) {
      if (System.currentTimeMillis() > deadline) throw AssertionError("no progress reported: ${events.log}")
      Thread.sleep(1)
    }
  }

  @Test fun reportsRisingProgressOnlyWhenItMoves() {
    val events = Events()
    manager.submit("job", events) { token ->
      for (step in listOf(0.25f, 0.25f, 0.255f, 0.5f, 0.9f)) {
        val before = events.progress().size
        tokens.value[token] = step
        // A real move (≥ 1%) is reported; anything smaller never is.
        if (step - (events.progress().lastOrNull() ?: 0f) >= 0.01f) awaitProgress(events, before + 1) else Thread.sleep(20)
      }
    }
    assertTrue(events.finished.await(5, TimeUnit.SECONDS))
    assertEquals(listOf(0.25f, 0.5f, 0.9f), events.progress())
  }

  @Test fun noProgressAfterTheJobsFinalState() {
    repeat(50) { i ->
      val events = Events()
      manager.submit("job-$i", events) { token ->
        tokens.value[token] = 0.5f
        awaitProgress(events, 1)
        tokens.value[token] = 0.99f
      }
      assertTrue(events.finished.await(5, TimeUnit.SECONDS))
      Thread.sleep(6) // a few more ticks
      val log = events.log.toList()
      val final = log.indexOf("SUCCEEDED")
      assertTrue(log.drop(final).none { it.startsWith("p:") }, "progress after the end: $log")
      assertEquals("RUNNING", log[log.indexOfFirst { it.startsWith("p:") } - 1])
    }
    assertTrue(tokens.live.isEmpty())
  }

  @Test fun progressIsClampedAndQueuedJobsStaySilent() {
    val events = Events()
    val gate = CountDownLatch(1)
    manager.submit("first", Events()) { gate.await(5, TimeUnit.SECONDS) }
    manager.submit("second", events) { token ->
      tokens.value[token] = 7f // a misbehaving source
      awaitProgress(events, 1)
    }
    Thread.sleep(20)
    assertEquals(listOf("QUEUED"), events.log.toList()) // waiting: no token, no progress
    gate.countDown()
    assertTrue(events.finished.await(5, TimeUnit.SECONDS))
    assertEquals(listOf(1f), events.progress())
  }

  @Test fun withoutATickerNothingIsReported() {
    val plain = JobManager(executor, tokens)
    val events = Events()
    plain.submit("job", events) { token ->
      tokens.value[token] = 0.6f
      Thread.sleep(20)
    }
    assertTrue(events.finished.await(5, TimeUnit.SECONDS))
    assertTrue(events.progress().isEmpty())
  }
}
