package com.photoplus.beauty

import com.photoplus.camera.CaptureOptions
import com.photoplus.camera.SessionConfig
import com.photoplus.edit.EditDocument
import com.photoplus.engine.EngineException
import com.photoplus.engine.NativeEngine
import org.json.JSONArray
import org.json.JSONObject
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows

/**
 * Beauty on Android: recipes from JS reach the engine in the right order and
 * range, live-shader uniforms match the desktop parity tool, and the
 * preview-space geometry lines masks and faces up with the viewfinder.
 * beauty_fixture.json is written by scripts/beauty-fixture.mts from the app's
 * own resolver and the desktop uniform code.
 */
class BeautyTest {
  private val fixture: JSONObject by lazy {
    JSONObject(javaClass.classLoader!!.getResourceAsStream("beauty_fixture.json")!!.bufferedReader().readText())
  }

  private fun toKotlin(v: Any?): Any? = when (v) {
    is JSONObject -> v.keys().asSequence().associateWith { toKotlin(v.get(it)) }
    is JSONArray -> (0 until v.length()).map { toKotlin(v.get(it)) }
    JSONObject.NULL -> null
    else -> v
  }

  private fun floats(a: JSONArray) = FloatArray(a.length()) { a.getDouble(it).toFloat() }

  @Test
  fun `every preset reaches the engine exactly as the app resolves it`() {
    val recipes = fixture.getJSONArray("recipes")
    assertEquals(14, recipes.length())
    for (i in 0 until recipes.length()) {
      val r = recipes.getJSONObject(i)
      @Suppress("UNCHECKED_CAST")
      val recipe = BeautyRecipe.fromMap(toKotlin(r.getJSONObject("recipe")) as Map<*, *>)
      assertNotNull(recipe, r.getString("id"))
      val expected = floats(r.getJSONArray("flat"))
      assertEquals(NativeEngine.BEAUTY_RECIPE_FLOATS, expected.size)
      assertArrayEquals(expected, recipe!!.toArray(), 1e-5f, r.getString("id"))
      assertTrue(recipe.active, r.getString("id"))
    }
  }

  @Test
  fun `the grade layout matches pp_beauty_grade`() {
    assertEquals(51, BeautyRecipe.GRADE_USED)
    assertEquals(58, NativeEngine.BEAUTY_GRADE_FLOATS)
    assertEquals(78, NativeEngine.BEAUTY_RECIPE_FLOATS)
  }

  @Test
  fun `bad values are clamped or dropped, never passed on`() {
    val r = BeautyRecipe.fromMap(
      mapOf(
        "portrait" to mapOf("blur" to 3.0, "smooth" to Double.NaN, "brighten" to -1),
        "face" to mapOf("eyes" to 2, "blushColor" to listOf(2, -1, 0.5)),
        "grade" to mapOf("temperature" to 5, "bgGlowSize" to 0, "bandWidth" to 400, "rimColor" to "red", "bgWarmth" to -9),
      ),
    )!!
    val a = r.toArray()
    assertEquals(1f, a[0]) // blur
    assertEquals(0f, a[1]) // smooth NaN -> 0
    assertEquals(0f, a[4]) // brighten
    assertEquals(1f, a[6]) // eyes
    assertArrayEquals(floatArrayOf(1f, 0f, 0.5f), a.copyOfRange(14, 17), 0f) // blush colour
    assertEquals(1f, a[19]) // temperature
    assertEquals(0.02f, r.grade[44]) // glow size
    assertEquals(90f, r.grade[18]) // band width
    assertArrayEquals(floatArrayOf(1f, 0.92f, 0.8f), r.grade.copyOfRange(29, 32), 0f) // rim colour default
    assertEquals(-1f, r.grade[50]) // background warmth
    assertNull(BeautyRecipe.fromMap(mapOf("portrait" to emptyMap<String, Any>())))
    assertNull(BeautyRecipe.fromMap(null))
    assertFalse(BeautyRecipe.none().active)
    assertEquals(0.75f, BeautyRecipe.none().grade[21]) // skin protection default
  }

  @Test
  fun `live uniforms match the desktop parity tool`() {
    val face = fixture.getJSONObject("face")
    val u = LiveBeautyUniforms.face(floats(face.getJSONArray("points")), face.getInt("w"), face.getInt("h"))!!
    val e = face.getJSONObject("expected")
    assertArrayEquals(floats(e.getJSONArray("frame")), u.frame, 1e-3f)
    assertArrayEquals(floats(e.getJSONArray("half")), u.half, 1e-3f)
    assertArrayEquals(floats(e.getJSONArray("marks")), u.marks, 1e-4f)
    assertArrayEquals(floats(e.getJSONArray("eyes")), u.eyes, 1e-3f)
    assertEquals(e.getDouble("eyeW").toFloat(), u.eyeW, 1e-3f)
    assertArrayEquals(floats(e.getJSONArray("mouth")), u.mouth, 1e-3f)
    val studio = fixture.getJSONArray("studioMean")
    for (i in 0 until studio.length()) {
      val s = studio.getJSONObject(i)
      assertEquals(s.getDouble("value").toFloat(), LiveBeautyUniforms.studioMean(s.getDouble("angle").toFloat()), 1e-4f)
    }
    val blur = fixture.getJSONArray("blur")
    for (i in 0 until blur.length()) {
      val b = blur.getJSONObject(i)
      val k = LiveBeautyUniforms.blurWeights(b.getDouble("sigma").toFloat())
      assertEquals(b.getInt("taps"), k.taps)
      assertArrayEquals(floats(b.getJSONArray("weights")), k.weights, 1e-5f)
    }
    val low = fixture.getJSONArray("low")
    for (i in 0 until low.length()) {
      val l = low.getJSONObject(i)
      val s = LiveBeautyUniforms.lowSize(l.getInt("inW"), l.getInt("inH"))
      assertEquals(l.getInt("f"), s.factor)
      assertEquals(l.getInt("w"), s.width)
      assertEquals(l.getInt("h"), s.height)
    }
    assertNull(LiveBeautyUniforms.face(FloatArray(10), 100, 100))
  }

  @Test
  fun `affine maps compose and invert`() {
    val a = Affine(2f, 0.5f, -1f, 3f, 4f, -2f)
    val inv = a.inverse()!!
    val (x, y) = a.apply(0.3f, -1.7f)
    val (bx, by) = inv.apply(x, y)
    assertEquals(0.3f, bx, 1e-5f)
    assertEquals(-1.7f, by, 1e-5f)
    val t = Affine.translate(1f, 2f)
    val s = Affine.scale(3f, 4f)
    assertEquals(s.apply(1f + 1f, 1f + 2f), s.after(t).apply(1f, 1f))
    assertNull(Affine(1f, 2f, 2f, 4f, 0f, 0f).inverse())
    // android.graphics.Matrix values are row-major [a c e / b d f].
    assertEquals(Affine(1f, 4f, 2f, 5f, 3f, 6f), Affine.fromValues(floatArrayOf(1f, 2f, 3f, 4f, 5f, 6f, 0f, 0f, 1f)))
  }

  @Test
  fun `preview space matches the props tracker for every rotation and mirror`() {
    // A 640 x 480 sensor-oriented buffer cropped to 600 x 450.
    val crop = PreviewSpace.Crop(20, 15, 620, 465)
    val box = com.photoplus.camera.FaceTracker.Mapping.Box(20, 15, 620, 465)
    for (rotation in listOf(0, 90, 180, 270)) {
      for (mirror in listOf(false, true)) {
        val m = PreviewSpace.fromBuffer(crop, rotation, mirror)
        val upright = com.photoplus.camera.FaceTracker.Mapping.uprightCrop(box, 640, 480, rotation)
        for ((bx, by) in listOf(100f to 50f, 600f to 400f, 320f to 240f)) {
          // The same buffer point, turned upright by hand.
          val (ux, uy) = when (rotation) {
            90 -> (480f - by) to bx
            180 -> (640f - bx) to (480f - by)
            270 -> by to (640f - bx)
            else -> bx to by
          }
          val (ex, ey) = com.photoplus.camera.FaceTracker.Mapping.point(com.photoplus.edit.PropFit.Pt(ux, uy), upright, mirror)
          val (px, py) = m.apply(bx, by)
          assertEquals(ex.toFloat(), px, 1e-4f, "rot $rotation mirror $mirror")
          assertEquals(ey.toFloat(), py, 1e-4f, "rot $rotation mirror $mirror")
        }
      }
    }
    assertEquals(450 to 600, PreviewSpace.uprightSize(crop, 90))
  }

  @Test
  fun `the shader orientation runs through sensor coordinates`() {
    // Output and analysis buffers both equal to sensor space (identity), no
    // crop, upright: target uv (bottom-left) maps to preview (top-left).
    val toPreview = PreviewSpace.fromBuffer(PreviewSpace.Crop(0, 0, 400, 300), 0, false)
    val o = PreviewSpace.orient(400, 300, Affine.IDENTITY, Affine.IDENTITY, toPreview)!!
    assertEquals(0f to 1f, o.apply(0f, 0f))
    assertEquals(1f to 0f, o.apply(1f, 1f))
    // An output buffer half the sensor's resolution.
    val half = PreviewSpace.orient(200, 150, Affine.scale(0.5f, 0.5f), Affine.IDENTITY, toPreview)!!
    assertEquals(0.5f, half.apply(0.5f, 0.5f).first, 1e-6f)
    assertNull(PreviewSpace.orient(10, 10, Affine(0f, 0f, 0f, 0f, 0f, 0f), Affine.IDENTITY, toPreview))
  }

  @Test
  fun `sessions, captures and edits carry beauty`() {
    assertTrue(SessionConfig.fromMap(mapOf("beauty" to true)).beauty)
    assertFalse(SessionConfig.fromMap(mapOf("resolution" to "max"), SessionConfig()).beauty)
    val recipe = mapOf(
      "portrait" to mapOf("smooth" to 0.5),
      "face" to mapOf("blush" to 0.4),
      "grade" to mapOf("bgGlow" to 0.3),
    )
    val capture = CaptureOptions.fromMap(mapOf("captureId" to "c1", "mode" to "standard", "beautyRecipe" to recipe, "beautyPreset" to "golden_goddess"))
    assertTrue(capture.beautyRecipeActive)
    assertTrue(capture.needsProcessing)
    assertEquals("golden_goddess", capture.beautyPreset)
    val doc = EditDocument.fromMap(mapOf("beauty" to mapOf("preset" to "dreamlight", "recipe" to recipe)))
    assertEquals("dreamlight", doc.beauty?.preset)
    assertTrue(doc.beauty!!.recipe.active)
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("beauty" to mapOf("recipe" to recipe))) }
    assertThrows<EngineException> { EditDocument.fromMap(mapOf("beauty" to mapOf("preset" to "x", "recipe" to "no"))) }
  }
}
