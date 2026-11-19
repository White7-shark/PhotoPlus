# Phase 11: Professional lighting, studio simulation and advanced effects

**Goal:** take the Photoshoot Studio beyond swapping the background. The person can be lit like a studio portrait, shot against a gradient backdrop, framed with a composition template, and finished with a photographer's grade.

**The honest limit, stated up front:** a photo is flat. It has no depth, no record of where the light came from, and no shape behind the person. Simulated light is therefore an estimate. It aims to look consistent, not to be physically exact. Every look comes with controls to correct it:
- **strength** (eases the light back toward the photo's own);
- **direction**;
- **softness**;
- **reset**.

## What the person can do

| Feature | Where | Plan |
|---|---|---|
| Studio light looks: Soft studio, Bright | Light panel | Free |
| Studio light looks: Rembrandt, Loop, Butterfly, Split, Clamshell, Rim light, Low key, Golden hour, Neon, Film noir | Light panel | Premium |
| Light strength | Light panel | Free |
| Turning the light (direction), softening it, rim light | Light panel | Premium |
| Gradient backdrops (11: studio grey, charcoal, midnight, warm sand, sunset, deep teal, violet night, blush, sky, forest, bright white) | Backgrounds, Colours tab | Free |
| Vignette, warmth, tint | Finish panel | Free |
| Simple grades: Vivid, Soft, Black & white | Finish panel | Free |
| Film looks: Warm film, Teal & orange, Matte, Classic chrome, Noir, Cinema (faded blacks, split toning, grain) | Finish panel | Premium |
| Composition templates: Centre, Close-up | Frame panel | Free |
| Composition templates: Left third, Right third, Editorial, Cover | Frame panel | Premium |
| Colour-temperature matching, colour harmonisation, light wrap, contact and cast shadows, background blur, Spotlight, colour bounce | Adjust panel (from Phase 7, now shared with the looks) | as before |

The free and Premium split is enforced in two places, and a test keeps the two in agreement:
- **The app**: locked choices show a crown and open Premium.
- **The device**: `PREMIUM_REQUIRED` for a Premium look, a changed light or a film finish.

Choosing a look also sets its backdrop, but only on a colour or gradient backdrop. A photo scene is never replaced. Choosing a look sets its finish too (Low key's vignette, Golden hour's warmth), and the person's own vignette and warmth add on top.

## How it works

All three steps live in `native/engine/src/studio/light.cpp`, run on the phone, and are tested in `native/engine/tests/studio_tests.cpp`.

### 1. Studio light on the person (`relight`)

Runs on the cut-out, before it is placed in the scene.

- **Shape from the outline.** The person's outline is "inflated" at two scales: a narrow one for arms and fingers and a wide one for the body. The surface is rounder in the middle and turns away at the edges. Where the face box is known, a face-shaped dome replaces it there, with a ridge down the middle for the nose, so a side light leaves the small shadow beside the nose that Rembrandt and loop light are named for.
- **Lights.**
  - **Key**: any direction around the person and any height above the eyes. Hard light falls off faster across the form; soft light wraps further round it.
  - **Fill**: from the other side, low and near the camera.
  - **Colours**: linear gains, used for gels (Neon's magenta and cyan) and warm sun with cool shade (Golden hour).
- **Relative.** The new light is measured against the person's own average and laid on with some contrast, so their exposure stays. Strength blends back toward the photo, and highlights roll off instead of clipping.
- **Rim light.** A thin band just inside the outline, on the rim light's side, brightest at the edge. It is never drawn along the photo's own border, where the person is cut off rather than lit. The first version lit a wide glowing band (seen in the preview sheet) and was narrowed.

### 2. Gradient backdrops (`paintGradient`)

- **Linear**: top to bottom, or any angle.
- **Radial**: a pool of the first colour behind the person.
- Blended in linear light and dithered below one step of 8-bit colour. The test measures the largest step between rows: 1 level, so there is no banding.
- A gradient counts as lit paper, so its colour bounces gently onto the person, as studio paper does.

### 3. Finish (`finish`)

Runs on the whole photo after compose, the way a lens and film would affect it. Steps, in order:
1. Warmth and tint, as colour gains that keep brightness.
2. Vignette, in light, like a lens. It can also lighten the edges.
3. Saturation, or black and white.
4. An S-curve for contrast.
5. Split toning: one colour in the shadows, another in the highlights.
6. Faded blacks.
7. Film grain at its own size, strongest in the mid-tones.

A transparent cut-out takes the light but no finish: a grade and a vignette belong to a whole photo.

## Speed

On the test machine (desktop, CPU), for a 1086 × 1448 portrait: relight about 55–85 ms, and compose plus gradient plus finish about 250 ms in total. The preview keeps the last lit cut-out, so dragging the person doesn't relight them.

## Wiring

- **C API.** `pp_studio_relight`, `pp_studio_finish_frame` and `pp_studio_gradient`, with the structs `pp_studio_light` and `pp_studio_finish`.
- **Android.**
  - JNI: `nativeStudioRelight` (21 values), `nativeStudioFinish` (17 values; a Kotlin test caught an earlier mismatch with the native side), `nativeStudioGradient`.
  - Kotlin: `SceneLight`, `SceneFinish` and `SceneBackground.Gradient`. Each checks and clamps every value, and knows what is Premium.
  - `StudioModule` lights the cut-out (after hair), composes, then finishes.
- **App.**
  - `src/services/studio/light.ts`: the catalogue of looks, grades, gradients and templates, and the request builders.
  - `src/screens/studio/StudioLightPanel.tsx`: the Light and Finish panels and the template row.
  - New Light and Finish tabs in the Scene Swap screen.
- **Accessibility.** The slider now tells screen readers when it is unavailable, and ignores their adjust gestures while it is (before this it could still be changed that way).

## Tests

- **Engine** (ASan):
  - Light from the left lights the left side (about 1.25 : 1 on a disc) and keeps the average brightness.
  - Strength 0 leaves the cut-out untouched.
  - A red rim on the right reddens the right edge only.
  - Black and white has equal channels.
  - A vignette darkens the corners and leaves the centre.
  - Warmth: redder, less blue, same brightness.
  - Grain changes pixels but not the mean.
  - Gradients are smooth (1-level steps) and radial ones are centred.
  - Bad settings are refused.
- **Kotlin:** light, finish and gradient requests are parsed, clamped and refused when malformed, and what is Premium is classified correctly.
- **Jest:**
  - The catalogue is consistent.
  - The free looks match the device's list exactly, read from the Kotlin source.
  - Every grade's Premium flag matches the device's film rule.
  - Request building is correct, and a transparent cut-out gets no finish.
  - The panels: free looks are chosen, locked ones open Premium, direction is locked on the free plan, and the reset, grade and template controls work.

## Previews

Preview sheets of every look were rendered on the test portraits. The person's photos are private, so the sheets stay in the session's scratchpad and are never committed.
