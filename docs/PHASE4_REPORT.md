# Phase 4 — Native Image-Processing Engine: Report

**Status:** Complete. No APK was built, as requested. Everything below was compiled and tested on the build host: the C++ engine (Release, ASan + UBSan, TSan), the Kotlin layer (compile, unit tests, Android lint) and the app (TypeScript, ESLint, Prettier, Jest). The GPU path was verified on a software GLES 3.1 driver (Mesa llvmpipe). It still needs a pass on physical phones (Adreno, Mali, PowerVR) before release. See "Open items".

Preview: [`design/phase4-engine.png`](design/phase4-engine.png). It shows real engine output: a Kodak test photo tilted by the engine to simulate shooting from below, then fixed with one tap of **Auto straighten lines**.

**Plan access.** The engine is shared infrastructure for every plan. Of what this phase exposes in the app, crop, rotate and straighten stay free. Perspective correction (manual and automatic) is registered under the Premium feature *Advanced editing tools*. Background replacement (the compositing and mask operations here) will be exposed under *Advanced background removal & replacement* when that feature ships.

## Deliverables

| Step | Requirement | Where / how |
|---|---|---|
| **4.1** | Decoding & encoding | `codec/` — JPEG, PNG, BMP, GIF via stb (public domain), with the image size checked against the memory budget **before** decoding, EXIF orientation applied for all 8 orientations, an optional area-filtered size limit while decoding, and atomic file writes (temporary file, then rename). `pp_decode_*`, `pp_encode_*` |
| | Resizing & cropping | Separable resampling: area (box average, no aliasing), bilinear, Lanczos-3 (anti-aliased when shrinking); *auto* picks area to shrink and Lanczos to enlarge. Uses a few rows of scratch, not a full-image buffer. `PP_OP_RESIZE` (+ filter), `PP_OP_CROP` |
| | Colour transformations | Tone, warmth, saturation, looks (existing); **3D LUTs** (tetrahedral, `.cube` order) and **3×4 colour matrices** (optionally in linear light). `pp_apply_lut3d`, `pp_color_matrix` |
| | Alpha compositing & blending | W3C *Compositing and Blending Level 1*: 12 modes (normal, multiply, screen, overlay, soft/hard light, darken, lighten, difference, add, colour, luminosity), straight alpha in and out, optional matte. `pp_blend` |
| | Masks | Invert, feather, grow/shrink (van Herk/Gil-Werman, constant time per pixel), threshold. **Edge-aware refinement** (fast guided filter): a model's low-resolution mask snaps to hair and fur at full size. `pp_mask_adjust`, `pp_mask_refine` |
| | Blur | Gaussian, any sigma. Sigma > 8 runs on a reduced copy, so cost stays flat (sigma 40 on 12 MP: 93 ms vs 300 ms for sigma 3). `PP_OP_BLUR` |
| | Sharpening | Unsharp mask on **luminance only** (no colour fringes), with radius and noise threshold. The editor scales the radius with image size, so the preview and the saved photo look the same. `PP_OP_SHARPEN` |
| | Noise reduction | Edge-preserving and scaled to the noise the photo actually has: gentle on luma, firm on chroma blotches. `PP_OP_DENOISE` |
| | Geometric transforms | Rotate, flip, straighten (existing); general homography warps. `pp_warp_perspective` |
| | Perspective correction | Keystone (vertical and horizontal), plus **automatic detection** of leaning verticals (line-direction vote, separating roll from keystone). In the editor: Crop › Vertical/Horizontal perspective and **Auto straighten lines**. `PP_OP_PERSPECTIVE`, `pp_detect_keystone` |
| **4.2** | Reduced-resolution previews | `pp_process_ex(…, max_edge)`: the image is area-reduced first, then pixel-sized settings (crop, resize, blur, sharpen radius) are scaled to match. The preview differs from a reduced full render by **1.19 levels** on average (test `core_previews_match_the_full_render`) |
| | Full-resolution rendering | `max_edge = 0`. Saves and exports always render on the CPU at full size |
| | Bounded memory | Every allocation goes through `MemoryTracker`. `pp_process_estimate` validates the whole pipeline first and returns a peak bound. That bound held in the tests (colour chain: 8.6 MB estimated / 8.6 MB measured; detail chain: 57.2 / 38.9). Over budget → `PP_ERROR_BUDGET_EXCEEDED`, never a crash |
| | Temporary-buffer reuse | Freed buffers ≥ 64 KB are kept for exact-size reuse, capped at 96 MB and at a quarter of the budget. They count against the budget and are evicted first. `pp_memory_trim()` returns them under memory pressure. No growth over repeated runs (test `repeated_processing_has_no_memory_growth`) |
| | Cancellation | One token per job, checked between rows. Cancelling a 12 MP job returns in **25 ms**. The app cancels by job id |
| | Safe cleanup | RAII throughout. C handles are destroyed explicitly. The Kotlin `JobManager` releases the native token and result **before** notifying JavaScript. The GPU context is released on shutdown and under critical memory pressure |
| | Progress reporting | Monotonic 0–1 per job (each operation is a step; parallel loops report per chunk). The app receives `PhotoPlusJobProgress` events, emitted only when progress moves by at least 1 %, and never after the job's final state (tested) |
| **4.3** | Common model interface | `com.photoplus.ml`: `ModelSpec`, `TensorInput`, `ModelRuntime.use { }`, `AcceleratorPlan`, `InferenceStats`. Native tensor preparation and mask decoding. Spotlight's depth model now runs through it |
| | Evaluation before selection | [`MODEL_EVALUATION.md`](MODEL_EVALUATION.md): licence (non-commercial models rejected: Depth Anything V2 Base/Large, BRIA RMBG-1.4/2.0), compatibility, measured memory (**+234 MB** for the depth model, now enforced before loading) and speed (host CPU: 244 ms with 4 threads) |
| **4.4** | GPU processing | OpenGL ES 3.1 compute shaders on a headless EGL context, on the engine's own thread. Runs tone, warmth, saturation, grayscale and blur (sigma ≤ 8) for previews up to 4.2 MP |
| | Only when supported, CPU fallback | At start-up a **self-test** renders every GPU operation and compares it with the CPU. The GPU is enabled only if every pixel is within 2 levels (llvmpipe: 0). Any GPU failure falls back to the CPU for that run. Saves and exports always use the CPU, so the files are identical on every device |

## Completion criteria

| Criterion | Evidence |
|---|---|
| **Processes large images safely** | Dimensions are checked before decoding (max 16 384 px per side, budget pre-check). Pipelines are estimated before running. Hostile files are rejected: truncated, wrong magic, huge declared sizes, zero-length (test `core_codec_rejects_hostile_files_safely`). 12 MP operations stay within a few multiples of the image size (table below). Clean under AddressSanitizer + UndefinedBehaviorSanitizer and ThreadSanitizer |
| **Predictable errors** | One status enum (`INVALID_ARGUMENT`, `OUT_OF_MEMORY`, `BUDGET_EXCEEDED`, `CANCELLED`, `UNSUPPORTED_FORMAT`, `INTERNAL`, `IO_ERROR`) with a message. Requests are validated in full before any work. Kotlin maps the statuses to `EngineException` codes, and JavaScript to `ProcessingError.code` |
| **Consistent results across devices** | Saves and exports run a deterministic CPU path (fixed operation order and kernels). The GPU is used only for previews, and only after matching the CPU within 2 levels on that device. The engine was **cross-checked against OpenCV 5.0** (below) |

### Cross-check against OpenCV 5.0 (independent reference)

`scripts/verify-engine-against-opencv.py` runs each operation through the engine's C API (`tools/ops_probe`) and through OpenCV, then compares them on a synthetic stress image (hard edges, fine texture, noise) and a real photo. Result: **44/44 within tolerance**.

| Check | Mean difference (levels) | Worst pixel |
|---|---|---|
| PNG decode | 0 (identical) | 0 |
| JPEG decode (stb vs libjpeg-turbo) | 0.05–0.08 | 3 |
| Area shrink ÷2, ÷2.5, ÷3 (vs `INTER_AREA`) | 0.000 | 1 |
| Bilinear ×1.7 (vs `INTER_LINEAR`) | 0.12 | 1 |
| Lanczos ×1.7 (3 lobes vs OpenCV's 4) | 0.08–0.47 | 7–12 |
| Gaussian blur σ 1.5 / 4 / 12 / 30 (vs `GaussianBlur`) | 0.004–0.17 | 2 |
| Perspective warp (vs `warpPerspective`) | 0.000 | 1 |
| 9 blend modes × 2 opacities (vs W3C formulas) | 0.000 | 1 |

### Performance (host, 4-core Xeon @ 2.1 GHz, Release, engine profiler; 12 MP = 4000×3000)

| Operation | Time | Peak engine memory (incl. 48 MB input) |
|---|---|---|
| Decode JPEG 12 MP | 86 ms | 97 MB |
| Resize 12 MP → 1600 px (auto = area) | 53 ms | 105 MB |
| Resize 12 MP → 27 MP (Lanczos-3) | 234 ms | 207 MB |
| Blur σ 3 | 300 ms | 149 MB |
| Blur σ 40 (reduced-copy path) | 93 ms | 146 MB |
| Perspective warp 12 MP | 108 ms | 105 MB |

Phone CPUs are typically slower per core than this host. The editor previews at screen size (≤ 1280 px), where these costs are ~8× smaller, and runs on the GPU when it is verified.

## Improvements to earlier work (as asked)

- **Editor previews** now use the reduced-resolution renderer and the verified GPU. Saves and exports stay on the CPU.
- **Sharpening** in the editor is luma-only with a noise threshold, and its radius follows image size: the preview no longer over-sharpens compared with the saved photo.
- **Progress** for every editor, Transform, Spotlight and enhance job (`onProgress` in `ImageProcessingService`).
- **Memory pressure** now also frees the engine's reusable buffers and, when critical, the GPU context (restarted, with its self-test, when pressure passes). Models are released as before, and are no longer loaded when the phone can't spare their measured memory.
- **Spotlight depth** runs through the common model runtime: GPU first if it passes its warm-up check, otherwise CPU, with statistics.
- **Older saved edits** (before perspective existed) reopen unchanged (`withDefaults`).
- **Keystone detection** is now a robust fit. It refits with a narrowing band, so texture edges that aren't part of straight lines no longer dilute the estimate. Before, it under-read real photos by ~30 % (kodim08: 0.69× the true tilt); now it reads 0.89–1.0× on real photos and 0.90–1.02× on a synthetic facade, in both directions. A new test (a textured scene) fails with the old fit (0.41 for 0.50) and passes with the new one (0.49–0.50).
- A leaked test timer in the processing tests, which made Jest warn intermittently, was fixed. ASan's leak checker also found a handle reused in one engine test, which is fixed.

## Tests

| Suite | Result |
|---|---|
| Engine (C++), Release with GPU | **102 tests pass**, including 18 new core tests and 4 GPU tests |
| Engine under ASan + UBSan | No reports |
| Engine under TSan | No reports (full suite) |
| OpenCV 5.0 cross-check | 44/44 |
| Kotlin unit tests | **115 pass** (the timing-based progress tests were stress-run: 750 jobs, no failures). New: job progress (rising, ≥ 1 %, clamped, never after the final state, silent while queued), the model interface (accelerator order, warm-up plausibility, ramp input layouts, input validation, commercial-licence allow-list, memory gate, statistics), perspective in the edit document, size-aware sharpening, new operation parameters |
| Android lint | 0 findings (`scheduleAtFixedRate` was replaced by fixed-delay scheduling, which lint recommends) |
| Jest | **205 pass**. New: job progress events (own job only, clamped, listeners removed), `detectPerspective` safety, older documents, perspective in geometry, the editor's Auto straighten lines (applies, fine-tunes, undoes) |
| TypeScript / ESLint / Prettier | Clean |

## OpenCV decision

OpenCV was evaluated for the app itself. The official Android SDK for 4.12 (current is 5.0) is a **118 MB** AAR. Even stripped to the needed modules, it would add tens of MB per ABI for operations the engine already implements. Those operations now **match OpenCV** within the tolerances above. Decision: **not shipped**. OpenCV is used on the development machine as the test oracle. It remains an option if a later phase needs something substantial it provides (for example feature matching for photoshoot replication). That would be evaluated on size then.

## Open items

1. **Device pass for the GPU path** on Adreno, Mali and PowerVR phones: check the self-test results (`getEngineInfo().gpu`) and preview timings.
2. **Model timings on devices** (`getEngineInfo().models`), to fill in the "Device" column of the model evaluation.
3. Blur on the CPU could use SIMD (NEON) for small sigmas, if device profiling shows it matters.
