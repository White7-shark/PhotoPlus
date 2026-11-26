# Masters: your photo painted as twenty great artists worked

Masters is a Photoshoot Studio tool. It is Premium; the free plan gets **one finished painting a day**. Previews and the artist cards are free and unlimited.

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
- `ArtAllowance` counts one painting per local calendar day, using the trusted clock (Integrity), so changing the phone's date doesn't reset it. Premium, or Masters redeemed with Rewards points, has no limit.
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

## Words and languages

Every new text was written by hand in English and translated by hand into Spanish, Brazilian Portuguese, French, Arabic, Hindi, Indonesian, Russian and Simplified Chinese. That is about 108 strings, including:
- the artist names as each language writes them (Léonard de Vinci, Miguel Ángel, 葛饰北斋, …);
- the techniques and descriptions;
- the two new FAQ entries;
- the native messages;
- the Rewards tool names.

The source is `tools/i18n/content/masters.py`, which merges into `src/i18n/strings/*.json`. `tools/i18n/build-strings.mjs` then generates `strings.xml` for each language.

The titles of Premium features are now string resources too (`feature_<name>`, all 59), and the Premium sheet shows them in the app's language. Masters' title is translated by hand; the rest are in `values/strings.xml` for Google Play to translate.

## Checks

- Engine: 6 art tests pass. On the host, a 900×960 photo takes about 0.4–4 s, depending on the artist.
- App:
  - `tsc` and `eslint` are clean.
  - Jest: 36 suites, 400 tests. The new `__tests__/masters.test.tsx` covers the catalogue, all 8 translations (with their placeholders), the service, the studio tile, the full screen flow (free painting, then Premium) and Premium without limits.
  - The i18n checks pass: no text left in code, and strings are up to date.
- Android:
  - `buildCMakeDebug`, `compileDebugKotlin`, `testDebugUnitTest` (with the new `ArtAllowanceTest`), `lintDebug` and `minifyReleaseWithR8` pass.
  - No APK was built.
