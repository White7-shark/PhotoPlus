package com.photoplus.storage

import com.photoplus.engine.EngineException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class AppPathsTest {
  @get:Rule val tmp = TemporaryFolder()

  @Test fun acceptsPathsInsideRoots() {
    val root = tmp.newFolder("files")
    val file = File(root, "projects/a.jpg")
    assertEquals(file.canonicalFile, AppPaths.resolveWithin(file.path, listOf(root)))
    assertEquals(file.canonicalFile, AppPaths.resolveWithin("file://${file.path}", listOf(root)))
  }

  @Test fun rejectsTraversalAndSiblingPrefixes() {
    val root = tmp.newFolder("files")
    tmp.newFolder("files-evil")
    assertThrows(EngineException::class.java) { AppPaths.resolveWithin("${root.path}/../secret", listOf(root)) }
    assertThrows(EngineException::class.java) { AppPaths.resolveWithin("${root.path}-evil/x", listOf(root)) }
    assertThrows(EngineException::class.java) { AppPaths.resolveWithin("/etc/passwd", listOf(root)) }
    assertThrows(EngineException::class.java) { AppPaths.resolveWithin("", listOf(root)) }
  }

  @Test fun storageUsageCountsAndClearsRecursively() {
    val dir = tmp.newFolder("cache")
    File(dir, "a.bin").writeBytes(ByteArray(100))
    File(dir, "nested").mkdirs()
    File(dir, "nested/b.bin").writeBytes(ByteArray(250))
    assertEquals(350L, StorageUsage.directorySize(dir))
    assertEquals(350L, StorageUsage.clearDirectoryContents(dir))
    assertEquals(0L, StorageUsage.directorySize(dir))
    assertEquals(true, dir.exists())
  }
}
