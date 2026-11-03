package com.photoplus.engine

import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Test

class ProcessRequestTest {
  private fun base(vararg ops: Map<String, Any?>) = mapOf(
    "inputPath" to "/data/in.jpg",
    "outputPath" to "/data/out.jpg",
    "operations" to ops.toList(),
  )

  @Test fun parsesAllOperationTypes() {
    val req = ProcessRequest.fromMap(
      base(
        mapOf("type" to "tone", "exposure" to 0.5, "contrast" to 0.2),
        mapOf("type" to "grayscale"),
        mapOf("type" to "rotate", "quarterTurns" to 1.0),
        mapOf("type" to "flip", "horizontal" to true),
        mapOf("type" to "crop", "x" to 1.0, "y" to 2.0, "width" to 30.0, "height" to 40.0),
        mapOf("type" to "resize", "width" to 100.0, "height" to 50.0),
        mapOf("type" to "sharpen", "amount" to 0.7),
        mapOf("type" to "saturation", "amount" to -0.3),
      ),
    )
    assertArrayEquals(intArrayOf(1, 2, 3, 4, 5, 6, 7, 8), req.typeIds())
    val params = req.packedParams()
    assertEquals(32, params.size)
    assertEquals(0.5f, params[0])
    assertEquals(0.0f, params[1]) // brightness default
    assertEquals(0.2f, params[2])
    assertEquals(1f, params[12]) // flip horizontal
    assertEquals(30f, params[18]) // crop width
    assertEquals(OutputFormat.JPEG, req.format)
    assertEquals(92, req.quality)
  }

  @Test fun rejectsUnknownTypesAndMissingFields() {
    val unknown = assertThrows(EngineException::class.java) { ProcessRequest.fromMap(base(mapOf("type" to "blur"))) }
    assertEquals(EngineException.INVALID_ARGUMENT, unknown.code)
    assertThrows(EngineException::class.java) { ProcessRequest.fromMap(base(mapOf("type" to "rotate"))) }
    assertThrows(EngineException::class.java) { ProcessRequest.fromMap(mapOf("outputPath" to "/x")) }
    assertThrows(EngineException::class.java) {
      ProcessRequest.fromMap(base(mapOf("type" to "tone", "exposure" to Double.NaN)))
    }
  }

  @Test fun validatesOutputOptions() {
    assertThrows(EngineException::class.java) { ProcessRequest.fromMap(base() + ("quality" to 0)) }
    assertThrows(EngineException::class.java) { ProcessRequest.fromMap(base() + ("maxDimension" to 10)) }
    val unsupported = assertThrows(EngineException::class.java) { ProcessRequest.fromMap(base() + ("format" to "tiff")) }
    assertEquals(EngineException.UNSUPPORTED_FORMAT, unsupported.code)
    assertEquals(OutputFormat.WEBP, ProcessRequest.fromMap(base() + ("format" to "webp")).format)
  }

  @Test fun capsOperationCount() {
    val many = Array(ProcessRequest.MAX_OPERATIONS + 1) { mapOf<String, Any?>("type" to "grayscale") }
    assertThrows(EngineException::class.java) { ProcessRequest.fromMap(base(*many)) }
  }

  @Test fun cropIsRescaledForDownsampledDecode() {
    val req = ProcessRequest.fromMap(
      base(mapOf("type" to "crop", "x" to 100.0, "y" to 50.0, "width" to 400.0, "height" to 200.0),
        mapOf("type" to "resize", "width" to 64.0, "height" to 64.0)),
    )
    val ops = req.operationsForDecodeScale(0.5)
    assertEquals(EngineOp.Crop(50, 25, 200, 100), ops[0])
    assertEquals(EngineOp.Resize(64, 64), ops[1]) // absolute targets are untouched
    assertEquals(req.operations, req.operationsForDecodeScale(1.0))
  }

  @Test fun cropAfterResizeIsNotRescaled() {
    val req = ProcessRequest.fromMap(
      base(mapOf("type" to "resize", "width" to 2000.0, "height" to 1500.0),
        mapOf("type" to "crop", "x" to 1000.0, "y" to 750.0, "width" to 1000.0, "height" to 750.0)),
    )
    assertEquals(EngineOp.Crop(1000, 750, 1000, 750), req.operationsForDecodeScale(0.512)[1])
  }
}
