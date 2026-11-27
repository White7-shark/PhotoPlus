# Pencil Art, the live studio and studio projects

Three additions to the studios:

1. **Pencil Art** (inside Photo Arts): any of the twenty masters draws the
   photo in pencil, with no paint, in HD and above.
2. **The live studio**: after choosing a photo and a master, the blank sheet
   appears with a pencil or brush moving over it. Each movement reveals part
   of the finished work, and the preview screen opens when it's done.
3. **Studio projects**: Scene Swap (Photoshoot), Photo Arts and Photo
   Motion all start from a project. Each project has its own photos and
   its own gallery of finished works, which can be edited again, renamed,
   deleted and downloaded.

## Plans

| | Free | Premium |
| --- | --- | --- |
| Pencil Art previews | Unlimited | Unlimited |
| Finished pencil drawings | 2 a day, each after a rewarded ad | Unlimited, no ads |
| Paintings | 1 a day (unchanged) | Unlimited |
| Signature / branding | Not available | Name, brand or studio line, year; script, serif or print; left, centre or right; size |

Interpretation of the request: it said "for premium users 2 pencil art per
day each unlocked by ads". It is read as 2 a day with ads on the free plan
and unlimited on Premium, the same pattern as Photo Motion.

- **Counting.** The device counts the drawings (`PencilAllowance`), using
  the trusted clock.
- **Ads.** An ad unlocks a drawing only from the ad network's reward
  callback (`AdsModule.showRewardedAdForTool("pencil")`).
- **Refusals.** `renderMasters` refuses a full drawing without a credit,
  with `AD_REQUIRED` or `PREMIUM_REQUIRED`.
- **Shared code.** `AdAllowance` is now shared by Photo Motion and Pencil
  Art.

## Pencil Art engine (`native/engine/src/art/pencil.cpp`)

It follows Lu, Xu and Jia, *Combining Sketch and Tone for Pencil Drawing
Production* (NPAR 2012), with each master's own hand on top.

1. **Lines.** Edge strength is given to the best of 8 directions and drawn
   as short straight strokes along it, so lines overshoot and cross like a
   real hand's. The convolution uses running sums along digital lines: one
   pass per direction, the same cost at any stroke length.
2. **Tone.** Values are histogram-matched to a pencil drawing's tones (bare
   paper, mid-tones, a few darks), then blended with the photo's own values
   so a dark coat stays darker than a face.
3. **Faces.** Each face's range is keyed into a readable band. Dark faces
   open up and light faces stay light, and contrast is never steepened.
   Highlights keep a touch of graphite, and the features keep fine lines.
   Away from the features, faces are blended smooth rather than scratched.
4. **Hatching.** Tone is built from layers of strokes: the master's
   direction first, then crossing layers in the darker values. For the
   masters who drew that way, the hatching turns with the forms in patches
   of straight strokes (12 quantised directions, blended). A blending
   stump's smooth tone is used where the master blended. The hatching is
   calibrated so its local mean matches the tone.
5. **The sheet.**
   - The paper's tooth catches the graphite.
   - Mediums: graphite, charcoal or red chalk.
   - Papers: white, cream, grey toned or tan toned. Toned papers get white
     chalk hatched into the lights.
   - The drawing fades out into bare paper at the edges, as real drawings
     do.
6. **Masters.** Each of the 20 has its own line length and weight, hatch
   angle (Leonardo's left-handed "\"), layers, form-following, smudge,
   contrast, darkness, how much background is drawn, sketchiness, grain,
   feature detail and vignette.

**Controls.** Finish level (Study, Drawing, Masterpiece), transformation
(Natural to Full), line strength and shading.

**Size and memory.**
- Full drawings are 1600, 2048 or 2560 px on the long side (study, drawing,
  masterpiece), capped by the phone's memory budget at about 64 bytes per
  pixel at peak.
- On the build machine, 2340×2560 takes 2.8 s with a peak of 429 MB,
  including the demo tool's own buffers.
- Previews (760 px) take about 0.5 s.

**Blank sheets.** `pencilSheet` is the same paper, pixel for pixel, that a
drawing leaves bare. `paintGround` is each painter's priming: lead white, a
brown imprimatura, plaster or paper.

## The live studio (`native/engine/src/art/reveal.cpp`)

**The stroke plan** is made from the finished work:
- **Drawings:** contour lines traced along the strongest edges, chained as a
  hand would (always on to the nearest next line). Then round patches of
  back-and-forth hatching, darkest first, at the master's hatch angle.
- **Paintings:** a block-in with broad strokes that reveal a softened
  underpainting, from the background inwards. Then strokes following the
  forms, then the finest strokes along the details.

**Stamping.**
- Each stroke stamps the finished work through a soft brush (with bristle
  streaks for paint).
- Stamp positions are fixed along each stroke, so the frames are the same at
  any frame rate.
- The last 8–9% brings in anything left, so the end is the finished work
  pixel for pixel.

**The tool sprite** is drawn by the engine:
- A sharpened yellow pencil (graphite point, wood, faceted body, ferrule,
  eraser), or a round brush carrying the colour it is laying down.
- A shadow that lifts as the tool hops between strokes, a slight sway, and a
  lean into the direction of travel.

**Speed.**
- 7 s for a drawing and 8 s for a painting, with a Skip button.
- On the build machine: 1.6 ms per frame (pencil) and 3.5 ms per frame
  (brush) at 900 px, with an 80–90 ms plan. It runs at the phone's frame
  rate.

**No waiting.** The finished HD work is made at the same time as the
animation plays. The animation uses the quick preview of the same work; if
the HD work isn't ready when the drawing ends, "Finishing touches…" shows
briefly.

**Android.** `ArtRevealView` is a Fabric component (`PhotoPlusArtRevealView`).
It loads the work and its blank on a worker thread, then draws an engine
frame on each `Choreographer` frame.

## The signature (Premium)

`ArtSignature.kt` writes the name, an optional brand or studio line, and the
year into the picture before the frame goes on.
- **Style:** script (`cursive`, slight lean), serif italic, or print
  capitals.
- **Ink:** the medium's own (graphite, charcoal, red chalk or paint).
  Light ink is used on a dark corner.
- **Settings:** it is remembered in Settings (`artSignature`). It is ignored
  on the free plan, even if sent.

## Studio projects

**Database (migration v3).**
- `studio_projects`: studio, name and dates.
- `studio_items`: a photo or a work, title, path, poster, media, source
  photo, and `settings_json`, which holds what made the work so it can be
  edited again.
- Deleting returns the files to remove. A photo's file is kept while a work
  made from it still needs it.

**Screens.**
- **`StudioProjects`** (a studio's projects): new (named, defaulting to
  "Project N"), rename, delete, cover, counts and last update. A photo just
  taken goes into the project chosen for it.
- **`StudioProject`:**
  - *Photos:* add many at once with the new multi-photo picker
    (`pickImages`, up to 30); tap one to create with it; hold to delete.
  - *Gallery:* the finished works, with video posters.
- **`StudioItem`** (a work, and the preview right after the live studio):
  before/after against the photo, size and date, Download (to the phone's
  gallery or videos), Share, Edit again, Rename, Delete, Open gallery.

**The tools.** `SceneSwap`, `Masters` and `Motion` take
`{ photo, projectId, edit }`.
- They redirect to the project list when there's no project.
- A photo picked inside a tool is added to the project's photos.
- Finished works are saved into the project. Photo Motion keeps its
  settings, arrows and pins, and its poster.

## Strings

123 new strings in English, each hand-translated into es, pt-BR, fr, ar,
hi, id, ru and zh-CN (`tools/i18n/content/pencil.py`). They include all 20
masters' drawing manners and two FAQ entries ("What is Pencil Art?", "How
do projects work in the studios?").

## Tests

| Suite | Tests | Covers |
| --- | --- | --- |
| Engine `pencil_tests.cpp` | 4 | graphite has no colour and keeps values; red chalk is one hue; the blank sheet matches the bare paper exactly; Rembrandt's ground is darker than Raphael's; masters differ; a seed repeats; toned paper gets chalk lights |
| Engine `pencil_tests.cpp` (reveal) | 2 | starts blank, only grows, ends exactly on the work, replays identically; pencil and brush |
| Engine `pencil_tests.cpp` (bad input) | — | bad input refused |
| Kotlin | 1 new | the pencil's own two a day; tools found by name |
| Jest `masters.test.tsx` | — | see below |
| Jest `pencilArt.test.tsx` | 5 | options and signature; service mapping; every word in 8 languages; the live studio: preview and HD at once, skip, "finishing touches", saved to the project, opens the preview, brush for paintings, errors |
| Jest `studioProjects.test.tsx` | 7 | the repository on real SQLite; create, rename and delete projects; multi-photo add; open a photo in the studio; a photo just taken; the work screen's download, share, edit again, rename and delete; video download |

The Photo Arts screen tests (`masters.test.tsx`) cover:
- the project list first;
- painting and pencil flows;
- the ad gate: an ad that doesn't finish unlocks nothing;
- Premium with no ads and a signature;
- the signature offered as Premium on the free plan;
- edit again.

Photo Motion and Scene Swap tests now open from a project. All 435 Jest
tests pass.

## Limitations

- **On-device testing.** The live view, multi-photo picker and signature
  fonts are written against current Android APIs but haven't been run on a
  device here. The animation and drawings were validated on the build
  machine with the same engine code.
- **Fonts.** The script signature uses the system's `cursive` font
  (Dancing Script on most phones). Phones without it fall back to the
  default font.
- **Library projects.** Existing photos in the general Projects library are
  not moved into studio projects.
