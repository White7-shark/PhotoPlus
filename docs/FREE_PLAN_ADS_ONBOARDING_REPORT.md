# Free plan limits, owed ads, Toon controls and onboarding

This round covers five things:

- a licence check of the cartoon model weights;
- Photo Arts and Photo Toon limited to 5 a day on the free plan, unlocked by ads and watermarked;
- an "owed ads" policy for when the phone is offline;
- more controls in Photo Toon;
- an animated onboarding that ends with a 7-day free trial, simulated in test builds.

## 1. Cartoon model weights: licence check

I looked for a published photo-to-cartoon model whose weights can be shipped in a paid app with no licence question. I found none.

| Model | Code licence | Weights | Commercial use of the weights |
| --- | --- | --- | --- |
| AnimeGANv2 / v3 (TachibanaYoshino) | Non-commercial (the author's terms) | Released by the author | No, unless licensed from the author |
| `bryandlee/animegan2-pytorch` | MIT | Some are converted from AnimeGANv2. Others were trained by the author on undisclosed data or data derived from CelebA, which is research-only. | Unclear, so not safe |
| `akhaliq/AnimeGANv2-pytorch` (Hugging Face) | The same weights as above | The same weights as above | Unclear, so not safe |
| White-box Cartoonization | CC BY-NC-SA | Released by the authors | No (non-commercial) |
| `minivision-ai/photo2cartoon` | MIT | On Google Drive. The training data's terms are not stated. The model handles face crops only. | Unclear, so not safe |

Sources:
- [AnimeGANv2 PyTorch port](https://github.com/bryandlee/animegan2-pytorch)
- [photo2cartoon](https://github.com/minivision-ai/photo2cartoon)
- [AnimeGANv2 weights on Hugging Face](https://huggingface.co/akhaliq/AnimeGANv2-pytorch)
- [Photo-to-anime API vs AnimeGAN v2/v3](https://dev.to/aiengine/photo-to-anime-api-vs-animegan-v2v3-open-source-5f0d)
- [lensmith-models](https://github.com/lensmith-app/lensmith-models)

The MIT licence on a code repository does not cover weights trained on data that carries its own terms. This is not legal advice.

**Decision.** No third-party weights are bundled. Photo Plus trains its own model instead: **ToonNet** (`tools/toon_model`, `docs/TOON_MODEL_REPORT.md`).
- It first learns from the app's own classic engine.
- It can then be sharpened on art the app has licensed.
- Until a ToonNet trained on licensed data is bundled, `ModelToonEngine` stays behind `BuildConfig.TOON_MODEL_ENGINE = false`, and the classic engine draws every picture.

## 2. Five a day, unlocked by ads, with the watermark

| Tool | Free plan | Premium |
| --- | --- | --- |
| Photo Arts (paintings and pencil drawings together) | 5 a day, each after a rewarded ad | Unlimited, no ads, no mark |
| Photo Toon | 5 a day, each after a rewarded ad | Unlimited, no ads, no mark |
| Photo Motion | 2 a day, each after a rewarded ad (unchanged) | Unlimited |

Previews and style cards stay free and unlimited.

How it works:
- **Counting.** The device counts uses natively (`ArtsAllowance` and `ToonAllowance`, both built on `AdAllowance`) against the trusted clock. Paintings and pencil drawings share one count, so Photo Arts is 5 in total, not 5 of each.
- **Watermark.** On the free plan, every finished output goes through `StudioModule.encodeStudio(full = true)`, which stamps the app's watermark (`PhotoWatermark`). The four-ad watermark-free export is unchanged.
- **Refusals.** The native side refuses a full render that has no unlocked use. It returns `AD_REQUIRED`, `PREMIUM_REQUIRED` (today's 5 are used) or `OWED_ADS` (frozen, see below). The screen cannot get around this.

## 3. Offline: owed ads and the weekly online rule

Most of the app works offline, but an ad cannot load without a connection. The policy:

1. **Weekly online check (already in place).** The app must reach the internet at least once every 7 days (`Integrity`, `MAX_OFFLINE_MS`). If it doesn't, the blocking screen asks the person to connect.
2. **Offline unlock, ad owed.** If the phone is offline when an ad is needed (no validated network, or the ad fails with a network error while offline), the use is unlocked anyway and one ad is recorded as owed for that tool. The screen says so.
3. **Frozen at 3.** When a tool owes 3 ads, it is frozen. The tool shows "Paused: 3 ads are owed from offline use. Connect to the internet and watch them to use this tool again.", and the native side refuses it with `OWED_ADS`.
4. **Paying off.** Once the phone is online, each ad watched pays off one owed ad before it unlocks a new use. All 3 must be watched before the tool unlocks again. After that it works normally until the day's limit.
5. **Per tool and kept.** The owed count is kept per tool (Photo Arts, Photo Toon, Photo Motion). It is stored natively, survives restarts and does not reset at midnight. An offline unlock still counts towards the day's 5, so being offline never gives more than the limit.

The pure rules are `AdAllowance.offlineUnlock` and `AdAllowance.watched`.

## 4. Photo Toon: more controls

A "More controls" panel below Fine-tune adds:
- outline;
- background softness;
- brightness;
- warmth;
- texture (shading, grain and screentone);
- face lines;
- hair lines;
- ink colour (the style's own, black, brown or coloured).

Every control starts at the style's own look, and "Back to the style's own look" resets them all. They are carried through all the layers:
- engine: `pp_toon_settings` with `pp_toon_settings_init`;
- JNI: 18 parameters;
- `StudioModule.renderToon`;
- the screen.

Full details are in `TOON_STUDIO_REPORT.md`.

## 5. Onboarding and the 7-day free trial

There are 9 animated pages:
1. Welcome
2. Camera
3. Pose Assistant
4. Photoshoot Studio
5. Photo Arts
6. Photo Motion
7. Photo Toon
8. Private on your phone
9. Get 7 days free trial

About the animations:
- Each page has its own illustration, built from React Native `Animated` loops on the native driver.
- When the system's reduce-motion setting is on, they are still.
- Pages swipe or move with Next. Skip jumps to the last page.

The last page offers **"Get 7 days free trial"** or "Continue with the free plan".

- **Testing builds (simulated).** In debug builds, or builds with `PREMIUM_SIMULATION`, the trial starts natively (`Entitlements.startTrialSimulation`):
  - It lasts exactly 7 days on the trusted clock.
  - It can be used once per install.
  - It unlocks Premium while it lasts.
  - The page notes that the trial is simulated for testing.
- **Production.** The button opens the Premium page, where Google Play Billing starts the subscription with the `free-trial-7d` offer. Play handles eligibility, the trial's end and conversion. The simulation is never active in a store build.

## Tests

- **Jest:** 42 suites, 451 tests passing. New or updated:
  - `onboarding` covers the pages, Next/Skip, the simulated trial, a trial already used, the store build sending people to Premium, the free plan, and all strings in 8 languages.
  - `masters`, `toon`, `motion` and `pencilArt` cover 5 a day, the offline owed-ads flow and the More controls.
- **Kotlin:**
  - `ArtsAndToonAllowanceTest`
  - `OwedAdsTest`: offline up to 3 and then frozen, the pay-off order, no extra uses.
  - `TrialTest`: exactly 7 days, never started, clock wound back.
- **Engine:**
  - `toon`: 13 tests, including the new controls and bad-input checks.
  - `art_`: 9 tests.
  - `pencil`: 4 tests.
- **Builds:** gradle `buildCMakeDebug`, `compileDebugKotlin`, `testDebugUnitTest`, `lintDebug` and `minifyReleaseWithR8` all pass.

## Limits

- Not tested on a phone. No APK was built.
- Real ad loading and the offline detection were checked by unit tests and mocks only.
- The trial is a simulation. The Play Billing offer has to be set up in Play Console, as `free-trial-7d` on the subscription.
- Owed ads only build up when the phone is really offline. If an ad fails for another reason while online (for example, no fill), the use is not unlocked on credit.
