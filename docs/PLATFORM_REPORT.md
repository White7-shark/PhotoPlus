# R8, Gradle, edge-to-edge and the Lucid theme

## R8: code and resource shrinking

Release builds now run R8: `minifyEnabled` and `shrinkResources` are on (`android/app/build.gradle`).

- **Rules** (`android/app/proguard-rules.pro`): libraries bring their own rules (React Native, AndroidX, WorkManager, ML Kit, Play services, Ads). Ours cover what reaches the app's code by name rather than by a normal call:
  - the engine's exception, created from C++ by class name and constructor (`photoplus_jni.cpp`);
  - every `native` method, which keeps its name so JNI finds it;
  - TurboModules, codegen specs and view managers, which React Native's C++ calls by method name;
  - WorkManager workers, which are created by class name;
  - MediaPipe, LiteRT and protobuf classes that their native code calls back into;
  - source file and line numbers, so crash reports stay readable with the mapping file.
- **Resources** (`res/raw/keep.xml`): React Native packs the pictures the JS uses (`require('…png')`) as drawables and loads them by name, so they are kept explicitly.
- **Verified** with `:app:minifyReleaseWithR8`, which compiles and shrinks the release code without packaging an APK:
  - R8 ran with no errors and no missing classes;
  - every class reached by name kept its name: `EngineException`, `NativeEngine`, all modules and specs, `PhotoPlusPackage`, `IntegrityWorker`, `MainApplication`;
  - every JS picture is marked "reachable from keep xml";
  - the dex went from 43 MB (debug, not shrunk) to 12 MB.
- **Before release:** test a release build on a phone. Upload `app/build/outputs/mapping/release/mapping.txt` with the bundle; Play Console usually takes it from the bundle automatically.

## Gradle

Already on Gradle 9: wrapper 9.9.1 with the Android Gradle Plugin 9.5.0. Nothing to change.

## Edge-to-edge (Google Play's requirement)

- **Policy:**
  - From Android 15 (target SDK 35), apps draw edge-to-edge, behind the status and navigation bars. Android 16 removes the opt-out.
  - Play Console warns about the APIs this makes deprecated, such as `Window.setStatusBarColor`, `setNavigationBarColor` and the cutout modes. Sources: [Android 15 behaviour changes](https://d.android.com/about/versions/15/behavior-changes-15), [AdMob consent SDK thread](https://groups.google.com/g/google-admob-ads-sdk/c/g7ugL1Vy7YQ), [Flutter issue on the warning](https://github.com/flutter/flutter/issues/169810).
- **Photo Plus:**
  - targets SDK 37, with React Native's `edgeToEdgeEnabled=true`;
  - `MainActivity` calls AndroidX's `enableEdgeToEdge()`, the replacement Google recommends;
  - the theme sets no bar colours;
  - none of the app's own code calls the deprecated APIs;
  - screens keep clear of the bars using the system's insets (`react-native-safe-area-context`): the tab bar, `Screen`, the block screen and the sheets;
  - the four pop-ups draw under both bars (`statusBarTranslucent` and `navigationBarTranslucent`), and the two camera sheets pad by the navigation bar's real height.
- **What remains:** React Native's own edge-to-edge helper (`WindowUtil.kt`) still sets the bar colours to transparent. React Native marks that call as deliberately deprecated, and it does nothing on Android 15 and later. If Play lists it, it comes from React Native and will go when React Native updates it; it doesn't affect users.

## Lucid: the see-through theme (from Dr. File)

Settings → Appearance → Theme → **Lucid**. Photo Plus becomes see-through: the phone's wallpaper (live wallpapers too), a photo or a short looping clip shows behind the app, with glass cards over it.

- **How it stays readable** (`src/theme/lucid.ts`, ported from Dr. File; WCAG luminance):
  - The picture decides the scheme: dark text on a light picture, white text on a dark one.
  - A veil is just strong enough for big titles to reach 3:1 against the worst tenth of the picture.
  - Glass is just tinted enough for body text on a card to reach 4.5:1.
  - The accent is the picture's most colourful colour, brought to 3:1.
  - A soft fade at the top keeps titles and the status bar readable over a busy top edge. This was added in Photo Plus after the preview showed a title over a dark stripe.
  - Tests check these ratios across 34 kinds of picture, from black to white and flat to very contrasty.
- **Sources** (`src/services/lucid/LucidService.ts`, `android/.../lucid/LucidModule.kt`):
  - **Phone wallpaper (free):** when Android lets the app read the picture, it is kept and measured. Newer Android keeps it private without an "all files" permission, which Photo Plus doesn't ask for, and live wallpapers move. In those cases the app's window turns see-through (`FLAG_SHOW_WALLPAPER`), so the wallpaper itself shows, and the look is worked out from the colours Android shares (`WallpaperColors`, including its "dark text" hint). The window flag is applied again each time the app returns to the front.
  - **Photo or video (Premium, `lucidCustom`):** picked with the system photo picker and copied into the app. A photo becomes a JPEG of at most 2160 px, the right way up; a clip can be up to 30 MB and plays silent and looping, filling the screen through a new `cover` option on the video view. The look is measured from the photo, or from four frames across the clip.
- **Persistence:**
  - The choice and look are saved (settings `internal.lucid`) and loaded at start-up, so the first frame is already Lucid.
  - If the background file has gone, Lucid reads the wallpaper again rather than falling back to plain light or dark.
  - The camera screen is unaffected, since it shows the camera.
- **App changes:**
  - the theme has `lucid` and the Lucid colours;
  - the aurora background becomes the picture with its veil;
  - screens and the navigator are transparent in Lucid;
  - the Lucid panel in Settings shows the sources and the colours taken from the picture.
- **Words, FAQ and privacy:**
  - all new words are in `strings.xml`; "Lucid" stays a brand name;
  - the FAQ has "What is Lucid?" and "How do I put my own photo or clip behind Lucid?";
  - the privacy policy says Lucid reads the wallpaper's colours and keeps the chosen picture on the phone only.

## Tests

- **Jest:** 394 tests in 35 suites, including `__tests__/lucid.test.tsx` (11 tests: readability, sources, saving, fallback, background and panel).
- **Kotlin:** `ImageStatsTest`.
- **Checks:** Android lint is clean for the new code, and `minifyReleaseWithR8` ran clean.
