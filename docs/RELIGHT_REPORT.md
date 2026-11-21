# Relight and download sizes (Photoshoot Studio)

## Relight

Relight puts studio light on the person in their own photo, without moving them. It was one of the studio's "coming soon" tools.

1. **Photo:** choose one, or take one with the in-app camera, which brings the shot straight back.
2. **Cut-out:** the person is found and cut out, as in Scene Swap (session, masks, hair edges).
3. **Light:** every studio light preset (soft studio, high key, Rembrandt, split, rim, neon…), with strength, direction, softness and rim light.
4. **Background:**
   - brightness, -100 to +100 (darker gives a studio-backdrop feel);
   - soften, 0-100. The blur leaves the person out, so their colours never halo onto the background;
   - colour, -100 to +100.
5. **Lips:** natural Pink lips, or a Lipstick.
6. **Finish:** a grade, vignette, warmth and tint.
7. **Save** at the download size chosen.

**How it works**

- **Engine:** `pp_studio_relight_compose` in `native/engine/src/studio/relight_compose.cpp`.
  - The background is blurred on a grid of at most 512 px, weighted by the background's share of each pixel (a normalised blur), so the person is left out.
  - Brightness uses an exposure curve that keeps black and white fixed.
  - The relit person is then placed back over the photo with their own alpha, exactly where they were.
- **Android:** `StudioModule.renderRelight` chains the cut-out, lips, light, compose and finish.
- **App:** `src/screens/studio/RelightScreen.tsx`.

**Plans:** Relight is Premium. Previews are free to try; saving needs Premium, as Scene Swap does.

## Download sizes

Every studio save (Scene Swap, Film & Collage and Relight) offers six sizes, measured on the photo's short edge:

| Size | Short edge | Plan |
|---|---|---|
| 360p | 360 px | Free |
| 480p | 480 px | Free |
| 720p | 720 px | Premium |
| 1080p | 1080 px | Premium |
| HD | 1440 px | Premium |
| 4K | 2160 px | Premium |

- **Android:** `ExportSize` checks the size and scales the saved photo with filtering. The long edge never exceeds 8192 px.
- **Plans:** sizes from 720p need Premium (`PREMIUM_REQUIRED` otherwise).
- **App:** `src/screens/studio/DownloadSize.tsx`. A locked size opens Premium. Saves start at 480p on the free plan and 1080p on Premium.

## The studio as a grid

The Photoshoot Studio lists its tools as a grid of small tiles: icon, name, a crown for Premium, and "Soon" for tools not yet built.

- Tapping a tile opens the tool.
- The **i** on each tile, or a long press, shows a tooltip: what the tool does, its plan, and whether it's coming soon.

## Glide is Lively

Glide clips now get the Lively finish, the same as the camera's photos (unless Lively colour is turned off in Settings).

- **Steady, no flicker:** a clip keeps one look state (`pp_lively_state`). Its tone curve, white balance, colour and skin target follow new frames gradually (8% per frame, about a second at 30 fps). The spatial work (smoothing, clarity, the person) still follows every frame.
- **Natural background:** the person gets the full finish and the background a gentle one.
  - The person mask comes from Glide's own (Spotlight, beauty, scene) when it has one.
  - Otherwise it comes from the fast person segmenter on a small copy of each frame.
- **Test (`lively_video_state_keeps_a_clip_steady`):**
  - identical frames give identical results;
  - a frame that suddenly darkens is not lifted all at once.
