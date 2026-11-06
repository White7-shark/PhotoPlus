# Phase 5 — Portrait Intelligence, Camera Beauty and Retouching: Report

**Status:** Complete. No APK was built, as requested. Everything below was compiled and tested on the build host:
- **Engine (C++):** Release build, ASan + UBSan and TSan.
- **Android (Kotlin):** compile, unit tests and Android lint.
- **App:** TypeScript, ESLint, Prettier and Jest.

The tools were validated on **49 real faces across six skin-tone groups** plus Kodak test portraits (see "Validation"). The phone-side face detection (ML Kit) was not run on a device here, because none is attached. That pass is listed under "Open items".

Preview:
- [`design/portrait-gallery.png`](design/portrait-gallery.png): before and after on real photos, plus the validation numbers.
- The editor screens [`portrait-basic`](design/portrait-basic.png), [`portrait-advanced`](design/portrait-advanced.png) and [`portrait-background`](design/portrait-background.png).

Every photo in them is real engine output.

## What already existed (Phase 2/3) and what changed

| Area | Before Phase 5 | Now |
|---|---|---|
| Face detection | ML Kit face contours, but only for the **most prominent face** (others got an oval). Used to draw coarse masks | Contours for **every face** (up to 8): each other face is looked at again in an enlarged crop where it is the main face. Converted to a precise **131-point landmark layout** that the engine itself uses. Faces that still have no contours get approximate outlines from their eye and mouth landmarks, which the engine applies more gently |
| Retouching | Glow: one strength drives smoothing, spot healing and tone evening together (3 presets) | Presets unchanged and free. Premium adds **separate** Smooth, Blemishes and Even tone controls, plus Shine and Under-eyes |
| Facial adjustments | None | Premium: **Eyes** (whiter whites, clearer irises), **Teeth** (whitens only bright, unsaturated pixels inside an open mouth), **Lips** (richer colour, same hue) and **Slim** (a smooth inward shift of jaw and lower cheeks, at most a few %) |
| Lighting | A gentle subject lift | Premium: **Face light**, which opens shadows on the face toward its own median and lifts a genuinely backlit face (see "Skin-tone fairness"). **Studio light** adds a soft key light shaped to the face, from a side you choose |
| Masks and edges | Luma guided-filter matte | Premium **Edge refinement**: a **colour** guided-filter matte separates hair from a background of similar brightness but another hue (misjudged pixels 233 → 85 in the test), and **colour decontamination** removes the old background's fringe from edge pixels (green rim 22.8 → 6.2 levels) |
| Background | Depth-aware blur in 3 levels | Premium: any **blur amount**, **background light** (±1 stop, highlights roll off) and **background colour** (down to black and white: "colour pop"). The subject is never changed (0.00 levels in the test) |
| Camera beauty | Glow only in Spotlight mode | A **Glow chip** on the camera for every photo mode except RAW (Off, Natural, Smooth, Flawless; remembered). Each level adds gentle eye, under-eye, shine and face-light touches. Applied when the photo is taken, recorded in its recipe and **re-applied by Transform** |
| Controls | Glow presets; undo for the editor in general | Portrait tool with **Basic and Advanced** modes. **Every control has its own intensity**, the preview is live, each drag is one undo step, **Reset to Glow** returns to the preset and **Restore original** puts the face back as it was |

### Plan access (clear separation)

| Free | Premium (`advancedPortrait`: "Advanced portrait: precise face, light and background controls") |
|---|---|
| Glow presets (Off / Natural / Smooth / Flawless) in the editor, in Spotlight and as camera beauty; Spotlight blur levels; tap-to-choose subject | Advanced mode of the Portrait tool (Skin, Face and Light sliders) and Spotlight's background controls (blur amount, background light, background colour, edge refinement) |

Free users see the Advanced areas with an explanation and an **Unlock with Premium** button. (Plan enforcement is still globally off until billing is connected, as with every Premium feature so far.)

## Step by step

| Step | Requirement | Where / how |
|---|---|---|
| **5.1** | Face detection with landmarks, offline | **ML Kit face detection 16.1.7 (bundled, offline)** with contours and landmarks, in `PortraitProcessor.findLandmarks` / `withContours`. `FaceLandmarks.kt` converts the contours to the engine's 131-point layout (`PP_FACE_*` in `pp_engine.h`), resampling each contour to the exact count, approximating faces without contours, and mapping between orientations. ML Kit was kept rather than adding MediaPipe; see MODEL_EVALUATION ("Face landmarks") |
| **5.2** | Advanced retouching | `pp_portrait` skin controls (smooth / blemish / even) + `pp_face_adjust` shine and under-eye |
| | More precise facial adjustments | `pp_face_adjust`. Masks for the eyes, inner mouth, lips, under-eyes and cheeks are drawn from landmarks with an anti-aliased polygon fill, at a face-sized resolution (~200 cells across the face), and applied per pixel at full size |
| | Advanced portrait masks | Landmark-precise feature masks for every face (eyes, brows, lips, mouth interior, under-eyes, cheeks). Faces beyond the first now also get exact protection of eyes, brows and lips during smoothing. Plus the colour matte |
| | Better edge refinement | Colour guided-filter matte and edge decontamination (`edge`) |
| | Advanced background blur | Blur amount slider on the existing depth-aware lens blur. Background light and colour; decontaminated edges against the new background |
| | Selective portrait adjustments | Subject vs background (background light and colour never touch the subject), and skin-only, eyes-only, teeth-only, lips-only and under-eye-only controls |
| | More sophisticated lighting corrections | Face light (shadow fill + backlight lift) and Studio light (an ellipsoid face model lit from the chosen side, in linear light, hue kept) |
| **5.4** | Adjustable intensity | Every control is a 0–100 slider (Light from: Left … Right) |
| | Preview | The editor's live, debounced preview renders the same document (stage cache keyed on the portrait settings) |
| | Undo | Each drag is one history step; Basic presets and Reset are steps too |
| | Restoration of original settings | **Restore original** clears all retouching (Spotlight's background choices stay). **Reset to Glow** drops the fine-tuning back to the preset. Hold **Original** compares |

## Validation

### Real faces (`scripts/validate-portrait.py`)

The test set has **49 adult faces from FairFace** (CC BY 4.0), a dataset built to balance faces across demographic groups. Results are grouped by **measured skin tone** (the Individual Typology Angle, the dermatology scale from very light to dark), not by any label: very light 4, light 8, intermediate 3, tan 11, brown 13, dark 10. Seven photos have two faces.

On the desktop, MediaPipe's face landmarker and selfie segmenter stand in for ML Kit. The 478-point mesh is mapped onto the same 131-point layout, and the masks are drawn as the app draws them.

| Skin tone | Faces | Glow Natural ΔE | Glow Flawless ΔE | Advanced ΔE | Advanced lightness | Texture kept (Advanced) |
|---|---|---|---|---|---|---|
| Very light | 4 | 0.34 | 0.34 | 0.34 | +0.77 L* | 89% |
| Light | 8 | 0.65 | 1.35 | 0.96 | +2.00 L* | 76% |
| Intermediate | 3 | 1.01 | 1.64 | 0.73 | +1.34 L* | 75% |
| Tan | 11 | 0.63 | 1.17 | 1.00 | +1.92 L* | 84% |
| Brown | 13 | 0.64 | 1.56 | 1.11 | +1.07 L* | 84% |
| Dark | 10 | 0.72 | 1.26 | 0.86 | +0.60 L* | 91% |

Values are medians on the cheeks:
- **ΔE:** colour change of the skin's own tone (a\*, b\*). Under 2 is barely perceptible.
- **Lightness:** change in L\*.
- **Texture kept:** the share of fine texture that remains.

"Advanced" is smooth 0.6, blemishes 1, even 0.6, eyes 0.6, teeth 0.6, lips 0.4, under-eyes 0.7, shine 0.7 and face light 0.6.

Other results:
- **Outside the face:** pixels away from the subject changed by at most 0.29 levels.
- **Resolution:** the same edit at full and half size changes the face the same way. The difference is 0.46–0.61 levels on a 4–7 level change, on photos up to 1086×1448.

### Skin-tone fairness: a problem found and fixed

The first backlight rule compared the face with the brightest part of the photo. On real faces it lifted **11 of 49 faces, every one of them brown or dark-skinned and none actually backlit**. A white shirt was enough to make the photo look "bright".

The rule now judges the **light, not the skin**. A lift needs two things together:
- a bright background **around the head** (beside and above it, never the clothes below);
- a face **without frontal highlights**. Any face lit from the front shows highlights on the forehead, nose and cheekbones, whatever its tone.

After the fix, one face is lifted (+0.5 stop), and it is genuinely backlit: sun and sky behind, face in shade. A regression test covers deep skin, lit from the front, in front of a white wall.

### Engine tests (`tests/face_tests.cpp`, 17 new)

These use a synthetic head-and-shoulders photo with landmarks, eye whites and irises, teeth in an open mouth, dark circles, shine, a side shadow or backlight, and pore texture, in **six skin tones**:

| Behaviour | Result |
|---|---|
| Neutral settings | The photo comes back bit-identical |
| Eyes | Whites +0.016 luma and half as red; iris detail up; cheeks and background untouched |
| Teeth | Less yellow (Cb −0.100 → −0.036) for deep and light skin; lips and skin untouched; a closed mouth is left alone |
| Lips | Saturation +18%, hue shift 0.01° |
| Under-eyes | The gap to the cheek shrinks 55–68% on every tone, with texture kept |
| Shine | The excess highlight is tamed and ordinary skin unchanged, on deep, medium and light skin |
| Face light | The shadowed cheek is lifted and nothing anywhere gets darker. Backlit faces of every tone get one stop with hue kept within 0.5°. Deep skin before a white wall, or in an ordinary photo, gets no lift |
| Studio light | Follows the chosen side |
| Slim | Jaw narrower by 4%, forehead unchanged, 92% of texture kept. The warp uses bicubic resampling; bilinear kept 87%, and the test catches that |
| All tools on six tones | Cheek hue shift ≤ 0.1° |
| Preview and full size | Faces at 1× and 4× change alike (0.59 levels apart on a 2.2-level change) |
| Bad input | Non-finite landmarks, too many faces and a non-finite angle are rejected; tiny faces are ignored; cancellation works |
| Background controls | Background light, darkening and black-and-white leave the subject at 0.00 levels |
| Edges | Edge refinement removes the old background's rim (22.8 → 6.2 levels); the colour matte separates hair from an equally bright background (233 → 85 misjudged pixels) |
| Skin controls | Separate skin controls are independent (smoothing softens, evening keeps texture), and the presets are unchanged |

### Speed (host, 4-core Xeon, 13.5 MP photo, face ~1500 px wide)

| Operation | Time |
|---|---|
| Glow Natural | 0.31 s |
| Every Advanced control | 0.81 s |
| Face adjustments alone | 0.62 s |
| Spotlight | 0.74 s |
| Spotlight with black-and-white background and edge refinement | 0.97 s |

Editor previews work at screen size (≤ 1280 px), where these costs are about 8× smaller.

## Tests

| Suite | Result |
|---|---|
| Engine (C++) | **119 pass**, including 17 new face and portrait tests. Clean under ASan + UBSan and TSan |
| Kotlin unit tests | **122 pass**. New tests cover landmark layout and resampling, approximate outlines, orientation mapping and packing, portrait parameters, the edit document's portrait block (parsing, clamping, cache key), and camera beauty options (processing turns on, never for RAW, bad values ignored) |
| Android lint | 0 findings |
| Jest | **212 pass**. New tests cover the Portrait tool (presets; Advanced sliders into the live preview; undo; Reset to Glow; Restore original), Spotlight background controls, the camera Glow chip (cycles, remembered, travels with every photo, not in Spotlight), recipes with beauty, and older saved edits |
| TypeScript, ESLint, Prettier | Clean |

## Note on backwards compatibility

The Android bridge used to send four portrait values. With the extended engine struct, those four values alone would have zeroed the new skin controls and silently turned Glow off. The bridge now fills in the engine's defaults first and accepts either 4 or 10 values, so every existing caller keeps working.

## Open items

1. **Device pass:**
   - ML Kit contours for groups (the crop re-detection) and timings on mid-range phones.
   - The camera Glow chip end to end.
2. **Live beauty preview in the camera viewfinder.** Glow is applied when the photo is taken (and the result screen shows it), not live in the viewfinder. That needs a GPU camera effect, which can only be built and checked on a device.
3. Colour-pop (black-and-white background) exposes any subject-mask mistake, as in the Kodak 04 preview. On phones the mask comes from ML Kit subject segmentation. If testers find misses, a matting model (MODNet, Apache-2.0, listed in MODEL_EVALUATION) is the next step.

## How the preview was made

- **Masks and landmarks:** `scripts/validate-portrait.py` produced them for Kodak 04 and 15 (MediaPipe on the desktop, mapped to the app's layout).
- **Engine output:** `portrait_demo` produced the "Advanced" and Spotlight images with the values shown on the screens.
- **Screens:** `scripts/render-portrait-preview.mjs` drew them and the gallery.

The validation photos (FairFace) and the owner's own photo were used only locally and are not committed.
