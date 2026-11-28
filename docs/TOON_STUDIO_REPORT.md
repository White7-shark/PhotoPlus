# Toon Studio and the live studio's speed

Two additions:

1. **Toon Studio**: a new studio that turns a photo into anime or a
   cartoon, with full controls. It sits next to Photo Motion, which stays
   as it is.
2. **Speed control in the live studio**: a slider for how fast the master
   draws or paints.

## Plans

| | Free | Premium |
| --- | --- | --- |
| Previews and style cards | Unlimited | Unlimited |
| Finished pictures (HD) | 5 a day, each after a rewarded ad, with the app's watermark | Unlimited, no ads, no mark |

This is the same pattern as Photo Motion and Photo Arts. Offline, ads are owed
(see `FREE_PLAN_ADS_ONBOARDING_REPORT.md`).
- **Counting.** The device counts pictures (`ToonAllowance`) on the trusted
  clock.
- **Ads.** An ad unlocks a picture only from the ad network's reward
  callback (`showRewardedAdForTool("toon")`).
- **Refusals.** `renderToon` refuses a full picture with no credit, with
  `AD_REQUIRED` or `PREMIUM_REQUIRED`. The screen then opens the ad sheet
  or the Premium sheet.

## What the person controls

**Style.** Each style has its own card, showing the person's own photo in
that style.

| Style | Look |
| --- | --- |
| Anime | Cel-shaded, bright and warm, sparkling eyes |
| Manga | Black ink and screentone on white paper |
| Comic | Bold ink, punchy colour, halftone dots in the shadows |
| Cartoon | Very flat colour, thick friendly outlines |
| Webtoon | Soft shading, coloured lines, pastel glow |
| Retro 90s | Muted cel colour, brown lines, film grain |

**Background.**
- Your scene: the photo's own background, drawn.
- Soft focus.
- Anime sky: cel-shaded cumulus clouds.
- Sunset.
- Starry night: a moon and stars.
- Pastel.

**Fine-tune.** Choosing a style sets every control to that style's own
values. "Back to the style's own look" resets them; the background stays.

| Control | Range | What it does |
| --- | --- | --- |
| Strength | 0–100% | How much of the photo shows through (blended) |
| Line thickness | 0–100% | Fine to bold line art |
| Shading steps | 2–6 | Number of cel bands |
| Colour | Muted to vivid | Colour strength (skin is never pushed orange) |
| Detail | 0–100% | Simple shapes to most detail kept |
| Anime eyes | 0–100% | The person's own eyes to large anime eyes with catchlights |
| Smooth skin | 0–100% | The skin's texture to clean two-tone anime skin |

**More controls** (folded away under "More controls"; all start in the middle,
which is the style's own look, and reset with it):

| Control | Range | What it does |
| --- | --- | --- |
| Outline | 0–100% | No outline round the person to a bold one |
| Background | 0–100% | Sharp drawn scene to a soft, far background |
| Brightness | Darker to brighter | The light of the whole picture |
| Warmth | Cooler to warmer | Blue to golden light |
| Texture | 0–100% | Clean flat colour to more shading, grain and screentone |
| Face lines | 0–100% | Fewer to more lines on the face |
| Hair lines | 0–100% | Fewer to more strands drawn in the hair |
| Ink | Style / Black / Brown / Colour | The colour of the line art |
| Glow | 0–100% | A soft bloom around the lights |

**Previews.**
- Previews are 1024 px and follow each change: taps at once, sliders when the
  finger lifts. A newer preview cancels the older one.
- The finished picture is the photo's full size, capped by the phone's
  memory.

**Projects.** As in the other studios, Toon Studio starts from a project.
- The finished picture is saved in the project's gallery, titled after its
  style and background (for example "Manga · Anime sky").
- The picture keeps its settings, so "Edit again" reopens the studio with
  the same choices.

## Engine v2 (`native/engine/src/toon/`)

The first version flattened the photo and traced it, which failed on real
photos:
- Edge detection turned eyes, nostrils and lips into black blobs.
- Dark skin went grey or purple, and low-light noise was posterised.
- Hair merged into the background.
- The outline sat off the colour.
- The styles looked alike.
- Halftone dots covered faces.

Version 2 is a rework, organised as:
- `toon.cpp`: the pipeline;
- `filters.cpp`: the image tools;
- `presets.hpp`: one table that holds every style's look.

### Pipeline

1. **Clean-up.**
   - Sensor noise is measured (Immerkær's estimate) and removed before
     anything is drawn: lightness lightly, with a guided filter; colour
     strongly, with a guided filter steered by lightness (computed at half
     size).
   - Low light is then brought up: an exposure curve on lightness, plus
     CLAHE (contrast-limited adaptive histogram equalisation, on the Lab
     lightness channel). It is strong when the photo is dark and light
     otherwise, and colour keeps its saturation as it brightens.
   - It is equivalent to OpenCV's non-local-means denoise and CLAHE, but
     written in the engine: no new dependency.
2. **Where things are.**
   - **Person:** ML Kit's subject mask, refined to the photo's own edges by
     a guided filter whose radius covers the mask's coarseness. The colour
     and the outline both come from this one refined mask, so they always
     meet.
   - **Faces:** ML Kit's contour points: oval, brows, eyes, lips and nose.
   - **Skin:** found by the face's own colour (beard, shine and features
     excluded).
   - **Hair:** the area above and around the face, inside the person,
     excluding face and skin.
3. **Shapes.**
   - Edge-preserving smoothing per style: multi-pass bilateral (Anime,
     Manga, Retro), Kuwahara (Comic, Cartoon) or guided filter (Webtoon).
   - It works at up to about 1100 px on the short side. For a larger
     export, it is brought back up guided by the full-size lightness, so
     edges stay crisp.
   - The hair's local contrast is raised first, so it keeps its shape and
     strands.
   - The kept background is blurred harder than the person, without the
     person's colours bleeding into it.
4. **Colour.**
   - **Palettes:** k-means in Lab, one for the person (5–14 colours by
     style, ± shading and detail) and one for the background (4–8, with
     soft boundaries, so smooth bokeh never becomes blotches).
   - **No hue hijack:** a palette colour can't be much more colourful than
     the pixel it replaces, and dark pixels keep their own hue (dark hair
     stays hair-coloured, not the jeans' blue).
   - **Skin tones:** shadow, base and highlight are made from the face's
     own median colour, hue-preserving. A skin hue keeps a minimum warmth,
     so it is never ashy.
   - **Shine:** highlights are clamped a few units above the base, so shine
     never turns into a white patch (the "moustache").
   - Skin and skin-coloured rims are never colour-boosted.
5. **The face, drawn from its contours.**
   - **Eyes:** an eye white, and an iris placed where the photo is darkest
     inside the eye, in its own colour (lightness at least 20). The pupil
     is a deep shade of the iris, never a black hole. Catchlights, a lash
     line that tapers towards the inner corner, and a light lower lid.
   - **Brows:** filled in their own colour, so brows and makeup stay.
   - **Nose:** a hint only: the nostrils' small curves, never filled.
   - **Lips:** two tones, a touch deeper than the skin. A closed mouth gets
     the mouth line; an open mouth gets a mouth colour and teeth (not pure
     white). A short shadow line under the lower lip.
   - **Without contours** (a face known only by its box), there's a thin,
     gated line pass in the feature zones instead.
   - All of it is anti-aliased: polygons are filled by supersampled
     scanlines; strokes are capsules with tapers.
6. **Line art.**
   - **XDoG on the smoothed lightness**, gated to real edges: the middle of
     a small dark spot gets a ring, not a fill.
   - **Width** scales with the image and the Line thickness control.
   - **Specks removed:** connected pieces below an area threshold are
     dropped (they're stricter in the background).
   - **Rounding:** Cartoon rounds its lines into smooth, even strokes.
   - **Faces:** no generic ink inside a face drawn from contours.
   - **Hair and background:** strands are kept in the hair at the style's
     weight; the background has few lines.
   - **Outline:** from the refined mask, at the style's width, on the
     colour's own edge.
7. **Finish.**
   - Manga screentone and Comic Ben-Day dots go only in shadows and the
     background, never on lit skin or a face.
   - Bloom comes from lights, not from skin (no orange haze past the
     outline).
   - Webtoon has a soft glow on the skin.
   - Retro has its period palette, cel shading, film grain, a slight
     colour fringe, VHS warmth and scanlines.
8. **Strength.** The picture is blended with the original photo (0 gives
   the photo, 1 the full drawing).

### The styles (`presets.hpp`)

| Style | Colour | Lines | Shading and finish | Background |
| --- | --- | --- | --- | --- |
| Anime | 12 colours, +vibrance | thin, brown | soft 3-tone cel, catchlights, slight bloom | blurred and quieter |
| Manga | grey | strong black | screentone in shadows only; white skin, line-only features | screentone and speed lines |
| Comic | 5 punchy colours, high contrast | bold black | Ben-Day dots in shadows, warm lights and cool shadows | quieter |
| Cartoon | 6 very flat bright colours | thick, rounded | flat 2-tone | simplified |
| Webtoon | soft gradients, pastel | thin, coloured | gentle skin glow | very soft |
| Retro 90s | 24-colour cel-paint palette | brown | hard cel, grain, colour fringe, VHS warmth | quieter |

### Engines (Android, `studio/toon/ToonEngine.kt`)

- `ToonEngine`: `isAvailable()` and `render(input, token)`.
- `ClassicToonEngine`: the native engine above. It covers every style and
  control, on every device.
- `ModelToonEngine`: a learned image-to-image model (ToonNet), run by the app's LiteRT runtime on the
  GPU, else the CPU.
  - It sits behind `BuildConfig.TOON_MODEL_ENGINE`, which is false.
  - **It is Photo Plus's own model, ToonNet** (`tools/toon_model`,
    `TOON_MODEL_REPORT.md`): one network for every style and control, which
    also reads the person mask. No published cartoon weights were found
    clearly free for commercial use. Until ToonNet is trained on licensed
    data and bundled, the flag stays off.
- `CloudToonEngine`: an interface for a future server img2img backend
  (line-art ControlNet with identity preservation). `NoCloudToonEngine`
  says it isn't available. Uploading a person's photo would first need
  consent, a privacy notice and a reviewed backend.
- **Selection:** `ToonEngines.pick` uses the model only when its flag is on
  and the model is present. A model failure falls back to the classic
  engine; a cancel does not.

**Plumbing.**
- `pp_toon_render_ex` takes the strength setting and the faces' contour
  points (131 per face, in ML Kit's order). `pp_toon_render` still works
  and calls it.
- `PortraitProcessor.personFacesAndContours` returns the mask, faces and
  contours, found once per photo and cached.

**Sizes.**
- Previews are 1024 px (they were 720).
- The finished picture is the photo's full size, as far as memory allows.
  Peak memory is about 120 bytes per pixel (1.13 GB measured at 9 MP), so
  the cap is `toonEdgeFor(engineBytes)`, between 1600 and 6144 px.

## Results

Test photos (at about 1024 px):
- a light-skin studio portrait;
- a dark-skin, low-light selfie with a cluttered background;
- a two-person photo;
- a low-light portrait with an afro and bokeh.

Person masks and face contours came from MediaPipe models on the build
machine, standing in for ML Kit on the phone.

| Check | Before | After |
| --- | --- | --- |
| Skin hue shift (worst colour style) | 24° (Comic, couple) | ≤ 4.7°; 9.4° for Retro (period palette) |
| Skin chroma kept (worst) | 0.60 | 0.65 (Retro, faded by design); others ≥ 0.82 |
| Near-black area inside eyes and lips (worst colour style) | up to 1.00 (eyes filled black) | ≤ 0.08; Comic on small faces 0.30 (lash line) |
| Hair separated from the background (vs the photo) | 0.12–0.97 | 0.75–3.2 |
| Most similar pair of styles (mean ΔE on the person) | 3.1–7.9 | 5.5–11.4 |
| Preview time, 4 cores (mean / max) | 0.50 / 0.84 s | 0.49 / 0.64 s |
| Preview time, 1 core (mean / max) | 1.42 / 2.24 s | 1.09 / 1.51 s |
| Export, 9 MP, 4 cores | — | 6.5 s, 1.13 GB peak |

Manga is grey by design; it keeps a small solid pupil (a manga
convention).

Phone timing hasn't been measured. A mid-range phone's big cores are
usually somewhat slower than this machine's; the single-core time (1.5 s
worst case) suggests a preview under 2 s with 2–4 cores.

**Interface.**
- **Strength** slider (0–100%), and Ink lines renamed **Line thickness**.
- **Preview cache:** the last 12 previews per photo are cached, so settings
  seen before return at once (the phone keeps as many preview files).
- **Labels:** style tiles show their name on a band inside the tile, so it
  is never clipped.
- **Compare:** the result screen already has its before/after slider.

## Tests

- **Engine `toon_tests.cpp`:** 12 tests.
  - Five earlier tests, run through the old API, which still works.
  - Seven new tests:
    - eyes from contours have whites and no black blobs, brows and lips
      are kept, and chin shine is no white patch;
    - skin hue holds for light and dark skin, in normal and low light;
    - the outline lands on the person's edge with a quarter-size mask
      offset by 4 px;
    - no dots on lit skin, and noise is flattened;
    - Strength 0 gives the photo, and 0.5 lies between;
    - every pair of styles differs;
    - the new API refuses bad input.
- **Kotlin `ToonEngineTest`:** contour encoding and its fallbacks, engine
  choice by flag and availability, model-to-classic fallback (but not on a
  cancel), no cloud engine.
- **Jest `toon.test.tsx`:** Strength kept across styles and clamped, the
  Strength slider, cached previews (no new render), and the saved picture
  keeping its Strength.

## Limitations (v2)

- **Not generative.** The anime look is drawn by the pipeline, faithful to
  the person; it isn't redrawn by a model. The model path is ready, but
  needs a licensed model.
- **On-device testing.** It hasn't been run on a phone here. ML Kit's
  contours and masks were stood in for by MediaPipe on the build machine
  (contours in the same layout).
- **No real edge.** Where a person's edge has almost no contrast with the
  scene, the refined outline follows the mask as given.
- **Progress.** Shown as a spinner with "Drawing…" / "Making it in HD…".
  The engine's progress isn't reported as a percentage.

## The live studio's speed

A **Drawing speed** or **Painting speed** slider (0.5× to 2.5×) appears in
two places:
- Under the create button in Photo Arts.
- On the live studio screen while the master works.

It is remembered in Settings (`artLiveSpeed`).
- 1× is the original pace: 7 s for a drawing and 8 s for a painting.
- 2× halves it, and 0.5× doubles it.
- Moving the slider mid-drawing keeps the progress already made
  (`ArtRevealView.durationMs` rescales the elapsed time). The pencil speeds
  up or slows down without jumping.

## Android

- `StudioModule.renderToon` (sizes, allowance gate, person mask and faces
  from the shared `artSubjects` cache, encoding) and
  `StudioModule.toonAllowance`.
- `NativeEngine.nativeToonRender`, which wraps `pp_toon_render`.
- `ToonAllowance` uses the shared `AdAllowance` class (5 a day, owed ads offline).
- `RewardsLedger` and `AdsModule` know the `toon` tool.

## App

- **Screen and data.**
  - `ToonScreen` (route `Toon`).
  - `services/toon/catalog.ts`: styles, backgrounds, clamping.
  - `services/toon/ToonService.ts`.
- **Studio hub.** A "Toon Studio" tile (Premium) and the `toon` studio
  kind for projects. Migration v3's studio check includes `toon`.
- **Feature.** `FEATURES.toon` and `FREE_DAILY_ALLOWANCE.toon = 5`.

## Strings

62 new strings in English, each hand-translated into es, pt-BR, fr, ar,
hi, id, ru and zh-CN (`tools/i18n/content/toon.py`). They include:
- the six styles, each with its description;
- the six backgrounds;
- the native refusals;
- the speed labels;
- a FAQ entry, "What is Toon Studio?".

## Tests

| Suite | Tests | Covers |
| --- | --- | --- |
| Engine `toon_tests.cpp` | 5 | texture removed and a shirt edge inked; manga is mono; eyes grow with a catchlight; sky behind but not over the face, and the background kept without a mask; styles differ and a seed repeats; bad input refused |
| Jest `toon.test.tsx` | 10 | see below |

`toon.test.tsx` covers:
- the catalogue and its clamping;
- the service: its mapping, the checks on what comes back, the allowance
  and the ad;
- the project redirect;
- previews, the style cards, the background, every slider, shading steps
  and reset;
- an ad that doesn't finish unlocking nothing, then a free picture saved
  to the project;
- Premium offered when today's two are used;
- unlimited on Premium, and edit again;
- `AD_REQUIRED` from the device;
- the failure message;
- every word in 8 languages;
- the live studio speed: 2× halves the duration, 0.5× doubles it, and the
  speed is remembered.

**Results.**
- All 445 Jest tests pass.
- Kotlin unit tests, Android lint and R8 pass.

## Limitations

- **Not generative.** There is no ML image-to-image model; the anime look
  comes from the rendering pipeline. Faces keep the person's own features
  and proportions (apart from the eye control). It is not redrawn in a
  "generated" anime face. This keeps it on-device, fast and faithful to the
  person.
- **The person mask.** Background replacement depends on the person mask.
  On the build machine, the test masks are rough, so some previews show
  clouds cutting into a ruffled dress. On a phone, the mask comes from ML
  Kit's subject segmentation, as in the other studios.
- **On-device testing.** The screen and native module are written against
  current Android APIs but haven't been run on a device here. The pictures
  were validated on the build machine with the same engine code.
