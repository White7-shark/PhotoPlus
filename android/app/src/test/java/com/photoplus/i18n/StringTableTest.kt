package com.photoplus.i18n

import java.io.File
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class StringTableTest {
  /** The JS side's English source (src/i18n/strings/en.json), from the repository. */
  private fun englishKeys(): Set<String> {
    var dir: File? = File("").absoluteFile
    while (dir != null && !File(dir, "src/i18n/strings/en.json").isFile) dir = dir.parentFile
    val json = File(requireNotNull(dir) { "repository root not found" }, "src/i18n/strings/en.json").readText()
    return Regex("\"([a-z][a-z0-9_]*)\"\\s*:").findAll(json).map { it.groupValues[1] }.toSet()
  }

  @Test
  fun `every key the screens read has its resource`() {
    assertEquals(englishKeys(), STRING_TABLE.keys)
    assertEquals(STRING_TABLE.size, STRING_TABLE.values.toSet().size)
    assertTrue(STRING_TABLE.values.all { it != 0 })
  }
}
