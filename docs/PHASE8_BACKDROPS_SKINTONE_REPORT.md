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

Skin Tone is separate from the beauty filters, which light and retouch. It changes the colour of the skin itself, softly and under the photo's own light: on the face, neck, chest, arms and hands. Hair, eyes, brows, lips, teeth, clothes and the background keep their own colours.

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

A first version moved the skin to an absolute target colour. On review it looked like heavy make-up: deep skin pushed toward a pale or golden target went ashen, khaki or greenish. It was rebuilt against a reference sheet of 13 shades on one face. In that sheet every shade stays in the natural skin family, the face and hand change together, and the room keeps its light.

**Measured from the reference:** fair shades lift the skin by about +0.4 to +0.7 stop, deep ones lower it by about −0.2 to −0.45 stop, and each re-balances red, green and blue a little.

1. **A shade is a change of the skin's own colour under the same light**: per-channel gains in linear light, as if the skin's albedo changed.
   - The scene's light colour, the person's shading, pores and flush all stay, so the new shade sits in the photo naturally.
   - A test checks this: under a warm lamp or a cool window, the skin takes the same change and the wall is untouched.
   - The gains were derived from the reference and then fitted on 18 real faces so neighbouring shades stay apart; each shade stays within ±0.08 of its design values.
2. **Natural-hue guard.** Skin hue lives in a narrow band, measured on these faces at 107–135° (Cb/Cr angle). Past the band, a colour eases back softly, never further than the person's own starting hue: deep skin never turns khaki or olive-green, and no skin turns bruised-magenta. The upper limit rises from about 135° on deep skin to 145° on fair skin.
3. **Headroom for fair skin.** Already-fair skin isn't lifted toward white, where it would lose its texture. It takes the shade's colour balance, not its lift. Highlights roll off just short of white, so nothing new clips.
4. **Shine** keeps the light's colour; only its brightness follows the shade.
5. **Even tone.** Colour blotches and colour noise are pulled a little toward the skin's local colour. Brightness detail — pores and the face's shape — is untouched.
6. **Finding the skin.** A weight per pixel combines:
   - colour similarity to the person's own skin (measured on the face), with a capped tolerance;
   - brightness, which keeps dark hair, brows and beards out;
   - texture, which keeps fair hair out: strands are busy, skin is smooth.

   The face area is feathered so no oval outline shows, and features (eyes, brows, lips) are protected with soft edges. The weight is firm on clear skin, so the face and the body take the same shade, and it follows the photo's own edges through a colour guided filter.

### Validation (`scripts/skintone-demo.py`)

**Faces:** your photo, two reference portraits and 15 FairFace faces from very fair to very deep, including group photos.

**Ground truth:** MediaPipe's multiclass segmenter labels face skin, body skin, hair and clothes, for measuring only; the app uses ML Kit plus the face landmarks. 234 renders.

| Measure (CIELAB ΔE unless noted) | Median | 90th percentile |
|---|---|---|
| Face skin change | 5.8 | 8.8 |
| Body skin change (neck, arms, hands) | 4.3 | 8.2 |
| Share of the shade's change reaching the face / body | 0.76 / 0.68 | — |
| Hair change | 0.75 | 4.3 |
| Clothes change | 0.29 | 2.3 |
| Background change | 0.01 | 0.08 |
| Eyes, brows, lips change | 0.9 | 1.5 |
| Skin texture relative to brightness (face) | 1.04 | 0.95–1.16 (10th–90th) |
| Newly clipped pixels | 0 in every render | — |

**Distinct shades.** On each face, the two closest shades differ by a median of 2.3 ΔE (about what most people can just tell apart), and by 2.5 on your photo.

Gentle shades sit close by nature; neighbours in the reference sheet are close too (Honey, Caramel, Bronze). On the three most underexposed FairFace faces the closest pair falls to 0.8–1.0.

**Speed:** 0.27–0.34 s per shade for your 8 MP photo, 0.07–0.15 s at 0.8 MP (desktop host, 4 cores).

**Your 5-second clip** (164 frames): face found in every frame. Cheek colour changes 0.52–0.81 levels frame to frame with a shade, against 0.57 with Glide alone. About 0.1 s per frame on the host.

**Known limits.**
- Skin-coloured hair (auburn, ginger, light brown) can pick up a little of the shade at the fringe (90th percentile 4.3 ΔE).
- On a very underexposed face, neighbouring shades are hard to tell apart.

## The backdrop video glitch

**Seen in your 5-second clip:** a shadowy form pulling in and out around the person.

**Cause.** Single frames where the person-finder made a mistake:
- In frame 61 a chunk of the wall above your head was marked as "person"; in frame 60, a fleck beside it.
- The head outline changed shape from one frame to the next.

Cut out and re-lit, those bits of the old wall flickered in and out of the new scene.

**Fixed in two places:**
- **The matte drops stray islands.** Patches of "person" not connected to anyone (smaller than 8% of the main subject) are removed. People standing apart are each large, and stay. This helps photos too.
- **Video uses a three-frame consensus.** The new `pp_video_consensus_mask` takes the per-pixel median of the previous, current and next frames' masks. A one-frame mistake is outvoted; real movement, which changes smoothly, passes through.

**Result on your clip (Stage):**
- The wall chunk and the fleck are gone (see frames 58–63 before and after).
- Frame-to-frame change in the background halved: 1.84 → 0.93 levels on average, worst frame 10.6 → 7.0.

**Not yet in the app:** Glide clips don't offer Backdrops yet (they use Spotlight's blur). The consensus call is ready for that, and for steadier Spotlight clips.

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
| Engine `backdrop_tests.cpp` | 10 tests, also under AddressSanitizer + UBSan. New: hair the mask missed and the wall seam; a stray patch of wall taken for the person is dropped |
| Engine `video_tests.cpp` | New: the three-frame consensus outvotes a one-frame blob and keeps a moving edge |
| Engine `skintone_tests.cpp` | 7 tests, also under ASan + UBSan:<br>• 6 starting tones × 3 shades: face and neck take the shade's gains (within 5%; the fairest skin takes the shade's colour balance, not more lift)<br>• the scene's light stays on the skin (warm lamp, cool window)<br>• background < 0.3 levels; eyes, iris, teeth and lips unchanged<br>• texture kept, no new clipping; strength blends<br>• no face (or a tiny one) = no change; bad input; cancellation |
| Kotlin | 3 new in `SkinToneTest` (catalogue match including gains, gentle range, parsing and clamping, editor document and re-render key); lint clean; native library builds with the NDK |
| Jest | 4 new in `skintones.test.tsx` (gentle gains, fair shades lift and deep ones deepen, free shades span the range, covers, validation and old edits, panel: apply / lock / strength / remove, no-face note) |

## Not verified on a phone

No APK was built, as instructed. The Kotlin compiles and its unit tests pass, and the native library builds for all ABIs. Speeds on phones will differ from the desktop figures above.
