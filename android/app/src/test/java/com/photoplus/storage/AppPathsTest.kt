package com.photoplus.storage

import com.photoplus.engine.EngineException
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.io.File

class AppPathsTest {
  @TempDir lateinit var tmp: File

  private fun newFolder(name: String) = File(tmp, name).apply { mkdirs() }

  @Test fun acceptsPathsInsideRoots() {
    val root = newFolder("files")
    val file = File(root, "projects/a.jpg")
    assertEquals(file.canonicalFile, AppPaths.resolveWithin(file.path, listOf(root)))
    assertEquals(file.canonicalFile, AppPaths.resolveWithin("file://${file.path}", listOf(root)))
  }

  @Test fun rejectsTraversalAndSiblingPrefixes() {
    val root = newFolder("files")
    newFolder("files-evil")
    assertThrows(EngineException::class.java) { AppPaths.resolveWithin("${root.path}/../secret", listOf(root)) }
    assertThrows(EngineException::class.java) { AppPaths.resolveWithin("${root.path}-evil/x", listOf(root)) }
    assertThrows(EngineException::class.java) { AppPaths.resolveWithin("/etc/passwd", listOf(root)) }
    assertThrows(EngineException::class.java) { AppPaths.resolveWithin("", listOf(root)) }
  }

  @Test fun storageUsageCountsAndClearsRecursively() {
    val dir = newFolder("cache")
    File(dir, "a.bin").writeBytes(ByteArray(100))
    File(dir, "nested").mkdirs()
    File(dir, "nested/b.bin").writeBytes(ByteArray(250))
    assertEquals(350L, StorageUsage.directorySize(dir))
    assertEquals(350L, StorageUsage.clearDirectoryContents(dir))
    assertEquals(0L, StorageUsage.directorySize(dir))
    assertEquals(true, dir.exists())
  }
}
