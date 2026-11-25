# App integrity: trusted time, weekly online check, updates, and the share sheet

## The blocking screen

The app is covered by a full-screen block in three cases:

| Case | Title on the screen |
|---|---|
| The phone's time is more than 5 minutes off the true time | "Your phone's date and time are wrong" |
| The app hasn't been online for more than 7 days | "Connect to the internet" |
| An update's 7-day period has ended | "Update Photo Plus" |

- **Back does nothing.** The gate's back handler swallows it, and the app underneath is hidden from screen readers.
- **The screen says why it is blocking:**
  - wrong time: how far the clock is off;
  - offline: how many days since the last online check;
  - update: that the update period has ended.
- **The way out is a check online** that must succeed:
  - **Check time:** if the phone is offline, the result says "No internet"; if the clock is still wrong, it says so and the block stays. There's also an "Open date & time settings" button.
  - **Check connection:** for the weekly check.
  - **Update now:** opens Google Play's in-app update, or the Play listing. "I've updated · check again" sits under it.
- **Lifting the block:** only a successful check that finds everything right removes it.

## Trusted time (`com.photoplus.integrity.TrustedTime`)

- **Online time:** read over HTTPS from Google (`generate_204`, its Date header), gstatic and Cloudflare (`cdn-cgi/trace`, an exact timestamp). Half the round trip is added, and the median of the answers is used.
- **Tracking:** the reading is stored with the phone's boot timer (`elapsedRealtime`), which can't be set. Until the next restart, the true time is the server time plus the boot timer's progress.
  - The clock counts as wrong when it is more than 5 minutes off.
  - After a restart, the clock may not be earlier than a true time already seen.
- **Offline time** is the longer of two measures, so setting the clock back or restarting doesn't shorten it:
  - the true time since the last online check;
  - the phone's running time since then, added up boot by boot.
- **Rewards run on trusted time:** points, check-ins, ad gaps, share locks and Premium time from rewards. The rewards ledger and rewarded ads refuse to work while the app is blocked.

## Updates (Google Play In-App Updates, `com.google.android.play:app-update` 2.1.0)

- **Deadline:** a new version on Play starts a 7-day deadline from when it was first seen. A still-newer version keeps the first deadline.
- **Banner:** shows "A new version of Photo Plus is ready · Update by …", with the days left near the end, and **Update** and **Later** buttons.
  - Later hides it until the next reminder.
  - On the last day it always shows.
- **Reminders:** a notification every 2 days ("Update Photo Plus … Update by …"). Notification permission is asked once, when an update is waiting.
- **Past the deadline:** the update block. Installing the new version clears it.

## Regular checks (WorkManager 2.12.0)

| When | What runs |
|---|---|
| Every 6 hours in the background, whenever there's a connection | Time and update check, plus the reminder |
| When the app opens, when it comes back to the foreground, and every 30 minutes while open | The same online check (at most every 10 minutes unless forced) |

## The in-app share sheet (Rewards share campaign)

- **The list:** every app on the phone that takes a picture, each with its real icon and name. An app with several share screens (a chat and a status) lists each one.
- **Order:** popular apps first, then the rest by name.
- **Scrolling:** a FlatList in its own bottom sheet window (78% of the screen), so nothing clips it and no outer scroll holds it.
- **Each tile** shows how many times Photo Plus was shared there, and its 8-hour countdown when it is locked.
- **Exact counting:** a share opens exactly that app's share screen (an explicit component), so it is recorded for exactly that app. The lock is once every 8 hours per app, whichever screen or form.
- **The system share sheet is no longer used,** so every share is counted.

## Tests

| Where | Count | What they cover |
|---|---|---|
| Kotlin | 4 | The trusted time against the boot timer (clock moved ahead and back, the tolerance), a restart with the clock set back, a week offline with the clock held back and across restarts, and the update policy (7-day deadline, reminders every 2 days, Later, the last day, a newer version, cleared on update) |
| Jest | 6 | All well; the time block (back swallowed, settings, still wrong, offline, confirmed); the offline block; the banner with Update and Later; the update block cleared after updating; clock-offset wording |
| Jest (Rewards) | — | The share sheet: every app with its screens, ordering, counts, locks, sharing straight to an app |
