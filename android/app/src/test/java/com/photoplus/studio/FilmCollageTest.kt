package com.photoplus.studio

import com.photoplus.engine.EngineException
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows

class FilmCollageTest {
  @Test fun filmRequestsAreReadAndChecked() {
    val spec = FilmCollage.filmFromMap(
      mapOf(
        "photos" to listOf("/a.jpg", "/b.jpg"),
        "frames" to listOf(
          mapOf("photo" to 0, "look" to 1, "zoom" to 9, "panX" to -3, "flip" to true),
          mapOf("photo" to 1),
        ),
        "style" to "booth",
        "vertical" to true,
        "portrait" to true,
      ),
    )
    assertEquals(FilmCollage.Style.BOOTH, spec.style)
    assertTrue(spec.vertical && spec.portrait && spec.markings)
    assertEquals(FilmCollage.Frame(0, 1, 4f, -1f, 0f, true), spec.frames[0])
    // The engine's 8 floats per frame, with the face as the focus (or -1 for none).
    assertArrayEquals(floatArrayOf(0f, 1f, 4f, -1f, 0f, 1f, 0.4f, 0.3f, 0f, 0f, 0f, 0f, 0f), spec.frames[0].floats(0.4f to 0.3f))
    assertArrayEquals(floatArrayOf(1f, 0f, 1f, 0f, 0f, 0f, -1f, -1f, 0f, 0f, 0f, 0f, 0f), spec.frames[1].floats(null))
    assertTrue(FilmCollage.Style.AMBER.premium && FilmCollage.Style.CINEMA.premium)
    assertFalse(FilmCollage.Style.CLASSIC.premium)

    for (bad in listOf(
      mapOf("photos" to emptyList<String>(), "frames" to listOf(mapOf("photo" to 0))),
      mapOf("photos" to listOf("/a.jpg"), "frames" to listOf(mapOf("photo" to 1))),
      mapOf("photos" to listOf("/a.jpg"), "frames" to listOf(mapOf("photo" to 0, "look" to 10))),
      mapOf("photos" to listOf("/a.jpg"), "frames" to emptyList<Any>()),
      mapOf("photos" to listOf("/a.jpg"), "frames" to List(13) { mapOf("photo" to 0) }),
      mapOf("photos" to listOf("/a.jpg"), "frames" to listOf(mapOf("photo" to 0)), "style" to "polaroid"),
    )) {
      val e = assertThrows<EngineException> { FilmCollage.filmFromMap(bad) }
      assertEquals(EngineException.INVALID_ARGUMENT, e.code)
    }
  }

  @Test fun framesAndFilmTakeTheirOwnSettings() {
    val spec = FilmCollage.filmFromMap(
      mapOf(
        "photos" to listOf("/a.jpg"),
        "frames" to listOf(mapOf("photo" to 0, "look" to 2, "lookStrength" to 0.25, "brightness" to 3, "contrast" to -0.5, "saturation" to 0.4, "warmth" to -2)),
        "grain" to 9,
        "spacing" to 0.1,
        "filmColor" to "#FFFFFF",
        "label" to "  Anna & Tom * 2026 ",
      ),
    )
    val f = spec.frames[0]
    assertEquals(FilmCollage.Frame(0, 2, 1f, 0f, 0f, false, 0.25f, 1f, -0.5f, 0.4f, -1f), f)
    // The engine is told how much of the look fades (1 - strength).
    assertEquals(0.75f, f.floats(null)[8])
    assertEquals(3f, spec.grain)
    assertEquals(0.3f, spec.spacing)
    assertEquals(0xFFFFFF, spec.baseColor)
    assertEquals("ANNA & TOM * 2026", spec.label)
    for (bad in listOf(mapOf("filmColor" to "white"), mapOf("label" to "émile <3"), mapOf("label" to "A".repeat(30)))) {
      val e = assertThrows<EngineException> {
        FilmCollage.filmFromMap(mapOf("photos" to listOf("/a.jpg"), "frames" to listOf(mapOf("photo" to 0))) + bad)
      }
      assertEquals(EngineException.INVALID_ARGUMENT, e.code)
    }
  }

  @Test fun downloadSizesAreCheckedAndPremiumFrom720p() {
    assertEquals(null, ExportSize.parse(null))
    assertEquals(1080, ExportSize.parse(1080))
    for (bad in listOf(1000, "4k", -1)) assertThrows<EngineException> { ExportSize.parse(bad) }
    assertFalse(ExportSize.needsPremium(360) || ExportSize.needsPremium(480) || ExportSize.needsPremium(null))
    assertTrue(listOf(720, 1080, 1440, 2160).all { ExportSize.needsPremium(it) })
    // The short edge becomes the size asked for; the long edge follows (and never passes the limit).
    assertEquals(3840 to 2160, ExportSize.target(4000, 2250, 2160))
    assertEquals(480 to 600, ExportSize.target(1080, 1350, 480))
    assertEquals(ExportSize.MAX_EDGE, ExportSize.target(900, 12000, 2160).second)
  }

  @Test fun collageRequestsAreReadAndChecked() {
    val spec = FilmCollage.collageFromMap(
      mapOf(
        "photos" to listOf("/a.jpg"),
        "cells" to listOf(mapOf("x" to 0, "y" to 0, "w" to 0.5, "h" to 1, "photo" to 0, "look" to 2)),
        "aspect" to 9,
        "gap" to 1,
        "background" to "#F6F1EA",
      ),
    )
    assertEquals(2.5f, spec.aspect)
    assertEquals(0.1f, spec.gap)
    assertEquals(0xF6F1EA, spec.background)
    assertEquals(2, spec.cells[0].frame.look)
    assertEquals(0xFFFFFF, FilmCollage.collageFromMap(mapOf("photos" to listOf("/a.jpg"), "cells" to listOf(mapOf("x" to 0, "y" to 0, "w" to 1, "h" to 1)), "background" to "red")).background)
    for (cell in listOf(mapOf("x" to 0.6, "y" to 0, "w" to 0.5, "h" to 1), mapOf("x" to 0, "y" to 0, "w" to 0, "h" to 1), mapOf("y" to 0, "w" to 1, "h" to 1))) {
      val e = assertThrows<EngineException> { FilmCollage.collageFromMap(mapOf("photos" to listOf("/a.jpg"), "cells" to listOf(cell))) }
      assertEquals(EngineException.INVALID_ARGUMENT, e.code)
    }
  }
}
