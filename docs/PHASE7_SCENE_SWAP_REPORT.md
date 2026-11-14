# Phase 7 — Background Removal and Replacement (Scene Swap)

Scene Swap is the photoshoot studio's first tool. It is Premium. You choose a photo of a person. The person is cut out on the phone, and you correct the cut-out where needed. Then you choose a scene, move and size yourself in it, and save the result. The scene's light, colour and shadows are matched automatically. You can also save the cut-out on its own with a transparent background.

It lives in the **Photoshoot Studio** (Home → Replication Studio → Scene Swap), not in the camera. Everything runs on the device; no photo leaves it.

## The critical decision: clean plates

The backgrounds are **clean plates**: real photographs of places with nobody in them. Nothing behind the person ever has to be invented, so there is no attempt to reconstruct parts of a reference photo that were never captured.

- **Built-in:** 15 plates, all CC0 photographs from Poly Haven (credits below).
- **Your own:** you can add photos of empty places ("Add your own").
- **Packs:** future packs bring plates in the same format: an image plus horizon, floor, light side, shadow and rim. They plug into the same library (category "From packs").

## Features

| Asked for | How it works |
|---|---|
| Person segmentation | <ul><li>ML Kit subject segmentation, with a mask at the photo's full size. Falls back to the bundled person model, then to the engine's tap cut-out.</li><li>A tap chooses who to keep in a group photo ("Wrong person?").</li><li>MediaPipe Selfie Multiclass adds a map of hair, skin, clothes and accessories.</li></ul> |
| Background removal | `studio::cutout`: a full-resolution matte. |
| Transparent-background output | "Cut-out only" saves a PNG with straight alpha, trimmed to the person. |
| Background replacement | `studio::compose`: places the person in any plate, plain colour or your own photo. |
| Background libraries | <ul><li>15 built-in scenes in 5 groups: Studio, Indoors, Outdoors, Golden hour, Night.</li><li>8 plain colours (some with a studio fall-off) and "No background".</li><li>Your own backgrounds (up to 30).</li><li>A slot for packs.</li></ul> |
| Subject resizing and positioning | <ul><li>Drag to move, pinch to resize, a size slider, Mirror, Tilt.</li><li>Default placement: people stand on the scene's floor, sized by its horizon. Portraits enter from the bottom edge. A person cut off at the top fills the frame's height.</li></ul> |
| Edge refinement | <ul><li>A wide colour guided filter snaps the model's edge onto the photo's edge.</li><li>A two-colour model runs per pixel at full resolution.</li><li>Settings: Softer edge; Shrink or grow.</li></ul> |
| Hair and clothing-edge handling | <ul><li>Hair zones get a wider band where colour decides how transparent each pixel is (Hair detail slider).</li><li>Clothes and skin get a narrow, firm band.</li><li>The **Fix hair** brush lets colour decide anywhere you paint.</li></ul> |
| Foreground-background blending | <ul><li>Each edge pixel's colour is freed of the old background (blur-fusion foreground estimation), so there's no fringe.</li><li>Compositing is done in linear light.</li><li>The scene's light wraps onto the edges, plus a rim light from backlit scenes.</li></ul> |
| Colour matching | The person's exposure, white balance and contrast move toward the light of the spot where they stand. The changes are damped and bounded, so clothes and skin keep their own colour. |
| Perspective adjustments | <ul><li>Horizon rule: a standing person's height follows from where their feet are relative to the horizon. Moving someone back toward the horizon shrinks them ("Follow the scene's depth").</li><li>Tilt fixes vertical perspective; the person can also be turned.</li><li>The frame is cropped to Portrait 4:5, Story 9:16 or Square around the person.</li></ul> |
| Basic contact shadows | <ul><li>Where the feet meet the floor: a tight contact shadow plus a soft ambient one.</li><li>On sunlit plates, a soft cast shadow away from the light.</li><li>Shadows stay below the horizon and are left out when the photo has no feet in it.</li></ul> |
| Background blur | A lens-like blur of the scene. Each plate has its own default. On the ground, the area near the feet stays in focus. |
| High-quality export | <ul><li>Saved at the size that keeps the person's own detail, up to 4096 px.</li><li>JPEG quality 95 into the gallery (Pictures/Photo Plus) and the library. From there, the normal sharing and export rules apply.</li></ul> |

### The workflow (as specified)

1. **Select a photograph:** "Choose a photo".
2. **The engine identifies the subject:** automatic, with a tap to choose who.
3. **Review and correct the mask:**
   - see it on the dimmed photo, checks, white or black;
   - brushes: **Keep**, **Remove**, **Fix hair**, each with a brush size;
   - undo and redo;
   - edge sliders: Hair detail, Softer edge, Shrink or grow; "Start over".
4. **Select a background:** the library.
5. **Place the subject:** drag, pinch, sliders.
6. **The engine adjusts composition and colour:** colour match, light wrap and rim, shadows, blur and grain. All automatic and all adjustable.
7. **Review and export:** "Save photo" or "Cut-out only".

## Research

| Source | What we took |
|---|---|
| Google, *ML Kit Subject Segmentation (Android)* | Foreground confidence mask at the input's size, and per-subject masks. It is a beta Play-services library, already in the app for Spotlight. |
| Google AI Edge, *Image segmenter guide*; *Selfie Multiclass model card* (Apache-2.0) | <ul><li>Classes: background, hair, body skin, face skin, clothes, accessories.</li><li>Runs at 256 × 256, about 72 ms on a Pixel 6 GPU.</li><li>Version 1, pinned by SHA-256 `c6748b12…6507e0e0`, fetched at build time.</li></ul> |
| He, Sun & Tang, *Guided Image Filtering* (PAMI 2013) | <ul><li>Colour guided filter for edge-aware matte refinement.</li><li>Coefficients are solved on a 1024 px grid and evaluated per pixel at full resolution.</li></ul> |
| Levin, Lischinski & Weiss, *A Closed-Form Solution to Natural Image Matting* (PAMI 2008); Chuang et al., *A Bayesian Approach to Digital Matting* (CVPR 2001) | The two-colour model I = αF + (1−α)B: alpha comes from a pixel's position on the F–B colour line. We trust it only where F and B differ and the pixel lies on that line. |
| Germer et al., *Fast Multi-Level Foreground Estimation* (ICPR 2020); Forte, *Approximate Fast Foreground Colour Estimation* (ICIP 2021) | Blur-fusion foreground estimation: no fringe of the old background at the edges. |
| Reinhard et al., *Color Transfer between Images* (2001); Sunkavalli et al., *Multi-scale Image Harmonization* (SIGGRAPH 2010) | Harmonisation, at a global scale and damped: exposure, white balance and contrast toward the scene, with the person's grain matched to the scene's noise. |
| Rhemann et al., *A Perceptually Motivated Online Benchmark for Image Matting* (CVPR 2009) | The metrics: SAD, MSE and gradient error, measured over the whole image and in the edge band. |
| Li et al., *Privacy-Preserving Portrait Matting* (P3M-10k, ACM MM 2021); Ke et al., *MODNet* (PPM-100, AAAI 2022) | The ground-truth mattes for the benchmark. Used locally only, never committed. |
| Poly Haven backplates (CC0) | Clean plates: real, people-free photographs. Each comes with an HDRI of the same place. |

## How it works

### Cut-out (`native/engine/src/studio/cutout.cpp`)

1. **Working grid:** a working grid of at most 1024 px. The person mask is cleaned of its faint haze (a model's few-percent "maybe" over the whole background would otherwise be a veil of the old room) and of stray islands.
2. **Strokes:** they are painted onto the grid. Keep and Remove are hard constraints; Fix hair marks where colour decides.
3. **Snap:** a wide colour guided filter (radius 1.5% of the short side) moves the mask's edge onto the photo's own edge.
4. **Where colour decides:**
   - a narrow band along the whole silhouette;
   - a wider band in hair zones, scaled by Hair detail;
   - every Fix hair stroke.

   Nearby person colour F and background colour B come from normalised convolution; F is taken from hair itself in hair zones. Alpha = (I−B)·(F−B)/|F−B|², weighted by how far apart F and B are and how close the pixel is to the line between them. F, B and the band are kept for the full-resolution pass, so curls and the gaps between them stay sharp.
5. **Guided-filter coefficients:** colour coefficients are computed on the grid and evaluated per pixel. The two-colour alpha takes over in its band. Alpha is stretched (≥ 0.96 → solid, ≤ 0.02 → clear) so the person is never see-through and the old room never a veil. Then the strokes and the softness and shift settings are applied.
6. **Foreground colour:**
   - blur fusion, smooth on the grid with the exact correction per pixel;
   - in the colour band, the strand's colour is unmixed from the old background.

The output is RGBA with straight alpha. It takes about 235 ms on a desktop for a 1600 px photo.

The hair recovery that Backdrops uses was tested here too and left out: it added dark background shapes next to dark hair, such as window bars. The helpers that Backdrops and the Studio share moved to `src/matte/tools.*`, and Backdrops' output is bit-identical before and after the move.

### Compose (`native/engine/src/studio/compose.cpp`)

1. **The person's box** in the cut-out, and whether their feet are in the photo.
2. **The placement** maps the frame to the cut-out: anchor at the bottom centre of the box, size, mirror, tilt, angle. A strongly reduced person is drawn from a pre-reduced copy, so hair doesn't alias.
3. **A 1024 px grid of the scene in linear light** gives:
   - statistics around where the person stands;
   - the lens blur, kept sharper on the ground near the feet;
   - the light that wraps;
   - the person's silhouette as placed;
   - the shadows: contact, ambient and cast.
4. **Harmonisation:**
   - exposure gain between 0.7 and 1.22 (a white studio is bright because its walls are white, not because the light is stronger);
   - white balance gains between 0.82 and 1.22;
   - contrast between 0.95 and 1.1;
   - all scaled by "Match the light".
5. **Per pixel:** the background is softened and shadowed; the person is harmonised and wrapped in scene light, with a rim on the side facing a backlight. Compositing is in linear light, and the person's grain is matched to the scene's noise.

## Validation

| Test | Result |
|---|---|
| **Engine unit tests** (`tests/studio_tests.cpp`, 11 tests, AddressSanitizer) | <ul><li>Cut-out against a low-res, shifted, hazy mask: edge error 0.200 → 0.005, no veil far away, solid inside.</li><li>Edge colours: distance from the person's colour 47 → 14.</li><li>Keep, Remove and Fix hair do what they say; hair detail keeps strands (0.06 → 0.31).</li><li>Shrink, grow and softer edge work; the report flags a person cut at the bottom.</li><li>Placement is exact, untouched outside the box, and mirrors correctly.</li><li>Colour match warms in a warm scene and stays bounded in a dark one; shadows fall at the feet only on the ground; the background blur leaves the person sharp.</li><li>Bad input is refused.</li></ul> The whole engine suite passes. |
| **Ground-truth benchmark, 600 portraits** (P3M-500-NP + PPM-100). Coarse mask from Selfie Multiclass at 256 px, a stand-in for ML Kit and harder, as it is lower resolution. | <ul><li>**Total error (SAD) 118.8 → 20.3 (83% less).**</li><li>**Edge-band SAD 12.0 → 8.8 (27% less); better on 568 of 600 photos.**</li><li>P3M: 115.4 → 19.6. PPM-100: 135.4 → 23.7, better on 100 of 100.</li></ul> |
| By hair | <ul><li>Dark: edge 11.8 → 8.3.</li><li>Medium: 12.6 → 9.5.</li><li>Light, blond or grey: 13.6 → 10.8.</li><li>Smooth: 10.2 → 6.9.</li><li>Wavy: 14.9 → 11.5.</li><li>Curly or flyaway: 18.8 → 16.4 (the hardest case; Hair detail at 100% helps on 53 of 75, and Fix hair is the tool for the rest).</li></ul> |
| By clothing | <ul><li>Dark: 11.4 → 8.2.</li><li>Mid: 12.0 → 8.8.</li><li>Light or white: 13.0 → 9.6.</li></ul> |
| By background | <ul><li>Low contrast with the person: 12.2 → 9.9.</li><li>Medium: 11.6 → 8.7.</li><li>High contrast: 12.2 → 8.0.</li></ul> |
| **Your photos** (4 portraits, 9 scenes) | <ul><li>Clean hair edges: short hair, braids, an afro.</li><li>No fringe.</li><li>Placement, light match and blur look natural.</li></ul> Shared with you privately, not committed. |
| **Kotlin** (`SceneStudioTest`: strokes, settings, backgrounds, export size; plus every existing test) | 169 tests pass. Android Lint: no issues. Native library and codegen build. |
| **Jest** (`studio.test.ts`, 12 tests; `sceneSwap.test.tsx`, 3 screen tests) | All pass, with ESLint and TypeScript clean. Covered: <ul><li>the library, crops, the horizon rule and its depth following, the placement cases and the look;</li><li>one finger moving and two fingers resizing, and the overlay matching the engine;</li><li>the full flow: pick, stroke, undo, scene, background, save into the gallery and library;</li><li>nobody found;</li><li>the studio hub.</li></ul> |

### Scenes (CC0, Poly Haven)

| Scene | Group | Poly Haven asset / backplate | Photographer |
|---|---|---|---|
| White Studio | Studio | photo_studio_broadway_hall DSC_7392 | Sergej Majboroda |
| Rose Chair | Studio | photo_studio_broadway_hall DSC_7402 | Sergej Majboroda |
| Boho Corner | Studio | brown_photostudio_02 14 | Sergej Majboroda |
| Neon Lines | Studio | neon_photostudio 03 | Sergej Majboroda |
| Sunlit Loft | Studio | photo_studio_loft_hall DSC_8544 | Sergej Majboroda |
| Flower Hall | Indoors | brown_photostudio_06 07 | Sergej Majboroda |
| Café Wall | Indoors | comfy_cafe DSC_6795 | Sergej Majboroda |
| Grand Hall | Indoors | old_hall DSC_4087 | Sergej Majboroda |
| Autumn Park | Outdoors | autumn_park DSC_0158 | Sergej Majboroda |
| Summer Meadow | Outdoors | meadow_2 14 | Sergej Majboroda |
| Garden Bench | Outdoors | studio_garden 14 | Sergej Majboroda |
| Desert Sunset | Golden hour | kiara_8_sunset 20160825-DSC01825 | Greg Zaal |
| Sunrise Beach | Golden hour | umhlanga_sunrise DSC08473 | Greg Zaal |
| Coastal Dusk | Golden hour | the_sky_is_on_fire IMG_0844 | Greg Zaal, Rico Cilliers |
| Night Piazza | Night | vignaioli_night DSC07544 | Greg Zaal, Rico Cilliers |

`scripts/studio-plates.py` rebuilds them. It verifies every download against Poly Haven's MD5 and records it in `src/services/studio/plates.json`. The app names the source under each scene.

Plates were rejected when they showed a person (one Venice sunrise), a company's sign, or too little usable space.

## App wiring

| Layer | What |
|---|---|
| Engine | `include/photoplus/studio.hpp`, `src/studio/{cutout,compose}.cpp`, `src/matte/tools.*` (shared with Backdrops). C ABI: `pp_studio_cutout`, `pp_studio_compose`. Tool: `studio_demo`. |
| JNI | `nativeStudioCutout`, `nativeStudioCompose` |
| Kotlin (`com.photoplus.studio`) | <ul><li>`StudioModule`: sessions; cut-out previews; scene previews, including the scene without the person for smooth dragging; full-quality and transparent exports (Premium); the gallery.</li><li>`PersonParts`: MediaPipe Selfie Multiclass.</li><li>`SceneStudio`: strokes, settings, backgrounds, crops, export size, plain colours.</li><li>Storage: `studio` and `backgrounds` folders.</li></ul> |
| JS | <ul><li>`src/services/studio/`: scene library and decisions, gestures, service.</li><li>`src/screens/studio/`: Photoshoot Studio hub, Scene Swap screen, brush canvas, placement view, background picker, `useSceneSwap`.</li><li>Home: Replication Studio is "Available now". Settings keep your backgrounds.</li></ul> |
| Plans | <ul><li>Premium (`backgroundReplace`, titled "Scene Swap").</li><li>On the free plan the whole workflow can be tried as a demonstration; saving opens the Premium sheet. The native side refuses full-quality saves on the free plan whatever the app asks.</li></ul> |
| Size | <ul><li>Plates and thumbnails: 9.7 MB of assets.</li><li>Person-parts model: 16.4 MB, fetched at build time; `-Pphotoplus.studioModel=false` leaves it out.</li></ul> |

## Limits

- **No reconstruction:** nothing behind the person in their own photo is reconstructed or reused, by design. Only clean plates go behind them.
- **One person:** one person per scene; a tap picks who.
- **Directional light:** relighting is global (exposure, colour, contrast, wrap, rim). A person lit hard from the left placed in a scene lit from the right keeps their own shadows. Choose scenes whose light suits the photo, or use "Mirror".
- **Hardest edges:** very fine flyaway hair against a busy background of a similar colour. Hair detail and the Fix hair brush are there for those spots.
- **Not tested on a phone:** the phone itself could not be exercised here. The engine, models and plates were validated on the desktop with the same model files. The Android build, lint and unit tests pass.
