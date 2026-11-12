# Phase 8 — Backdrops and Skin Tone: Report

Asked for:
- background designs ("Backdrops"), more than 10, at a professional level. The camera uses one default that suits any photo; the rest live in the photo editor, so the camera screen stays uncluttered;
- a fix for hair that was cut off on every backdrop except Signature;
- more than 10 Skin Tone filters, separate from the beauty filters, that change the colour of the person's skin (face and visible body).

Everything runs on the device. There are no cloud APIs and no generated images.

Your own photo and clip were used only in the session's scratch space and are not in the repository. Neither are any previews made from them. The cover images in the app are made from the public Kodak test portrait.

## Backdrops

| # | Backdrop | Plan | Look |
|---|---|---|---|
| 1 | Signature | Free | A fast lens's soft depth, the person carrying the frame. **The camera's default** (Spotlight), as strong as the Depth blur slider |
| 2 | Soft Studio | Free | A creamy, calm studio wash |
| 3 | Stage | Free | A black stage, light falling off the shoulders |
| 4 | Noir | Premium | Stage light in dramatic black and white |
| 5 | Porcelain | Premium | A bright, high-key white studio |
| 6 | Canvas | Premium | A hand-painted portrait backdrop |
| 7 | Golden Hour | Premium | Low sun, warm haze and a golden rim of light |
| 8 | City Lights | Premium | Night falls; the city melts into bokeh |
| 9 | Neon | Premium | Magenta and cyan glow with matching edge lights |
| 10 | Colour Pop | Premium | You in colour, the world in black and white |
| 11 | Velocity | Premium | A zoom burst rushing toward you |
| 12 | Swirl | Premium | The swirling bokeh of a vintage portrait lens |
| 13 | Aurora | Premium | Ribbons of northern light |
| 14 | Dream | Premium | Pastel haze and a soft bloom |

How a backdrop is made (`native/engine/src/backdrop/backdrop.cpp`):
1. **Matte.** The person is cut out along the photo's own edges, using a colour guided filter on a working grid of about 768 px, evaluated per pixel at full resolution.
2. **Clean plates.** The old background is rebuilt without the person, so none of the person bleeds into the new scene.
3. **The design.** Each design is painted procedurally: light falls, bokeh, aurora curtains, zoom and swirl sampling.
4. **Edges and light.** Hair and edge pixels are freed of the old background's colour (I = aF + (1 − a)B). The person is re-lit for the scene: rim light, light wrap, stage fall-off.
5. **Composite.** The person and the new background are combined in linear light, with a breath of grain so gradients never band.

### The hair fix

**Cause.** Person masks, ML Kit's included, stop short of soft or curly hair, and they can notch the crown. Signature keeps the real background, so this didn't show there. Every repainted design cut the head along the mask's line.

**Fix.** Hair the mask missed is now recovered from the photo itself:
- **Holes are filled first.** A gap with the person on both sides (left and right, or above and below), within 7% of the frame, is a mask error such as a notch at the crown. It's filled where its colour matches the person.
- **Then hair beyond the silhouette is searched for**, up to about 3% of the frame out. A pixel counts as hair only if all three of these hold:
  - its colour sits on the line from the nearby background to the nearby hair;
  - it is busy at fine scale like the hair inside the mask (a wall seam is smooth);
  - **no view of the background explains it**: not the neighbourhood, not the same row, not the same column. A seam, shelf or horizon behind the head is explained by its own row.
- **Shape check.** An opening removes thin spikes off the silhouette. Hair is a soft layer around the head; a stray background line is a spike.
- **Halo trimming is lighter**, so hair keeps its soft edge.

**Verified.**
- **Your photo:** the crown notch and the cut-off hair are restored on all 13 repainted designs, with no shelf from the wall seam behind your head. See the before/after crops.
- **Regression test:** `backdrop_keeps_hair_the_mask_missed_but_not_a_seam_in_the_wall`. A mask is shrunk 9 px and notched, against a wall with a seam. Levels measured:

  | Scene | Hair ring | Crown notch | Seam beside the head |
  |---|---|---|---|
  | Dark curly hair on white | 107 (hair ≈ 40, white ≈ 240) | 83 | 241 |
  | Fair hair on black | 181 (hair ≈ 190, stage ≈ 10) | 170 | 12 |

  Before the fix, every part of the hair ring read as backdrop.
- **Speed is unchanged:** 0.23–0.53 s per design for a 3 MP photo on the desktop host (4 cores). Wide blurs now run as running boxes, whose cost doesn't grow with the blur width.

**Limit.** In your dim 5-second clip, dark hair against a dark brown wall has almost no colour difference to work with. The edge there follows the mask, which is still better than before.

## Skin Tone

Skin Tone is separate from the beauty filters, which light and retouch. It changes the colour of the skin itself: on the face, neck, chest, arms and hands. Hair, eyes, brows, lips, teeth, clothes and the background keep their own colours.

| # | Shade | Plan | Character |
|---|---|---|---|
| 1 | Porcelain Rose | Premium | Fair with a cool, rosy undertone |
| 2 | Ivory | Free | Fair and neutral |
| 3 | Peach | Premium | Light with a warm peach undertone |
| 4 | Golden Beige | Premium | Light-medium, soft golden beige |
| 5 | Honey | Free | Medium with a golden-yellow undertone |
| 6 | Olive | Premium | Medium with a green-olive undertone |
| 7 | Sun-Kissed | Premium | A warm, glowing tan |
| 8 | Caramel | Free | Medium-deep, warm caramel |
| 9 | Bronze | Premium | Deep golden bronze with sheen |
| 10 | Cinnamon | Premium | Deep with a red-brown undertone |
| 11 | Mocha | Premium | Deep, neutral cocoa |
| 12 | Espresso | Free | Very deep and rich |
| 13 | Ebony Glow | Premium | The deepest shade, with a luminous sheen |

The four free shades span the whole range: fair, medium, medium-deep and very deep. Shades are in `src/services/skintones/skintones.json`, and `SkinTone.kt` mirrors them; a unit test checks that they match exactly.

In the editor, the new **Skin Tone** tool (next to Backdrop) shows a tile per shade and a Strength slider (default 80%). If a photo has no face to measure, the panel says the skin was left as it was.

### How it works (`native/engine/src/skintone/skintone.cpp`, `pp_skin_tone`)

1. **Measure the person's own skin** on the face, the median colour and its spread, as the beauty engine does.
2. **Find their skin everywhere.** A weight per pixel combines:
   - colour similarity to that skin, with a capped tolerance so mixed light or a group photo doesn't take in clothes;
   - brightness, which keeps dark hair, brows and beards out;
   - texture, which keeps fair or blonde hair out: strands are busy at fine scale and skin is smooth, and the test is stricter around and above the face;
   - the face and person masks, with eyes, brows and lips protected.

   The weight follows the photo's own edges through a colour guided filter.
3. **Brightness moves part of the way:** half the distance in stops, eased off toward about 0.7 stop either way, in linear light. Highlights are rolled off, so nothing new clips.
4. **The undertone (hue) moves all the way** to the shade's. Saturation follows only as far as brightness could go, and always at least half way. Without that, a pale shade on a face that stays deep looks ashen grey, and a deep shade on a face that stays fair looks orange. Shine stays nearly white, which avoids a metallic look. Out-of-range colours are eased back by reducing chroma, never by clipping, so texture stays.
5. The person's own variation — flush, shading, freckles, pores — is kept around the new shade.

**A deliberate choice.** A shade changes the person's colouring, but not who they are. Brightness is limited to about ±0.7 stop. Porcelain Rose on a deep-skinned face gives a lighter, rosier version of that face, not a different person's skin. The same holds the other way. Strength scales everything.

### Validation (`scripts/skintone-demo.py`)

**Faces:** your photo, two reference portraits and 15 FairFace faces, from very fair to very deep, including group photos.

**Ground truth:** MediaPipe's multiclass segmenter labels face skin, body skin, hair and clothes, for measuring only. The app uses ML Kit plus the face landmarks. 18 faces × 13 shades = 234 renders.

| Measure (CIELAB ΔE) | Median | 90th percentile |
|---|---|---|
| Face skin change | 10.2 | 16.2 |
| Body skin change (neck, arms, hands) | 7.4 | 14.4 |
| Distance to the shade's colour, after / before (face) | 0.51 | — |
| Hair change | 1.1 | 6.7 |
| Clothes change | 0.45 | 3.7 |
| Background change | 0.01 | 0.08 |
| Eyes, brows, lips change | 1.0 | 1.9 |
| Skin texture kept (fine detail, after / before) | 1.09 (face), 1.07 (body) | 0.94–1.20 (10th–90th) |
| Newly clipped pixels | 0 | max 0.8% (one low-light photo) |

**Distinct shades.** On each face, the two closest shades differ by a median of 3.6 ΔE (2.3 is about what most people can just tell apart). The exceptions are the most underexposed FairFace faces, where the brightness limit squeezes neighbouring light shades together: Golden Beige vs Olive at 1.2 on the darkest. The shade colours were fitted with a model of the engine on these faces, keeping each shade close to its name.

**Speed:** 0.29–0.33 s per shade for your 8 MP photo, 0.05–0.09 s at 0.8 MP (desktop host, 4 cores).

**Your 5-second clip** (164 frames): face found in every frame. The skin is as steady as the clip itself: cheek colour changes 0.74 levels frame to frame with a shade, against 0.57 with Glide alone. About 0.1–0.24 s per frame on the host.

**Known limits.**
- Hair the same colour as the skin (auburn, ginger, some light browns) can pick up part of the shade, especially at the fringe. The hair change reaches 10–13 ΔE on the worst photos; the Strength slider tones it down.
- Clothing in a skin-like colour (beige, pink) near the skin can shift slightly (90th percentile 3.7 ΔE).

## App wiring

| Layer | Backdrops | Skin Tone |
|---|---|---|
| Engine | `pp_backdrop` | `pp_skin_tone` |
| Android | `nativeBackdrop`, `Backdrop.kt`, `BackdropChoice` | `nativeSkinTone`, `SkinTone.kt` (`SkinShade`, `SkinToneChoice`) |
| Pipeline | `PortraitProcessor.applyBeauty`: one set of masks, models run once. Order: skin tone → beauty → backdrop | (same) |
| Camera | Spotlight uses Signature (`cameraBackdrop`) | Editor only |
| Editor | `EditDocument.backdrop`, Backdrop tool | `EditDocument.skinTone`, Skin Tone tool; the render reports whether a face was found |
| Plans | `backdrops` (free 3), `backdropsPremium` | `skinTones` (free 4), `skinTonesPremium` |

## Tests

| Suite | Result |
|---|---|
| Engine `backdrop_tests.cpp` | 9 tests (1 new: hair the mask missed and the wall seam), also under AddressSanitizer + UBSan |
| Engine `skintone_tests.cpp` | 6 new, also under ASan + UBSan. For 6 starting tones × 3 shades: undertone 92–102% of the way on face and neck, brightness within the limit, richness never past the shade. Background < 0.3 levels and eyes, iris and teeth < 2 levels unchanged; texture kept, no new clipping; strength blends; no face = no change; bad input; cancellation |
| Kotlin | 143 tests, 3 new in `SkinToneTest` (catalogue match, parsing and clamping, editor document and re-render key); lint clean; native library builds with the NDK |
| Jest | 248 tests, 4 new in `skintones.test.tsx` (order fair to deep, free shades span the range, covers, validation and old edits, panel: apply / lock / strength / remove, no-face note) |

## Not verified on a phone

No APK was built, as instructed. The Kotlin compiles and its unit tests pass, and the native library builds for all ABIs. Speeds on phones will differ from the desktop figures above.
