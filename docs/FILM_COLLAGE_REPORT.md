# Film & Collage (Photoshoot Studio)

A studio tool that turns photos into a **film strip** or a **collage**. Every frame or cell can be transformed on its own.

## Film strip

There are four styles:

| Style | Look | Plan |
|---|---|---|
| Classic 35mm | Black film, sprocket holes, amber edge markings | Free |
| Photo booth | A white paper strip of portrait frames, the name in the foot | Free |
| Amber negative | The orange base of colour negative film | Premium |
| Cinema | Wide 2.39:1 frames, small holes | Premium |

Strip options:

- vertical or horizontal;
- portrait or landscape frames;
- edge markings on or off (the stock name, plus frame numbers "1 ▶1A" like real film);
- 1 to 12 frames.

## Transformations, per frame or cell

- **Look** (ten): Original, Black & white, Sepia, Vintage, Cool, Punchy, Cross process, Faded, Golden and Noir. Noir also adds a vignette.
- **Zoom** from 1× to 3× in the app (the engine allows up to 4×).
- **Pan**, left/right and up/down.
- **Mirror.**
- **Which photo** the frame shows.

Crops and zooms centre on the photo's face, found with ML Kit on the phone. Without a face, a crop keeps a little more of the top, since faces sit high in photos. Film styles add a fine grain.

**Mix it up** builds a contact sheet: frames cycle through the photos, and each pass varies the look, the crop (a closer frame on the face) and the direction. One photo is enough for a whole strip.

## Collage

- **Layouts** (ten): Side by side, Stacked, One big and two small, Tall and two, Grid of four, One big and three small, Four in a row, Mosaic, Grid of six and Grid of nine.
- **Shapes:** 4:5, 1:1, 9:16 and 3:2.
- **Spacing**, rounded corners, and six background colours.
- The same per-cell transformations as film frames.

## How it works

- **Engine:** `native/engine/src/studio/film.cpp`, through `pp_studio_filmstrip` and `pp_studio_collage`.
  - Photos are sampled bilinearly and cover-fitted into each frame.
  - Rounded corners are anti-aliased.
  - Edge markings use a small built-in 5×7 font, drawn along the strip (rotated on vertical strips).
- **Android:** `FilmCollage.kt`, plus `StudioModule.renderFilm` and `renderCollage`.
  - Photos are decoded within the memory budget: 900 px for previews, up to 2400 px for saves.
  - Faces are found once per photo and cached.
  - Previews go to the cache. Saves go to the studio folder and the gallery, with the Photo Plus mark on the free plan (the clean copy is kept beside it).
- **App:** `FilmCollageScreen.tsx`, opened from the Photoshoot Studio.
  - Film strip and Collage tabs, and a photo tray (gallery, or the in-app camera, which brings the shot straight back).
  - Style and layout options, a frame-by-frame editor, Mix it up, and Save.

## Tests

- **Engine (`studio_tests.cpp`):**
  - strip size and layout, frame looks, the film base, sprocket holes and the booth's paper;
  - mirror, and zoom on the focus;
  - collage cells, the gap and rounded corners;
  - invalid input.
- **Kotlin (`FilmCollageTest`):** request checking and the engine's frame layout.
- **App (`filmCollage.test.tsx`):**
  - Mix it up variety and keeping frames valid when photos are removed;
  - the request shapes;
  - the whole screen: add photos, change style, transform a frame, collage layout, save, and the camera round trip.
