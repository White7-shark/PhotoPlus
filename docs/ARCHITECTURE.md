# Photo Plus — Architecture (Phases 1–2)

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
│ Pipeline · Operations (tone, grayscale, saturation, flip, rotate, crop, resize,    │
│ sharpen) · Profiler                                                                │
│ Enhance: align · robust merge · super-resolution · denoise · deconvolve · tone     │
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

- Masks are small ALPHA_8 bitmaps passed to the engine without copies (the JNI wraps locked pixels); the photo is processed in place.
- The engine never allocates full-size scratch planes for portraits (~30 MB working set at any size).
- Premium checks go through `useFeature` / `checkFeature` (enforcement stays off until Play Billing ships); the feature catalogue lists Clarity, Spotlight and Share as free, and multi-shot modes and Vibe Match as Premium.
- User-facing text uses brand names only; native error codes map to plain sentences in `services/errors.ts`.
