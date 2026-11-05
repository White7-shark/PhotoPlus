# Phase 2 — Professional Camera Engine: Report

**Status:** Complete. No APK was built, as requested. Everything below was compiled, run or analysed without producing an installable package. The camera has **not** yet run on a physical phone (none is attached to this environment). Hardware behaviour is covered by the design decisions, tests and checks listed here, and needs a device pass before release (see "Open items").

Design preview: [`design/phase2-overview.png`](design/phase2-overview.png). It uses real engine output; see "How the preview was made" below.

## Deliverables

| Step | Requirement | Where |
|---|---|---|
| **2.1** | CameraX for the main workflow | `camera/CameraController.kt`: Preview + ImageCapture (+ ImageAnalysis for peaking) in a `UseCaseGroup` with a `ViewPort` |
| | Camera2 where advanced controls need it | Camera2 interop: `Camera2CameraControl` capture-request options (AE/AF/AWB off, sensitivity, exposure time, focus distance, colour-correction gains), `Camera2CameraInfo` characteristics, a session capture callback for live metadata |
| | Lifecycle, preview, focus, capture, recovery | Bound to the Activity lifecycle. `CameraState` is observed; critical errors rebind with exponential backoff (0.5 s, 1 s, 2 s), then report `CAMERA_UNAVAILABLE` |
| **2.2 Free** | Front/rear switching | Lens prop on the preview; manual settings reset per camera (their ranges differ) |
| | Capture | `camera/CaptureProcessor.kt`, standard mode |
| | Autofocus, tap to focus | Continuous AF; native tap → `FocusMeteringAction` (AF+AE+AWB, 5 s auto-cancel), with a focus ring that turns gold on lock |
| | Basic exposure | EV dial in about ⅓-stop steps from the camera's own range |
| | Flash and torch | Off / Auto / On; torch API; hidden when the camera has no flash unit |
| | Zoom | Native pinch-to-zoom plus pills (ultra-wide if present, 1×, 2×, 5×) within the lens range |
| | Grid, timer | Rule-of-thirds overlay; 3 s / 10 s self-timer (cancellable; cancelled when leaving the screen) |
| | Standard aspect ratios | 4:3, 16:9 and 1:1. The 1:1 frame is a ViewPort crop of the 4:3 stream, so the saved photo matches the preview |
| | Photo preview | `screens/PhotoPreviewScreen.tsx`: photo, capture details, delete, Enhance |
| | Capture-resolution selection | `ResolutionPolicy.kt`: only sizes the sensor really has for the chosen ratio (Max, 12, 8, 5, 2 MP), in the camera's top bar and in Settings |
| **2.3 Premium** | Manual focus | Focus-distance dial (∞ to the lens's closest focus, in metres/cm) |
| | ISO, shutter | Dials of photographic stops clamped to the sensor. Choosing only a shutter speed raises ISO to keep the brightness (`compensatedIso`) |
| | White balance | Camera presets (Tungsten, Fluorescent, Daylight, Cloudy, Shade) and a Kelvin dial (2000–9000 K) via colour-correction gains |
| | Advanced exposure | AE lock, AWB lock, live ISO / shutter / aperture / focus readout, manual/auto indicator |
| | RAW capture | DNG + JPEG pair (`OUTPUT_FORMAT_RAW_JPEG`) where the camera supports it |
| | Advanced focus assistance | Focus peaking from the live frame, plus a centre sharpness score |
| | Additional capture presets | Portrait, Night, Sport (1/1000 s), Landscape, Document. Each is offered only when the hardware can honour it |
| | Processing modes | Enhance (single frame), Night (burst fusion), Super-res (2× burst super-resolution) |
| **2.4** | Expose only supported features | `CameraCapabilities.kt` (pure, unit-tested) builds the capability map from CameraX/Camera2 facts. Manual sensor controls are never offered on LEGACY hardware. JS validates the map (`services/camera/parse.ts`) and hides anything missing |
| **2.5** | Efficient camera → engine pipeline | See "Zero-copy path" |

Premium features show a crown and are registered under `advancedCamera` in the feature catalogue. As in Phase 1, enforcement stays off until Play Billing is wired up.

## Zero-copy path (step 2.5)

| Stage | What happens | Copies avoided |
|---|---|---|
| Capture | In-memory JPEG from CameraX (`ImageProxy`, direct `ByteBuffer`) | — |
| Decode | `ImageDecoder` reads that direct buffer in place. The decoder crops (ViewPort) and downscales (tier plan) while decoding, straight into the bitmap the engine reads | No byte-array copy, no separate crop or scale pass |
| Engine | `pp_enhance_into` locks the frame bitmaps and **writes into the output bitmap** (new C API) | No intermediate result image and no copy-out |
| Orientation | Kept as an EXIF tag (`ExifInterface.rotate` / `flipHorizontally`) | No rotated copy |
| EXIF read | Through an `InputStream` over the same direct buffer | No copy |
| Focus peaking | The live Y plane is read in place by `pp_focus_peaking` into a small double-buffered overlay | No per-frame copy |

Burst frames are queued back to back, so CameraX pipelines them and the shake between frames stays sub-pixel. Frame counts and working sizes follow the device tier (`CapturePolicy.kt`):

| Tier (engine budget) | Enhance | Night | Super-res |
|---|---|---|---|
| Entry (≤ 320 MB) | 1 × 2048 px | 4 × 1600 px | 6 × 960 px → 1920 px |
| Standard (512 MB) | 1 × 3072 px | 6 × 2400 px | 8 × 1280 px → 2560 px |
| Performance (768 MB) | 1 × 4096 px | 6 × 3200 px | 8 × 1600 px → 3200 px |

If the engine reports `BUDGET_EXCEEDED` (for example while another job is running), the frames are scaled down about 30% and the shot is retried. It is not failed. Flash never fires during a burst: repeated flashes would differ from frame to frame and break the merge.

## "Weak camera → strong photo" (completion criterion)

The engine was benchmarked on 8 Kodak images through a simulated cheap camera: ¼ of the megapixels, a soft lens, sensor noise and JPEG compression, with an 8-frame handheld burst ([`benchmarks/README.md`](benchmarks/README.md)).

| | PSNR | SSIM |
|---|---|---|
| Ordinary upscale (bicubic) | 25.12 dB | 0.706 |
| Photo Plus, single frame | 25.71 dB | 0.736 |
| **Photo Plus, 8-frame burst** | **27.02 dB** | **0.783** |

The burst beats the ordinary upscale on every image (+1.9 dB, +0.077 SSIM). After the review fixes the benchmark was re-run, and every score was identical.

## Reliability (completion criteria)

| Scenario | Handling |
|---|---|
| Camera switching | Rebinds on lens change; capabilities are refreshed on every `onReady`; manual settings reset for the new camera |
| Focus | Tap → metering action with a result event; in manual-focus mode a tap still meters exposure and white balance |
| Capture | Standard shots can be taken while a processed shot is still running. Only one processed shot runs at a time (`BUSY` otherwise), which bounds memory. Cancellation works during both the burst and the processing |
| Backgrounding | The preview deactivates (CameraX releases the camera), the self-timer is cancelled, and capture requests in flight fail cleanly with `ERROR_CAMERA_CLOSED` |
| Orientation | `OrientationEventListener` with ±10° hysteresis sets the capture rotation, so photos come out upright although the UI is portrait-locked. Icons counter-rotate. Peaking analysis keeps the display rotation so the overlay stays aligned |
| Recovery | `CameraState` errors are surfaced, and critical ones trigger a backoff rebind with a "Reconnecting camera…" status. The error screen offers a full remount |

## Testing

| Suite | Result |
|---|---|
| Jest (TypeScript) | **145 passed** (9 suites). New: capability parsing, camera model (modes, zoom, EV, dials, presets, ISO compensation), CameraService, enhancement helpers, data-safety regression tests, and UI tests for the camera screen (configure, capture per mode, Pro panel, failure toast, aspect re-select), Pro panel, StopDial and CompareView |
| Kotlin JVM (JUnit 6) | **66 passed**. New: CameraConfig, ManualMath (exposure fill/clamp, stops, Kelvin gains, labels), ResolutionPolicy, CameraCapabilities (LEGACY distrust, hidden features), CapturePolicy/CaptureGeometry (EXIF/crop rotation, decode plans), OrientationMath (hysteresis), OverlayGeometry (all rotations, mirroring, crop) |
| C++ engine | **41 passed** under ASan + UBSan and under TSan. New: `pp_enhance_into` gives the same pixels as `pp_enhance` and rejects a wrong-size destination |
| Static checks | `tsc` clean, ESLint (with `no-deprecated`) clean, Prettier clean, Kotlin compile with 0 warnings, Android Lint: **No issues found** |

## Code review

The full Phase 2 diff got a structured review. Every finding was fixed, and regression tests were added where the logic is testable:

1. **Data loss (fixed):** keeping an enhanced copy inherited the source's RAW file and "_original" companion, so deleting the copy deleted the source's files. The copy now gets fresh metadata (`enhancedCopyMetadata`), and deletion is computed by `filesToDelete`; both are tested.
2. Re-selecting the current aspect ratio or resolution left the camera stuck in "starting". Only real changes rebind now (tested).
3. Peaking analysis followed the device rotation, which misaligned the overlay in landscape. It now uses the display rotation.
4. Kelvin white balance was offered on devices where it then silently fell back to auto. Its requirement now matches its capability flag.
5. A Kelvin request made before the first frame was permanently downgraded. The user's request is now kept and re-applied when the colour matrix arrives.
6. Large sharp photos were flagged as "soft": blur was scaled up together with the estimator's floor. Blur is now reported at the analysis size.
7. Engine: a textureless pyramid level broke the coarse-to-fine shift scale (fixed).
8. Engine performance: the reference pyramid is now built once per burst, and robustness maps are computed once for super-resolution.
9. Engine: `noiseAfter` is now measured on the restored image (the old value made single-frame Enhance report 0% noise removed).

The design preview also exposed a usability problem: the default focus-peaking threshold (0.16) marked 53–60% of a sharp, textured frame. Measured on the Kodak set at analysis size, 0.4 marks roughly 4–25% depending on scene texture, about 10–16% on most images. It is now the default.

## How the preview was made

`scripts/make-preview-assets.py` simulates a weak-camera 8-frame burst of Kodak image 19 (the same model as the benchmark) and runs it through the real engine (`enhance_bench`). It also computes the peaking mask the way the app does (640 px analysis frame, threshold 0.4). `scripts/render-phase2-preview.mjs` lays the screens out with the app's sizes, colours and icons. The "What the engine did" numbers (noise −57%, 8/8 frames fused) are that run's actual report.

## Open items for a device pass

- Verify Camera2 manual controls on representative LIMITED/FULL/LEVEL_3 devices (vendor HAL quirks), and the RAW pair delivery (handled with a 15 s timeout fallback).
- Measure burst and processing times on entry and standard tiers, and tune the `CapturePolicy` sizes if needed.
- Peaking overlay alignment assumes the preview view has the selected aspect ratio, which the camera screen guarantees. Check it on very tall screens with 16:9.

---

# Phase 2 additions: Spotlight, Glow, Vibe Match, Share

Design preview: [`design/spotlight-overview.png`](design/spotlight-overview.png), and how Spotlight uses depth: [`design/spotlight-depth.png`](design/spotlight-depth.png). Every photo in them is real engine output on Kodak test photos, and every number is that run's own report.

## What changed for people

| Feature | Plan | What it does |
|---|---|---|
| **Clarity** (single-photo enhance) | Free, **default mode** | Sharper, cleaner, brighter in one tap. |
| **Moonlight** (multi-shot low light) | Premium | Blends a few quick shots for bright, clean night photos. |
| **Ultra Detail** (multi-shot 2× detail) | Premium | Twice the detail from a few quick shots. |
| **Spotlight** | Free | The subject stands out while the background melts into a lens-like blur that grows with distance, as with a real lens: the ground they stand on stays sharp, and what is further back softens more. Tap the preview, or a saved photo, to choose what shines; untapped, it picks the people (or the main subjects). Blur: Light / Medium / Strong. |
| **Glow** (part of Spotlight) | Free | Skin retouch: smooths, clears spots and blemishes, evens and brightens tone, and keeps each person's own skin colour. Natural / Smooth / Flawless. |
| **Vibe Match** | Premium | Pick any photo whose style you love. The camera adjusts exposure and white balance toward it, and every shot (and any saved photo) is finished in that style. Subtle / Balanced / Full. |
| **Share** | Free | System share sheet from the photo preview. |
| **Edit a photo** | Free | Bring a gallery photo in (system photo picker, no permission) and use every tool on it. |

**Names and wording.** The app uses brand names only and never shows technical terms. Error codes map to plain sentences (`services/errors.ts`), and the developer screens (device report, self-test) are hidden in release builds. A test checks that no mode or tool label contains words such as "engine", "frame", "burst", "noise" or "model".

## How it works

**Engine (C++, `native/engine/src/portrait`)**
- **Matte.** A coarse subject mask is aligned to the photo's own edges with a fast guided filter. Its coefficients live on a ~1024 px grid, and alpha is evaluated per pixel, so no full-size mask is ever stored.
- **Depth-aware lens blur (3D, not a flat cut-out).** Like iPhone and Pixel portrait modes, each area's blur radius grows with its depth distance from the subject's focus plane:
  - The focus plane spans the subject's own depth, feet included, so the ground they stand on and anything beside them stays sharp. Ground further back softens progressively, the far background melts, and things in front of the subject blur too (a little less).
  - The image is split into depth bands. Each band gets its own disc ("bokeh") blur in linear light with highlight bloom, and the bands are composited back to front. A blurred background never bleeds over nearer things, and the in-focus slab is inserted from the full-resolution photo.
  - Stronger blur also means a thinner in-focus slab, like opening up the aperture.
  - Depth is smoothed along the photo's own edges, so bands never switch blotchily.
  - Subject pixels are excluded from the background average (normalised convolution), so there is no halo of subject colour. Grain matching the photo is re-added, slightly lighter, so blurred areas don't look plastic.
  - Without a depth map the engine estimates one: a ground plane for a standing subject (the ground recedes upwards from the feet), or a far background for a close-up.
- **Glow.** It runs where the face is about 180 px across, then applies through frequency separation in one streaming pass:
  - The skin model is calibrated to each face's **own** colour (robust median and spread) and **own brightness**: every threshold is relative to the person's skin luma, so deep and light skin are treated alike.
  - Spots are found by their *relative* contrast against surrounding *skin only*, then healed by inpainting from clean skin. Only blemish-sized marks are healed; larger dark areas (nostrils, creases, jaw shadow) are features and are left alone.
  - Edge-preserving smoothing keeps 10–55% of real texture, depending on the Glow level. Strong local contrast (lash line, nostril, lip edge) is kept.
  - Blotches are evened toward the person's own tone, at each spot's own brightness, so a darker feature never picks up a tint.
  - The lift is relative (exposure-like) and scales colour with brightness, so hue and saturation hold. Ratios are bounded, so near-black pixels can never turn a false colour.
  - Eyes, brows, lips and nostrils are protected, and strength fades for small faces (group and full-length shots).
- **Tap cut-out (fallback).** Colour models seeded by the taps are refined along edges, and only regions connected to a tap are kept. A reliability check swaps a patchy result for a soft, deliberate focus fall-off.
- **Look transfer.** A monotone, slope-bounded tone curve (luma quantiles), colour by tonal zone (split toning), saturation, vignette and grain. The camera-side mapping (`lookCameraSettings`) turns the look into exposure compensation and white balance (Kelvin where available, otherwise the nearest preset).
- **Cost.** 12 MP Spotlight takes ~0.5 s on 4 desktop cores, with a ~30 MB working set whatever the photo size. Look transfer on 12 MP takes ~0.2 s.

**Android (`studio/PortraitProcessor.kt`)**
- **Subject.** ML Kit subject segmentation (people and objects; Play services module, downloaded once) chooses the tapped subjects. Fallbacks, in order: the bundled ML Kit person segmentation, then the engine's tap cut-out.
- **Faces.** ML Kit face contours (bundled) give face areas plus protected features. Face detection and subject segmentation run concurrently, and every wait is bounded. When no face is found in the whole photo (full-length shots), detection runs again on an enlarged crop of the top of the person.
- **Depth (`DepthEstimator.kt`).** Depth Anything V2 Small (Apache-2.0) runs on-device with LiteRT 2.3.2 at 686×518 (portrait photos are turned landscape first), concurrently with the other models. Its output is stretched between the 1st and 99th percentiles into a 0–255 mask (`DepthMath`, unit-tested). The model loads on first use, is shared by the camera and editor, and is released under memory pressure. If it is missing or fails, the engine's own ground-plane estimate is used, so Spotlight always works.
- **Model delivery.** The 28 MB model is not in the repository. The Android build fetches it at a pinned revision, verifies its SHA-256, caches it in the Gradle home, and packages it uncompressed as an asset (`fetchDepthModel` in `app/build.gradle`). Offline builds can pass `-Pphotoplus.depthModel=false`.
- **Geometry.** Taps map from the portrait-locked (mirrored, for selfies) preview to the upright photo, then to the decoded bitmap, in any phone orientation (`MaskGeometry`, unit-tested).

## Testing

| Suite | Result |
|---|---|
| Engine (ASan+UBSan, TSan) | **62 passed**. Depth round: blur grows with distance like a lens (texture kept at the feet 0.99, mid-ground 0.34, far ground 0.16, wall 0.07, in front 0.63); stronger blur gives shallower focus; the ground plane is estimated without depth; close-ups blur the whole background; deep and very deep skin: near-black nostrils get no colour cast (shift 0.004) and are not healed away, blemishes cleared (−91% to −95% contrast, 11 of 11), shading kept. Earlier: New, social-media-oriented checks: background clearly blurred (−86% texture) with the subject crisp; no colour halo; spots −86% with texture kept (not plastic); **hue within 0.1° and saturation within 1% for deep, medium and light skin tones**; side-light shading 95% kept; eyes, brows and lips stay sharp; no retouching without faces; neutral settings change nothing; deterministic and leak-free; cancellation is reported as cancellation; tap chooses the right subject (IoU 0.98–0.99); look transfer cuts the distance to the reference by 81%; ~28 MB working set on a large face |
| Kotlin JVM | **77 passed**. New: depth model input normalisation, robust depth stretch, head crop for small faces. Earlier: tap mapping in all orientations and for selfies, mask rotation, Spotlight and look option parsing |
| Jest | **165 passed**. New: plans (Clarity and Spotlight free, multi-shot and Vibe Match Premium, with enforcement on), no-jargon check, Spotlight tap and settings flow to capture, Vibe Match camera settings and capture, exposure reset when Vibe Match is turned off, photo-preview tools, Share, Spotlight on before/after photos, edits never overwrite originals, old style photos removed, "Edit a photo" import |
| Static | tsc, ESLint (no-deprecated), Prettier, Kotlin 0 warnings, Android Lint "No issues found" |

## Real-photo checks, and what they changed

Synthetic tests were not enough on their own, so the tools were run on Kodak portraits:
- **A full-length shot with a small face looked ashy.** Retouch strength wasn't tied to face size, and the lift didn't scale colour. Fixed, and a saturation test was added for every skin tone.
- **The colour-only tap cut-out confused dark clothing with a dark sculpture, and skin with a pink backdrop.** It now detects this and falls back to a harmonious soft-focus look. On phones, ML Kit's segmentation is the primary mask. With a person mask (MediaPipe's selfie segmenter standing in for ML Kit on this machine), Spotlight gives clean hair edges with no halo.
- **The spot count was inflated** by stray pixels; only blemish-sized clusters are counted now.
- **Spotlight looked flat (2D), and the ground the woman in kodim18 stood on was blurred** along with the background. A plain cut-out cannot tell ground at the feet from ground far behind. Spotlight is now depth-aware (above): the barrel and grass at her feet stay sharp, the rusty sculpture is sharp where her hand touches it and softens with distance, and the arch and trees melt.
- **Glow on deep skin (a dark-skinned man's close-up) turned nostrils blue, flattened shading, looked pale and "healed" 41 spots.** Cause: clarity could push a near-black pixel below zero, and the brightening step then divided by ~0.001, flipping and multiplying its colour; absolute thresholds and smoothing strength were also biased against deep skin. Fixed (relative thresholds, bounded ratios, blemish-size limit). It now heals 8 real marks with no colour cast. On kodim18 (dark-skinned woman with facial spots) Glow clears the forehead and cheek marks: 5 blemishes, see `design/spotlight-glow.png`.

## Code review

Ten findings, all fixed with regression tests:
- A cancel during retouch was misreported as low memory, then retried.
- A plain photo with Vibe Match on was decoded at full sensor size; it is now bounded.
- Spotlight taps were ignored on before/after photos.
- Turning Vibe Match off left its exposure offset.
- The photo picker could hang if the activity went away.
- NaN checks in looks.
- Edits could overwrite the original.
- Old style photos were never deleted.
- A per-pixel matte evaluation in the blur grid (now per cell).
- Face and subject models now run concurrently.

## Open items for the device pass

- Check subject segmentation's first-use download and the fallback path on devices without Google Play services.
- Tune Glow levels on a range of real selfies, in varied lighting and across skin tones.
- Measure Spotlight time on entry-tier phones, including the depth estimate (~0.2 s on 4 desktop cores; GPU or NPU acceleration can be added later).
