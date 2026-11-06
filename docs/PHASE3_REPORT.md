# Phase 3 — Photo Editor, Watermark and Branding: Report

**Status:** Complete. No APK was built, as requested. Everything below was compiled, run or tested without producing an installable package. The editor has **not** yet run on a physical phone (none is attached to this environment). It needs a device pass before release (see "Open items").

Design preview: [`design/editor-gallery.png`](design/editor-gallery.png) and the four screens `design/editor-{adjust,frames,props,watermark}.png`. Every photo in them is real engine output; see "How the preview was made".

## Deliverables

| Step | Requirement | Where |
|---|---|---|
| **3.1** | Image preview | `screens/editor/EditorScreen.tsx`: the native renderer draws a screen-sized preview (debounced 120 ms; slow stages are cached) |
| | Editing toolbar | Ten tools: Adjust, Crop, Looks, Spotlight, Retouch, Focus, Transform, Props, Frames, Watermark (`panels.tsx`) |
| | Adjustment controls | `components/ValueSlider.tsx`: drag or tap, accessible as an adjustable control |
| | Undo and redo | `services/edit/history.ts`: whole-document steps (60 kept); one drag counts as one step |
| | Before / after | Hold **Original** to see the untouched photo |
| | Save and export | **Save** keeps an edited copy in the app; **Share** and **Save to gallery** go through the export pipeline (step 3.5) |
| | Original preserved | Edits are a document replayed on the untouched base. The base is never written to. Saved edits record `editBase` + the document, so they can be reopened and changed |
| **3.2 Free** | Crop, rotate | Free crop or fixed aspect (1:1, 4:5, 3:4, 16:9 …), quarter turns, flips and fine straighten (engine crops to the largest same-shape rectangle, so no empty corners) |
| | Brightness, contrast, exposure, saturation | Engine tone ops; warmth is also available |
| | Basic filters | 12 Looks (8 free, 4 premium), each with a strength slider |
| | Basic sharpening | Sharpen slider |
| | Basic retouching | Glow: Natural, Smooth or Flawless (smooths skin, clears blemishes, keeps each person's own colour) |
| | Basic focus effects | Radial or tilt-shift focus: tap what stays sharp |
| | Basic portrait effects | Spotlight in the editor: tap what should shine, background blur levels |
| **Requested** | Transform in the editor | The Transform tool (premium, 2 a day free) runs first in the edit, so every other edit applies on top of it |
| | Spotlight in the editor | As above, with the same tap-to-choose markers as the camera |
| | Props (hats, glasses, outfits) | 15 props: 7 hats, 4 glasses, 4 outfit pieces; free and premium are marked. See "Props" |
| | Frames | 11 photographic frames (5 free, 6 premium), in star, heart, hexagon, diamond, arch, circle and other shapes. See "Frames" |
| **3.3** | Predefined watermark styles | Signature (default), Classic, Pill, Minimal, Badge |
| | Fonts and layouts | Modern, Elegant, Signature, Bold, Type; side by side or stacked |
| | Size and opacity within limits | Free: 3.5–8 % of the photo's short side, opacity 60–100 % |
| | Position, restore default | Nine positions; **Restore default** returns to bottom right at the default size and opacity |
| | Mark rendered into the image | Drawn into the photo's pixels by Android Canvas and composited by the engine (`pp_overlay`), never a screen overlay |
| **Requested** | Camera photos marked on the free plan | Captures (and Clarity, Spotlight, Transform and Look results) carry the signature mark on the free plan. See "Watermark on camera photos" |
| **3.4 Premium** | Custom text, logo import | Up to 40 characters; a logo from the photo picker |
| | Fonts and colours | Every font and eight colours |
| | Opacity, size, alignment, position | Wider limits (2–20 %, opacity 15–100 %) and text alignment |
| | Reusable presets | Named branding presets kept in settings |
| | No watermark | A switch to turn the mark off |
| **3.5** | Native policy by entitlement | `ExportPolicy` (Kotlin) decides from the native entitlement, not from anything the app's screens send. See "Export policy" |

## Export policy (step 3.5)

The watermark rule is enforced in Kotlin, at the last step before a file is written:

1. `Entitlements.plan()` is read natively. The app is on the free plan until Play Billing is connected; debug builds can switch plans for testing.
2. `ExportPolicy.resolve(spec, plan)`. On the free plan, the Photo Plus brand is forced: custom text, logos, colours and "no watermark" are ignored, and size and opacity are held within the free limits. A spec that is missing or broken falls back to the default mark.
3. `WatermarkRenderer.apply` draws the mark. The engine reports how many of its pixels are visible (alpha ≥ 25 %).
4. `ExportPolicy.verify` fails the export with `WATERMARK_MISSING` if a free export's mark covers less than 15 % of its box (for example, if drawing failed). Nothing is written, and the app shows a plain message ("This photo couldn't be prepared for sharing").
5. Only files written to the app's exports folder can be shared (`StorageModule.shareImages` refuses anything else). So a free user can't share an unmarked copy by another route.

### Clean companions

Free-plan photos are saved with the mark baked in, next to an unmarked `<name>_clean.jpg`, which the app never shows or shares. Edits, Transform, Spotlight and Looks start from the clean copy. The mark is therefore never stamped twice, and an upgrade to Premium can export the same photo without it.

## Watermark on camera photos

The signature mark follows the example image:

- the Photo Plus emblem;
- "Created with" in a handwritten script (Great Vibes);
- a bold "PhotoPlus" wordmark;
- a gold swoosh;
- placed bottom right, with a 3.5 % margin.

Its ink adapts to what is behind it. The renderer measures the brightness of the area under the mark:

- **Darker or busy backgrounds:** white ink with a soft shadow.
- **Bright backgrounds** (above 62 %): deep charcoal ink with a light shadow.

At the default size, the mark's height is 5.75 % of the photo's short side. The preview gallery shows it on a dark, busy photo, on a mid-tone street and on a bright, high-key photo.

## Props

The art for each prop is drawn as SVG with lighting and material textures (felt, knit, straw, satin, tortoiseshell, metal, pearl) by `scripts/make-props.mjs`, then shipped as PNG. To make a prop look worn rather than pasted on:

- **Fit to the face.** ML Kit face landmarks (eyes, forehead, chin, face width) place each prop along the face's own axes, so a tilted head gets a tilted hat. Hats meet the forehead at a measured anchor (the brim's front edge sits just above the eyebrows). Glasses scale to the eye distance and sit on the eye line. Neckwear hangs below the chin.
- **Blend into the scene** (`pp_place_prop`):
  - **Light and colour:** the scene's light falloff and colour, sampled from near-grey pixels, are applied to the prop.
  - **Focus:** the prop is softened to match the photo's own sharpness.
  - **Grain:** the photo's noise is added to the prop.
  - **Contact shadow:** a soft shadow is cast where the prop touches.
  - **Linear-light compositing:** all blending happens in linear light.

The user can drag, resize, rotate and flip a prop, or tap **Fit to face** to place it again.

## Frames

The engine draws frames at full resolution (`pp_frame`):

- **Shapes:** each shape is a signed-distance shape (rectangle, rounded rectangle, ellipse, arch, star, heart, hexagon, diamond), so its edges stay smooth at any size.
- **Backdrops:** solid, gradient, paper texture, or a blurred copy of the photo.
- **Finishing details:** borders (optionally metallic), outlines, drop shadows, glows and film sprockets.

The designs aim to look like gallery mats, instant prints and current social-media layouts, not cartoons. The photo can be zoomed and moved inside the window.

## Tests

| Suite | Result |
|---|---|
| Engine (C++, `photoplus_engine_tests`) | **80 tests, 0 failed**. Includes 8 new edit tests: straighten, looks/warmth, focus, prop placement and scene match, prop softness, frames, overlay visibility, leaks |
| Engine under AddressSanitizer + UBSan | 80 tests, 0 failed, no reports |
| Kotlin unit tests | **87 tests, 0 failed** (new `EditTest`: export policy, free-plan limits, verify, placement, document parsing, frame styles) |
| Android Lint | No issues |
| Jest | **187 tests, 0 failed** (the error-message test also checks the new watermark message). New `editor.test.tsx` covers tools, undo/redo, Original, save metadata, export and the watermark notice; camera and preview tests now check the watermark and clean copies |
| TypeScript, ESLint, Prettier | Clean |

## Real-photo checks

`scripts/make-editor-assets.py` runs the engine on Kodak test photos (and, optionally, a portrait for hats and neckwear) and checks the output:

- **Watermark:**
  - On the street (kodim01), the busy parrot (kodim23) and the portrait (kodim04), the mark draws in white ink, with 1,512 visible pixels each.
  - On the high-key photo, it switches to charcoal ink (1,471 visible pixels).
  - Each of these is well above the policy minimum.
- **Frames:** all 11 render at their own aspect ratio with no clipped decorations. The film strip now fits a whole number of sprocket holes across the frame.
- **Props:** glasses sit on the eye line of a three-quarter face (kodim15). On a head-and-shoulders portrait, every hat meets the forehead and every neck piece hangs at the collar.

Fixes made during this pass:

- The developer tool `edit_demo` silently ignored `--frame` (an off-by-one in its argument check). It now rejects any argument it can't read.
- The film frame's sprocket holes were cut at the right edge.
- The tortoiseshell glasses texture looked blotchy; it is now a realistic acetate pattern.
- Undo and redo had chevron icons; they now have proper undo and redo arrows.

## How the preview was made

```
python3 scripts/make-editor-assets.py KODAK_DIR edit_demo face_landmarker.task OUT_DIR [PORTRAIT]
node scripts/render-editor-preview.mjs OUT_DIR
```

The Python script mirrors `WatermarkRenderer.kt`'s signature layout in Pillow, using the same fonts, emblem, sizes, swoosh and ink rule. The engine then composites it with `pp_overlay`, exactly as on the phone. Frames and props are rendered by the engine. MediaPipe's face landmarker stands in for ML Kit on the desktop; the app itself uses ML Kit. The screens mirror `EditorScreen.tsx` and are captured at full scroll height.

## Open items

- A device pass: editor performance on low-end phones, ML Kit face fit on many faces, MediaStore export on Android 10 and on 14+.
- Play Billing: until it is connected, every user is on the free plan, so Premium branding is reachable only in debug builds.
- Props can't yet go behind hair or hands (no occlusion). The user can move or resize them.
