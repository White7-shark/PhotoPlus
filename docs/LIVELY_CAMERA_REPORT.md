# Lively colour and the studio camera

## Why

Photos from many Android phones come out flat: skin looks grey or ashy, blacks look washed out, and the light looks dull. A flagship phone shot of the same scene is warm, rich and bright. Before this change the app's **Photo** mode saved the phone camera's JPEG as it was ("Just the shot, untouched"), so it looked flat too.

We compared an Android shot with an iPhone 17 Pro shot of the same person and scene (private photos, not in the repository):

| Measure | Android | iPhone 17 Pro |
|---|---|---|
| Skin colour (chroma, median) | 0.064 | 0.109 |
| Skin colour for its brightness (chroma / luma) | 0.12 | 0.24 |
| Whole-photo colour (mean chroma) | 0.046 | 0.056 |
| Whole-photo median brightness | 0.45 | 0.37 |

The biggest difference is skin colour: the iPhone renders about 1.7× more colour in the skin, at the same hue. It also has deeper blacks and more contrast. The photo is not simply brighter.

## What Lively does (`pp_lively`)

Each step measures first, so a photo that already looks right changes little.

1. **White balance from grey things** (walls, white clothes). Skin keeps two thirds of its warmth when the correction would cool it.
2. **Warmth, only for a cool scene.** Sunsets and lamp light stay as they are.
3. **Tone:** black point, gain toward full range (at most 1.5×), and a mid-tone lift toward 0.42 for a dark photo. The lift is held back when the photo already has bright, well-exposed parts (a low-key shot on purpose). Then a gentle S-curve and a soft highlight shoulder.
4. **Clarity:** mid-size contrast (shapes, not grain).
5. **Skin:** colour brought toward 0.22 × brightness, at most 1.6×, along the skin's own hue. A grey shine (oily or wet skin) takes the colour of the skin around it and is dimmed slightly.
6. **Other colours:** vibrance (muted colours gain the most), scaled down for an already colourful photo.
7. **Neutral shadows** (black clothes) settle deeper. Skin is excluded.

When a pixel is brightened, its colour grows more slowly than its brightness, so lifted skin does not turn orange.

## Smooth and even (second version)

The first version looked rough and patchy up close. Measured on the user's face:

- fine texture rose from 0.031 to 0.041;
- colour blotchiness rose from 0.0033 to 0.0088.

There were four causes:

1. Clarity boosted every fine detail, including grain.
2. The skin colour gain was worked out from each pixel's own noisy colour, so low-colour noise got the largest boost.
3. Skin was detected one pixel at a time.
4. The shadow deepening also decided one pixel at a time.

Now every decision comes from a smooth map of the photo (at most 1600 px; luma, colour and skin weight):

- **Even colour gains:** neighbouring pixels get the same gain, so noise is not boosted into blotches.
- **Clarity on shapes only:** it works from the smooth map against the wide base, so grain is not boosted.
- **Smooth skin surface:** 75% of the skin's fine texture is eased toward its surroundings. A range weight keeps edges sharp (eyes, lips, hair, a beard's outline). Elsewhere, only small grain-sized differences are softened (25%).
- **Colour noise evened out:** each pixel's colour is drawn toward the smooth colour (up to 90%), except across real colour edges.
- **One even skin colour:** skin is drawn 55% toward the person's own median skin hue at the boosted strength. Redder, greyer or purple patches blend into one warm, even tone, and grey shine is dimmed more.
- **Saturated colours protected:** strongly saturated colours with skin-like hues (a red hat, orange clothes, wood) are left alone.

| User's face | Android | First Lively | Smooth and even |
|---|---|---|---|
| Fine texture | 0.031 | 0.041 | 0.034 |
| Colour blotchiness | 0.0033 | 0.0088 | 0.0050 |
| Skin colour | 0.078 | 0.094 | 0.090 |

The remaining texture and blotchiness above the Android original come from the added contrast and the 1.6× richer skin colour. Relative to how strong the colour is, the new version is smoother than the original.

New test `lively_skin_comes_out_smooth_and_even_and_a_red_hat_stays_red`, on a blotchy, grainy face:

- skin hue spread drops from 12.5° to 8.3°;
- roughness drops from 0.0099 to 0.0075, despite the added contrast;
- a saturated red hat keeps its hue and colour.

## Results (engine, `light_demo lively`)

| Photo | Skin boost | Lift | Warmth | Colour room |
|---|---|---|---|---|
| User's Android selfie (flat skin) | 1.60 | 0 | 0.40 | 1.00 |
| Warm, graded studio portrait | 1.00 | 0.30 | 0 | 0.25 |
| The same portrait, made dull | 1.36 | 0.68 | 0 | 1.00 |
| Kodak 04 (colourful) | 1.00 | 0.10 | 0 | 0.25 |
| Long-hair portrait (dull copy) | 1.60 | 0.10 | 0.42 | 1.00 |

Speed: about 0.25 s for 8 MP on one desktop core group.

Tests in `native/engine/tests/enhance_tests.cpp`:

- flat skin gains colour (×1.3 or more) at the same hue (within 8°), and a grey shirt gets darker;
- a dark photo is lifted;
- skin that already has healthy colour gains almost none;
- strength 0 leaves the photo untouched;
- BGRA gives the same result as RGBA;
- invalid input is rejected.

## In the app

- **Photo mode** is still saved straight from the camera at full resolution. Lively then finishes it in place. The phone's own rendering is kept as `*_original.jpg` for before/after. On failure or low memory, the photo stays exactly as captured.
- **Clarity, Moonlight, Ultra Detail and Spotlight** apply Lively after enhancement and Auto Light, before beauty and looks.
- **RAW** is never touched.
- **Settings › Lively colour** is on by default. Off keeps the phone camera's own look (the shot is sent with `lively: 0`).
- Photo mode's description is now "Bright, lively colour in one tap".

## Photoshoot Studio camera

The Scene Swap first step has a **Take a photo** button next to **Choose a photo**:

1. The button opens the app's camera with `returnTo: 'SceneSwap'`, and the camera shows "Photoshoot Studio · your photo opens there next".
2. After the shot, the camera goes straight back to the studio with the photo. Free-plan photos go back without the watermark; exports add it.
3. The studio starts cutting out the person right away. There is no gallery or file picker step.
4. The camera's close button goes back to the studio.

Tests:

- `__tests__/sceneSwap.test.tsx`: the button opens the camera, and a photo that comes back starts the studio once.
- `__tests__/cameraUi.test.tsx`: Lively is sent with every shot and is off when turned off; a shot for the studio goes back with its path; closing goes back to the studio.
