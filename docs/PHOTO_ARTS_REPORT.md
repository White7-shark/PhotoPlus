# Photo Arts: your photo painted as twenty great artists worked

(The tool was called Masters, then Masterstroke. Code identifiers still say `masters`, but the name people see is **Photo Arts**.)

Photo Arts is a Photoshoot Studio tool. It is Premium; the free plan gets **one finished painting a day**. Previews and the artist cards are free and unlimited.

It is not a filter laid over the photo. The engine paints a new picture from it the way each artist worked:
- the ground they painted on;
- brush strokes from broad to fine that follow the forms;
- their palette and light;
- the finish of the real object (canvas, panel, varnish, craquelure, laid paper, an engraved plate, a woodblock print, gold leaf).

## The artists

| Movement | Artist | Technique (what the engine does) |
|---|---|---|
| Renaissance | Leonardo da Vinci | Sfumato: soft blended modelling, umber and olive, a blue haze in the distance, varnish and fine cracks on a wood panel |
| | Michelangelo | Red chalk on cream laid paper: hatching that follows the form, with the paper left bare around the figure |
| | Raphael | Smooth, invisible brushwork; clear ultramarine and vermilion |
| | Albrecht Dürer | Copper engraving: burin lines that swell in the shadows, cross-hatched in the darks, black ink on white paper |
| | Titian | Venetian glazes: deep reds and golds in broad, soft strokes |
| Baroque & Golden Age | Caravaggio | Tenebrism: one hard light, the figure brought out of a black ground |
| | Rembrandt | Chiaroscuro: golden light on the face, impasto in the lights, glazed brown shadows |
| | Johannes Vermeer | Window daylight from the left, cool shadows and warm lights, pearly highlights |
| | Diego Velázquez | Loose, silvery alla prima strokes |
| Romantics & printmakers | Francisco Goya | Dark, rough, expressive brushwork in blacks and earth colours |
| | Katsushika Hokusai | Ukiyo-e woodblock print: flat colour blocks, Prussian blue, black key lines, washi paper; the sitter's own skin tone |
| | J. M. W. Turner | Light dissolving form: a golden haze, sweeping washes, the sitter held in the glow |
| Impressionists | Claude Monet | Broken colour: dabs of pure colour, violet shadows, a bright high key |
| | Edgar Degas | Pastel hatching on toned paper, with the paper showing through and charcoal contours |
| | Pierre-Auguste Renoir | Soft, feathery, rosy strokes |
| Post-Impressionists & modern | Paul Cézanne | Parallel diagonal strokes that build planes of colour, with blue contours |
| | Georges Seurat | Pointillism: dots of pure colour that the eye mixes |
| | Vincent van Gogh | Thick, swirling impasto dashes that follow the forms, with deep blue outlines |
| | Paul Gauguin | Flat, warm colour fields with bold dark-blue contours, on coarse canvas |
| | Gustav Klimt | A finely painted face set in gold-leaf ornament: mosaic, spirals, rings and checks |

## Finish levels

The full painting's long edge, capped by the phone's memory budget:
- **Study** (1100 px): loose; the finest brush layer is left out.
- **Painting** (1500 px): every layer.
- **Masterpiece** (1800 px): one extra, finer brush layer.

At every level the face gets its own fine-brush pass, so it stays true to the person.

## How it works

**Engine (C++)**
- Files: `native/engine/src/art/` (`core.*` holds the painter's tools; `art.cpp` holds one recipe per artist). The API is `pp_art_paint`.
- Strokes:
  - Layers run from broad to fine and are driven by error: each layer repaints only where the canvas still differs from the photo.
  - Strokes follow a structure-tensor orientation field, and bend only where that field is coherent. This avoids "wormy" strokes in skies.
  - Each stroke has bristle streaks, a dry, broken tail and impasto height. The height is then lit as relief.
- Person and faces: ML Kit finds the person mask and the faces on the phone (`PortraitProcessor.personAndFaces`). Faces are added into the mask. This lets the background be treated the way the artist would (a dark ground, haze, gold) while the person stays the subject.
- The engine is deterministic for a given seed ("Paint again" changes the seed), runs in parallel tiles, and can be cancelled.
- Engine tests (`tests/art_tests.cpp`) check that:
  - every artist differs from the input and from every other artist, and alpha is kept;
  - results are deterministic;
  - each artist shows its traits (Caravaggio is dark, Dürer is ink on white, and so on);
  - the levels differ;
  - painting works without a mask;
  - bad input is refused and cancelling works.

**Android**
- `StudioModule.renderMasters` renders at three qualities:
  - `thumb`: 300 px, Study level, for the cards;
  - `preview`: 760 px;
  - `full`: the finished painting.
- A full render needs `ArtAllowance.allowed`. Otherwise it is refused with `PREMIUM_REQUIRED` and a translated message.
- `ArtAllowance` counts one painting per local calendar day, using the trusted clock (Integrity), so changing the phone's date doesn't reset it. Premium, or Photo Arts redeemed with Rewards points, has no limit.
- The free plan's finished paintings carry the watermark, like every other free export.
- The person mask and faces are cached for the last three photos, so changing the artist doesn't run detection again.
- R8 keeps the JNI method through the existing `native <methods>` keep rule (checked with `minifyReleaseWithR8`).

**App**
- `MastersScreen`:
  1. Choose a photo (or open one passed in).
  2. Pick a movement from the chips.
  3. Pick an artist card; each card shows the photo already painted by that artist (drawn one at a time).
  4. Read the artist's technique and how they worked.
  5. Choose Study, Painting or Masterpiece.
  6. Hold the picture to compare it with the photo.
  7. Tap "Paint it", then save to the gallery, share, or paint again.
- Below the button, a line shows what is left: one free painting today, today's painting used, or Premium with no limit.
- When the day's painting is used, the Premium sheet opens.
- A newer preview cancels the older one.
- The catalogue (`src/services/masters/catalog.ts`) holds names, techniques, descriptions and movements as string resources. Years are numbers.

## Your touch: the person's controls

A collapsible panel on the screen ("Your touch") guides the artist's hand. Every control starts at the artist's own way. Each slider shows how far it is from that (0, +30, −20) and names its two ends.

| Control | Range (default) | What it does in the engine |
|---|---|---|
| Brush size | 0.6–1.6 (1) | Scales every mark: strokes, hatching, dots, tesserae, contours. Places in the picture (where the light falls, the haze) don't move. |
| Colours | your photo's 0 – artist's palette 1 (1) | Takes the photo's hue and colourfulness at the painting's own light, so there is no glaring colour in the shadows. |
| Vividness | −1–1 (0) | Raises quiet colours most and strong ones (skin, a red dress) little, so skin stays natural. Lower values mute. |
| Likeness | artistic 0 – true to you 1 (0.5) | Uses finer brushes and a lower repaint threshold on the face. Above the middle, the photo's fine light and shade are drawn back into the face. |
| Texture | 0–1.5 (**0.4: smooth by default**; 1 is the artist's full texture) | Canvas weave, paint relief and paper grain. It also governs the marks themselves: bristle streaks, dry broken tails, paint ridges and thickness, and how much chalk or pastel skips the paper's grain. |
| Age | fresh paint 0 – centuries old 1.5 (1) | Varnish and craquelure. |
| Background | your scene 0 – artist's 1 (1) | How far the artist takes over the background (Caravaggio's black ground, Leonardo's haze, Klimt's gold). At 0 your own background is painted as it is. |

How the controls behave:
- The preview repaints when the finger lifts, and screen readers can step each slider.
- "Paint it the artist's way" resets everything.
- The finished painting uses the same touch.
- The artist cards always show each artist's own way.
- **Engine:**
  - `pp_art_controls` is the C struct; pass `NULL` for the artist's way.
  - Values out of range are refused.
  - Android clamps the values and passes them through JNI.
  - A test checks that each control changes what it says and that the defaults give exactly the artist's picture.

## Teeth and the whites of the eyes

Some artists blackened, crossed out or yellowed teeth:
- Dürer's engraved lines and Michelangelo's chalk hatching left black specks.
- Hokusai and Gauguin flattened the teeth into skin or drew lines across them.
- Rembrandt, Leonardo and Turner turned them yellow-brown under the varnish.

After painting, the engine now finds the teeth and the whites of the eyes in the photo and gives them back their light.

**Where it looks.** Only where they can be:
- **Mouth:** the opening between the inner lip lines (ML Kit's upper-lip-bottom and lower-lip-top contours). A closed mouth has no opening, so the shine on closed lips stays the lips' own.
- **Eyes:** around each eye centre.
- **Fallbacks:** when there are no contours, the mouth landmarks are used. When there are no landmarks, it uses where these features sit in a face box.

**What counts.** Within those areas, a pixel counts when it is:
- clearly brighter than the face's median skin (by half again on dark skin, by a step on light skin), and
- far less colourful than the skin, and
- not lip-pink.

Gaps between teeth are closed, and specks are opened away, at the face's scale.

**What it does.** The whites are lifted to the photo's own light, scaled to how the painting lit the face, so they are never brighter than the painting allows. They take the painting's tint, desaturated, so Dürer's teeth are paper-white and Michelangelo's are chalk-paper.

**API.** Each face passes 12 floats: the box, the mouth opening and the eye centres (`face_stride` 12). The older 4-float faces still work.

**Test.** `art_keeps_teeth_white` checks that:
- a smile stays white and clearly brighter than the skin under Dürer, Michelangelo, Caravaggio, Hokusai and Gauguin;
- a closed mouth gets no teeth;
- a face box alone still finds the teeth.

## Checked on real photos

All 20 artists were rendered on three photos:
- the user's own phone selfie: flash-lit, closed shiny lips, dark skin;
- a real studio couple photo: two smiling faces, small in the frame;
- a studio portrait with a wide smile.

Then the mouths and faces were compared before and after. Teeth are white and natural for every artist. Closed lips keep their colour, and no shine on a cheek or forehead turns into a white dot. The smooth default removes the rough canvas, grain and specks (Van Gogh, Monet, Cézanne, Degas) and keeps each artist's brushwork.

The person masks and face points for these checks were made by hand on the computer. On the phone, ML Kit provides them. The previews stay local and are not committed.

## Settings

Settings has a Photo Arts section, stored as the validated `photoArts` setting:
- **Finish to start with:** Study, Painting or Masterpiece.
- **Remember my touch:** keeps your controls for next time. When it's off, every session starts at the artist's way.
- **Paint the artist cards:** when it's off, cards show the artist's initial. This saves battery.
- **Reset my touch.**

## Premium simulation (testers; to be removed for production)

- **Where:** Settings has a Testing section with a **Simulate Premium** switch. It gives this phone Premium without a subscription: unlimited Photo Arts paintings, no watermark, and every Premium tool, because the native plan is what decides these.
- **When it shows:** only where the build allows it. That is debug builds, and builds made with `PREMIUM_SIMULATION`, which `android/app/build.gradle` sets to `true` for now.
- **Before the production release**, do one of these:
  - set the default to `false`, or
  - build with `-Pphotoplus.premiumSimulation=false`.

  The switch then disappears and any simulation already stored on a phone has no effect.

## Words and languages

Every new text was written by hand in English and translated by hand into Spanish, Brazilian Portuguese, French, Arabic, Hindi, Indonesian, Russian and Simplified Chinese. That is about 146 strings, including:
- the artist names as each language writes them (Léonard de Vinci, Miguel Ángel, 葛饰北斋, …);
- the techniques and descriptions;
- the two new FAQ entries;
- the native messages;
- the Rewards tool names;
- the Your touch controls, the Photo Arts settings and the Premium simulation texts.

The source is `tools/i18n/content/masters.py`, which merges into `src/i18n/strings/*.json`. `tools/i18n/build-strings.mjs` then generates `strings.xml` for each language.

The titles of Premium features are now string resources too (`feature_<name>`, all 59), and the Premium sheet shows them in the app's language. Photo Arts's title is translated by hand; the rest are in `values/strings.xml` for Google Play to translate.

## Checks

- Engine: 8 art tests pass (245 engine tests in all). On the host, a 900×960 photo takes about 0.4–4 s, depending on the artist.
- App:
  - `tsc` and `eslint` are clean.
  - Jest: 37 suites, 406 tests. The new `__tests__/masters.test.tsx` covers the catalogue, all 8 translations (with their placeholders), the service, the studio tile, the full screen flow (free painting, then Premium) and Premium without limits.
  - The i18n checks pass: no text left in code, and strings are up to date.
- Android:
  - `buildCMakeDebug`, `compileDebugKotlin`, `testDebugUnitTest` (with the new `ArtAllowanceTest`), `lintDebug` and `minifyReleaseWithR8` pass.
  - No APK was built.

## Engine v2: genuine painting, not filtered photos (after an outside review)

An outside review of the first results said several artists looked like the photograph with a colour filter. It also found too much orange, faces losing their features, sketch-like Michelangelo and Hokusai, uniform Dürer lines, distorted Gauguin faces and over-saturated Van Gogh. It asked for:
- a genuine painting simulation per artist;
- face-aware processing, with every face in a couple or group handled on its own;
- adaptation to each photo;
- an intensity control;
- canvas and paper choice;
- artistic backgrounds;
- museum frames;
- a before/after slider;
- a results gallery;
- the seven categories below.

**What changed in the shared pipeline (all artists)**
- **Adaptive input:** an underexposed or flat photo is levelled first. Its tones stay in order, so a skin's own depth isn't lightened.
- **Abstraction:** an edge-preserving simplification removes the photo's micro-texture (pores, noise, fabric weave) before the broad and middle strokes. This is what removes the "photo with a filter" look.
- **Detail only where it lives:** the finest brushes go only to faces and strong edges. Backgrounds and clothes stay visibly painted.
- **Face-sized brushes:** every face gets its own pass, with brushes sized to that face, then its eyes, brows, nose and mouth with the finest. A couple's or group's smaller faces keep their features.
- **Feature detail:** the features' own light and shade is drawn back in at the face's scale (per-artist amount). Flat styles (Hokusai, Gauguin) ink the features as "darker than the skin around it". This holds on dark skin and in low light, where outlines are faint.
- **Natural skin:** skin keeps its own hue and is never much more colourful than in the photo (per-artist strength). This ends orange and red faces.
- **Intensity (Transformation):**
  - Natural keeps more of the photo's colour, with finer brushes and gentler abstraction.
  - Full art uses bolder brushes, the artist's full palette and stronger abstraction.
  - Balanced is the default.
- **Paint as signature:** Rembrandt, Van Gogh, Velázquez, Goya, Monet and Cézanne keep a minimum of their paint texture even at the smooth default.

**Artists reworked**
- **Michelangelo:** now buon fresco, as on the Sistine ceiling: sculptural modelling, a mineral palette on lime plaster, tratteggio hatching in the half-shadows, a lapis sky, a matte plaster surface. It was a red-chalk sketch before.
- **Dürer:** on faces, fine lines only in the shadows, stippling in the half-tones and bare paper in the light. Only the features are outlined, at the face's scale. This replaces uniform stripes.
- **Hokusai:** a finished print. The skin gets two cut tones, key lines are cleaned of specks, the features are inked, and blocks are flatter and cleaner.
- **Gauguin:** the face is painted in three soft warm tones of the sitter's own colour, as one field with no fragments, and inside it only the features are drawn. Proportions hold.
- **Van Gogh:** controlled saturation, impasto kept.
- **Monet:** larger, cleaner broken-colour patches.
- **Cézanne:** stronger colour planes.
- **Leonardo:** less yellow.
- **Klimt:** the face and the hair around it are never covered by ornament, and the gold is brighter.
- **A latent bug fixed:** Dürer and Michelangelo showed black blobs in pure-white areas. A rounding overshoot above 1.0 produced NaN.

**Studio features**
- **Painted on:** the artist's own, linen canvas, fine art paper, watercolour paper, rough canvas, vintage parchment, or smooth illustration board.
- **Background:** the artist's, your scene, dark studio, warm umber, sky, landscape, or gold leaf. A chosen background is composed behind the person before painting, so it is painted in the artist's hand.
- **Frames** (`pp_art_frame`): gilded, baroque gold, black gallery, walnut, or white mat. Each is a lit moulding profile with mitred corners and its material, and it casts its shadow on the painting. Frames apply to the preview too, so what you see is what you save.
- **Before/after:** a draggable divider. With a frame, the photo sits inside the frame's opening.
- **Gallery:** every finished painting of the session, each with Save and Share.
- **Categories:**

  | Category | Artists |
  |---|---|
  | Renaissance | Leonardo, Michelangelo, Raphael |
  | Classical masters | Rembrandt, Vermeer, Velázquez, Titian |
  | Impressionism | Monet, Renoir, Degas, Seurat |
  | Post-Impressionism | Van Gogh, Cézanne, Gauguin |
  | Expressionism & Romanticism | Goya, Turner |
  | Printmaking & decorative art | Dürer, Hokusai, Klimt |
  | Dramatic realism | Caravaggio |

**Face data from the phone.** Every face now passes its box, its mouth opening (inner lip contours) and its eye centres. Contours are measured for every face, not only the most prominent one.

**Neural style transfer.** The review suggested it as an option for some artists. It was not added in this round, for three reasons:
- Fast style-transfer models distort faces, which works against the review's first requirement, identity.
- They add several MB of models.
- They cannot be validated without downloading them.

The traditional engine now meets the review's list. A neural pass could be tried later, styles first and faces protected.

**Test collection** (all 20 artists on each):
- the user's phone selfie: dark skin, flash, closed shiny lips;
- a real studio couple photo: two small faces, smiling;
- a real full-body couple photo: tiny faces, striped clothes;
- a light-skinned portrait;
- a low-light portrait with a lamp in frame;
- a portrait in a busy living room;
- a studio portrait with a wide smile.

A group photo of four or more wasn't available, so that case is still untested.

In these checks, person masks and face points were made by hand on the computer (GrabCut). Where a hand-made mask was wrong, a stray shape appears next to the person. On the phone, ML Kit provides them. The previews stay local.

**Checks**
- 246 engine tests pass, including teeth, intensity, surfaces, backdrops and frames.
- Jest: 37 suites, 408 tests, including the studio flow, the compare slider, the gallery and the frame inset.
- tsc and eslint are clean.
- Gradle: CMake, Kotlin, unit tests and lint pass.
- R8 passes.
- No APK was built.
