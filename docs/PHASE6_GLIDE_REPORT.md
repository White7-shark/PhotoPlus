# Phase 6 — Real phone photos and video, and Glide: Report

This phase had two parts:

1. Run a real phone photo and a 5-second phone video through everything built in Phases 2–5, including Spotlight, and show before and after.
2. Add a new camera option: a video of up to 7 seconds that is steadied, cleaned and sharpened, with beauty and optional Spotlight.

The new option is called **Glide** ("7-second pro video: steady, sharp, glowing").

The user's photo and video, and every result made from them, stay out of the repository. They were processed in the session's scratch space and sent only to the user. This report gives measurements only.

## Part 1 — Real phone media through Phases 2–5

### Photo (selfie, warm indoor light, deep skin tone)

Stages: Clarity, then Glow (Smooth plus face touches), then Advanced portrait, then Spotlight (depth model), then Colour pop.

The real photo showed two faults that the synthetic test scenes had missed.

| Problem seen | Cause | Fix | Before → after |
|---|---|---|---|
| Clarity made deep skin look grey (b\* 9.3 → 6.9) | White balance cooled the whole frame, skin included | **Skin-sparing white balance.** A cooling correction reaches skin-hue pixels at one third strength (`tone.cpp`) | b\* 9.3 → 8.7 (was 6.9) |
| Clarity tripled fine texture (3.5 → 10.4) | The phone had already sharpened the photo; deconvolution sharpened it again | **Detail budget.** Gain in fine texture is capped at 1.7× the input (`applyDetailBudget`, `kDetailBudget`) | ×1.9, then ×1.2 after Glow (was ×3.0) |

Glow healed 25–29 spots on this photo. No backlight lift was applied, as expected: the rule checks the light around the head, not skin brightness. Spotlight used the depth model (blur radius 58.8, focus 0.83).

Engine tests added, all of which fail without their fix:
- `white_balance_neutralises_the_light_but_keeps_skin_warm`: skin keeps more than 93% of its warmth (96% with the fix, 89% without).
- `enhance_does_not_oversharpen_a_photo_the_phone_already_sharpened`
- `detail_budget_caps_texture_gain_and_leaves_moderate_restoration_alone`

### Video (5.5 s, 1080×1920, 30 fps, hand-held selfie, dim room)

The first Glide run looked washed out: mean luma 0.27 → 0.41, and the 5th percentile doubled from 0.078 to 0.157.

The cause was auto-levels lifting a dim frame with a gamma curve, which also raises the blacks. It now uses an exposure-style lift, `v·(1+k)/(1+k·v)` with k ≤ 1 and a target mean of 0.38. This brightens the midtones and leaves the blacks where they are. Regression test: `auto_levels_brighten_a_dim_photo_without_lifting_its_blacks` (median 31 → 59, shadows 13 → 10; the gamma version gave 13 → 24).

The final run, with shake measured independently using OpenCV phase correlation (`scripts/glide-demo.py`):

| | Original | Glide | Glide + Spotlight |
|---|---|---|---|
| Frame-to-frame shake (px, independent measure) | 4.62 | **1.74** (−62%) | 1.88 |
| Engine's own shake estimate (px) | 4.70 | 2.97 | 2.97 |
| Crop used for steadying | — | 1.12× | 1.12× |
| Luma: 5th percentile / median / 95th percentile (frame 80) | 0.078 / 0.220 / 0.584 | 0.090 / 0.341 / 0.718 | 0.094 / 0.347 / 0.718 |
| Face skin L\* / a\* / b\* (frame 80) | 37.0 / 13.2 / 1.3 | 56.7 / 14.6 / 1.5 | 56.7 / 14.8 / 1.5 |

The face is brighter with its hue kept, the blacks stay deep, and noise is cleaned. Spotlight blurs the background while the steadying holds.

Host timings per 1080p frame (4-core Xeon): enhance 66 ms, portrait 153 ms, portrait with Spotlight 324 ms.

## Part 2 — Glide

### How it works for the user

1. Pick **Glide** in the camera's mode switcher. The camera rebinds for 16:9 video and asks for the microphone once. Without the microphone, clips are recorded silently; Glide is never blocked.
2. Choose **Spotlight**: Off, Light, Medium or Strong. **Glow** follows the camera's Glow chip (Off, Natural, Smooth or Flawless).
3. Tap the red shutter. A badge counts down from "7s left". Tap again to stop early; at 7 seconds recording stops on its own. Clips under 1 second are discarded with an explanation.
4. A progress pill shows "Finding the shake", "Reading the light" and "Crafting your Glide · NN%", with a cancel button.
5. The clip is saved to the gallery (Movies/Photo Plus), and the **Glide preview** opens. It offers Glide / As recorded, sound on or off, "What Glide did" (for example "63% steadier"), Share and Save. A **Watch** chip in the camera reopens the last clip.

While a clip is recording, the mode switcher and the lens flip are hidden. Photo-only controls stay hidden in Glide mode: flash, props, Vibe Match, aspect ratio and resolution.

### Plan access

Glide is free, as Spotlight and Glow are for photos (`features.ts: glide`, `PRODUCT_SPEC §2.1`). Free-plan clips carry the Photo Plus mark in every frame, as free photos do. The mark comes from the same `ExportPolicy` / `WatermarkRenderer` and is drawn before encoding, so no unmarked copy is ever written.

### Pipeline

```
CameraX VideoCapture<Recorder> (FHD, fallback HD; front camera mirrored as previewed)
  └─ captures/glide/GLIDE_<t>_raw.mp4  (≤ 7 s; raw clips kept 24 h for "As recorded")
EngineModule.glide → GlideProcessor (job, cancellable, progress with a stage)
  pass 1 (640 px copies):  pp_video_estimate_motion (similarity, outliers such as a walking person ignored)
                           ML Kit selfie segmentation (stream mode) + face contours → LandmarkSmoother
  middle frame:            noise and blur measured; clip-wide tone curves (pp_video_fit_curves)
  plan:                    pp_video_stabilize: Gaussian-smoothed path, crop ≤ 1.12×, eases off where the crop would run out
  pass 2 (full size):      pp_video_enhance_frame (temporal denoise with per-channel agreement, deblur, curves)
                           → pp_portrait (Spotlight / Glow, steadied mask) → pp_face_adjust
                           → pp_video_warp → watermark (free plan)
                           → GlideEncoder: MediaCodec H.264 (~0.2 bits per pixel per frame, ≈12 Mbit/s at 1080p30),
                             exact timestamps, original sound copied
  → exports/PhotoPlus_Glide_<date>.mp4 → MediaStore Movies/Photo Plus
```

New code:

| Layer | Files |
|---|---|
| Engine | `include/photoplus/video.hpp`, `src/video/video.cpp`, `pp_video_*` in `pp_engine.h`, `tests/video_tests.cpp`, `tools/video_demo.cpp` |
| JNI | `nativeVideoMotion / Stabilize / EnhanceFrame / Warp / FitCurves / SteadyMask`, `nativeRgbaToYuv420` |
| Kotlin | `video/GlideOptions.kt`, `GlideProcessor.kt`, `GlideEncoder.kt`, `FaceMasks.kt`, `VideoPlayerView.kt` / `VideoPlayerManager.kt` (Fabric `PhotoPlusVideoView`), `CameraController.startRecording`, `CameraModule.startVideo/stopVideo`, `StorageModule.saveVideoToGallery`, `camera-video` dependency, `RECORD_AUDIO` |
| JS | `services/camera/glide.ts` (request, timing, labels), `CameraService.startVideo/stopVideo`, `ImageProcessingService.glide` (with `onStage`), `StorageService.saveVideoToGallery`, microphone permission, `screens/camera/useGlide.ts`, `GlidePanel`, the Glide shutter, `GlidePreviewScreen`, `components/VideoView.tsx` |

## Tests

| Suite | Result |
|---|---|
| Engine (`photoplus_engine_tests`) | 131 / 131, also under AddressSanitizer + UBSan |
| Kotlin unit (`testDebugUnitTest`) | 129 / 129. New `GlideTest`: options parsing and ranges, exact frame times, bitrate, landmark smoothing, video sessions |
| Android lint (`lintDebug`) | No issues |
| Jest | 225 / 225. New `glide.test.tsx`, 13 tests: request building, timing, labels, the camera service, the job's stage and progress events, and the camera flow end to end. The flow covers recording with and without sound, early stop, a clip that is too short, a crafting failure and switching back to photos. The Glide preview covers playback, compare, mute, share, save and a clip that won't play |
| TypeScript, ESLint, Prettier | Clean |

## Not yet verified on a phone

No APK was built (as instructed), so the following have been compiled and unit-tested but not run on a device:

- CameraX recording, including the duration limit and finalize events.
- `MediaMetadataRetriever.getFramesAtIndex` decoding.
- The MediaCodec encoder with `getInputImage` and the YUV conversion.
- ML Kit on video frames.
- On-device speed. A rough estimate is 2–3× the host per frame, which gives about 1–2 minutes for a 7-second clip, or longer with Spotlight. Progress and cancel are in place for this.

The engine work itself was validated on the real clip above.

## Open items

- Speed on low-memory phones: a lower working size for clips on 3 GB devices.
- Spotlight in Glide finds the subject automatically; tap-to-choose (as in photo Spotlight) is not offered for video yet.
- Glide clips are not yet listed in Projects. They live in the gallery, and the camera's Watch chip reopens the last one.
