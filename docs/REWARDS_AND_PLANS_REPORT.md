# Premium plans, ads and Rewards

## Premium plans (`src/services/billing/plans.ts`, Premium screen)

| Plan | Price | Per month | Saving | Trial |
|---|---|---|---|---|
| Monthly | GHS 50 | GHS 50 | — | 7 days free |
| 3 months | GHS 135 | GHS 45 | 10% | 7 days free |
| Yearly | GHS 480 | GHS 40 | 20% | 7 days free |

- **Pricing:** monthly is fixed at GHS 50. Quarterly and yearly are placeholders until release (`PRICES_CONFIRMED = false`).
- **Play setup:** one Play subscription (`photoplus_premium`) with base plans `monthly`, `quarterly` and `yearly`, and the offer `free-trial-7d`.
- **Until Play Billing is connected:** "Start 7-day free trial" says subscriptions open in a coming update and points to Rewards. Once billing is live, Play's own localised prices replace these.
- **Entry points:** the Premium sheet (shown wherever a Premium tool is tapped) links to the plans and to Rewards, and Settings has both entries.

## Ads (Google AdMob)

- **Rewarded ads** use Google Mobile Ads (`play-services-ads`), behind the User Messaging Platform consent flow. Debug builds always use Google's test ad unit.
- **Real ad units for release** are Gradle properties:
  - `photoplus.admobAppId`
  - `photoplus.admobRewardedId`
- **Two uses:**
  - the existing 4-ads watermark-free unlock for a photo;
  - Rewards points (`PhotoPlusAds.showRewardedAdForPoints`).
- **Counting:** points count only from the SDK's reward callback, natively.

## Rewards (free plan only)

The ledger is native (`com.photoplus.rewards.RewardsLedger`, SharedPreferences), so the app's screens cannot grant points or Premium. `Entitlements.plan` is Premium while Premium time from Rewards is active, and `Entitlements.allows(tool)` honours a one-day tool unlock at the device's own Premium checks. Subscribers see that they already have Premium.

| Earn | Points | Limit |
|---|---|---|
| Daily check-in streak | 10, 15, 20, 25, 30, 40, 60 (200 a week) | once a day; a missed day restarts |
| Rewarded ad | 20 | 8 a day, a minute apart |
| Daily tasks: 3 photos, an edit, a studio creation | 15, 15, 20 | once a day each |
| One-time tasks: first photo, edit, product shot, film/collage, project, Glide | 30–40 (210 in all) | once |

| Spend | Points |
|---|---|
| One Premium tool for 24 hours (Scene Swap, Relight, Pro product setups, HD & 4K downloads, Studio light, Hair, Batch editing) | 250 |
| Premium for 1 day | 600 |
| Premium for 3 days | 1,500 |
| Premium for 7 days | 3,200 |

**Balance.** The most a day can earn is about 240 points (every ad, the check-in and every task). Rewards give at most 10 days of Premium in any 30 days, the share campaign included.

- **Dedicated grinder:** about a third of the month as Premium, while watching 8 ads a day.
- **Casual user:** a few check-ins and ads, roughly a day of Premium a week.

Rewards are a real taste of Premium, not a substitute for it, and they bring ad impressions.

**Clock.** Time only moves forward: setting the clock back doesn't reopen a check-in, an ad or a share lock.

**Policy.** There is no "rate the app" task, because Google Play forbids rewarding ratings and reviews. Tasks count what people do in the app (activity hooks in the camera, editor, studio tools, Product Studio, Film & Collage, projects and Glide).

## The share campaign

**Headline.** Every picture and caption leads with **"You can now do a photoshoot right on your phone!"** (in the app's language: see I18N_REPORT.md), styled as a celebration: an "IT'S HERE!" ribbon, the word *photoshoot* in the brand gradient, confetti and sparkles along the sides.

**What's in the picture and what isn't.** The pictures carry no link and no hashtags. Both go in the caption sent with the picture, so the link (on a line of its own) can be tapped and the hashtags (the last line) are recognised by the app. The caption is also copied to the clipboard, so it can be pasted into apps that keep only the picture.


- **Goal:** share Photo Plus 30 times for 2 days of Premium. It opens again 30 days after it is completed.
- **Per-app lock:** each app counts once every 8 hours, whatever form was shared.
  - An app opened from the list counts when it opens.
  - An app picked from the system share sheet counts when the system reports the choice (`EXTRA_CHOSEN_COMPONENT`).
  - Apps listed: WhatsApp, WhatsApp Business, Facebook, Instagram, Messenger, TikTok, Snapchat, Telegram, X, Threads, LinkedIn, Pinterest and Messages, when they are on the phone. Any other app is reached through "More apps".
- **Two ways to share, each with its own picture and words:**
  - **Post** (1080×1350) and **Status or story** (1080×1920): a big picture covering what Photo Plus does (Photoshoot Studio, Relight, Product Studio, Beauty & Spotlight, Lively camera, Glide & Film), "No studio. No laptop. No experience needed", **7 DAYS FREE TRIAL**, a Google Play badge and the link. The caption adds the features, the trial, the Play Store link and 10 hashtags.
  - **To friends** (1080×1080): a personal note ("You have to try Photo Plus…") with the capabilities, the 7-day free trial and the Play Store link, without hashtags.
- **Variety:** three versions of each text rotate, so repeated posts don't look copied.
- **Link:** the Play link carries a referrer (`utm_source=photoplus_app`, medium post or friends) for install tracking.
- **Regenerating the pictures:** they are made from the app's own brand, fonts and studio scenes (no people, no third-party photos) by `node tools/promo/render-promo.mjs`; rerun it after changing the copy.

**Caution.** Some platforms' own rules (Facebook's in particular) don't allow rewarding people for sharing. Sharing here goes through Android's share intents, not those platforms' SDKs, but review this before launch.

## Tests

- **Kotlin:** 6 ledger tests covering check-in streaks, the ad gap and daily limit, task claims, redemptions with stacking and the 10-day cap, 30 shares with 8-hour app locks and collection, and setting the clock back.
- **Jest:** 8 tests covering plans and savings, the Premium screen, the Premium sheet links, share texts (link, trial, hashtags, rotation), check-in, ads, tasks, redeeming a tool, sharing by form with locked apps, share-sheet events, collecting the share reward, and subscribers.
