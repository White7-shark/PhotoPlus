# Photo Plus — Architecture (Phases 1–6)

```
┌──────────────────────────── React Native (TypeScript) ────────────────────────────┐
│ screens ─ components ─ theme            state (zustand)          navigation       │
│        │                                   │                                       │
│ services: capabilities · permissions · database · processing · storage · camera   │
│           entitlements (catalogue, enforcement off) · lifecycle · logging          │
└──────────────┬──────────────────── TurboModules / Fabric (codegen) ───────────────┘
               │  src/native/specs/*.ts  →  com.photoplus.specs.*Spec (generated)
┌──────────────▼──────────────────────────── Kotlin ────────────────────────────────┐
│ DeviceModule (CapabilityProbe, MemoryPressureMonitor)                              │
│ EngineModule (JobManager, ImageCodec, MemoryBudgetPolicy)  ── JNI ──┐              │
│ DatabaseModule (SQLite)   StorageModule (AppPaths)                  │              │
│ CameraModule · CameraController (CameraX + Camera2 interop)         │              │
│ CaptureProcessor · FocusPeakingAnalyzer · PhotoPlusCameraPreview ───┤              │
└─────────────────────────────────────────────────────────────────────┼─────────────┘
┌─────────────────────────────── C/C++ engine (native/engine) ────────▼─────────────┐
│ pp_engine.h — stable C ABI                                                         │
│ ImageBuffer (RAII, 64-byte aligned) · MemoryTracker (budget) · CancellationToken   │
│ Pipeline (previews at max_edge, estimates, progress) · ThreadPool · buffer reuse   │
│ Ops: resize (area/bilinear/Lanczos) · blur · luma sharpen · denoise · warp/keystone│
│      LUT 3D · colour matrix · 12 blend modes · masks + guided refine · Profiler    │
│ Codec (stb: JPEG/PNG/BMP/GIF, EXIF, budget-checked) · Tensors (model in/out)       │
│ GPU: GLES 3.1 compute, self-tested against the CPU, previews only                  │
│ Enhance: align · robust merge · super-resolution · denoise · deconvolve · tone     │
│ Video (Glide): motion · stabilisation plan · warp · clip curves · frame enhance    │
│ pp_enhance_into (writes into a caller bitmap) · pp_focus_peaking (reads Y in place)│
└────────────────────────────────────────────────────────────────────────────────────┘
```

## Key decisions

| Area | Decision | Why |
|---|---|---|
| Bridge | New Architecture TurboModules + Fabric component with codegen | Typed contracts, lazy loading, no legacy bridge |
| Engine boundary | Plain C ABI (`pp_engine.h`); C++23 internals with `std::expected` results | ABI-stable, exception-free, usable from any host |
| Toolchain | Latest stable everywhere (see `DEPENDENCIES.md`); `no-deprecated` lint, Lint and Gradle deprecation checks kept at zero for our code | Avoids carrying migration debt into later phases |
| Pixel I/O | Kotlin decodes (ImageDecoder, EXIF-aware, downsampled) → C++ processes straight-alpha RGBA → Kotlin encodes atomically | Uses Android's hardware codecs; keeps C++ platform-agnostic and host-testable |
| Memory | Engine-wide budget by device tier; allocations fail with `BUDGET_EXCEEDED` instead of OOM-kill; trimmed on memory pressure | Acceptance: no uncontrolled memory growth |
| Concurrency | Bounded worker pool (1–2 by tier), per-job cancel tokens, cleanup before completion callbacks | Predictable peak memory; prompt cancel |
| Storage | SQLite via a thin native bridge; schema/migrations/repositories in TS | Migrations tested against real SQLite (sql.js) on CI |
| Files | All app data in app-private storage; JS-supplied paths are sandbox-checked | No storage permission needed; path traversal blocked |
| Camera | CameraX bound to the Activity lifecycle; preview active only when focused and foregrounded; Camera2 interop only for manual controls and metadata | Interruptions (calls, app switches) release and reacquire the camera automatically; CameraX handles device quirks |
| Camera capabilities | Pure `CameraCapabilities.build()` (Kotlin) → validated by `parse.ts` → `availableModes`/`availablePresets` | Only real features are shown; product rules are unit-tested off-device |
| Capture pipeline | Decode from the CameraX direct buffer (crop + scale in the decoder) → `pp_enhance_into` into the output bitmap → EXIF orientation | No avoidable copies; memory bounded per device tier |
| Device tiers | Same thresholds in TS (`tiering.ts`) and Kotlin (`MemoryBudgetPolicy.kt`), pinned by tests on both sides | UI features and native budgets never disagree |
| Entitlements | Feature catalogue mirrors spec §2; `ENFORCEMENT_ENABLED = false` | Infrastructure now, enforcement with Play Billing later |
| Design system | Tokens → components; glass built from layered translucency + pre-blurred aurora; contrast verified by tests | RN has no backdrop blur on Android; legibility is enforced, not eyeballed |

## Startup sequence (`src/app/bootstrap.ts`)

1. Open + migrate the database (**critical**: on failure the app shows a retryable error screen).
2. Load settings (corrupt values fall back to defaults).
3. In parallel: detect capabilities (4 s timeout → cached snapshot), engine info, prepare folders.
4. Housekeeping (history pruning) runs without blocking startup.

## Lifecycle & interruptions

- **Background:** camera preview deactivates (CameraX releases the hardware), in-flight processing jobs are cancelled.
- **Foreground:** camera permission is re-checked (it may have changed in Settings) and memory-pressure budgets are restored.
- **Memory pressure:** `onTrimMemory` → engine budget halves or quarters → JS is notified.
- **Process death:** `MainActivity.onCreate(null)` avoids react-native-screens restore crashes; state rehydrates from SQLite.

## Camera (Phase 2)

```
CameraScreen (JS) ── configure / setManual / capturePhoto ──► CameraModule ──► CameraController
     ▲                                                                            │ CameraX use cases
     │ DeviceEventEmitter: state · zoom · focus · metadata · orientation ·        │ Camera2 interop options
     │ peaking · shutter · progress                                               ▼
     └───────────────────────────────────────────────────────────────── CaptureProcessor
                                                                 standard │ RAW+JPEG │ burst ──► ImageDecoder
                                                                                              ──► pp_enhance_into
```

- **Session config** (aspect ratio, resolution, RAW) lives natively and is applied on every bind; a change rebinds only when the streams differ.
- **Manual controls** are stored as the user asked for them; every apply derives what the hardware can do right now (clamped to sensor ranges), so a temporary downgrade is never remembered.
- **Recovery:** `CameraState` critical errors → rebind with backoff (0.5 s, 1 s, 2 s) → `CAMERA_UNAVAILABLE`.
- **Orientation:** the UI is portrait-locked; the photo's rotation follows the device (±10° hysteresis), analysis keeps the display rotation so overlays line up.

## Spotlight, Glow and Vibe Match

```
capture / saved photo ─▶ decode (bounded size) ─▶ [Clarity] ─▶ PortraitProcessor ─▶ pp_portrait ─▶ [pp_style_apply] ─▶ JPEG + EXIF
                                                                 │ ML Kit subject segmentation (taps choose subjects)
                                                                 │ ML Kit face contours (faces + protected features; small faces: enlarged head crop)
                                                                 │ LiteRT depth estimate (blur grows with distance; else engine ground plane)
                                                                 └ fallbacks: person segmentation ▸ pp_subject_mask (taps)
```

### Transform

```
photo ─▶ recipe (untouched base + effects) ─▶ decode base ─▶ pp_transform ─▶ [Spotlight replay] ─▶ [look replay] ─▶ JPEG
                                                    │ lens model fit (blur², fringes) ▸ fringe re-alignment
                                                    └ enhance at native size (lens-set deblur + corner pass) ▸ optional 2× Catmull-Rom
```

- `pp_lens_analyze` feeds the plain-language lens notice. Readings are remembered per camera lens in an internal settings row.
- Daily allowances (Transform: 2 a day on the free plan) are checked with `checkAllowance` and counted in an internal settings row.

- Masks are small ALPHA_8 bitmaps passed to the engine without copies (the JNI wraps locked pixels); the photo is processed in place.
- The engine never allocates full-size scratch planes for portraits (~30 MB working set at any size).
- Premium checks go through `useFeature` / `checkFeature` (enforcement stays off until Play Billing ships); the feature catalogue lists Clarity, Spotlight and Share as free, and multi-shot modes and Vibe Match as Premium.
- User-facing text uses brand names only; native error codes map to plain sentences in `services/errors.ts`.

## Photo editor (Phase 3)

```
EditorScreen ─ history (whole-document undo/redo) ─▶ renderEdit(base, document, preview|save)
                                                      │
EditRenderer (Kotlin): decode base ─▶ [Transform] ─▶ geometry (rotate · flip · straighten · crop)
   ─▶ [Spotlight / Glow via PortraitProcessor] ─▶ tone · warmth · saturation · look · focus · sharpen
   ─▶ props (pp_place_prop, fitted with ML Kit face landmarks) ─▶ frame (pp_frame) ─▶ watermark ─▶ JPEG + EXIF
```

- **Non-destructive.** An edit is a small JSON document replayed on the untouched base in a fixed order. `EditDocument.kt` clamps every value, and allows only known prop and frame ids. The slow stages (Transform, Spotlight) are cached by input, so a preview while dragging a slider only re-runs the fast stages.
- **Watermark.** `WatermarkRenderer` draws the mark with Android Canvas into a stamp (fonts and emblem from assets). The engine composites the stamp (`pp_overlay`) and reports how many of its pixels are visible.
- **Export policy.** `ExportPolicy.resolve` / `verify` decide what the mark must be from the native entitlement (`Entitlements.kt`). A free export whose mark did not render fails with `WATERMARK_MISSING`, and nothing is written. Sharing accepts only files from the exports folder.
- **Clean companions.**
  - Free-plan captures and tool results are saved with the mark baked in, next to an unmarked `<name>_clean.jpg`. The app never shows or shares that copy.
  - Edits and re-exports start from the clean copy, so the mark is never doubled.
  - Deleting a photo deletes its clean copy too.
- **Props and frames** are engine operations at full resolution. Prop art is generated SVG (`scripts/make-props.mjs`) bundled as PNG. Frames are signed-distance shapes, so their edges are smooth at any size.

### Props in the camera

```
ImageAnalysis (640×480) ─▶ FaceTracker (ML Kit contours, ≈15 fps) ─▶ PhotoPlusCameraFaces event (preview fractions)
                                   │ viewport crop → upright → preview; mirrored for the front camera
JS: CameraProps.tsx ─ fitToFace + the person's adjustment (along the face) ─▶ live overlay (drag / pinch)
capture ─▶ CaptureOptions.props ─▶ CameraProps.kt: preview → upright photo (rotation, mirroring)
            ─▶ refit to the face found in the photo (FaceFinder + PropFit, same rules as placement.ts)
            ─▶ pp_place_prop into the pixels ─▶ watermark (free plan) ─▶ JPEG + EXIF
```

- One analysis stream serves either face tracking (props on) or focus peaking.
- Props people make: `createCustomProp` cuts the tapped object out (subject segmentation, then the engine's tap cut-out) and saves a transparent PNG under the app's `props` folder. `PropArt` loads bundled and made props for both the editor and the camera.
- Frame previews: `frameThumbnails` renders every frame around the person's photo at thumbnail size, cached per photo.

### Watermark-free unlock (rewarded ads)

`AdsModule` handles consent (User Messaging Platform) and Google Mobile Ads rewarded ads. `AdRewards` counts rewards per photo from the SDK's reward callback only. `EngineModule.exportImage` asks `AdRewards.isUnlocked(photo)` and `ExportPolicy.resolveForExport` before choosing the mark, so an unlocked free photo exports clean and nothing else does.

## Native engine (Phase 4)

Full report: [`PHASE4_REPORT.md`](PHASE4_REPORT.md). Model choices: [`MODEL_EVALUATION.md`](MODEL_EVALUATION.md).

```
JS  ImageProcessingService ── onProgress ◄── PhotoPlusJobProgress ◄──┐
Kotlin  EngineModule ─ JobManager (executor + progress ticker) ──────┘
        EditRenderer: preview → nativeProcessEx(max_edge, AUTO)    save/export → (0, CPU)
        ModelRuntime (ml/) ─ InferenceSession (LiteRT GPU → CPU, warm-up check, memory gate)
C ABI   pp_process_ex · pp_process_estimate · pp_decode/encode · pp_warp_perspective ·
        pp_detect_keystone · pp_apply_lut3d · pp_color_matrix · pp_blend · pp_mask_* ·
        pp_image_to_tensor · pp_tensor_to_mask · pp_gpu_* · pp_memory_trim
C++     validate everything → estimate peak vs budget → [reduce for preview] →
        runs of GPU-capable ops on the verified GPU, everything else (and any GPU
        failure) on the CPU thread pool → result
```

| Concern | How |
|---|---|
| Memory | One `MemoryTracker` budget for every buffer (set from device RAM by `MemoryBudgetPolicy`, lowered under pressure). The pipeline's peak is estimated before work starts. Freed buffers are reused (exact size, ≤ ¼ of budget) and trimmed under pressure |
| Threads | One persistent pool (≤ 4 workers). The calling thread joins in, and waiting threads help, so nested parallel loops cannot deadlock. Every chunk checks the cancel token and reports progress |
| Errors | `pp_status` + message → `EngineException(code)` → `ProcessingError.code`. Nothing throws across the C ABI |
| Consistency | Saves and exports use only the CPU path. The GPU is used for previews only after matching the CPU within 2 levels on that device. The operations are cross-checked against OpenCV 5.0 on the host |
| Models | Every model is declared in `ModelRegistry` with its licence and measured memory. It loads only when the phone can spare that memory, is released under pressure, and the feature falls back if the model is unavailable |

## Portrait intelligence (Phase 5)

Full report: [`PHASE5_REPORT.md`](PHASE5_REPORT.md).

```
ML Kit face detection (contours + landmarks, offline)
   └─ PortraitProcessor.withContours: every face gets contours (per-face crop re-detection)
   └─ FaceLandmarks: 131-point layout (PP_FACE_*), resampled; approximated when contours are missing
ML Kit subject / selfie segmentation ─ DepthEstimator (optional)
   │
   ├─ pp_portrait   subject matte (luma, or colour + decontamination when edge > 0)
   │                skin: smooth / blemish / even (or the Glow preset), lift, clarity
   │                background: depth-aware blur, light, colour
   └─ pp_face_adjust (landmarks)  eyes · teeth · lips · under-eye · shine · slim · face light · studio light
```

| Where | What |
|---|---|
| `EditDocument.portrait` / `document.ts` `portrait` | The Premium fine controls; `null` means presets only. Older saved edits load with `null` |
| `EditRenderer.heavy` | Glow/Spotlight presets plus the fine controls, in the cached slow stage (the cache key includes them) |
| `CaptureOptions.beauty*` | Camera Glow outside Spotlight. It turns on the processed capture path (never for RAW), and is recorded in the recipe so Transform re-applies it |
| `PortraitPanel.tsx` | Basic (presets) and Advanced (Premium) modes, Reset to Glow, Restore original; `SpotlightAdvanced` holds the background controls |
| Fairness rule | Every face threshold is relative to the person's own measurements. Exposure lifts are judged on the light around the head and the presence of frontal highlights, never on skin brightness |

## Glide: 7-second pro video (Phase 6)

Full report: [`PHASE6_GLIDE_REPORT.md`](PHASE6_GLIDE_REPORT.md).

```
Camera (Glide mode) ── configure({ video: true, aspectRatio: '16:9' }) ── CameraX VideoCapture<Recorder>
   startVideo ≤ 7 s → captures/glide/*_raw.mp4
EngineModule.glide → GlideProcessor
   motion (pp_video_estimate_motion) → plan (pp_video_stabilize) → per frame:
   pp_video_enhance_frame → pp_portrait / pp_face_adjust (Spotlight, Glow) → pp_video_warp → watermark
   → GlideEncoder (MediaCodec H.264 + original sound) → exports → gallery (Movies/Photo Plus)
GlidePreview: Glide / As recorded, share, save
```

| Where | What |
|---|---|
| `services/camera/glide.ts` | Request building (Spotlight level, Glow from the camera chip, watermark), timing, labels, "% steadier" |
| `screens/camera/useGlide.ts` | Record → craft → save flow; microphone asked once (silent clips without it); one clip at a time; cancellable |
| `GlideOptions.kt` | Request parsing with ranges; bitrate; exact frame times; `LandmarkSmoother` keeps face touches from shimmering |
| Plan | Free. Free-plan clips are marked in every frame before encoding (`ExportPolicy` + `WatermarkRenderer`) |

