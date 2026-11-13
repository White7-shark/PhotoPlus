package com.photoplus.camera

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class AutoLightTest {
  private fun reading(
    plan: LightPlan,
    lux: Float? = 5f,
    frames: Int = 8,
    bias: Float = 0f,
    night: Float = 0.7f,
    color: Int = 0xFFE0C0,
  ) = LightReading(lux, 0.05f, 0.01f, 5f, false, plan, frames, bias, night, color)

  private fun context(front: Boolean = false, hasFlash: Boolean = true, index: Int = 0, screen: Boolean = front) =
    LightContext(front, hasFlash, exposureStep = 1f / 3f, exposureMin = -12, exposureMax = 12, exposureIndex = index, screenFlashReady = screen)

  private fun request(
    mode: CaptureMode = CaptureMode.STANDARD,
    flash: FlashSetting = FlashSetting.AUTO,
    multi: Boolean = true,
    auto: Boolean = true,
  ) = CaptureOptions(captureId = "t", mode = mode, flash = flash, autoLight = auto, autoLightMultiShot = multi)

  @Test fun meterOutputIsReadSafely() {
    val out = floatArrayOf(1.2f, 5.7f, 1f, 0.05f, 0.01f, 0.2f, 6f, 0f, 1f, 9f, -0.7f, 0.8f, 255f, 230f, 200f)
    val r = LightReading.fromMeter(out)
    assertEquals(LightPlan.MOONLIGHT, r.plan)
    assertEquals(5.7f, r.lux)
    assertEquals(9, r.frames)
    assertEquals(-0.7f, r.exposureBias)
    assertEquals(0xFFE6C8, r.screenColor)
    // Unknown exposure: no lux. Nonsense numbers are kept in range.
    val bad = out.copyOf().also { it[2] = 0f; it[9] = Float.NaN; it[10] = 7f; it[11] = 3f; it[12] = 999f; it[8] = 42f }
    val b = LightReading.fromMeter(bad)
    assertNull(b.lux)
    assertEquals(1, b.frames)
    assertEquals(0f, b.exposureBias)
    assertEquals(1f, b.night)
    assertEquals(0xFF, b.screenColor shr 16)
    assertEquals(LightPlan.NORMAL, b.plan)
    val input = LightReading.meterInput(800, 33_000_000L, 1.8f, front = true, hasFlash = false, faceLuma = null, focusDiopters = 2f)
    assertEquals(LightReading.METER_IN, input.size)
    assertEquals(0.033f, input[1], 1e-6f)
    assertEquals(1f, input[3])
    assertEquals(-1f, input[5])
  }

  @Test fun planChangesOnlyOnceItHolds() {
    val s = LightSteadier(hold = 3)
    assertTrue(s.feed(reading(LightPlan.NORMAL)))
    // A single dark frame (a hand over the lens) does not switch the plan.
    assertFalse(s.feed(reading(LightPlan.MOONLIGHT)))
    assertFalse(s.feed(reading(LightPlan.NORMAL, lux = 300f)))
    assertEquals(LightPlan.NORMAL, s.current!!.plan)
    assertEquals(300f, s.current!!.lux) // numbers follow within the plan
    assertFalse(s.feed(reading(LightPlan.MOONLIGHT)))
    assertFalse(s.feed(reading(LightPlan.MOONLIGHT)))
    assertTrue(s.feed(reading(LightPlan.MOONLIGHT)))
    assertEquals(LightPlan.MOONLIGHT, s.current!!.plan)
    s.reset()
    assertNull(s.current)
  }

  @Test fun normalLightLeavesTheShotAlone() {
    assertNull(AutoLight.decide(request(), reading(LightPlan.NORMAL), context(), DeviceTier.STANDARD))
    assertNull(AutoLight.decide(request(), null, context(), DeviceTier.STANDARD))
    // Off, or a mode Auto Light does not touch.
    assertNull(AutoLight.decide(request(auto = false), reading(LightPlan.MOONLIGHT), context(), DeviceTier.STANDARD))
    for (mode in listOf(CaptureMode.RAW, CaptureMode.SUPER_RES, CaptureMode.PORTRAIT)) {
      assertNull(AutoLight.decide(request(mode = mode), reading(LightPlan.MOONLIGHT), context(), DeviceTier.STANDARD))
    }
  }

  @Test fun dimRoomsGetAMoonlightBurstWithNightTone() {
    val d = AutoLight.decide(request(), reading(LightPlan.MOONLIGHT, frames = 11, night = 0.9f), context(), DeviceTier.STANDARD)!!
    assertEquals(LightPlan.MOONLIGHT, d.plan)
    assertEquals(CaptureMode.NIGHT, d.mode)
    assertEquals(8, d.frames) // the tier's limit
    assertEquals(LightFlash.NONE, d.flash)
    assertEquals(0.9f, d.tone!!.night)
    assertEquals(0.6f * 0.9f, d.tone!!.whiteBalance, 1e-6f)
    assertEquals("Moonlight", d.label)
    assertEquals(10, AutoLight.decide(request(), reading(LightPlan.MOONLIGHT, frames = 12), context(), DeviceTier.PERFORMANCE)!!.frames)
    assertEquals(6, AutoLight.decide(request(), reading(LightPlan.MOONLIGHT, frames = 12), context(), DeviceTier.ENTRY)!!.frames)
  }

  @Test fun withoutMultiShotItIsOneFrameAndTone() {
    val d = AutoLight.decide(request(multi = false), reading(LightPlan.MOONLIGHT, frames = 11), context(), DeviceTier.PERFORMANCE)!!
    assertEquals(CaptureMode.ENHANCE, d.mode)
    assertEquals(1, d.frames)
    assertNotNull(d.tone)
    val b = AutoLight.decide(request(multi = false), reading(LightPlan.BRIGHT_BALANCE, frames = 5, bias = -2f), context(), DeviceTier.PERFORMANCE)!!
    assertEquals(1, b.frames)
    assertEquals(-3, b.exposureIndex) // one stop at most for a single frame
    assertEquals(-1f, b.tone!!.captureBias, 1e-5f)
  }

  @Test fun harshLightIsHeldBackAndRestoredInTone() {
    val d = AutoLight.decide(request(), reading(LightPlan.BRIGHT_BALANCE, frames = 5, bias = -1.5f), context(), DeviceTier.STANDARD)!!
    assertEquals(LightPlan.BRIGHT_BALANCE, d.plan)
    assertEquals(CaptureMode.NIGHT, d.mode)
    assertEquals(5, d.frames)
    assertEquals(-4, d.exposureIndex) // -1.5 EV in 1/3 steps (rounded half up)
    assertEquals(-4f / 3f, d.tone!!.captureBias, 1e-5f)
    assertEquals(0f, d.tone!!.night)
    // The user's own exposure choice is respected: nothing held back, nothing to restore.
    val user = AutoLight.decide(request(), reading(LightPlan.BRIGHT_BALANCE, bias = -1.5f), context(index = 2), DeviceTier.STANDARD)!!
    assertNull(user.exposureIndex)
    assertEquals(0f, user.tone!!.captureBias)
  }

  @Test fun veryDarkUsesTheRightLight() {
    val dark = reading(LightPlan.FLASH, lux = 0.4f, frames = 6)
    // Rear camera with a flash: Natural Flash (ambient burst + flash frame).
    val rear = AutoLight.decide(request(), dark, context(), DeviceTier.STANDARD)!!
    assertEquals(LightFlash.NATURAL, rear.flash)
    assertEquals(CaptureMode.NIGHT, rear.mode)
    assertEquals(6, rear.frames)
    assertEquals("Natural Flash", rear.label)
    // Front camera: the screen, tinted toward the room.
    val front = AutoLight.decide(request(), dark, context(front = true), DeviceTier.STANDARD)!!
    assertEquals(LightFlash.SCREEN, front.flash)
    assertEquals(1, front.frames)
    assertEquals(0xFFE0C0, front.screenColor)
    assertEquals("Selfie Light", front.label)
    // Flash off: never a flash, the longest burst instead.
    for (ctx in listOf(context(), context(front = true))) {
      val off = AutoLight.decide(request(flash = FlashSetting.OFF), dark, ctx, DeviceTier.STANDARD)!!
      assertEquals(LightFlash.NONE, off.flash)
      assertEquals(8, off.frames)
      assertEquals("Moonlight", off.label)
      assertTrue(off.tone!!.night >= 0.8f)
    }
    // No flash unit and no screen light: Moonlight.
    assertEquals(LightFlash.NONE, AutoLight.decide(request(), dark, context(hasFlash = false), DeviceTier.STANDARD)!!.flash)
    assertEquals(LightFlash.NONE, AutoLight.decide(request(), dark, context(front = true, screen = false), DeviceTier.STANDARD)!!.flash)
  }

  @Test fun flashOnIsHonoured() {
    // Front + flash on: the screen light even in a bright room.
    val selfie = AutoLight.decide(request(flash = FlashSetting.ON), reading(LightPlan.NORMAL, lux = 400f), context(front = true), DeviceTier.STANDARD)!!
    assertEquals(LightFlash.SCREEN, selfie.flash)
    // Rear + flash on in a dim room: the flash's detail with the room's light, not a flashless burst.
    val dim = AutoLight.decide(request(flash = FlashSetting.ON), reading(LightPlan.MOONLIGHT), context(), DeviceTier.STANDARD)!!
    assertEquals(LightFlash.NATURAL, dim.flash)
    // Auto in a dim (not very dark) room: no flash.
    assertEquals(LightFlash.NONE, AutoLight.decide(request(), reading(LightPlan.MOONLIGHT), context(), DeviceTier.STANDARD)!!.flash)
  }

  @Test fun labelsFollowTheFlashChoice() {
    val dark = reading(LightPlan.FLASH)
    assertEquals("Natural Flash", AutoLight.label(dark, context(), FlashSetting.AUTO))
    assertEquals("Selfie Light", AutoLight.label(dark, context(front = true), FlashSetting.AUTO))
    assertEquals("Moonlight", AutoLight.label(dark, context(), FlashSetting.OFF))
    assertEquals("Bright Balance", AutoLight.label(reading(LightPlan.BRIGHT_BALANCE), context(), FlashSetting.OFF))
    assertNull(AutoLight.label(reading(LightPlan.NORMAL), context(), FlashSetting.AUTO))
  }

  @Test fun longBurstsAreDecodedSmaller() {
    val base = ModePlan(6, 3200, 1)
    val p = AutoLight.burstPlan(base, 10)
    assertEquals(10, p.frames)
    assertTrue(p.decodeDimension < 3200)
    // Same pixel budget (frames × size²) within rounding.
    val ratio = (p.frames.toDouble() * p.decodeDimension * p.decodeDimension) / (6.0 * 3200 * 3200)
    assertEquals(1.0, ratio, 0.02)
    assertEquals(ModePlan(4, 3200, 1), AutoLight.burstPlan(base, 4))
    assertEquals(1, AutoLight.burstPlan(ModePlan(1, 2048, 1), 1).frames)
  }

  @Test fun lowLightBoostTurnsOnInTheDarkAndOffOnlyWhenClearlyBrighter() {
    assertTrue(AutoLight.wantLowLightBoost(false, reading(LightPlan.MOONLIGHT, lux = 4f)))
    assertFalse(AutoLight.wantLowLightBoost(false, reading(LightPlan.MOONLIGHT, lux = 25f)))
    assertFalse(AutoLight.wantLowLightBoost(false, reading(LightPlan.NORMAL, lux = 4f)))
    // Once on, the brighter live view (25 lux) keeps it on; a bright room turns it off.
    assertTrue(AutoLight.wantLowLightBoost(true, reading(LightPlan.MOONLIGHT, lux = 25f)))
    assertFalse(AutoLight.wantLowLightBoost(true, reading(LightPlan.MOONLIGHT, lux = 80f)))
    assertFalse(AutoLight.wantLowLightBoost(true, reading(LightPlan.NORMAL, lux = 30f)))
    assertTrue(AutoLight.wantLowLightBoost(true, null))
  }

  @Test fun onlyMeaningfulChangesAreReported() {
    val a = reading(LightPlan.MOONLIGHT, lux = 5f)
    assertTrue(AutoLight.worthReporting(null, a))
    assertFalse(AutoLight.worthReporting(a, a.copy(lux = 5.5f)))
    assertTrue(AutoLight.worthReporting(a, a.copy(lux = 12f)))
    assertTrue(AutoLight.worthReporting(a, a.copy(plan = LightPlan.FLASH)))
    assertTrue(AutoLight.worthReporting(a, a.copy(lux = null)))
  }

  @Test fun captureOptionsParseAutoLight() {
    val defaults = CaptureOptions.fromMap(mapOf("mode" to "standard"))
    assertTrue(defaults.autoLight)
    assertFalse(defaults.autoLightMultiShot)
    val set = CaptureOptions.fromMap(mapOf("mode" to "standard", "autoLight" to false, "autoLightMultiShot" to true))
    assertFalse(set.autoLight)
    assertTrue(set.autoLightMultiShot)
  }

  @Test fun toneParamsAreInTheEngineOrder() {
    val p = LightTone(night = 0.5f, captureBias = -1f, whiteBalance = 0.3f).toParams()
    assertEquals(8, p.size)
    assertEquals(0.5f, p[0])
    assertEquals(-1f, p[1])
    assertEquals(-1f, p[2])
    assertEquals(0.3f, p[5])
    assertEquals(-1f, p[6])
    assertEquals(1f, p[7])
  }
}
