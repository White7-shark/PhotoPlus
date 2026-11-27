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
| Finished pictures (HD) | 2 a day, each after a rewarded ad | Unlimited, no ads |

This is the same pattern as Photo Motion and Pencil Art.
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
| Ink lines | 0–100% | Fine to bold line art |
| Shading steps | 2–6 | Number of cel bands |
| Colour | Muted to vivid | Colour strength (skin is never pushed orange) |
| Detail | 0–100% | Simple shapes to most detail kept |
| Anime eyes | 0–100% | The person's own eyes to large anime eyes with catchlights |
| Smooth skin | 0–100% | The skin's texture to clean two-tone anime skin |
| Glow | 0–100% | A soft bloom around the lights |

**Previews.**
- Previews are 720 px and follow each change: taps at once, sliders when the
  finger lifts. A newer preview cancels the older one.
- The finished picture is up to 2048 px on the long side, capped by the
  phone's memory.

**Projects.** As in the other studios, Toon Studio starts from a project.
- The finished picture is saved in the project's gallery, titled after its
  style and background (for example "Manga · Anime sky").
- The picture keeps its settings, so "Edit again" reopens the studio with
  the same choices.

## Engine (`native/engine/src/toon/toon.cpp`)

This is a non-photorealistic rendering pipeline, made the way cel
animation is drawn. It is not a filter over the photo, and not a
generative model.

1. **Anime eyes.** A smooth bulge warp centred on each eye, scaled by the
   control.
2. **Flat colour areas.** An iterated, separable bilateral filter in Lab
   (with tabulated range weights). Edges stay sharp while areas go flat.
   The Detail control sets the radius and passes.
3. **Skin.**
   - The skin colour is the face's median colour.
   - Skin is found everywhere on the person (neck, arms, hands) by
     closeness to that colour. Inside the face, only skin-coloured and
     skin-light pixels count, so a beard or hair keeps its own colour.
   - Skin is painted in two tones, a base and one soft-edged shadow, on the
     broad forms.
   - It takes one clean colour. Its saturation is kept when it is
     lightened, and a skin hue keeps a minimum warmth, so dark skin stays
     rich brown and never turns ashy.
4. **Cel shading.**
   - Bands are placed at the image's own value quantiles, with soft steps.
   - The light is lifted, but less on skin.
   - The deepest darks (black hair, a beard, a dark coat) keep close to
     their own depth.
5. **Ink.**
   - XDoG line art from the smoothed light.
   - A feature pass on the photo's own light, so the eyes, brows and mouth
     read even on a light face.
   - A bold outline around the person, taken from the person mask.
   - Skin is kept clean of ink, except for the features.
   - Webtoon inks in a darker shade of the colour beneath; retro inks in
     brown.
6. **Backgrounds.** Painted behind the person through the mask:
   - Skies are gradients with cel-shaded clouds: low-frequency noise
     thresholded with crisp anti-aliased edges and a shaded underside.
   - Sunset and night have their own palettes, and night has stars and a
     moon.
   - Without a mask, the photo's own background is kept.
7. **Style touches.**
   - Manga: screentone (halftone by shadow depth) and pure black-and-white.
   - Comic: Ben-Day dots in the shadows.
   - Glow: a screen blend.
   - Anime eyes: catchlights.
   - Retro: a grade, grain and a vignette.

It is deterministic for a seed, parallel by rows, and cancellable.

**Speed on the build machine.**

| Size | Time |
| --- | --- |
| 1100×1200 | 1.0–1.8 s by style |
| 740×1070 | 0.6–0.8 s |

**Tuning on real photos.** Four test photos, all six styles:
- Dark hair and beards were lifted into grey; they now stay dark.
- A pale palm turned orange; colour boost is now capped on anything
  skin-coloured.
- A low-colour face under cool light turned lavender; skin now keeps its
  warmth.

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
- `ToonAllowance` uses the shared `AdAllowance` class (2 a day).
- `RewardsLedger` and `AdsModule` know the `toon` tool.

## App

- **Screen and data.**
  - `ToonScreen` (route `Toon`).
  - `services/toon/catalog.ts`: styles, backgrounds, clamping.
  - `services/toon/ToonService.ts`.
- **Studio hub.** A "Toon Studio" tile (Premium) and the `toon` studio
  kind for projects. Migration v3's studio check includes `toon`.
- **Feature.** `FEATURES.toon` and `FREE_DAILY_ALLOWANCE.toon = 2`.

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
