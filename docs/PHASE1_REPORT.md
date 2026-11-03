# Phase 1 — Application Foundation & Architecture: Report

**Status:** Complete. No APK was built, as requested. Every check below compiles, runs or analyses the code without producing an installable package.

## Deliverables

| Requirement | Where |
|---|---|
| **A. React Native app** | |
| RN + TypeScript | RN 0.88.1, React 19, TS strict (`tsconfig.json`) |
| Navigation | `src/navigation/` — native stack + tabs with a custom glass dock |
| Reusable UI components | `src/components/` — AppText, GlassSurface, AuroraBackground, Button, IconButton, Badge, ListRow, ToggleRow, SegmentedControl, ProgressBar, StatTile, PressableCard, Screen, Logo, Icon set |
| Centralised state | `src/state/store.ts` (zustand), optimistic settings with rollback |
| Theme & design system | `src/theme/` — "Darkroom Aurora" tokens, dark + light themes, gradients, type ramp, spacing, motion |
| Responsive layouts | `src/hooks/useResponsive.ts` — Material window classes, max-width column, font-scale-aware columns |
| Loading / error / empty states | `src/components/States.tsx`, `ErrorBoundary.tsx` (per-screen isolation) |
| Accessibility | roles/labels/states everywhere, 48dp targets, reduce-motion, live regions, capped font scaling, **contrast enforced by tests** |
| **B. Kotlin native integration** | |
| TurboModule architecture | `src/native/specs/*` → codegen → `com.photoplus.*Module` |
| Native camera interface | `camera/` — CameraX preview (Fabric component) + capture module |
| Native file-processing interface | `engine/EngineModule`, `ImageCodec`, `storage/StorageModule` |
| Permission management | `src/services/permissions/` (API 29–37 mapping, partial photo access, blocked handling) + manifest |
| Lifecycle management | CameraX lifecycle binding, `LifecycleCoordinator`, `MemoryPressureMonitor`, process-death-safe `MainActivity` |
| Background task coordination | `engine/JobManager` — bounded pool, per-job cancel tokens, cleanup before completion |
| **C. C/C++ processing foundation** | `native/engine/` |
| Native library + stable C ABI | `include/photoplus/pp_engine.h` |
| Memory management | RAII `ImageBuffer`, aligned allocations, `MemoryTracker` with budget |
| Image format handling | RGBA8/BGRA8/RGB8/Gray8 + conversions; straight-alpha contract; Kotlin decode/encode JPEG/PNG/WebP/HEIF |
| Error reporting | status codes + thread-local messages → `EngineException(code)` → JS `ProcessingError.code` |
| Cancellation | cooperative per-row polling; cancel from any thread |
| Resource cleanup | exception firewall on every C entry point; JNI `BitmapLock` RAII |
| Performance profiling | always-on scoped timers → JSON report → Engine Lab screen |
| **D. Local storage** | `src/services/database/` |
| Settings, projects, image metadata, processing history, asset registry | SQLite schema v1 + append-only atomic migrations + repositories |
| Storage usage management | `StorageModule` usage + cache clearing; history pruning; asset eviction candidates |
| **E. Device capability detection** | `device/CapabilityProbe.kt` + `src/services/capabilities/` |
| Android version, RAM, cameras, formats, controls, acceleration, storage | probed per section with fault isolation |
| Model compatibility | `modelCompatibility()` — tier, GPU delegate, 64-bit, storage |
| **Brand** | `src/brand/logoGeometry.ts`, `assets/brand/`, generated launcher + adaptive icons |

## Testing evidence

| Suite | Result |
|---|---|
| TypeScript `tsc --noEmit` (app + tests) | ✅ 0 errors |
| ESLint (`@react-native` config) | ✅ 0 errors, 0 warnings |
| Jest: 7 suites (services, real-SQLite DB tests via sql.js, UI accessibility, boot flow) | ✅ **111 / 111** |
| C++ engine tests under **AddressSanitizer + UBSan** | ✅ **27 / 27**, no reports |
| C++ engine tests under **ThreadSanitizer** | ✅ 27 / 27, no races |
| C++ Release build | ✅ 27 / 27, `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` clean |
| Kotlin compile (all 4 ABIs for native) | ✅ |
| Kotlin JVM unit tests | ✅ **32 / 32** (job manager run twice to check for flakiness) |
| Android Lint | ✅ 0 errors (18 warnings: RN template resources, dependency-update notices, KTX style hints) |
| Production JS bundle (Metro) + Hermes bytecode compile | ✅ |

### Acceptance criteria → how they are verified

| Criterion | Verification |
|---|---|
| Opens reliably | Boot sequence unit-tested on success, DB failure (retryable error screen), and degraded paths (capability timeout → cached profile, missing engine, storage failure). `App` render test covers splash → error. |
| Handles interruptions | Camera preview active only when focused and foregrounded; CameraX lifecycle binding; shutter disabled until rebind (`onCameraReady`); jobs cancelled on background; permission re-checked on foreground; lifecycle coordinator tests. |
| Manages permissions correctly | 7 tests across API 29/33/34 mappings, grant, deny, never-ask-again → settings, recovery, partial access. |
| Processes test images without crashes | 27 sanitizer-clean engine tests incl. invalid input, out-of-bounds crops, NaN params and unknown ops. Engine Lab runs the full pipeline on a generated photo on-device. |
| No uncontrolled memory growth | `repeated_processing_has_no_memory_growth`: 300 pipeline runs return exactly to baseline with a bounded peak. `memory_budget_fails_cleanly…`: over-budget work fails gracefully instead of being OOM-killed. Cancellation and concurrency tests assert zero leaked bytes. Engine Lab reports "memory after run" on real devices. |

## Code review

A formal multi-angle review of the full Phase 1 diff found 10 issues. All are fixed, with regression tests where testable:

1. Crop coordinates were rescaled even after a resize → only crops before the first resize are rescaled (Kotlin test).
2. Tonal ops ran on premultiplied Android pixels, corrupting translucent edges → straight-alpha contract; decode unpremultiplied and convert other configs safely.
3. `settings.reset()` wiped internal bookkeeping → only user keys are reset (test).
4. Capture dimensions ignored EXIF orientation → display-oriented bounds.
5. UI tier and native memory budget used different RAM thresholds → shared 90%-of-marketed rule, pinned by matching tests in TS and Kotlin.
6. Profiler JSON could truncate under concurrency → retry-with-headroom.
7. Engine Lab showed a stale peak → peak reset before measuring (test).
8. Shutter stayed enabled while the camera was rebinding → session resets whenever the preview deactivates.
9. Duplicated internal-settings SQL with unguarded `JSON.parse` → reuses the guarded repository API (corrupt-value test).
10. Dead `estimatePeakBytes` code → removed.

Found while verifying the fixes:

- UBSan flagged an out-of-range enum load at the C ABI → `format` parameters are now `int32_t`.
- The job manager notified listeners before releasing native resources → cleanup now happens first (test tightened).
- White labels on the bright spectrum gradient failed WCAG AA → a deeper `action` gradient for text-bearing controls (test).
- Glass tab dock text collided with scrolled content (no backdrop blur on Android) → denser smoked glass.
- Pack badges were illegible on bright artwork → a `media` badge style.

## Known limitations (intentional for Phase 1)

- No on-device run yet: there is no emulator or device in this environment, so camera binding and on-device performance still need a device-lab pass. The Engine Lab screen and `docs/design` previews support that.
- OpenCV and inference runtimes are not integrated yet. The engine's C ABI and the model-compatibility layer are ready for them.
- Entitlements are catalogued but not enforced (`ENFORCEMENT_ENABLED = false`), as specified. No Play Billing yet.
- Glass is built from layered translucency over a pre-blurred aurora, because React Native views on Android have no live backdrop blur.
- Gallery import (photo-picker flow) is permission-ready but has no UI yet. It belongs to the editing phase.

## Design preview

`docs/design/`, rendered from the app's own tokens, gradients, logo geometry and icon paths:

- `overview-1.png`, `overview-2.png`: all seven screens in device frames
- `brand-sheet.png`: logo, app and adaptive icons, wordmark, palette, components
- `screen-*.png`: individual screens at 2×

Regenerate with `node scripts/render-design-preview.mjs` (icons: `npm run icons`).
