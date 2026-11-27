# Photo Motion

Photo Motion turns one still photo into a short video that loops seamlessly.
It is a separate studio from Photo Arts: Photo Arts paints a still picture,
Photo Motion makes the photo move. Everything runs on the phone.

## What the person can do

| Tab | Controls |
| --- | --- |
| **Camera** | 8 camera moves: Still, Push in, Pull out, Orbit, Pan, Dolly zoom, Float (the default), Rise; Strength |
| **Animate** | Motion arrows (drag; a curved stroke becomes a chain of arrows), Hold still pins (tap), Erase (tap), Clear; Motion speed; Keep faces still (on by default); Moving sky with speed and direction (offered only when sky is found) |
| **Effects** | None, Snow, Rain, Bokeh lights, Sparkles, Petals, Fireflies, Light leak, Film, Sun rays; Amount |
| **Video** | Length 3 / 5 / 8 / 10 s; Seamless loop (on) or one move; Full HD, 30 fps, MP4 |

Every change makes a new **preview** (480 px, 12 fps) after a 250 ms pause,
so a burst of taps makes one preview and a newer preview cancels an older
one. **Create video** makes the finished video (up to 1080 px, 30 fps).
Finished videos can be saved to the gallery or shared. They also collect in
a "Your videos" strip, where each one can be played again, saved or shared.
The stage shows small tags for **3D depth** (the depth model ran), **Sky
found**, and **Preview**.

## The plan

| | Free | Premium |
| --- | --- | --- |
| Previews | Unlimited | Unlimited |
| Finished videos | 2 a day, each after a rewarded ad | Unlimited |
| Ads | One rewarded ad per video | None |
| Mark | Small Photo Plus watermark | None |

- **Counted on the device.** `MotionAllowance.kt` counts the videos per day
  using the trusted clock (Integrity), so changing the phone's date doesn't
  give more. A credit is added only from the ad network's reward callback
  (`AdsModule.showRewardedAdForMotion`).
- **What `renderMotion` refuses.** It turns down a full render that has no
  credit, with `AD_REQUIRED` (videos are left today) or `PREMIUM_REQUIRED`
  (the day's two are used). The JS side can't grant a video.
- **Credits.** An ad unlocks one video at a time, never more than are left
  that day. If a render fails after the ad, the credit is kept for the next
  try.
- **Unlimited.** Premium is unlimited, and so is Photo Motion redeemed with
  Rewards points (`motion` is in the Rewards tools).
- **Plan interpretation.** The request said "free plan 2 per day with ads
  and unlimited for free plan". It is read as: 2 a day with ads on the free
  plan, unlimited on Premium.

## How it works (engine: `native/engine/src/motion/motion.cpp`)

1. **Layers.** The person (an ML Kit selfie/subject mask) is a layer of its
   own. The background behind them is filled with a push-pull fill, so a
   camera move reveals background rather than a hole.
   *Sources: 3D Ken Burns; One Shot 3D Photography.*
2. **Depth parallax.** A depth map from the LiteRT depth model makes near
   things move more than far ones. Without one, the depth is estimated from
   the layout: lower is nearer, and the person is forced near. Each output
   pixel is found by a 4-step inverse mapping. A small base zoom keeps the
   frame's edges from showing.
3. **Camera loops.** Every move follows a cosine ping-pong, so frame 0 and
   the last frame meet. The Dolly zoom keeps the person the same size while
   the background's scale changes.
4. **Living motion.** The drawn arrows build a static velocity field
   (Gaussian influence around each segment). A pin holds everything within
   its radius still, with a smooth edge from r to 1.8 r. Every face is
   pinned automatically.
5. **Seamless flow loop.** Two phases of the flow are cross-faded, so the
   last frame equals the first:
   `out = (1 − t)·I(q − V t) + t·I(q + V(1 − t))`.
   *Source: Holynski et al., Eulerian motion fields.*
6. **Sky.** The sky is found as smooth blue/cyan or bright overcast area
   flood-filled from the top edge (purple walls are excluded). It drifts on
   its own when **Moving sky** is on.
7. **Effects.** Particles move a whole number of cycles per loop, so they
   loop too. "Behind" particles (some snow, bokeh, petals) are hidden by the
   person's mask, which places the person inside the scene. Light leak, film
   and sun rays are overlays on top.

Speed: about 30 ms per frame at 640 px on the build host. Preparing a photo
takes 70–100 ms.

## The app

- **Native.**
  - C API: `pp_motion_*` in `pp_engine.h` and `pp_api.cpp`.
  - JNI: `nativeMotionCreate`, `nativeMotionRender`, `nativeMotionSkyShare`,
    `nativeMotionDestroy`.
  - `StudioModule`:
    - `startMotion` decodes at 1280 px, runs the person mask, faces and
      depth, measures the sky, and keeps a session. At most two sessions are
      kept.
    - `renderMotion` encodes H.264 with `GlideEncoder`, crops frames to
      multiples of 16, and watermarks free-plan frames. It writes a poster
      frame. Full videos go to `exports/PhotoMotion_<time>.mp4`. Older
      previews are deleted only after the new one is written.
    - `endMotion` closes a session; `motionAllowance` reports the day's
      count.
- **JS.**
  - `src/services/motion/catalog.ts`: the moves, effects, lengths and
    settings, plus stroke-to-arrows, erase hit-testing and packing.
  - `src/services/motion/MotionService.ts`: the bridge; it checks results
    and maps errors.
  - `src/screens/studio/MotionScreen.tsx` and `MotionCanvas.tsx` (SVG
    arrows and pins).
  - A **Photo Motion** tile in the Studio hub and a `Motion` route.
- **Words.** Every string (104, including the FAQ answer and the device's
  messages) is in English and hand-translated into es, pt-BR, fr, ar, hi,
  id, ru and zh-CN (`tools/i18n/content/motion.py`). "Photo Motion" is a
  brand name and stays as is.

## Tests

| Suite | Tests | Covers |
| --- | --- | --- |
| Engine (`motion_tests.cpp`) | 5 | still and loop return; arrows move and pins hold; parallax (background moves more than the person); effects animate and the sky is found; bad input and cancel |
| Kotlin (`MotionAllowanceTest`) | 3 | two videos a day, each unlocked by an ad; ads never unlock more than are left; another day starts afresh |
| Jest (`__tests__/motion.test.tsx`) | 10 | see below |

The Jest tests cover:

- the catalogue order and names;
- stroke to arrows and erase hit-testing;
- packing limits;
- every word in all 8 languages, with placeholders kept;
- the service's requests, result checks and error codes;
- the ad unlock;
- the studio tile;
- the full free flow: preview → camera → draw → pin → erase → effect →
  length → an ad that doesn't finish unlocks nothing → ad → video → save and
  share → second video → Premium offered at the limit;
- Premium with no ads;
- the device asking for an ad when the screen thought one was unlocked;
- the session closed on leaving.

## Limitations

- **Untested content.** No real photo with sky or water was available, so
  the sky drift and water flow were checked only on synthetic scenes and
  drawn arrows. No group photo of 4+ people was available.
- **Depth only on the phone.** The real depth model runs only on the phone.
  The host previews use the layout estimate, so on-device parallax should
  look better than the previews.
- **Revealed background.** Large camera moves on a person who fills the
  frame show the filled background. It is smooth but has no detail.
  Strength keeps this in check.
- **Progress.** The full render shows an indeterminate progress indicator;
  the studio module has no progress events.

## Research

- [Best photo animation apps (CyberLink)](https://www.cyberlink.com/blog/photo-editing-best-software/429/best-photo-animation-app): the feature set of current apps, including camera moves, motion arrows and anchors, sky and overlays.
- [Apps for photo animation (Filmora)](https://filmora.wondershare.com/ai-generation/app-for-photo-animation.html)
- [3D Ken Burns Effect from a Single Image (Niklaus et al.)](https://arxiv.org/pdf/1909.05483): depth-based camera paths and filling what a move reveals.
- [One Shot 3D Photography (Kopf et al.)](https://ar5iv.labs.arxiv.org/html/2008.12298): layered depth with inpainting, on mobile.
- [Animating Pictures with Eulerian Motion Fields (Holynski et al., CVPR 2021)](https://openaccess.thecvf.com/content/CVPR2021/papers/Holynski_Animating_Pictures_With_Eulerian_Motion_Fields_CVPR_2021_paper.pdf): a static flow field for fluid motion, looped by symmetric splatting. Here, two cross-faded phases.
- [Fast computation of seamless video loops (Liao, Finch, Hoppe)](https://www.hhoppe.com/fastloops.pdf): what makes a loop read as seamless.
