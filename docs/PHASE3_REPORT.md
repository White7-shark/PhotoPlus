# Phase 3 — Photo Editor, Watermark and Branding: Report

**Status:** Complete. No APK was built, as requested. Everything below was compiled, run or tested without producing an installable package. The editor has **not** yet run on a physical phone (none is attached to this environment). It needs a device pass before release (see "Open items").

Design preview: [`design/editor-gallery.png`](design/editor-gallery.png) and the screens `design/editor-{camera,adjust,frames,props,watermark,unlock,create}.png`. Every photo in them is real engine output; see "How the preview was made".

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
| | Props (hats, glasses, outfits) | 25 props: 11 hats, 7 glasses, 7 outfit pieces, free and premium in every group, each shown as a preview tile; plus props people make ("Mine"). See "Props" |
| | Frames | 22 photographic frames (10 free, 12 premium): star, heart, hexagon, octagon, diamond, sparkle, wavy circle, arch, oval, circle, capsule, story, film and more, each previewed around the person's own photo. See "Frames" |
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

The signature mark reproduces the brand reference exactly, measured from it in units of the wordmark's cap height (C):

- **Camera:** a hand-drawn camera outline, tilted 6°, with a glossy lens (magenta to cyan, dark pupil, highlight) and a gold swoosh cutting across it. It is 2.28 C wide and runs from the top of "Created" to the wordmark's baseline.
- **"Created with":** a thin, slanted script (Petit Formal Script, a touch heavier, as in the reference), as wide as the wordmark plus 5 %.
- **"PhotoPlus":** bold italic (Poppins ExtraBold Italic). "Photo" is in the ink colour, with its first "o" drawn as a camera aperture; "Plus" has a yellow-to-orange gold gradient.
- **Swoosh:** a tapered gold swoosh arcs under the wordmark, from below the "h" to below the "u".
- **Shadow:** soft, from the mark's own silhouette.
- **Place and size:** bottom right with a 3.5 % margin. At the default size, the cap height is 3.2 % of the photo's short side, as in the reference.

Its ink adapts to what is behind it. The renderer measures the brightness of the area under the mark:

- **Darker or busy backgrounds:** white ink with a soft dark shadow.
- **Bright backgrounds** (above 62 %): charcoal ink, a deeper gold for "Plus", and a light shadow.

The artwork is drawn by `scripts/make-brand-mark.mjs` (camera outline, lens and swoosh, aperture). The outline and aperture are tinted with the ink colour at draw time. The preview gallery shows the mark on a dark, busy photo, on a mid-tone street and on a bright, high-key photo.

## Props in the camera

Props can be worn in the camera:

- **Tray:** a props chip opens a tray of preview tiles (Hats, Glasses, Outfit, Mine). Tap one to wear it.
- **Live tracking:** the camera tracks the main face in the preview (ML Kit, about 15 times a second, measured within exactly what the preview shows and mirrored for the front camera), and the prop follows it.
- **Adjusting:** drag the prop to move it, or pinch it to resize and turn it. The adjustment is kept relative to the face, so it keeps following it. Camera gestures (focus, pinch-to-zoom) still work everywhere else on the screen.
- **Into the photo:**
  - The engine composites the prop into the captured photo's pixels (light, colour, softness, grain and contact shadow), before any watermark.
  - When the prop was following a face, it is fitted again to that face in the photo itself, keeping the person's adjustment, so a move between the preview frame and the shutter doesn't misplace it.
  - Device rotation and front-camera mirroring are both accounted for.
- **Editing:** after a shot, an **Edit** button under the thumbnail opens it in the editor. After Clarity, Spotlight or Transform, **Keep and edit photo** saves the result and opens it in the editor. The photo screen also keeps its **Edit photo** button.

## Making your own props (Premium)

**Make a prop** works from any photo: tap the object and it is cut out. Subject segmentation is tried first, then the engine's tap cut-out; soft edges are kept and the cut-out is trimmed to the object. The person names it and chooses where it goes (head, eyes, neck or anywhere), so it fits faces like the built-in props.

Saved props appear under **Mine** in the camera and the editor. They are stored as transparent PNGs in app storage, with ids the native side validates, and can be deleted. Cutting out is Premium, checked natively.

## Watermark-free photos with ads (free plan)

On the free plan, watching **four** rewarded ads for a photo lets that photo be shared, saved to the gallery or exported without the watermark:

- **Progress:** kept per photo, so people can stop and come back. The marked photo and its clean copy count as the same photo.
- **Where it's offered:** in the editor and on the photo screen.
- **Counting is native:** the reward ledger (`AdRewards`) counts only the ad SDK's reward callback, and the export pipeline checks it before deciding on the mark (`ExportPolicy.resolveForExport`). The app's screens cannot grant an unlock. A free export that isn't unlocked still fails if its mark didn't render.
- **Consent:** Google's User Messaging Platform asks for consent where the law requires it, and ads are requested only when consent allows.
- **Ad units:** debug builds always use Google's test ad unit. Release builds use the IDs set as the Gradle properties `photoplus.admobAppId` and `photoplus.admobRewardedId`, or Google's test IDs until real ones are set.

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
| Engine (C++, `photoplus_engine_tests`) | **80 tests, 0 failed**. The frame test now covers all 10 shapes (adds octagon, wavy circle and sparkle) |
| Engine under AddressSanitizer + UBSan, and ThreadSanitizer | 80 tests, 0 failed, no reports (run before the three new shapes; the new shapes are covered by the regular run) |
| Kotlin unit tests | **100 tests, 0 failed**. New `PropsAndAdsTest` covers: face fitting (hats, glasses, outfits, a turned head, adjustments that follow the face), preview to photo mapping (rotation, mirroring), camera prop validation, live-face mapping within the preview crop, custom prop ids, mask scaling and trimming, 20+ frames, the four-ad unlock and its export check, and the default mark size |
| Android Lint | No issues |
| Jest | **199 tests, 0 failed**. New `propsAndAds.test.tsx` covers: the catalogue matching the native lists and bundled art, made props, live prop geometry, the camera tray (tracking, the prop in the capture request, taking it off), Edit after a shot, the ad unlock (progress, only full ads count) and making a prop |
| TypeScript, ESLint, Prettier | Clean |

## Real-photo checks

`scripts/make-editor-assets.py` runs the engine on Kodak test photos (and, optionally, a portrait for hats and neckwear) and checks the output:

- **Watermark:**
  - On the street (kodim01), the busy parrot (kodim23) and the portrait (kodim04), the brand mark draws in white ink, with 3,267 visible pixels each.
  - On the high-key photo, it switches to charcoal ink (3,196 visible pixels).
  - Placed beside the reference image's own mark, it matches its camera, script, two-tone wordmark, aperture "o", swoosh and size.
  - Each of these is well above the policy minimum.
- **Frames:** all 22 render at their own aspect ratio with no clipped decorations. The film strip now fits a whole number of sprocket holes across the frame.
- **Props:** all seven glasses sit on the eye line of a three-quarter face (kodim15). On a head-and-shoulders portrait, every hat meets the forehead and every neck piece hangs at the collar. The bucket hat (it covered the eyes), the flower crown and the scarf (both too small) were adjusted after this check.

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
- Ads: real AdMob app and rewarded unit IDs are needed for release (test IDs are used until then), plus the Play Console's ads declaration and data-safety form.
- Live props: tracking performance on low-end phones needs a device pass (the analysis stream runs at 640×480, about 15 fps).
