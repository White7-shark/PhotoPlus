# Phase 6 — Pose Detection and Photography Assistant

An offline photography assistant in the camera. It helps people place themselves and compose the photo, follow a professional pose, or copy the pose from any photo they choose.

- **Free:** the basics.
- **Premium:** live matching, scoring, guidance and automatic capture.

Everything runs on the phone, with no network at any point:

- Detection uses MediaPipe Pose Landmarker models bundled in the app.
- Description, matching and advice are done by the engine (`native/engine/src/pose`).
- The words come from the app.

## What the user gets

| Feature | Plan | Where |
|---|---|---|
| Composition grids: thirds, golden ratio, centre cross, diagonals | Free | Camera → assistant chip → Grid |
| Level indicator: a line that turns green when level, plus "Turn the phone a little to the left/right" | Free | Assistant → Level |
| Face positioning: eyes near the upper third, room above the head, away from the edges | Free | Assistant → Face guide |
| Simple pose demonstrations: 6 poses (Classic stand, Hand on hip, Arms crossed, Chin rest, Over the shoulder, Relaxed lean), each with its outline and tips | Free | Assistant → Poses |
| Basic photography tips, rotated in the coach | Free | Coach panel |
| Full pose library: 25 poses in 5 groups (standing, portrait, seated, hands, playful), each with tips | Premium | Assistant → Poses |
| **A pose from my photo:** pick any photo; the person's pose is detected and becomes a pose to follow (up to 30 kept) | Premium | Assistant → "Use a pose from my photo" |
| Live silhouette: the reference pose drawn over the preview, carried onto the person's own position and size | Premium | Preview |
| Landmark alignment: arrows from each hand, elbow, knee or foot to where the reference has it | Premium | Preview |
| Pose-match score, 0–100, in a ring | Premium | Coach |
| Advice in plain words, framing first, at most two at a time. For example: "Raise your left hand", "Step back a little", "Turn slightly to your right", "Tilt your head a little to your left", "Aim the camera a little lower". Camera-distance and body-position advice is part of this. | Premium | Coach |
| Guided capture, one step at a time: Frame yourself → Match the pose → Hold still → Ready | Premium | Assistant → Guided |
| Automatic capture: the photo is taken once the score has stayed above the chosen match level, with the person still, for the chosen time. Match level: Relaxed 70, Close 80 or Exact 90. Time: a moment, 1 second or 2 seconds. Optional and off by default. | Premium | Assistant → Take the photo for me |
| Mirror the pose (the other hand, the other side) | Premium | Assistant |

Sides are always the user's own. On a mirrored selfie preview, the model's "left hand" label is the user's right hand, and the app says "your right hand". With the rear camera, the screen's left is the subject's right. `src/services/pose/guidance.ts` handles both cases, and the tests check them.

## Research

| Source | What we took |
|---|---|
| Google AI Edge, *Pose landmark detection guide for Android* (developers.google.com/edge/mediapipe/solutions/vision/pose_landmarker/android) | `PoseLandmarker` with `RunningMode.LIVE_STREAM` and `detectAsync` for the camera, and `RunningMode.IMAGE` for photos. GPU delegate with a CPU fallback. Results are normalised landmarks with `visibility` and `presence`. |
| Bazarevsky et al., *BlazePose: On-device Real-time Body Pose Tracking* (CVPR Workshops 2020), and the MediaPipe Pose model card | The 33-landmark topology. Lite vs Full trade-offs (we use Lite for live, Full for photos). Apache-2.0 licence. Landmarks outside the frame are still predicted; `presence` says whether they are in the picture. |
| Google Maven `com.google.mediapipe:tasks-vision` | 1.2.3, the latest stable release (checked against `maven-metadata.xml`). |
| Real data: the user's video and photos, and the pose photos sent earlier, run through the same models on the desktop (MediaPipe Python, same `.task` files) | <ul><li>Calibration: out-of-frame guesses come with *high* visibility but *low* presence, so reliability needs both.</li><li>Body turn from depth (`z`) alone was too noisy; the torso's shoulder width relative to its height is steady.</li><li>Hidden parts must lower coverage, not the score.</li></ul> |
| Classic composition guidance (rule of thirds, phi grid, head room, eyes on the upper third) | The free grids and face guide. |

### Why MediaPipe here (and not ML Kit pose detection)

- **ML Kit:** its pose detection is still a beta artifact.
- **MediaPipe Tasks Vision:** stable at 1.2.3. It also returns `presence`, which we need to tell an out-of-frame guess from a hidden limb.
- **Package size:** the transitive LLM runtime (`litertlm-android`) is excluded, because only `ImageEmbedder` uses it.
- **Models:** fetched at build time and verified by SHA-256 before packaging (`fetchPoseModelLite` / `fetchPoseModelFull`). They are stored uncompressed (`noCompress "task"`) so they can be memory-mapped.

| Model | Version | Size | SHA-256 |
|---|---|---|---|
| `pose_landmarker_lite.task` (live) | float16 / 1 | 5.8 MB | `59929e1d…690d574a` |
| `pose_landmarker_full.task` (photos) | float16 / 1 | 9.4 MB | `5134a3aa…a839011b1` |

## How it works

```
camera frame (analysis stream) ── upright crop, ~15 fps ── PoseLandmarker (Lite, LIVE_STREAM, GPU→CPU)
        │                                                        │ 33 × (x, y, z, visibility, presence)
        │                                         mirrored selfie → pose::mirror (flip + left/right swap)
        ▼                                                        ▼
LevelSensor (gravity, 10 Hz) ── "PhotoPlusLevel"      pose::Tracker (holds hidden points 0.8 s, stillness)
                                                                 ▼
                                    pose::describe → pose::match(reference) → advice
                                                                 ▼
                                         "PhotoPlusPose" → coach, silhouette, arrows, auto capture (JS)

photo the user picks ── ImageDecoder (≤ 1280 px) ── PoseLandmarker (Full, IMAGE) ── describe ── "My pose N"
```

### Representation (`pose::describe`)

- **Reliability** of each landmark:
  `weight × smoothstep(0.4, 0.75, presence) × smoothstep(0.35, 0.7, visibility) × in-frame`.
  - `weight` is below 1 only while the tracker is holding a hidden point.
  - Separately, `inPicture` (presence × in-frame) says whether a part is in the photo at all, covered or not. The shot type uses it, so a hand over the shoulders does not turn a head-and-shoulders shot into a face shot.
- **Normalised coordinates:**
  - Anchor: the shoulder centre, or the nose plus a typical neck length when the shoulders are not seen.
  - Unit (scale): the 75th percentile of the reliable lengths, each divided by its typical share of a shoulder width. This works whatever part of the body is seen.
  - So the description does not depend on where the person stands or how big they are.
- **Joint angles:** 12 bone directions (upper arms, forearms, thighs, shins, torso sides, neck/head), and elbow and knee bends.
- **Body orientation:**
  - Body turn comes from the shoulder width relative to the torso height, with its sign from depth.
  - Head turn comes from the nose between the ears or eyes.
  - Head tilt comes from the eye line.
- **Confidence:** a reliability for every bone, plus the body and head readings.
- **Shot:** face, head and shoulders, half body, three-quarter, or full body. Also the extent of the body in the picture.

### Matching (`pose::match`)

- **Pose score:** a geometric mean of agreement, part by part, over:
  - the bone directions;
  - the wrist positions relative to the body;
  - the body turn and the head turn.
  
  Each part is weighted by how reliable it is in *both* poses. Each penalty is capped, so one wild part cannot zero the score.
- **Coverage:** how much of what the reference shows is seen now. The total is `base × (0.35 + 0.65 × coverage)`, so a view without the hands cannot fully match a pose about the hands.
- **Framing score:** the offset and size compared with the reference. It is 20 % of the total when framing is on.
- **Advice:** ranked candidates, framing first, at most four:
  - for parts: up, down, in, out, show;
  - for the body: closer, step back, move left or right, aim higher or lower, turn, lean, level the shoulders;
  - for the head: tilt, look;
  - then hold still / perfect.

### Tracking and occlusion (`pose::Tracker`)

- **A hidden landmark:**
  - It is held at its last position for 0.8 s, moving with the body.
  - Its weight fades during that time, so guidance does not flicker when a hand passes in front of the face.
  - After the hold it is dropped, and the app asks to "Show your left hand" if the reference needs it.
- **A lost person** (a frame with nobody): the last pose fades out over the same 0.8 s.
- **Across the C ABI** (to the app and back), a held point's lowered trust is carried in its visibility only. The value is set with the exact inverse of the smoothstep, so the reliability read back is the held one, and presence still says the part is in the picture.
- **Stillness:** measured from the body's speed in shoulder widths per second. Automatic capture needs it.

## Validation

| Test | Result |
|---|---|
| **Engine unit tests** (`tests/pose_tests.cpp`, 8 tests, under AddressSanitizer) | <ul><li>Reliability rejects guessed and hidden points.</li><li>The description does not change with position or size.</li><li>The same pose scores ≥ 95 and says "perfect".</li><li>A lowered arm scores < 65, with "Raise …".</li><li>Framing, tilt and turn each give the right advice.</li><li>Hidden parts lower coverage, not the pose score, and are asked for.</li><li>Covered legs or shoulders keep the shot, so no false "step back", including while the tracker holds them.</li><li>The tracker holds a hand for 300 ms, lets go after 1.3 s, fades a lost person, and measures stillness.</li><li>Mirroring works.</li><li>Bad input is refused.</li></ul> The whole engine suite passes. |
| **Your 5.5-second video against your own photo as the reference** (same lite model as the phone, every frame) | <ul><li>Mean score 62 (range 44–76).</li><li>The advice is the real difference: move a little to one side, aim lower, tilt the head, level the shoulders.</li></ul> |
| **The same video against a chin-rest photo** (another person, hand to chin) | <ul><li>Mean score 23 (17–40).</li><li>Asks to show the hand, step back and move across.</li><li>Correctly much lower than your own pose.</li></ul> |
| **Against a cheek-rest photo** | Mean score 16 (10–32). |
| **Library ranking from real photos** (each uploaded photo's detected pose against all 25 library poses) | <ul><li>The chin-on-hand photo ranks Chin rest and Cheek rest first.</li><li>The cheek-on-hand photo ranks Cheek rest first.</li></ul> |
| **Occlusion** (your video, with the mouth, ears and shoulders covered for 1.3 s, then the person lost for 0.2 s) | <ul><li>While covered: the mean score is 57 vs 64 with them seen. Coverage drops to 0.68, the shot stays head-and-shoulders, and the advice stays the same framing advice.</li><li>While lost: the score holds (60) instead of dropping to zero.</li><li>Afterwards it matches the clean run exactly.</li></ul> |
| **Detected poses in your uploaded photos** (Full model, IMAGE mode) | Pose found in every photo with a person, including tight portraits where only the face and shoulders are in shot. |
| **Kotlin unit tests** (`PoseDataTest`: event mapping, sides, level maths; plus all existing camera tests) | 164 tests pass. Android Lint: no issues. |
| **Jest** (`pose.test.ts` with 14 tests, `poseAssistant.test.tsx` with 4 screen tests) | All 274 tests pass, and ESLint is clean. Covered: <ul><li>library integrity;</li><li>premium gating;</li><li>mirroring;</li><li>plain wording of every action and side;</li><li>reference-on-person placement;</li><li>face and level tips;</li><li>automatic capture firing once after the hold and resetting on any slip;</li><li>guided steps;</li><li>a photo becoming "My pose N";</li><li>the camera screen sending the reference to the camera, the coach's words, and automatic capture taking the photo.</li></ul> |

The previews made from your video and photos stay private. They are sent to you directly and are not in the repository.

## App wiring

| Layer | What |
|---|---|
| Engine | <ul><li>`include/photoplus/pose.hpp` and `src/pose/pose.cpp`.</li><li>C ABI: `pp_pose_mirror`, `pp_pose_describe`, `pp_pose_match`, `pp_pose_tracker_create/update/destroy`.</li><li>`tools/pose_demo` replays landmark files.</li></ul> |
| JNI | `nativePoseTrackerCreate/Destroy`, `nativePoseStep` (track, describe, match, advice and the steady points in one call), `nativePoseDescribe` |
| Kotlin (`com.photoplus.pose`) | <ul><li>`PoseEstimator`: models, GPU→CPU fallback, photo analysis.</li><li>`PoseAnalyzer`: upright crop, ~15 fps, async results, restart on error.</li><li>`LevelSensor`.</li><li>`PoseData`: event payloads, sides.</li><li>`CameraController`: the analyzer shares the analysis stream with face tracking and focus peaking; `setPoseGuide`; `setLevel`.</li><li>`CameraModule`: `setPoseGuide`, `setLevel`, `analyzePosePhoto`.</li></ul> |
| JS | <ul><li>`src/services/pose/`: library (25 poses from `scripts/pose-library.py`), guidance (words, placement, tips, guided and automatic capture), settings, fromPhoto.</li><li>`src/screens/camera/pose/`: `PoseOverlay` (grids, level line, silhouette, arrows), `PoseCoach`, `PoseSheet`, `usePoseAssistant`.</li></ul> |
| Plans | <ul><li>`poseBasic` (free): grids, level, face guide, 6 demonstrations, tips.</li><li>`poseGuidance` (Premium): everything else.</li><li>A free user who picks a pose sees its outline and tips, with an offer to see how well they match.</li></ul> |
| Storage | Photos chosen for "My pose" are copied into the app's `poses` area. Their landmarks are kept in settings. |

## Limits

- One person at a time: the model is asked for a single pose, so in a group it follows one person.
- Sides and turn are judged from a single camera; a strong turn away from the camera lowers confidence and the advice says less, rather than guessing.
- The phone itself could not be exercised here. The Android build, lint and unit tests pass, and the engine and models were validated on the desktop with the same model files and the user's real video.
