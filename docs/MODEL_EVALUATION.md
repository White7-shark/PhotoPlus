# On-device models: evaluation and selection (Phase 4, step 4.3)

Every model the app runs goes through one interface (`com.photoplus.ml`), and is chosen only after four checks: **compatibility**, **commercial licence**, **memory** and **inference speed**. This page records those checks, what was measured and on what hardware, and which candidates were turned down.

> **About the numbers.** "Host" figures were measured in this repository's build environment (4-core Intel Xeon @ 2.1 GHz, LiteRT 2.3.0 Python runtime with the XNNPACK CPU delegate). They are useful for comparing models and catching regressions. They are **not** phone timings. No phone was attached to this environment, so device numbers are left blank. They will be filled in from the app's own diagnostics (`getEngineInfo().models`), which record the accelerator used, load time, average and worst run time, and failures for each model.

## The common interface

| Piece | What it does |
|---|---|
| `ModelSpec` | One place per model: asset, purpose, **licence**, source, input tensor, accelerator order, measured working memory |
| `TensorInput` | Size, layout (NHWC/NCHW), fit (stretch, letterbox, centre-crop) and normalisation. Validated on creation. The engine prepares the tensor natively (`pp_image_to_tensor`): it resizes with an area filter, normalises, and returns the placement so outputs map back to the photo |
| `ModelRuntime.use(spec) { … }` | Loads on first use and runs one model at a time per model. Under memory pressure it releases the model (a model that is running is released when its run finishes). A missing or failing model returns `null`, so every feature falls back to its non-ML path |
| `AcceleratorPlan` | Tries the GPU, then the CPU. Each must pass a warm-up check: a structured ramp input must give finite, non-zero output, because broken GPU paths usually give NaN or zeros. A GPU that failed is not retried in this session. **A model loads only when the device can spare its measured memory plus 25 %**, and never under critical pressure (`canLoad`). Otherwise the feature uses its fallback and tries again next time |
| `InferenceStats` | Per model: accelerator, load time, runs, average and worst time, failures, and skips for lack of memory |
| Engine helpers | `pp_image_to_tensor` and `pp_tensor_to_mask` (sigmoid or softmax, strips the letterbox, fails on NaN), then `pp_mask_refine` (a guided filter snaps a low-resolution mask to hair and fur edges at full size) |

## Shipped

| Model | Used for | Licence | File | Host CPU, 4 threads | Host CPU, 1 thread | Working memory (host, peak) | Device |
|---|---|---|---|---|---|---|---|
| **Depth Anything V2 Small**, LiteRT, int8 weights, float activations (`litert-community/depth-anything-v2-small`, rev `178427e`, SHA-256 checked at build) | Spotlight: background blur that grows with distance | **Apache-2.0** | 27.7 MB | load 111 ms, run **244 ms** median (first 234 ms), input 686×518 NCHW | run 992 ms | **+234 MB** (weights unpacked to float by XNNPACK) | to be measured |
| ML Kit selfie segmentation (bundled with ML Kit 16.0.0-beta6) | Spotlight: person mask when no subject was tapped | Google ML Kit terms (free for commercial use) | 0.25 MB | run 5 ms, input 256×256 | — | +16 MB | to be measured |
| ML Kit face detection 16.1.7 (bundled) | Glow, props fitting, camera props, **Phase 5 face landmarks** (contours for every face via per-face crops) | Google ML Kit terms | bundled | not measured separately | — | — | to be measured |

The depth model's `memoryBytes` was raised from an estimate of 160 MB to the measured **240 MB**, and is now enforced through `canLoad`. On low-memory phones the depth model is therefore skipped. Spotlight then uses the engine's ground-plane depth estimate, which needs no model and was already the fallback.

## Candidates considered

| Candidate | Task | Licence | Decision |
|---|---|---|---|
| Depth Anything V2 **Small** | depth | Apache-2.0 | ✅ **Shipped** (above) |
| Depth Anything V2 Base / Large / Giant | depth | **CC-BY-NC-4.0** | ❌ Non-commercial: cannot ship in a paid app |
| MiDaS v2.1 small | depth | MIT | ⏸ Commercially fine, but older and coarser; kept as a fallback candidate if a device class can't run Depth Anything Small |
| BRIA RMBG-1.4 | background removal | BRIA licence (**non-commercial** without an agreement) | ❌ Rejected on licence |
| BRIA RMBG-2.0 | background removal | **CC-BY-NC-4.0** | ❌ Rejected on licence |
| BiRefNet | background removal | MIT | ⏸ Good quality and commercially fine, but the standard model uses a large Swin backbone at 1024² input, too heavy for mid-range phones. Revisit for the photoshoot phase with a lite variant measured on a device |
| MODNet | portrait matting | Apache-2.0 | ⏸ Commercially fine and small. Candidate for background replacement (portrait matting), to be measured when that feature is built |
| U²-Net / U²-Netp | salient object | Apache-2.0 | ⏸ Commercially fine. U²-Netp is tiny (~4.7 MB) but soft-edged; our guided refinement compensates. Candidate for object cut-outs |
| MediaPipe selfie segmenter (Tasks Vision) | person mask | Apache-2.0 | ❌ Not as a separate runtime: ML Kit gives the same capability without ~35 MB of extra native code (see DEPENDENCIES "On-device vision choice") |
| Segment Anything / MobileSAM | tap-to-select | Apache-2.0 | ⏸ Commercially fine. The image encoder is heavy for mid-range phones. Tap cut-out uses ML Kit subject segmentation plus the engine's own cut-out today |

✅ shipped · ⏸ allowed, not yet needed or not yet measured on a device · ❌ rejected

## Face landmarks (Phase 5)

| Candidate | Points | Licence | Decision |
|---|---|---|---|
| **ML Kit face detection (contours)** | 133 contour points (+ 10 landmarks) per face | Google ML Kit terms | ✅ **Used.** Already shipped and offline. Contours are measured only for the most prominent face, so each other face is detected again in an enlarged crop where it is the main face. Converted to the engine's 131-point layout |
| MediaPipe Face Landmarker | 478-point mesh | Apache-2.0 | ⏸ Richer (irises), but adds ~35 MB of native code and a second vision runtime (see DEPENDENCIES). Used only on the desktop, to validate (`scripts/validate-portrait.py`) |

## Selection rules (applied to every future model)

1. **Licence first.** Only licences that allow commercial use in a closed-source paid app (Apache-2.0, MIT, BSD, or a vendor licence that grants it). Each licence is checked on the exact weights used, not only on the code repository, because they often differ. The licence is recorded in `ModelSpec.license`, and a unit test fails the build if a shipped model's licence is not on the allow-list.
2. **Compatibility.** LiteRT `CompiledModel` with GPU and CPU accelerators. Every operation must be supported on CPU, because the CPU is the guaranteed fallback. 16 KB page-aligned native code. Fixed input shape preferred. The model is fetched at build time with a pinned revision and SHA-256.
3. **Memory.** The peak working memory is measured (not the file size) and stored in `ModelSpec.memoryBytes`. The runtime enforces it.
4. **Speed.** Measured on the host for comparison, then on the target devices through `InferenceStats`. Each feature has its own latency budget (Spotlight runs once per photo, so a few hundred ms is acceptable; anything per-frame in the camera must fit in the frame time).
5. **Fallback.** Every model-backed feature keeps a working path without the model.

## Reproducing the host measurements

```sh
python3 -m venv .venv && .venv/bin/pip install ai-edge-litert numpy
# the depth model is fetched by the Android build (task fetchDepthModel) into
# android/app/build/generated/depthModel/spotlight_depth.tflite
```

Load the model with `ai_edge_litert.interpreter.Interpreter(model_path, num_threads=N)`, run it six times on a normal-distributed input, and report the median of runs 2–6 plus the change in peak RSS.
