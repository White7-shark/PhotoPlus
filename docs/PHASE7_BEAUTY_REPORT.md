# Phase 7 — Beauty filters, the beauty engine and live camera beauty: Report

Asked for:
- 13 beauty filters (2 free, 11 premium), each matched to a reference screenshot that serves as its label in the app;
- a six-layer beauty engine (skin, face, background, lighting, colour, compositor);
- live beauty in the camera, with a GPU preview, presets, intensity, Skin / Face / Lighting / Background sliders and an Original comparison;
- Portrait mode working with beauty, with independent controls.

Everything runs on the device. There are no cloud APIs and no generative models.

Your own photos (the selfie and the earlier video) were used only in the session's scratch space and are not in the repository.

## The 13 filters

| # | Filter | Plan | What makes it distinct |
|---|---|---|---|
| 1 | Natural Glow | Free | Light skin refinement, dewy sheen, soft face light; neutral colour |
| 2 | Soft Radiance | Free | Warm key light, highlight bloom, gentle soft focus, warm backdrop |
| 3 | Velvet Skin | Premium | Strongest smoothing (texture kept), shine tamed, darker vignetted backdrop |
| 4 | Golden Goddess | Premium | Golden room light and glow, gold highlights / warm shadows, skin kept off the cast |
| 5 | Crystal Beauty | Premium | Cool editorial grade, luminous highlights, cool rim light, blue backdrop |
| 6 | Blush Romance | Premium | Blush, rosy skin, pink haze and soft focus, lifted blacks |
| 7 | Bronze Luxe | Premium | Contour and highlight, bronze warmth, deep shadows, dark vignette |
| 8 | Pure Elegance | Premium | Bright studio light, shadow lift, white, desaturated backdrop |
| 9 | Midnight Glam | Premium | Directional studio light, contrast, crushed blacks, near-black backdrop |
| 10 | Peach Perfection | Premium | Peach blush and backdrop, dewy sheen, soft warm glow |
| 11 | Emerald Aura | Premium | Selective emerald hue band, green shadows, warm practical light, skin protected |
| 12 | Dreamlight | Premium | Soft focus and bloom, lilac haze, creamy background glow |
| 13 | Studio Royal | Premium | Studio key light, rim light, grey-blue backdrop, atmospheric glow |

The definitions live in `src/services/beauty/presets.json`. Each preset groups its settings by the camera's sliders: Skin, Face, Lighting and Background, plus a colour look that only Intensity scales.

The same resolver (`src/services/beauty/recipe.ts`) serves the app, the tests and the desktop validation tools.

### Cover images

Each filter's cover is cropped from the matching screenshot, in order: `src/assets/beauty/01-natural-glow.jpg` to `13-studio-royal.jpg`, at 240 px.

**Rights:** these are third-party photos. One (Crystal Beauty's source) carries a photographer's mark. Confirm the rights to use them in a published app, or swap in licensed or commissioned images. Replacing a file with the same name is all it takes.

## The beauty engine

| Layer | Where | What |
|---|---|---|
| A Skin | `portrait/portrait.cpp` (existing) | Edge-aware smoothing by frequency separation, so texture is kept. Spot healing, even tone toward the person's own tone, exposure-style lift |
| B Face | `portrait/face.cpp` | Eyes, lips, teeth, under-eye, shine, studio key light. New: **blush, contour, highlight**, analytic in face coordinates (`makeupAt`) so the shader matches |
| C Background | `beauty/beauty.cpp` | Light (stops, highlight roll-off), saturation, tint (luminance kept), glow at a position, haze, vignette, warmth |
| D Lighting | `beauty/beauty.cpp` | Softbox light on the subject, angled by the face's own shading (`estimateLightAngle`). Key-light colour, rim light on the subject's edge, bloom, soft focus, shadow recovery, skin sheen |
| E Colour | `beauty/beauty.cpp` | White balance, contrast, blacks, highlights, saturation, vibrance, split toning, one selective hue band, skin-only warmth / rosiness / luminance. The scene grade is kept off skin (`skinProtect`) |
| F Compositor | `beauty::apply`, `pp_beauty` | A → B → C/D/E from one recipe; `pp_beauty_measure` gives the scene (skin colour and spread, subject extent, light side) |

Rules learnt from real photos and enforced by tests:
- **Added light never clips.** Added light rolls off below 255; nothing newly clips.
- **Faces always count as subject.** Segmentation missed a brightly lit cheek against a pale wall; the landmark face area now always counts as subject.
- **Coloured light is protected on skin.** Key-light colour and rim colour are skin-protected, so a cool or golden look tints the room, not the face.
- **Shadow recovery is for the subject.** The background has its own light control.

## Live camera beauty

```
CameraX Preview ─ CameraEffect(PREVIEW) ─ BeautyProcessor (GL thread, EGL, GLES 3)
   camera frame (external OES)
     ├─ down.frag    quarter resolution, linear light (+ subject mask in alpha)
     ├─ blur.frag    separable Gaussian (sigma 1.5% of the short side)   — the grade's low-pass
     ├─ down + blur  half resolution (sigma 0.4%)                        — the skin base
     └─ composite.frag  skin → face → grade (line-for-line mirror of the engine's shade())
ImageAnalysis ─ BeautyAnalyzer (~12/s): faces every frame, selfie mask every other frame,
                scene twice a second (pp_beauty_measure on a 256 px preview-space frame)
```

- **Preview space:** masks, landmarks and positions are expressed in what the viewfinder shows (upright, cropped, mirrored for the front camera). The shader's orientation is composed from CameraX's sensor-to-buffer transforms (`PreviewSpace.orient`). The props tracker's mapping is unit-tested against it for every rotation and mirror.
- **No frame copies through JS or Kotlin:** frames stay on the GPU. JS sends only the recipe, about 80 numbers (`setLiveBeauty`). Changing presets or sliders never rebinds the camera; only turning live beauty on or off does.
- **Original:** hold to see the camera untouched (`setBeautyOriginal`).
- **Fallback:** on low-RAM phones, phones without GLES 3 external-texture support, or after any GL error at run time, the camera rebinds without the effect. The Beauty tray then says the preview shows the camera as it is, and photos and clips still get the full filter.

### Preview matches the saved photo

`scripts/beauty-gpu-parity.mjs` runs the same shader files in WebGL2 (headless Chromium) on real photos. It compares them with the CPU engine for every preset (CIELAB ΔE):

| | Mean over the frame | Mean on the face | Notes |
|---|---|---|---|
| Lighting, background and colour only | 0.1–0.27 | ≤ 0.08 | Below what the eye can see |
| Full presets, three real photos | 0.4–1.6 | 0.9–2.7 | The rest is the saved photo's face-precise skin retouch |

Building the parity check found three gaps between the shader and the engine, all fixed: shine, even tone and skin lift. Shine was the largest: face p95 went from 9.3 to 2.6.

## Portrait mode with beauty

Spotlight has its own controls: Depth Blur, Beauty (intensity), and under "Light, tone & edges" Subject Light, Background Light, Background Tone and Edge Refinement.

These controls are independent of the filter: Natural Glow with strong blur, or Midnight Glam with none. The filter can be changed while in Spotlight. The live preview shows the softened background; the saved photo gets the engine's depth-aware lens blur.

Beauty also applies to Glide clips (every frame) and to imported photos in the editor (a new **Beauty** tool).

## Validation on real faces (`scripts/beauty-demo.py`)

The inputs:
- your selfie (private);
- the Kodak portrait;
- four reference photos;
- 32 FairFace faces, grouped by measured skin tone (ITA, very light to dark).

MediaPipe stands in for ML Kit.

| Measure | Result |
|---|---|
| Distinct looks | The closest two presets differ by mean ΔE 3–4.6 over the frame (median 3.8 on FairFace). The exception is a photo on a white studio backdrop (1.8): brightening a white wall changes little |
| Skin hue on the cheeks (CIELAB h_ab) | Mean shift 0.9–7.6°. Neutral presets 0.9–1.8°; Golden Goddess, Blush Romance and Dreamlight warm or pink the skin on purpose |
| Skin texture kept | 0.67–1.16 of the original (0.67 is Dreamlight's soft focus; most 0.84–0.99) |
| New clipping | 0 for every preset on every photo |
| Speed (host, 4 cores) | 130–270 ms per preset for 0.4–1.6 MP photos: the engine's whole compositor. The masks and landmarks from the on-device models are prepared beforehand |

## Tests

| Suite | New |
|---|---|
| Engine | `beauty_tests.cpp`, 12 tests, also run under AddressSanitizer + UBSan. A neutral recipe is an exact no-op. Warm looks reach the room, not the skin, for 6 tones. Green bands, background and subject separation, the key light following shading, the rim on the edge, no clipping, make-up placement with texture kept, distinct looks, bad input, cancellation |
| Kotlin | `BeautyTest`, 8 tests. Every preset reaches the engine exactly as JS resolves it (shared fixture from `scripts/beauty-fixture.mts`). Ranges clamped. Live uniforms equal the desktop tool's. Affine and preview-space geometry against the props tracker; session, capture and editor parsing |
| Jest | `beauty.test.tsx`, 13 tests. Catalogue order and plans; every pair of presets differs in at least 5 parameters; intensity and group scaling; Portrait independence; engine order; settings validation; tray locking and sliders; Portrait controls; editor tool |
| Jest | Camera: filter choice, live recipe, Original hold, photos and Spotlight shots carry the recipe, the live-unavailable note. Glide: clips carry the filter |

## Not verified on a phone

No APK was built, as instructed.

**Verified off the phone:**
- The Kotlin compiles, including CameraX's `CameraEffect` and `SurfaceProcessor` APIs.
- The native library builds with the NDK.
- The shaders compile and match the engine in WebGL2.

**Not yet verified on a device:**
- The GL thread on real camera frames.
- Live orientation on real sensors, especially the front camera.
- Live frame rate.

The design keeps that risk small: any GL error turns the effect off and the camera carries on.

## Open items

- Live frame rate on low-end GPUs. The composite runs at the preview's resolution; a lower internal resolution for weak GPUs is a small change.
- The live face touches use the largest face only; saved photos treat every face.
- Live skin smoothing is an approximation (mean ΔE 1.8 on the face against the saved photo).
