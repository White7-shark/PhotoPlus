package com.photoplus.integrity

import android.os.SystemClock
import java.net.HttpURLConnection
import java.net.URL

/**
 * The true time from the internet: well-known HTTPS servers (TLS proves who
 * answered), read from their Date header or, for Cloudflare, the exact
 * timestamp it reports. Half the round trip is added; the median of the
 * answers is used, so one wrong server can't move it.
 */
object TimeSource {
  private val SERVERS = listOf(
    "https://www.google.com/generate_204",
    "https://www.cloudflare.com/cdn-cgi/trace",
    "https://www.gstatic.com/generate_204",
  )
  private val TS = Regex("""(?m)^ts=(\d+)(?:\.(\d+))?$""")

  /** The true time now (ms) and the boot timer it goes with, or null when offline. */
  fun fetch(timeoutMs: Int = 8_000): Pair<Long, Long>? {
    val readings = SERVERS.mapNotNull { url -> runCatching { read(url, timeoutMs) }.getOrNull() }
    if (readings.isEmpty()) return null
    // Everything at one moment (the last reading's boot timer), then the median.
    val at = readings.maxOf { it.second }
    val values = readings.map { it.first + (at - it.second) }.sorted()
    return values[values.size / 2] to at
  }

  private fun read(url: String, timeoutMs: Int): Pair<Long, Long>? {
    val start = SystemClock.elapsedRealtime()
    val conn = (URL(url).openConnection() as HttpURLConnection).apply {
      connectTimeout = timeoutMs
      readTimeout = timeoutMs
      useCaches = false
      instanceFollowRedirects = false
      setRequestProperty("Cache-Control", "no-cache")
    }
    try {
      val code = conn.responseCode
      if (code !in 200..399) return null
      val body = if (url.endsWith("/trace")) conn.inputStream.bufferedReader().use { it.readText() } else ""
      val end = SystemClock.elapsedRealtime()
      val server = TS.find(body)?.let { m ->
        val frac = (m.groupValues[2] + "000").take(3).toLong()
        m.groupValues[1].toLong() * 1000 + frac
      } ?: conn.getHeaderFieldDate("Date", 0L).takeIf { it > 0 }?.plus(500) // Date has whole seconds
      ?: return null
      return server + (end - start) / 2 to end
    } finally {
      conn.disconnect()
    }
  }
}
