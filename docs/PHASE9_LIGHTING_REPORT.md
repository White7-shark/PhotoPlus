# Phase 9 — Auto Light: Dark Rooms, Night and Harsh Light

The camera now reads the light in every scene and handles it the way the
best phone cameras do:

- **Moonlight:** in a dim room it takes a burst and merges it. The photo comes out clear, but night still looks like night.
- **Natural Flash:** in a very dark room, with something near, it adds the flash's detail to the room's own light.
- **Selfie Light:** on the front camera, the screen lights the face. The screen is tinted toward the room's light, so a warm room stays warm.
- **Bright Balance:** in harsh or backlit light, it holds the exposure back to keep the sky, then opens up the shadows.

The camera shows a small Auto Light chip saying what it is doing, and Settings has an on/off switch for it.

## Research

The design follows published work, not memory alone:

| Source | What we took |
|---|---|
| Google Research, *Night Sight: Seeing in the Dark on Pixel Phones* (2018) | Operating points for the plans: a single shot struggles below ~30 lux, merged bursts work to ~3 lux, and Night Sight goes to ~0.3 lux. Burst length grows as the light fades. Tone keeps night looking like night: a lower key, true blacks, a gentle S-curve, and "brighter than reality" but not daylight. Learned white balance; we use a grey-edge estimate kept a touch warm. |
| Hasinoff et al., *Burst photography for high dynamic range and low-light imaging on mobile cameras* (SIGGRAPH Asia 2016); Google Research, *Live HDR+ and Dual Exposure Controls* (2020) | The burst is underexposed to protect highlights, merged to remove noise, then local tone mapping restores the shadows. Exposure for the highlights and tone for the shadows are separate controls. |
| Petschnigg et al., *Digital Photography with Flash and No-Flash Image Pairs* (SIGGRAPH 2004); Eisemann & Durand, *Flash Photography Enhancement via Intrinsic Relighting* (SIGGRAPH 2004) | Natural Flash: the ambient photo filtered with the flash photo as a guide (joint filtering), detail transfer from the flash frame, and masks for flash shadows and hot spots, which are taken from the ambient photo. |
| Android Developers, *Screen flash implementation guidelines* (Camera2); CameraX release notes 1.4–1.6 | Selfie Light: `ImageCapture.FLASH_MODE_SCREEN` with `PreviewView.setScreenFlashWindow`, `getScreenFlash` and `setScreenFlashOverlayColor`. CameraX raises the overlay to full brightness and lets exposure settle on it before capture. **Low Light Boost** (`CameraInfo.isLowLightBoostSupported`, `CameraControl.enableLowLightBoostAsync`, Android 15+) brightens the live view in the dark. All of these are stable in CameraX 1.6.2, the version the app uses, checked against the library itself. |
| He, Sun & Tang, *Guided Image Filtering* (2010/2013) | The edge-aware base layer for local tone and the joint filter for fusion. |
| van de Weijer, Gevers & Gijsenij, *Edge-Based Color Constancy* (2007) | Grey-edge white balance for sodium and mixed light at night. |
| Brooks et al., *Unprocessing Images for Learned Raw Denoising* (CVPR 2019) | The shot-plus-read noise model behind the dark-room simulator. |
| True Tone flash (Apple) | The idea of matching the flash's colour to the room. We apply it to the screen light. |

## How it works

### Meter (`light::meter`, `pp_light_meter`)

- **Brightness:** scene brightness comes from the camera's own exposure: EV100 = log2(N²/t) − log2(ISO/100). It is corrected by how bright the preview actually came out, because in the dark the camera runs out of exposure. lux ≈ 2.5 · 2^EV100.
- **Contrast:** read from a 160 px preview: highlights, shadows, dynamic range (2nd–98th percentile), and backlight (a face much darker than the scene).
- **Plan:**
  - Normal above ~40 lux.
  - Moonlight below that, with 4–12 frames as the light fades. The night look is full by ~3 lux and kept low for portraits.
  - Flash below ~1 lux, but only when something is near: a selfie, a face, or the lens focused within ~2.5 m. A skyline gets a long burst instead.
  - Bright Balance for burnt-out highlights, both ends of the range in use, or backlight. The exposure is held back 1–2 stops.
- **Screen-light colour:** white tinted halfway toward the room's mid-tones, never below 78% in any channel.

### Tone (`light::tone`, `pp_light_tone`)

1. Log luminance on a 640 px grid, with an edge-aware base from the guided filter.
2. **Automatic amounts from headroom:**
   - Highlights are compressed only when the brightest 1% sit more than 2.5 stops above the scene (a sky or a lamp). Skin shine stays as it is.
   - Shadows are lifted only when the darkest 5% sit more than 3 stops below the scene.
3. **Exposure:**
   - Any exposure the camera held back on purpose is restored in full.
   - The scene's own darkness is lifted only part way: half a stop, then half of the rest. A dim room still feels like one, and deep skin and dark clothes never turn grey.
   - Night scenes are brought to a level set by their lit parts, not their sky.
4. **Night look:**
   - Flat, dark areas (sky, empty walls) sink toward black and lose their noise.
   - True black point and a gentle S-curve.
   - Lifted shadows have their colour noise calmed.
5. Grey-edge white balance for low light, kept a touch warm.

### Natural Flash (`light::flashFuse`, `pp_flash_fuse`)

1. **Alignment:** the flash frame is lined up to the ambient one by its structure, not its brightness, because the two are lit differently.
2. **Fusion:**
   - The ambient photo is filtered with the flash photo as the guide, for the room's colour and mood.
   - The flash frame's fine detail is added where its signal is reliable.
   - A little of the flash's own light is kept, at 30%, recoloured to the room's light.
3. **Flash shadows and hot spots** are found from the flash/ambient ratio and its sudden drops, and taken from the ambient photo.

## Validation

All comparison images stayed private (sent in the conversation, not committed).

| Test | Result |
|---|---|
| **Six real scenes** from Google's public HDR+ dataset (raw bursts, `scripts/light-bench-prep.py`): night city, dim chapel, dark restaurant portrait, dim evening (dog), harsh sun, backlit street | Side by side with Google's own HDR+ results: <ul><li>Night city: dark sky, lit buildings. Very close to Google.</li><li>Harsh sun and backlit street: skies held, shadows open.</li><li>Portraits natural.</li></ul> The merge removes most of the noise (e.g. 0.0555 → 0.0029 on the restaurant scene). |
| **Your photo in a simulated dark room** (4.5 stops darker, real sensor noise, hand shake: `scripts/light-darkroom-sim.py`) | <ul><li>Metered at ~2.7 lux: Moonlight, 10 frames.</li><li>Noise 0.079 → 0.003.</li><li>The result is the closest of all versions to the well-lit original.</li><li>An earlier version made the forehead grey and hazy; this was fixed by the headroom-based amounts above.</li></ul> |
| **Your real 5-second video** (a dim room), tone on every frame | <ul><li>Brighter and clearer, and skin keeps its warmth.</li><li>An earlier version lifted it to a grey, ashy look; this was fixed by the gentle lift above.</li><li>Steady: the average frame-to-frame brightness change was lower than in the original (0.0074 vs 0.0098 stops).</li></ul> |
| **Real flash / no-flash pairs** (Petschnigg et al. data: carpet, cave, lamp, pots, puppets) | Sharp, keeps the ambient mood and colour, and flash shadows are filled. Compared side by side with the published result. |

## App wiring

| Layer | What |
|---|---|
| Engine | `pp_light_meter`, `pp_light_tone` (with `capture_bias`), `pp_flash_fuse` |
| JNI | `nativeLightMeter`, `nativeLightTone`, `nativeFlashFuse` (in place) |
| Kotlin | <ul><li>`AutoLight.kt`: plan steadying, capture decision, tier caps (6 / 8 / 10 frames; longer bursts are decoded smaller), Low Light Boost hysteresis.</li><li>`CameraController`: metering loop, screen light, Low Light Boost, exposure held back and restored.</li><li>`CaptureProcessor`: flash frame, fusion, tone.</li></ul> |
| JS | <ul><li>`light.ts`: Auto Light chip, progress prediction, the flash button on the front camera (Selfie Light).</li><li>`CameraEvents.light`.</li><li>Settings → Auto Light.</li><li>The toast names what happened ("Moonlight photo saved").</li></ul> |
| Plans | <ul><li>Free: Auto Light on single shots (tone, Natural Flash with one ambient frame, Selfie Light, Bright Balance at most one stop).</li><li>Premium (`multiShot`): the bursts.</li></ul> |

Auto Light never overrides the user:

- Flash off means no flash and no screen light.
- A manual exposure compensation is left alone.
- RAW, Ultra Detail and Spotlight shots are untouched.

## Tests

| Suite | Result |
|---|---|
| Engine `light_tests.cpp` | 13 tests, also under AddressSanitizer + UBSan: <ul><li>Plans by light.</li><li>Flash only for something near.</li><li>Harsh and backlit.</li><li>Screen tint.</li><li>No exposure data.</li><li>Night stays night.</li><li>Harsh day balanced without clipping.</li><li>Even scene barely touched.</li><li>Cast corrected but warm.</li><li>Held-back exposure restored while a dim room is lifted gently.</li><li>Fusion lines up a moved flash frame, keeps the room's colour (r/b within 12%) and brightness, cuts error 4×, and fills flash shadows.</li><li>Strength 0, bad input, cancellation.</li></ul> Full engine suite: 174 tests passed. |
| Kotlin `AutoLightTest` | 14 tests: <ul><li>Meter output parsing.</li><li>Plan steadying.</li><li>Normal untouched.</li><li>Moonlight burst and tier caps.</li><li>Single-shot without multi-shot.</li><li>Bright Balance exposure (respects the user's).</li><li>Very-dark flash choices, flash off honoured.</li><li>Flash on honoured.</li><li>Labels.</li><li>Burst sizing.</li><li>Low Light Boost hysteresis.</li><li>Reporting.</li><li>Options.</li><li>Tone order.</li></ul> All 48 camera tests pass; lint clean; native library builds. |
| Jest `light.test.ts` | 8 tests: <ul><li>Modes.</li><li>Shot prediction.</li><li>Caps.</li><li>Single-shot.</li><li>Selfie Light.</li><li>Chip wording with no jargon.</li><li>Flash button on the front camera.</li><li>Toast and progress label.</li></ul> All 256 Jest tests pass. |

## Limits and not verified on a phone

- No APK was built, as instructed. The Kotlin compiles, its unit tests pass, and the native library builds. Screen light, the flash frame's timing and Low Light Boost depend on the phone and need a device check.
- Frames are 8-bit JPEGs, so a burst cannot be held back more than about 2 stops without losing shadow detail. Bright Balance is capped there. A RAW burst path would allow more.
- The meter reads the live view about every 0.7 s. The plan changes only after three matching readings, so it does not flicker.
- Speeds on phones will differ from the desktop figures (tone ~0.25 s for a 2 MP photo on one desktop core group).
