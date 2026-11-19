# Phase 7 — Background Removal and Replacement (Scene Swap)

Scene Swap is the photoshoot studio's first tool. It is Premium. You choose a photo of a person. The person is cut out on the phone, and you correct the cut-out where needed. Then you choose a scene, move and size yourself in it, and save the result. The scene's light, colour and shadows are matched automatically. You can also save the cut-out on its own with a transparent background.

It lives in the **Photoshoot Studio** (Home → Photoshoot Studio → Scene Swap), not in the camera. Everything runs on the device; no photo leaves it.

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
| JS | <ul><li>`src/services/studio/`: scene library and decisions, gestures, service.</li><li>`src/screens/studio/`: Photoshoot Studio hub, Scene Swap screen, brush canvas, placement view, background picker, `useSceneSwap`.</li><li>Home: Photoshoot Studio is "Available now". Settings keep your backgrounds.</li></ul> |
| Plans | <ul><li>Premium (`backgroundReplace`, titled "Scene Swap").</li><li>On the free plan the whole workflow can be tried as a demonstration; saving opens the Premium sheet. The native side refuses full-quality saves on the free plan whatever the app asks.</li></ul> |
| Size | <ul><li>Plates and thumbnails: 9.7 MB of assets.</li><li>Person-parts model: 16.4 MB, fetched at build time; `-Pphotoplus.studioModel=false` leaves it out.</li></ul> |

## Update: true-to-scene size, Spotlight, Glide in a new scene, Glide length

### People the right size for the scene

The first version sized a standing person with a fixed rule (the horizon crosses them at 90% of their height). That assumes the plate was shot from eye level, about 1.5 m. Many backplates are shot from much lower. So in the White Studio and Autumn Park tests the person came out about half the size the scene gives them, which reads as fake at once.

- **Each plate is measured.** We recorded the horizon, the height of the camera, and the open floor: the farthest point feet can go, in front of furniture.
  - The measurements come from things of known size in each photo: a 45 cm side table, dining chairs, doors, railings, a studio cube, and which top faces are visible.
  - An object Z metres tall standing at y, with its top at y_t, gives Z / camera = (y − y_t) / (y − horizon).
  - Camera heights range from 0.8 m (Sunlit Loft, Night Piazza) to 1.65 m (Neon Lines).
  - Several horizons had been set at the floor line rather than at eye level; those are corrected.
- **The person's real height is measured.** The engine now reports the box of the face (face skin in the parts map, inside the cut-out). Hairline to chin is about 0.19 m, so the face gives the person's height.
  - For a whole person, the measurement is weighed with an adult's 1.7 m, because fringes and hats move the hairline.
  - For a portrait, it gives the size of the box.
- **The size rule:** height in the frame = (feet − horizon) × real height ÷ camera height. This holds in any crop of the plate.
- **Auto framing** searches crops of the plate (down to 66% of it, anywhere up and down) and spots on the open floor. It aims for a standing person filling about 72% of a portrait frame, feet near the bottom, head inside.
  - Near a low camera, the feet may go just out of frame: a three-quarter shot, as a photographer there would take.
  - A portrait gets its eyes brought near the horizon (where a camera at eye level puts them).
  - The background blur follows how close the person is.
- **Size slider:** shows 100% at the true size. It notes when the person is larger or smaller than the scene gives, and Auto size brings them back. Dragging keeps the chosen proportion as they move back or forward.
- **Result:** in the park and white-room tests the person is now 0.69–0.72 of the frame, against 0.27–0.34 before. In every measured scene the auto size equals the true size (unit-tested).

### Spotlight in the Photoshoot Studio

A Spotlight slider in Adjust:
- a pool of light on the person, with the scene falling off outside it (corners about 30% darker at full strength);
- the scene softened further, as a wider aperture would;
- the person lifted.

It is engine-tested and previewed on three scenes.

### Glide in a new scene (Premium)

In the Glide preview, **Put me in a new scene** works like this:
1. Pick a scene or colour, and optionally adjust the Size.
2. The app finds where the person usually is in the clip: the median box over a dozen frames, and the edges that cut them off.
3. It frames them with the same auto framing as photos, in the clip's own shape.
4. Every frame is then processed:
   - the person's mask: the selfie segmenter and the portrait matte fused (below), a three-frame vote, room memory, steadied;
   - a studio cut-out at 960 px;
   - the steadying warp applied to the cut-out, so the person is steady and the scene never moves;
   - a studio compose at a **fixed box** (one size and place for the whole clip, even when an arm goes up or the person leaves the frame), with the **colour match measured on the first frame and kept** (no flicker). Grain changes every frame, as a camera's does.

The engine tests cover the fixed box (the body is unchanged when an arm goes up, against a large change with the frame's own box), an empty frame, and the kept colour match.

### The head no longer cuts off or flickers in Glide

**Report:** in a Glide clip, part of the head kept disappearing, coming back after a movement, then disappearing again.

**Investigation:** I built a desktop replica of the phone's per-frame chain (`tools/scene_video_demo.cpp`: vote, room memory, steadying, cut-out, compose) and ran it on the reported clip, a hand-held selfie under a ceiling beam. There were three separate causes.

1. **The scene video clipped the person to their usual box** (the main cause of "cut off, back after moving"). The fixed box that keeps the person one size also limited what was drawn. Whenever the head rose above its usual place, everything above the box line was cut off flat.
   - **Fix:** the fixed box now sets only size and place; the whole cut-out is drawn.
   - **Test:** a raised arm above the box is drawn.
2. **The person model hesitates at the top of the head.** Dark hair against the dark underside of the beam is hard for it:
   - a piece of ceiling becomes a grey cap on the head for a few frames;
   - a piece of hair goes missing for up to 25 frames;
   - during a camera jolt, a column up the ceiling is taken for the head.

   The three-frame vote outvotes only one-frame mistakes, and room memory only takes back pieces of room it has already seen.
   - **First fix, withdrawn: "shape memory".** It remembered the person's outline and moved it with them, accepting a change only as fast as the frame supported it. On the synthetic tests it looked right, but on your clip it kept a dark tuft of the beam above the head, which made the head look taller and stretched. (I measured the face's height-to-width ratio in the before and after frames: it was the same, 1.00 to 1.02, so nothing was actually stretched; the extra dark shape on top was what read as stretching.) You saw that the "after" clip was worse than the "before", and it was removed completely.
   - **Fix: a second, stronger model for hair.** The selfie segmenter is a small, fast model that sees the frame at 256 px and has never been good at hair. Glide now also runs a **portrait matting model** (MODNet, Apache-2.0, 26 MB, fetched and SHA-256-checked at build time; LiteRT build). It is made for exactly this: the outline of a person's head and hair, also against dark backgrounds.
     - MODNet alone is not enough either: on the blurred frames of a camera jolt it lost the face altogether. The two are fused (`video::fuseMasks`, `pp_video_fuse_masks`):
       - the segmenter's solid core is always kept, so the face and body never go missing;
       - MODNet decides the outline, but only near the segmenter's person;
       - and only below each column's own head line, so a piece of ceiling next to the head cannot sit on top of it.
     - The frame is letterboxed for MODNet, not stretched (stretching collapsed the mask on some frames).
     - When the model can't be loaded (a low-memory phone: it needs about 205 MB while running), Glide uses the segmenter alone, as before.
   - **Engine test:** a lost face is kept from the segmenter (255), ceiling next to the head stays out (0), hair only MODNet saw is kept (255), far room stays out (0), the body is solid (255).
3. **The cut-out's edge snapping flattened hair against a dark beam.** Where the person and the room beside them are the same colour, colour can't separate them, and the fit cut along the beam's straight edge.
   - **Fix:** the cut-out now checks how much of the mask colour explains nearby, and keeps the model's edge where it can't.
   - **Check:** the 600-photo ground-truth benchmark is unchanged or slightly better (SAD 20.3 → 20.2, edge SAD 8.80 → 8.71).

**Result on the reported clip** (the head's top edge, frame to frame, across 105 frames):

| | Big jumps (over 2% of the frame) | Largest jump | Mean movement |
|---|---|---|---|
| Before (segmenter alone) | 13 | 6.2% | 2.9‰ |
| After (segmenter + MODNet fused) | 7 | 2.5% | 2.3‰ |

The lost face (frames 13 and 14), the grey cap of ceiling (16), the flat-topped hair (20) and the bites out of the hair (90, 100) are gone. Two frames (28, 29) still show a thin sliver at the top of the hair; that is the honest remaining limit. Glide with Spotlight or Glow uses the same mask chain, so it benefits too.

### Fast and slow motion

Glide now has a **Speed** choice: Normal, Fast 2× and Slow motion.
- **Fast 2×** plays like a video fast-forwarded in a player: each pair of frames is merged into one, so movement keeps a light, natural motion blur instead of jumping.
- **Slow motion** is made the way films slow a moment down: a new frame is made between every pair of real frames from the motion between them (block motion found both ways, to half a pixel). Where the two frames don't agree, such as an eye opening or a hand coming into view, it blends instead, so nothing tears.
  - On a test with a textured object moving 12 px, the made frame's error against the true middle frame is 6.6, against 18.0 for a plain blend.
  - It costs about 128 ms per made frame at 540 × 960 on a computer.
- Fast and slow clips are silent, since sped-up or slowed sound is not wanted in a highlight. The camera says "no sound" when one is chosen.
- A clip moved into a new scene keeps its speed.

### Glide length by plan

- **Standard (free):** 7 s, and always with the Photo Plus watermark. The watermark is now drawn natively on every free clip, whatever the request says.
- **Premium:** up to 20 s (first set at 30 s, then changed to 20 s at your request).
- **Where it is enforced:** the recorder reads the plan natively, and processing also stops at the plan's limit (an imported longer clip is cut). The camera's countdown follows the plan.
- **Memory:** a long clip no longer holds a mask for every frame (about 140 MB at 600 frames). Masks are made as the second pass reaches them, so at most three frames and three masks are held whatever the length.

## Update: studio paper, colour picker, outfit colours, couples

From two screenshots of a couple's studio shoot (a seamless mustard paper, soft even light, coordinated outfits, close poses), these were the differences from what the studio made:

| In the reference | In the studio before | Now |
|---|---|---|
| Paper lit by a background light: brighter behind the people, falling off to the edges and corners | A flat colour, or a dark ring at the edges | **Studio light** on plain colours (`pp_studio_backdrop`): a pool of light behind the person, a fall-off to the corners, the top of the wall and the floor a touch darker, a faint paper texture. Worked in linear light, so the hue is the same everywhere, and dithered, so dark colours never band (largest step 0.41 of a level in the test). |
| The paper's colour warms the people (whites take a mustard tint) | No colour reached the person from a plain colour (colour match is off for plains, rightly: a red paper is not a red light) | **Colour bounce** in compose: the scene's tint around the person, as a per-channel gain with a luminance of one, so their colour follows the paper and their brightness does not. A grey paper tints nothing. Test: a grey person on mustard goes from 150/150/150 to 159/148/142, brightness within 4%. |
| Any colour the photographer chose | 8 fixed colours | A **colour picker** (colour, richness, brightness on tracks that show what they do, or a colour code), and 11 studio paper colours added (mustard, terracotta, sage, dusty blue, peach, lilac, olive, burgundy, teal, navy, sand). |
| Outfits chosen to work with the paper | No help choosing | **Suits your outfit:** the cut-out reports the outfit's two main colours (clothes in the parts map, grouped in a colour histogram). Papers are scored the way photographers choose them: stand apart from the outfit in lightness; opposite on the colour wheel, or a quieter shade of the same family; a bold warm paper for plain outfits (white, black, denim); never the outfit's own colour. White and denim → mustard first, as in the reference. |
| A couple, faces at different heights | The size came from one box around all face skin, as tall as the faces' height difference, so a couple came out too small | The **largest single face** sets the size (faces are separated into connected pieces), and the number of faces is reported. Test: faces of 45 and 38 px, 30 px apart in height → measured 45 px, not 68. |

Previews of the studio paper with real people are private and were sent separately.

### Old light on the edges (a golden fringe on hair)

**Report:** in the studio-light previews, the woman's hair had a yellow band on the right side, on every colour.

**Cause:** her photo was taken against a low sun. Its golden light shone through and onto the right side of her hair, and the cut-out kept that light, so it went with her into every scene.

**Fix:** the cut-out takes the old background's light off the edges. In a band along the outline, each pixel is compared with its own kind of surface further inside, using the parts map: hair with hair, skin with skin, clothes with clothes. Where the edge is coloured like the old background beyond that surface's own colour, it moves toward the own colour. Where it is far brighter than the surface's own colour and tinted like a bright old background, most of that added light is taken out.
- A white or grey background has no colour to take, so nothing changes. Without a parts map, nothing changes either.
- First attempt, corrected: one "own colour" for every surface turned her sunlit arm green, because it was compared with her green dress. Comparing each surface with its own kind fixed it.
- Engine tests: a gold rim on dark hair against a sunset goes from 205/160/45 to 60/40/16. Skin beside a warm wall is unchanged. A bright edge beside a white wall is unchanged. A sunlit arm beside a green dress stays skin-coloured (223/159/99).
- On the private test photos, ordinary backgrounds change in 0.0–0.9% of pixels. The two photos with a strong sun change by 4–6%.

### Glide: a shadow flickering on the shoulder

**Report:** a shadow flickered on the person's shoulder in Glide.

**Cause:** a dark shirt against a dark room. During the camera jolt, the portrait matte took a block of dark room above the shoulder for a few frames, then left a hole in the shoulder in the next few. The fusion let the matte reach up to 7% of the frame beyond the person model everywhere, a reach meant for hair around the head. The head-line guard (against ceiling on the head) also cut the person model's shoulder wherever the matte had a hole.

**Fix:** both now apply to the head only. The head is told apart from the shoulders by how far below the person's highest point each column starts, measured on the matte near the person, so a strip of ceiling is never mistaken for the top. Along the shoulders and arms the matte may only refine the person model's edge (1.5% of the frame), and the person model's shoulder stands.
- Engine test: dark room just above a shoulder is kept out (23 of 255) and a hole in the other shoulder is filled (255). The earlier head cases (lost face, ceiling, hair, far room) are unchanged.
- On your clip: average frame-to-frame flicker on the right shoulder 6.3 → 3.8. The head is unchanged (5.95 → 5.97), and the frames that showed the head problems look the same.

## Update: an arm torn at the edge, and hair (Premium)

### The arm beside the hand looked ripped off

**Report:** in the green-dress photo, the part by her hand looked torn off.

**Cause:** her brown arm against a brown background. Where the person and the room beside them are the same colour, colour can't place the edge. The cut-out still let colour decide, so the arm's outer edge came out ragged and partly see-through. This was older than the recent changes: the arm's alpha was identical before them. A second, smaller problem *was* new: the edge-light step turned her gold brooch greenish. The parts map calls the brooch part of the dress, so it was compared with the green dress.

**Fix:**
- The person model's own outline is kept. Where the nearby person and background colours are close, the final edge follows that outline, made crisp. Where they differ clearly, colour still decides. Hair always keeps colour's say, because strands need it.
- The edge-light step may only take light out, never change what a thing is. An edge whose hue is far from its surface's own (a gold brooch said to be part of a green dress) is left alone, and jewellery is always left alone.
- **600-photo ground-truth benchmark:** average error 20.23 → 19.94; edge error 8.71 → 8.67. Edge squared error rose 2% (65.4 → 66.7): a few outlines are a little softer where the model was a little off.

### Hair: colours and styles (Premium)

In the Scene Swap's **Hair** panel there are three tabs: Colour, Women and Men.

- **18 colours:** natural blacks and browns, blondes, platinum, ash grey, copper, auburn, burgundy, rose gold, pink, purple, blue, teal and ginger.
  - Each hair pixel keeps its light relative to the hair's average, so strands, shine and shadow stay.
  - The average sits a little below the swatch colour (a swatch shows the light side of hair). Shadows between strands are kept deeper in light colours, and brightly lit hair stays brighter softly instead of going flat.
- **Styles: 23 for women; for men, dreadlocks, rasta, afro and bald** (the men's list was cut to these four at your request). Women: long straight, silk press, waves, beach waves, curls, bob, bob with bangs, long bob, pixie, curtain bangs, shag, side swept, top knot, sleek ponytail, curly updo, afro, afro puff, twist-out, Bantu knots, box braids, cornrow braids, long locs and finger waves.
- **How a style is made** (`native/engine/src/studio/hair.cpp`, all on the phone). The first version filled flat shapes with noise, which looked cartoonish (your feedback), so it was rewritten:
  1. **The face is found by its colour.** The parts map called part of her cheek and temple hair (in shadow), so removing the old hair chipped the face, and the style sat off-centre. Now:
     - The skin's colour is learnt from the face itself, and the face is grown outward from the parts map's face over smooth, skin-coloured pixels, inside a face-shaped oval. Hair is textured even when the sun makes it skin-coloured, so it isn't mistaken for skin.
     - Everything the skin encloses (eyes, brows, nostrils, lips) counts as face, after thin feature lines are sealed. A pocket of hair between the jaw and a shoulder doesn't count.
     - The parts map's face is also trusted below the top of the forehead, which matters for a beard or a hand at the chin.
     - The head is centred on the skin found at cheek level.
  2. **The person's own hair is taken out:** everything above the chin that isn't face, skin, clothes or jewellery. Hair in front of the body is filled in smoothly from the body around it.
  3. **The style is drawn on its own grid** (at most 560 px per face height):
     - **Strands:** up to 60,000 fine strands traced along the style's flow from the parting (combed back for sleek styles, around the bun for buns), with coherent waves or per-strand curls. They are lit as hair fibres are: a fine white highlight and a broader coloured one along the strands, deeper strands darker, over a dark under-layer so no background shows through.
     - **Coils:** for afros, puffs and twist-outs, tight curls in clumps that share their light, fuzzy at a lumpy outline.
     - **Ropes:** for locs, braids, twists, cornrows and knots, rounded ropes with a fibrous or braided surface, stray fibres on locs, and uneven thickness for rasta.
     - **Dreadlocks** (high-top dreads, a look common in men's dreadlock guides): the sides and back faded close to the skin, short, even locs rising from the top and falling forward and out like a fountain. **Rasta** stays different: long, thick, uneven freeform locs past the shoulders.
     - **Bald:** sized from the head's own outline (less the hair's thickness), not from the forehead, so the temples and the sides above the ears are kept (the first version was as narrow as the forehead and cut them off). It is round over the top with sides straight down to the ears. Otherwise: the person's own skin carried up from the forehead and temples (a smooth fill, so it matches where it meets them). The scalp is as wide as the forehead and round over the top, lit relative to the forehead, with a soft sheen and skin grain, blending into the forehead across a band.
  4. **Finishing:** the new hair casts a soft shadow on the face, and is softened and grained like the photo. Long hair falls behind the neck.
- **Long hair over the shoulders** (tested on two long-haired women, with the hair over both shoulders, and over one shoulder with an arm raised and the head tilted). On the first try a short style left gaps where the long hair had been: horizontal streaks beside the neck, smears of skin over the chest, the shoulders missing, and a cheek cut where hair had hidden it. Now:
  - **The body under the hair is rebuilt row by row.** Where the body's edge meets the background (or the photo's border) it is seen. Where it meets hair it was hidden, so it is drawn as a shoulder's curve from the neck to the next edge seen lower down (or mirrored from the other side), sloping down from the neck, never narrower than the body seen and never past the person's outline. Hair over the body becomes body; hair past it goes.
  - **What the hair covered is filled with what belongs there.** The neck stays skin. Out past the neck, the shoulders take what the body nearby shows: a sleeve, or bare skin for a sleeveless top. Each is filled smoothly from its own colour, never a blend of skin and cloth, with a faint weave. Scraps of cloth glimpsed between strands are absorbed into the fill instead of floating as patches. (Copying texture from the other side or from below drew streaks and seams, so it was dropped.)
  - **A cheek hidden by hair comes back.** The face's outline is fitted as an oval, tilted with the head, to the edges that border the background or the neck, and it must contain all of the visible face. Inside it, below the hairline, the hidden cheek is filled from the mirror side of the face, or from the skin's colour.
  - Faint wisps beside the head that the parts map missed are removed with the hair.
  - Limits: the shoulders under the hair are an estimate, smooth rather than textured (a patterned blouse comes back plain), and a long style still covers them, as hair does.
- **Memory:** all analysis runs on a working grid with a long edge of at most 1280 px, applied smoothly at full size, so a full-resolution save stays light.
- **Colour:** each hair pixel keeps its light relative to the hair's average. The shadows keep some of the natural colour and are deeper and richer; the highlights stay soft and a little lighter. Hair that is part of the face area (at the hairline) keeps its own colour, which reads as natural roots.
- **Premium:** on the free plan, choosing opens Premium, and the device refuses hair requests from the free plan too. A style needs a face in the photo, and it says so if there isn't one.
- **Honest limits:** the styles are drawn by the engine, strand by strand, not generated from photographs by AI. Long, straight and wavy hair, afros and locs hold up well at photo size. Cornrows and buns are the least convincing. Truly photographic swaps of any style need a large AI image model, which doesn't run on a phone. Ears hidden under the old hair don't reappear under a shorter style.
- **Tests:**
  - Engine: colour keeps the strands and leaves the face untouched. Every style draws, keeps the face and body, and removes the old hair's far sides. Long hair over the shoulders: under a pixie the clothes come back where the hair lay over the body, and the hair past the body's side is gone. Unknown styles, a missing face and a missing parts map are refused.
  - Kotlin: the hair request is checked.
  - Jest: the catalogue's ids and lists match the engine's table exactly; requests carry only what was chosen; the picker opens Premium on the free plan.

## Limits

- **No reconstruction:** nothing behind the person in their own photo is reconstructed or reused, by design. Only clean plates go behind them.
- **People:** everyone found is kept (a couple stays together); a tap picks one person. Each person is cut out from their own photo, so two people from two different photos can't yet be placed together.
- **Directional light:** relighting is global (exposure, colour, contrast, wrap, rim). A person lit hard from the left placed in a scene lit from the right keeps their own shadows. Choose scenes whose light suits the photo, or use "Mirror".
- **Hardest edges:** very fine flyaway hair against a busy background of a similar colour. Hair detail and the Fix hair brush are there for those spots.
- **Glide in a new scene** cuts out and composes every frame, so it takes several times longer than a plain Glide. Progress and Stop are shown. The cut-out runs at 960 px: the person is resized into the scene anyway.
- **Measured plates only:** people's own backgrounds have no measurements, so a hand-held camera height (1.5 m) and a middle horizon are assumed. The Size slider corrects the rest.
- **Not tested on a phone:** the phone itself could not be exercised here. The engine, models and plates were validated on the desktop with the same model files. The Android build, lint and unit tests pass.
