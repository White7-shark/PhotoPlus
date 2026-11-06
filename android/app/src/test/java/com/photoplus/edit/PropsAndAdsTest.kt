package com.photoplus.edit

import com.photoplus.ads.AdRewards
import com.photoplus.camera.CameraProps
import com.photoplus.camera.FaceTracker
import com.photoplus.edit.PropFit.Pt
import com.photoplus.studio.FaceFinder
import com.photoplus.studio.MaskGeometry
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import java.io.File

class PropsAndAdsTest {
  // A level face, 200 px wide, eyes 80 px apart, forehead top to chin 260 px.
  private val face = FaceFinder.Geometry(
    top = Pt(500f, 300f),
    chin = Pt(500f, 560f),
    eyeL = Pt(460f, 400f),
    eyeR = Pt(540f, 400f),
    width = 200f,
  )

  // ---- fitting props to a face (mirrors placement.ts) --------------------------

  @Test fun hatsSitOnTheForeheadGlassesOnTheEyesOutfitsUnderTheChin() {
    // Fedora: crown base at 0.705 of the art, 1.3 face widths across the crown.
    val hat = PropFit.fit(PropFit.Spec(PropFit.Anchor.HEAD, 0.5f, 0.705f, 1.3f / 0.476f, 0.05f, 0.56f), face)
    assertEquals(500f, hat.cx, 0.01f)
    assertEquals(1.3f / 0.476f * 200f, hat.width, 0.01f)
    // Its anchor row lands 5% of the face height below the forehead top.
    val anchorY = hat.cy + (0.705f - 0.5f) * hat.width * 0.56f
    assertEquals(300f + 0.05f * 260f, anchorY, 0.5f)
    assertEquals(0f, hat.rotation, 0.01f)

    val glasses = PropFit.fit(PropFit.Spec(PropFit.Anchor.EYES, 0.5f, 0.5f, 2.5f, 0f, 0.38f), face)
    assertEquals(500f, glasses.cx, 0.01f)
    assertEquals(400f, glasses.cy, 0.01f)
    assertEquals(200f, glasses.width, 0.01f) // 2.5 × eye distance

    val tie = PropFit.fit(PropFit.Spec(PropFit.Anchor.NECK, 0.5f, 0.136f, 1.4f, 0.4f, 0.7f), face)
    assertTrue(tie.cy > 560f, "outfit pieces hang below the chin")
  }

  @Test fun propsTurnWithTheHeadAndKeepThePersonsAdjustment() {
    // The same face turned 20° clockwise about the nose.
    fun turn(p: Pt): Pt {
      val r = PropFit.rotate(p.x - 500f, p.y - 450f, 20f)
      return Pt(500f + r.x, 450f + r.y)
    }
    val tilted = FaceFinder.Geometry(turn(face.top), turn(face.chin), turn(face.eyeL), turn(face.eyeR), 200f)
    val spec = PropFit.Spec(PropFit.Anchor.EYES, 0.5f, 0.5f, 2.5f, 0f, 0.38f)
    val fitted = PropFit.fit(spec, tilted)
    assertEquals(20f, fitted.rotation, 0.05f)
    // Shifted half a face width "up" the face and made 10% bigger: the shift
    // follows the face's own axes, not the photo's.
    val adjusted = PropFit.adjusted(fitted, tilted, PropFit.Adjust(du = 0f, dv = -0.5f, scale = 1.1f, turn = 5f))
    val up = PropFit.rotate(0f, -100f, 20f)
    assertEquals(fitted.cx + up.x, adjusted.cx, 0.05f)
    assertEquals(fitted.cy + up.y, adjusted.cy, 0.05f)
    assertEquals(fitted.width * 1.1f, adjusted.width, 0.01f)
    assertEquals(25f, adjusted.rotation, 0.01f)
  }

  @Test fun theNearestFaceIsChosen() {
    val other = face.copy(top = Pt(100f, 300f), chin = Pt(100f, 560f), eyeL = Pt(60f, 400f), eyeR = Pt(140f, 400f))
    assertEquals(other, PropFit.nearest(listOf(face, other), 120f, 420f))
    assertEquals(face, PropFit.nearest(listOf(face, other), 480f, 380f))
    assertNull(PropFit.nearest(emptyList(), 0f, 0f))
  }

  // ---- camera: preview → photo -------------------------------------------------

  private fun placed(cx: Float, cy: Float, rotation: Float = 10f, flip: Boolean = false) = CameraProps.Placed(
    "fedora", cx, cy, 0.4f, rotation, flip, PropFit.Spec(PropFit.Anchor.HEAD, 0.5f, 0.7f, 2.7f, 0.05f, 0.56f), null,
  )

  @Test fun previewPositionsLandOnTheSameSpotInThePhoto() {
    // Phone upright, back camera: same fractions.
    val (p, flip) = CameraProps.toPhoto(placed(0.25f, 0.3f), 3000, 4000, 0, mirrored = false)
    assertEquals(750f, p.cx, 0.01f)
    assertEquals(1200f, p.cy, 0.01f)
    assertEquals(1200f, p.width, 0.01f)
    assertEquals(10f, p.rotation, 0.01f)
    assertFalse(flip)
    // Front camera saved unmirrored: left and right swap, the art flips, the tilt reverses.
    val (m, mflip) = CameraProps.toPhoto(placed(0.25f, 0.3f), 3000, 4000, 0, mirrored = true)
    assertEquals(2250f, m.cx, 0.01f)
    assertEquals(-10f, m.rotation, 0.01f)
    assertTrue(mflip)
    // Phone turned 90° clockwise: the photo is landscape; the preview's width is its height.
    val (t, _) = CameraProps.toPhoto(placed(0.25f, 0.3f, rotation = 0f), 4000, 3000, 90, mirrored = false)
    val (ux, uy) = MaskGeometry.previewToUpright(0.25f, 0.3f, 90, false)
    assertEquals(ux * 4000, t.cx, 0.01f)
    assertEquals(uy * 3000, t.cy, 0.01f)
    assertEquals(0.4f * 3000, t.width, 0.01f)
    assertEquals(90f, t.rotation, 0.01f)
  }

  @Test fun cameraPropsAreValidated() {
    val parsed = CameraProps.parse(
      listOf(
        mapOf("id" to "fedora", "cx" to 0.5, "cy" to 0.3, "width" to 0.4, "anchor" to "head", "ax" to 0.5, "ay" to 0.7, "fit" to 2.7, "tracked" to true, "du" to 0.1, "scale" to 1.2),
        mapOf("id" to "../../etc/passwd", "cx" to 0.5),
        mapOf("id" to "custom-abcdef0123456789", "cx" to Double.NaN, "width" to 99.0),
        "junk",
      ),
    )
    assertEquals(2, parsed.size)
    assertEquals(PropFit.Anchor.HEAD, parsed[0].spec.anchor)
    assertNotNull(parsed[0].adjust)
    assertEquals(1.2f, parsed[0].adjust!!.scale, 1e-6f)
    // Bad numbers fall back to defaults or are clamped; unknown anchors are free.
    assertEquals(0.5f, parsed[1].cx, 1e-6f)
    assertEquals(3f, parsed[1].width, 1e-6f)
    assertEquals(PropFit.Anchor.FREE, parsed[1].spec.anchor)
    assertNull(parsed[1].adjust)
    assertEquals(CameraProps.MAX_PROPS, CameraProps.parse(List(20) { mapOf("id" to "cap") }).size)
  }

  @Test fun liveFacesAreMeasuredWithinWhatThePreviewShows() {
    // A 640×480 sensor frame rotated 90° for a portrait screen, cropped to 3:4.
    val crop = FaceTracker.Mapping.Box(80, 0, 560, 480) // in buffer pixels
    val upright = FaceTracker.Mapping.uprightCrop(crop, 640, 480, 90)
    assertEquals(FaceTracker.Mapping.Box(0, 80, 480, 560), upright)
    val (x, y) = FaceTracker.Mapping.point(Pt(120f, 200f), upright, mirror = false)
    assertEquals(0.25, x, 1e-6)
    assertEquals(0.25, y, 1e-6)
    // The front camera's preview is mirrored, and so are the faces (eyes stay left/right as seen).
    val g = FaceTracker.Mapping.toPreview(
      FaceFinder.Geometry(Pt(240f, 200f), Pt(240f, 440f), Pt(200f, 300f), Pt(280f, 300f), 240f),
      crop, 640, 480, 90, mirror = true,
    )
    @Suppress("UNCHECKED_CAST")
    val eyeL = g["eyeL"] as Map<String, Double>
    @Suppress("UNCHECKED_CAST")
    val eyeR = g["eyeR"] as Map<String, Double>
    assertTrue(eyeL["x"]!! < eyeR["x"]!!)
    assertEquals(1.0 - 200.0 / 480.0, eyeR["x"]!!, 1e-6)
    assertEquals(0.5, g["width"] as Double, 1e-6)
    // Other rotations keep the crop inside the upright frame.
    assertEquals(FaceTracker.Mapping.Box(80, 0, 560, 480), FaceTracker.Mapping.uprightCrop(crop, 640, 480, 0))
    assertEquals(FaceTracker.Mapping.Box(80, 0, 560, 480), FaceTracker.Mapping.uprightCrop(crop, 640, 480, 180))
    assertEquals(FaceTracker.Mapping.Box(0, 80, 480, 560), FaceTracker.Mapping.uprightCrop(crop, 640, 480, 270))
  }

  // ---- custom props ------------------------------------------------------------

  @Test fun customPropIdsAreSafe() {
    assertTrue(EditDocument.isKnownProp("fedora"))
    assertTrue(EditDocument.isKnownProp("medal"))
    assertTrue(EditDocument.isKnownProp(CustomProps.newId()))
    assertFalse(EditDocument.isKnownProp("custom-../../x"))
    assertFalse(EditDocument.isKnownProp("custom-ABC123456789"))
    assertFalse(EditDocument.isKnownProp("unicorn"))
    assertEquals(25, EditDocument.PROPS.size)
  }

  @Test fun masksScaleSmoothlyAndTrimToTheObject() {
    val small = byteArrayOf(0, 0, 0, 255.toByte())
    val big = MaskGeometry.resizeBilinear(small, 2, 2, 8, 8)
    assertEquals(0, big[0].toInt() and 0xFF)
    assertEquals(255, big[63].toInt() and 0xFF)
    val mid = big[3 * 8 + 4].toInt() and 0xFF
    assertTrue(mid in 1..254, "edges stay soft")
    val mask = ByteArray(100 * 80)
    for (y in 20 until 50) for (x in 30 until 70) mask[y * 100 + x] = 200.toByte()
    assertEquals(MaskGeometry.Crop(30, 20, 40, 30), MaskGeometry.bounds(mask, 100, 80))
    assertNull(MaskGeometry.bounds(ByteArray(16), 4, 4))
  }

  // ---- frames --------------------------------------------------------------------

  @Test fun thereAreTwentyOrMoreDistinctFrames() {
    val ids = FrameStyles.ALL.map { it.id }
    assertTrue(ids.size >= 20)
    assertEquals(ids.size, ids.toSet().size)
    for (f in FrameStyles.ALL) {
      assertTrue(f.shape in 0..9, f.id)
      assertTrue(f.window[0] >= 0f && f.window[1] >= 0f && f.window[0] + f.window[2] <= 1f && f.window[1] + f.window[3] <= 1f, f.id)
    }
  }

  // ---- rewarded ads unlock ---------------------------------------------------------

  private class MapStore : AdRewards.Store {
    val map = HashMap<String, Int>()
    override fun getInt(key: String) = map[key] ?: 0
    override fun putInt(key: String, value: Int) {
      map[key] = value
    }
    override fun remove(key: String) {
      map.remove(key)
    }
    override fun keys(): Set<String> = map.keys.toSet()
  }

  @Test fun fourAdsUnlockOnePhotoWithoutTheWatermark() {
    val rewards = AdRewards(MapStore())
    val photo = File("/data/captures/PP_1.jpg")
    val clean = File("/data/captures/PP_1_clean.jpg")
    val other = File("/data/captures/PP_2.jpg")
    assertEquals(AdRewards.Progress(0, 4, false), rewards.progress(photo))
    repeat(3) { rewards.reward(photo) }
    assertFalse(rewards.isUnlocked(photo))
    // The untouched copy and the marked photo are the same photo.
    assertEquals(AdRewards.Progress(4, 4, true), rewards.reward(clean))
    assertTrue(rewards.isUnlocked(photo))
    assertTrue(rewards.isUnlocked(clean))
    // Extra ads don't go past the target, and other photos are unaffected.
    assertEquals(4, rewards.reward(photo).watched)
    assertFalse(rewards.isUnlocked(other))
  }

  @Test fun onlyRecentPhotosAreRemembered() {
    val store = MapStore()
    val rewards = AdRewards(store)
    repeat(AdRewards.MAX_PHOTOS + 10) { rewards.reward(File("/p/$it.jpg")) }
    assertEquals(AdRewards.MAX_PHOTOS, store.keys().count { it.startsWith("photo:") })
    assertEquals(1, rewards.progress(File("/p/${AdRewards.MAX_PHOTOS + 9}.jpg")).watched)
    assertEquals(0, rewards.progress(File("/p/0.jpg")).watched)
  }

  @Test fun anAdUnlockedFreeExportHasNoMarkAndPassesTheCheck() {
    assertNull(ExportPolicy.resolveForExport(null, Plan.FREE, adUnlocked = true))
    assertNotNull(ExportPolicy.resolveForExport(null, Plan.FREE, adUnlocked = false))
    // Not unlocked: a free export without its mark still fails.
    val failed = runCatching { ExportPolicy.verify(null, Plan.FREE, 0, 1000, 1000) }
    assertTrue(failed.isFailure)
    ExportPolicy.verify(null, Plan.FREE, 0, 1000, 1000, adUnlocked = true)
  }

  @Test fun theDefaultMarkMatchesTheBrandReferenceSize() {
    val mark = ExportPolicy.resolve(null, Plan.FREE)!!
    // Cap height ≈ 3.2% of the short side (C = 0.46 h).
    assertEquals(0.0322f, 0.46f * mark.height, 0.0015f)
  }
}
