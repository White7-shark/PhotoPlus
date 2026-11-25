# Photo Plus — Architecture (Phases 1–9, plus the pose assistant and Scene Swap)

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
│ Beauty: grade (lighting · background · colour) · compositor (pp_beauty) · measure  │
│ Pose: describe · match + advice · tracker (holds hidden landmarks) · pp_pose_*     │
│ Studio: cut-out (matte, hair, strokes, edge colour) · compose · pp_studio_*        │
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

## Lively colour (the camera's default finish)

```
Photo:      CameraX JPEG ─▶ copy kept as *_original.jpg ─▶ decode (≤ 4096/6144/8192 by tier) ─▶ person mask (≤ 512 px) ─▶ pp_lively_masked ─▶ JPEG + EXIF ─▶ props ─▶ watermark
Clarity…:   burst ─▶ pp_enhance ─▶ Auto Light ─▶ person mask ─▶ pp_lively_masked ─▶ beauty / Spotlight / look ─▶ JPEG + EXIF
```

`native/engine/src/enhance/lively.cpp`: grey-pixel white balance (skin keeps its warmth), warmth only for a cool scene, a tone curve (black point, gain, mid-tone lift held back for low-key photos, S-curve, highlight shoulder), clarity, skin brought toward a healthy colour-for-brightness (every skin tone keeps its own hue), grey shine coloured like the skin around it, vibrance scaled by how colourful the photo already is, neutral shadows deepened. Large-scale measurements run on a ≤ 1024 px grid; every per-pixel change (colour gains, skin smoothing, colour evening, shadows) is decided from a smooth ≤ 1600 px map, so the result is smooth and even rather than noisy. Setting: *Lively colour* (on by default; off keeps the phone camera's own look). See `docs/LIVELY_CAMERA_REPORT.md`.

## Pink lips and Lipstick (Photoshoot Studio)

ML Kit face contours (once per studio session) ▸ engine landmarks ▸ `pp_studio_lips` on the cut-out (after hair, before studio light). Natural pink adapts to the person (fair faces and already-pink lips get very little); lipstick is applied as chosen. Lips keep their own shading and gloss; the open mouth (teeth) is left out. UI: `src/screens/studio/LipsPanel.tsx` (Pink lips, Lipstick), shades in `src/services/studio/lips.ts`.

## Film & Collage (Photoshoot Studio)

Photos (decoded within budget; faces found once, the focus for crops) ▸ `pp_studio_filmstrip` (classic 35mm, photo booth, amber negative, cinema; sprocket holes and edge markings) or `pp_studio_collage` (ten layouts, gap, corners, background), each frame with one of ten looks, a zoom, a pan and a mirror. UI: `src/screens/studio/FilmCollageScreen.tsx`, catalogues in `src/services/studio/film.ts`. See `docs/FILM_COLLAGE_REPORT.md`.

## Relight, download sizes and the studio grid

`pp_studio_relight_compose` puts the relit cut-out back over the person's own photo (background brightness, colour and a person-free blur). Studio saves take a download size (360p…4K, short edge; Premium from 720p) through `ExportSize`. Glide applies Lively per frame with a clip state (`pp_lively_frame`) so the look stays steady. See `docs/RELIGHT_REPORT.md`.

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

## Glide: pro video, 7 s standard / 20 s Premium (Phase 6, lengthened in Phase 7)

Full report: [`PHASE6_GLIDE_REPORT.md`](PHASE6_GLIDE_REPORT.md).

```
Camera (Glide mode) ── configure({ video: true, aspectRatio: '16:9' }) ── CameraX VideoCapture<Recorder>
   startVideo ≤ 7 s standard / 20 s Premium (the plan is read natively) → captures/glide/*_raw.mp4
EngineModule.glide → GlideProcessor   (frames capped by the plan; free clips always marked)
   motion (pp_video_estimate_motion) → plan (pp_video_stabilize) → per frame:
   pp_video_enhance_frame → person mask (made as pass 2 reaches it: ≤ 3 masks held;
     selfie segmenter + portrait matte fused (pp_video_fuse_masks) → vote of three → room memory → steadied) →
     Spotlight / Glow: pp_portrait / pp_face_adjust → pp_video_warp
     or a new scene (Premium): pp_studio_cutout → pp_video_warp of the cut-out → pp_studio_compose
       (fixed box, colour match kept from frame 1)
   → speed: normal as is · fast merges frame pairs · slow adds a motion-matched frame between each pair (pp_video_interpolate)
   → watermark → GlideEncoder (MediaCodec H.264; original sound at normal speed only) → exports → gallery (Movies/Photo Plus)
GlidePreview: Glide / As recorded, share, save, "Put me in a new scene" (glideSubject → autoFrame → glide)
```

| Where | What |
|---|---|
| `services/camera/glide.ts` | Request building (Spotlight level, Glow from the camera chip, watermark), timing, labels, "% steadier" |
| `screens/camera/useGlide.ts` | Record → craft → save flow; microphone asked once (silent clips without it); one clip at a time; cancellable |
| `GlideOptions.kt` | Request parsing with ranges; bitrate; exact frame times; `LandmarkSmoother` keeps face touches from shimmering |
| Plan | Free. Free-plan clips are marked in every frame before encoding (`ExportPolicy` + `WatermarkRenderer`) |

## Beauty filters and live camera beauty (Phase 7)

Full report: [`PHASE7_BEAUTY_REPORT.md`](PHASE7_BEAUTY_REPORT.md).

```
presets.json (13: 2 free, 11 premium) ─ recipe.ts resolveRecipe(preset, Intensity + Skin/Face/Lighting/Background, Portrait controls)
   │  one resolver for the app, the tests and the desktop tools
   ├─ camera live:   CameraService.setLiveBeauty(recipe) ─ LiveBeautyState ─ BeautyProcessor (CameraEffect, GLES 3)
   │                 BeautyAnalyzer: faces, selfie mask, scene (pp_beauty_measure) in preview space
   ├─ photo:         CaptureOptions.beautyRecipe ─ PortraitProcessor.applyBeauty ─ pp_beauty
   ├─ Spotlight:     the same recipe + Portrait controls (depth blur, subject/background light, tone, edges)
   ├─ Glide:         GlideOptions.recipe ─ pp_beauty on every frame
   └─ editor:        EditDocument.beauty ─ EditRenderer heavy stage ─ pp_beauty
pp_beauty: skin (portrait::apply) → face (adjustFaces + blush/contour/highlight) → grade (C background, D lighting, E colour)
```

| Where | What |
|---|---|
| `native/engine/src/beauty/beauty.cpp` | The grade as one per-pixel `shade()`, mirrored line for line by `assets/beauty/composite.frag` |
| `scripts/beauty-gpu-parity.mjs` | Runs the shader files in WebGL2 against the engine on real photos |
| `scripts/beauty-fixture.mts` | Writes the fixture pinning the Kotlin bridge to the JS resolver and the live uniforms |
| Fallback | No GLES 3 external textures, low-RAM phone, or a GL error: rebind without the effect; photos still get the recipe |

## Backdrops and Skin Tone (Phase 8)

Full report: [`PHASE8_BACKDROPS_SKINTONE_REPORT.md`](PHASE8_BACKDROPS_SKINTONE_REPORT.md).

```
backdrops.json (14: 3 free) ─ catalog.ts ─ camera: Spotlight uses Signature (cameraBackdrop(depth blur))
                                          └ editor: EditDocument.backdrop
skintones.json (13: 4 free) ─ catalog.ts ─ editor: EditDocument.skinTone
                       │ SkinTone.kt / Backdrop.kt mirror the catalogues (unit tests compare them)
PortraitProcessor.applyBeauty(recipe?, backdrop?, skinTone?): one set of masks (subject, faces, protect, depth)
   ├─ pp_skin_tone  skin colour first (face + visible body; hair, features, clothes kept)
   ├─ pp_beauty     retouch, face, grade
   └─ pp_backdrop   the background last, over the finished person
```

| Where | What |
|---|---|
| `native/engine/src/backdrop/backdrop.cpp` | Matte (hair the mask missed recovered by colour, colour guided filter), clean plates, 14 painted designs, relighting, composite |
| `native/engine/src/skintone/skintone.cpp` | Skin weight (colour, brightness, texture, feathered region); the shade as gains on the skin's own colour in linear light (the scene's light stays), natural-hue guard, headroom for fair skin, even tone |
| `pp_video_consensus_mask` | Three-frame median of subject masks: one-frame segmentation mistakes never reach a clip |
| `pp_video_room_*` (`video::RoomMemory`) | The room remembered behind the person (follows the camera, corrects exposure drift): pieces of room the model swallows for many frames are given back; the person's long-held outline and dark close calls are kept. Used by Glide (with `MaskVote`) before steadying |
| `pp_video_fuse_masks` (`video::fuseMasks`) | Joins the two person models Glide runs on each frame: the portrait matte (MODNet, which draws hair and the head's outline, also against a dark background) and the selfie segmenter (which always finds the face and body, even in a blurred frame). Around the head the matte may reach past the segmenter (hair) and a head line keeps ceiling off the top; along the shoulders and arms it only refines the segmenter's edge, and the segmenter's solid body is always kept. Used by Glide before the vote |
| `pp_video_interpolate` (`video::interpolate`) | A frame between two frames, for slow motion: block motion searched both ways (to half a pixel), smoothed, and each block and pixel trusted only as far as the two moved frames agree. Where they don't (an eye opening, a hand appearing), it falls back to a plain blend, so it never tears. A plain blend is used for fast motion |
| `scripts/backdrop-demo.py`, `scripts/backdrop-video-demo.py` | Sheets and clips on real photos and video |
| `scripts/skintone-demo.py`, `scripts/skintone-video-demo.py` | Sheets, per-region measurements (face, body, hair, clothes, background) and clips |


## Auto Light: dark rooms, night and harsh light (Phase 9)

Full report, with the research it is based on: [`PHASE9_LIGHTING_REPORT.md`](PHASE9_LIGHTING_REPORT.md).

```
live view ── every 0.7 s: PreviewView.getBitmap → 160 px ─┐
camera exposure (ISO, shutter, aperture, focus) ──────────┼─ pp_light_meter → LightReading
live beauty analysis (face brightness, when on) ──────────┘        │ LightSteadier (3 readings to change plan)
                                                                   ├─ "PhotoPlusCameraLight" → Auto Light chip (JS)
                                                                   ├─ Low Light Boost on/off (Android 15+, hysteresis)
shutter ─ AutoLight.decide(request, reading, LightContext, tier) ──┘
   ├─ Normal          the shot as asked
   ├─ Moonlight       NIGHT burst (frames from the meter, ≤ tier cap) → pp_enhance merge → pp_light_tone (night)
   ├─ Bright Balance  exposure held back 1–2 stops → burst → merge → pp_light_tone (restores it, opens shadows)
   ├─ Natural Flash   ambient burst (flash off) → one flash frame → merge → pp_flash_fuse → pp_light_tone
   └─ Selfie Light    FLASH_MODE_SCREEN (overlay tinted by the meter) → one frame → enhance → pp_light_tone
```

| Where | What |
|---|---|
| `native/engine/src/light/light.cpp` | `meter` (lux from the exposure, plan, burst length, screen-light colour), `tone` (local tone mapping, night look, low-light white balance), `flashFuse` (flash/no-flash fusion) |
| `android/.../camera/AutoLight.kt` | Plain-Kotlin rules: plan steadying, the capture decision (never overrides flash off or a manual exposure), tier caps, Low Light Boost hysteresis |
| `CameraController` | Metering loop, screen flash set-up (`PreviewView.setScreenFlashWindow`, `ImageCapture.screenFlash`), Low Light Boost, `prepareLight` / `finishLight` (exposure held back for a burst, then restored) |
| `CaptureProcessor` | The Natural Flash frame after the ambient burst; fusion and tone after the merge, before beauty and looks |
| `src/services/camera/light.ts` | The chip, the progress prediction, the flash button on the front camera |
| `scripts/light-bench-prep.py`, `scripts/light-darkroom-sim.py`, `light_demo` | The bench: real raw bursts, a simulated dark room, flash pairs |

## Pose assistant: poses, framing and guided capture

Full report, with the research it is based on: [`PHASE6_POSE_REPORT.md`](PHASE6_POSE_REPORT.md).

```
analysis stream ─ PoseAnalyzer (upright crop, ~15 fps) ─ MediaPipe PoseLandmarker (Lite, LIVE_STREAM, GPU→CPU)
                                                            │ 33 landmarks (x, y, z, visibility, presence)
                     nativePoseStep: mirror (selfie) → pose::Tracker → describe → match(reference) → advice
                                                            │
LevelSensor ─ "PhotoPlusLevel"            "PhotoPlusPose" ──┴─→ usePoseAssistant (JS): coach, silhouette,
                                                                 arrows, guided steps, automatic capture
picked photo ─ PoseEstimator.analyzePhoto (Full, IMAGE) ─ nativePoseDescribe ─→ "My pose N" (settings)
```

| Where | What |
|---|---|
| `native/engine/src/pose/pose.cpp` | Reliability (presence, visibility, in frame), description (anchor, scale, 12 bones, bends, body and head turn, shot), matching (score, coverage, framing, advice), tracker (0.8 s hold, stillness) |
| `android/.../pose/` | `PoseEstimator` (models, photo analysis), `PoseAnalyzer` (live), `LevelSensor`, `PoseData` (event payloads) |
| `CameraController` / `CameraModule` | The pose analyzer shares the analysis stream; `setPoseGuide`, `setLevel`, `analyzePosePhoto` |
| `src/services/pose/` | Library (25 poses, generated by `scripts/pose-library.py`), guidance words and decisions, settings, a pose from a photo |
| `src/screens/camera/pose/` | `PoseOverlay` (grids, level, silhouette, arrows), `PoseCoach`, `PoseSheet`, `usePoseAssistant` |
| Models | `pose_landmarker_lite.task` and `pose_landmarker_full.task`, fetched and SHA-256-verified at build time, stored uncompressed |

## Photoshoot Studio: Scene Swap (background removal and replacement)

Full report, with the research it is based on: [`PHASE7_SCENE_SWAP_REPORT.md`](PHASE7_SCENE_SWAP_REPORT.md).

```
photo ─ ML Kit subject mask (→ person model → tap cut-out) ─┐
      └ MediaPipe Selfie Multiclass (hair / skin / clothes) ─┼─ pp_studio_cutout ─→ cut-out (RGBA, straight alpha)
strokes (Keep / Remove / Fix hair) + edge settings ─────────┘        │ previews at 1280 px, export at full size
                                                                      ▼
clean plate (assets, my backgrounds, packs) or plain colour ── crop ── pp_studio_compose ─→ scene (JPEG) / PNG
  (a plain colour with Studio light: pp_studio_backdrop, lit seamless paper)
placement (anchor, size, mirror, tilt) + look (match, wrap, rim, shadows, blur, grain, colour bounce)
studio light on the cut-out (pp_studio_relight) before compose; a gradient backdrop (pp_studio_gradient);
the finished frame graded (pp_studio_finish_frame: vignette, warmth, film looks)
cut-out report → largest face (true size, also for couples), faces, outfit colours → "Suits your outfit"
```

| Where | What |
|---|---|
| `native/engine/src/studio/` | `cutout.cpp` (snap, two-colour edges and hair, guided matte at full resolution, blur-fusion foreground colour, the old background's light taken off the edges by kind of surface), `compose.cpp` (placement, harmonisation, colour bounce, wrap and rim, contact and cast shadows, lens blur, grain; the studio paper backdrop) |
| `native/engine/src/matte/tools.*` | Shared with Backdrops: colour triplets, blurs, clean-plate colours, hair recovery, islands |
| `android/.../studio/StudioModule.kt` | Sessions, previews, scenes without the person (for dragging), Premium exports, gallery |
| `android/.../studio/PersonParts.kt`, `SceneStudio.kt` | Parts map; strokes, settings, backgrounds, crops, export size |
| `native/engine/src/studio/hair.cpp`, `src/services/studio/hair.ts` | Hair (Premium): recolouring the person's own hair; a new style: the old hair taken out (the body and a hidden cheek rebuilt under it, with the garment's own texture), then the style drawn strand by strand and fitted to the head, tilted with it (`pp_studio_hair`); finished by hand (`pp_studio_hair_edited`: shape, fit, colour finish, brushes; `src/screens/studio/HairEditor.tsx`) |
| `native/engine/src/studio/light.cpp`, `src/services/studio/light.ts`, `src/screens/studio/StudioLightPanel.tsx` | Phase 11: studio light on the person (key, fill, rim, gels; shape estimated from the outline and face), gradient backdrops, the finish (vignette, warmth, tint, grades, film looks), composition templates; free and Premium split checked on the device too. Report: [`PHASE11_STUDIO_LIGHT_REPORT.md`](PHASE11_STUDIO_LIGHT_REPORT.md) |
| `native/engine/src/studio/product.cpp`, `android/.../studio/ProductModule.kt`, `src/services/product/`, `src/screens/product/` | Phase 12: product cut-out (backdrop measured across the photo, its shadow left out, object mask from ML Kit), catalogue shots (backdrops, contact/soft/cast shadows, reflection, colour correction, templates), export profiles, batch editing, saved setups and looks, packs, projects with kinds and defaults; originals never changed. Report: [`PHASE12_PRODUCT_WORKFLOWS_REPORT.md`](PHASE12_PRODUCT_WORKFLOWS_REPORT.md) |
| `src/services/studio/colors.ts` | Colour picker conversions, the studio paper palette, and paper colours that suit an outfit |
| `src/services/studio/` | Plate library (`plates.json` from `scripts/studio-plates.py`, with each plate's measured horizon, camera height and open floor), auto framing (true-to-scene size), frames, looks, gestures, service |
| `src/screens/studio/` | Photoshoot Studio hub, Scene Swap screen, brush canvas, placement view, background picker, colour picker |
