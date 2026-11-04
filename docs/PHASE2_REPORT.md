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
