# Photo Plus — Toolchain & Dependency Versions

**Policy:** use the latest stable release of every language, toolchain and package, with **no deprecated APIs in our code**. When the latest release can't be used because an upstream package hard-requires something older, we use the newest compatible release, and the reason is recorded below.

**Last audited:** 3 Nov 2026, against the official registries: npm, Google Maven, Maven Central, services.gradle.org, the Android SDK repository (`sdkmanager`), nodejs.org and Adoptium.

## Languages & runtimes

| Item | Version | Latest stable | Notes |
|---|---|---|---|
| Java (JDK) | **25** (Temurin 25.0.5 LTS) | 25 LTS | Gradle daemon pinned via `android/gradle/gradle-daemon-jvm.properties`. Bytecode target is 17 because React Native aligns every module to 17. |
| Kotlin | **2.4.21** | 2.4.21 | AGP 9 built-in Kotlin (the deprecated `kotlin-android` plugin was removed from the app) |
| C++ | **C++23** (strict ISO, no GNU extensions) | C++23 | Uses `std::expected` for fallible results |
| TypeScript | **6.0.3** | 7.0.2 | ⚠️ typescript-eslint 8.72.1, the latest, supports TypeScript < 6.1. Type-aware linting (including the no-deprecated rule) needs that compatibility. |
| Node.js | **26.13.0** LTS "Lithium" (`.nvmrc`) | 26 LTS | |
| npm | 11.20.0 | 11.20.0 | |

## Android toolchain

| Item | Version | Latest stable |
|---|---|---|
| Gradle | **9.9.1** | 9.9.1 |
| Android Gradle Plugin | **9.5.0** (built-in Kotlin + new DSL enabled) | 9.5.0 |
| compileSdk | **37, minor API level 2** (Android 37.2) | 37.2 |
| targetSdk | **37** | 37 |
| minSdk | 29 (product spec: Android 10) | — |
| Build tools | **37.0.0** | 37.0.0 |
| NDK | **30.0.16248370** (r30) | r30 |
| CMake (SDK) | **4.1.2** | 4.1.2 |

## Android libraries

| Library | Version | Latest stable |
|---|---|---|
| androidx.camera (camera2 / lifecycle / view / video) | **1.6.2** | 1.6.2 |
| androidx.exifinterface | **1.4.2** | 1.4.2 |
| androidx.core:core-ktx | **1.19.1** | 1.19.1 |
| androidx.appcompat | **1.8.0** | 1.8.0 |
| androidx.activity:activity-ktx | **1.13.0** | 1.13.0 (1.14 is alpha) |
| ML Kit face detection (bundled) | **16.1.7** | 16.1.7 |
| ML Kit selfie segmentation (bundled) | **16.0.0-beta6** | 16.0.0-beta6 (Google ships it only as beta) |
| ML Kit subject segmentation (Play services) | **16.0.0-beta1** | 16.0.0-beta1 (Google ships it only as beta) |
| LiteRT (on-device inference, 16 KB page aligned) | **2.3.2** | 2.3.2 |
| LiteRT GPU accelerator (`litert-gpu`, Phase 4; +3.2 MB on arm64) | **2.3.2** | 2.3.2 |
| MediaPipe Tasks Vision (pose landmarker; `litertlm-android` excluded, only `ImageEmbedder` uses it) | **1.2.3** | 1.2.3 |
| MediaPipe Selfie Multiclass 256 × 256 (hair, skin, clothes; Apache-2.0; float32 version 1, fetched and SHA-256-verified at build time) | **v1** | — |
| Studio scenes: 15 Poly Haven backplates (CC0, MD5-verified, rebuilt by `scripts/studio-plates.py`) | 2.56 K | — |
| MediaPipe Pose Landmarker Lite and Full models (Apache-2.0; float16 version 1, fetched and SHA-256-verified at build time) | **v1** | — |
| Depth Anything V2 Small, LiteRT 8-bit weights (Apache-2.0; fetched and SHA-256-verified at build time, revision `178427e`) | **2024 release** | — |
| JUnit (Jupiter, via BOM) | **6.1.3** | 6.1.3 (migrated from JUnit 4, which is maintenance-only) |
| org.json (tests) | **20260814** | 20260814 |

## JavaScript packages

| Package | Version | Latest stable | Notes |
|---|---|---|---|
| react-native | **0.88.1** | 0.88.1 | |
| react | **19.3.0** | 19.3.1 | ⚠️ React Native 0.88.1 ships its own copy of React's renderer, built for exactly 19.3.0. The `react` package must match it. |
| @react-navigation/native, native-stack, bottom-tabs | 7.5.1 / 7.21.0 / 7.21.0 | same | |
| react-native-screens | 4.30.1 | 4.30.1 | |
| react-native-svg | 15.16.0 | 15.16.0 | |
| react-native-safe-area-context | 5.11.1 | 5.11.1 | |
| zustand | 5.0.16 | 5.0.16 | |
| @react-native-community/cli | 20.2.0 | 20.2.0 | |
| eslint | **10.14.0** (flat config) | 10.14.0 | ESLint 9 is end-of-life. Replaces `@react-native/eslint-config`, whose bundled plugins don't support ESLint 10. |
| typescript-eslint | 8.72.1 | 8.72.1 | Includes `@typescript-eslint/no-deprecated` as an **error** |
| eslint-plugin-react-hooks | 7.1.1 | 7.1.1 | Includes React Compiler rules |
| @eslint-community/eslint-plugin-eslint-comments | 4.8.1 | 4.8.1 | Replaces the unmaintained `eslint-plugin-eslint-comments` 3.x |
| eslint-plugin-jest / eslint-config-prettier | 29.17.0 / 10.1.8 | same | |
| prettier | **3.9.10** | 3.9.10 | |
| jest / @types/jest | **30.5.2 / 30.0.0** | same | |
| babel-jest (override) | **30.5.2** | 30.5.2 | npm `overrides`: React Native's jest preset still pins 29 |
| test-exclude (override) | **8.0.0** | 8.0.0 | npm `overrides`: removes the deprecated `glob` 7/10 and `inflight` |
| @testing-library/react-native / test-renderer | 14.1.0 / 1.3.1 | same | `react-test-renderer` (deprecated) removed |
| sql.js | 1.15.2 | 1.15.2 | |
| patch-package | 8.0.1 | 8.0.1 | |
| @babel/core, preset-env, runtime | 7.29.x | 8.x | ⚠️ React Native's Babel preset and Metro require Babel 7 (`@babel/core ^7.25.2`) |

A clean `npm ci` reports **0 deprecated packages** and **0 warnings**. `@parcel/watcher`'s optional build-from-source install script is explicitly denied in `allowScripts`, since prebuilt binaries are used.

## Bundled fonts and generated art (Phase 3)

| Asset | Version / source | Licence |
|---|---|---|
| Poppins SemiBold ("Modern") | Google Fonts | SIL OFL 1.1 |
| DM Serif Display ("Elegant") | Google Fonts | SIL OFL 1.1 |
| Great Vibes ("Signature") | Google Fonts | SIL OFL 1.1 |
| Petit Formal Script (the brand mark's "Created with") | Google Fonts | SIL OFL 1.1 |
| Poppins ExtraBold Italic (the brand mark's "PhotoPlus") | Google Fonts | SIL OFL 1.1 |
| Bebas Neue ("Bold") | Google Fonts | SIL OFL 1.1 |
| Space Mono ("Type") | Google Fonts | SIL OFL 1.1 |

The licence texts ship in `android/app/src/main/assets/fonts/LICENSES.txt`. Prop artwork and the brand mark's camera are original: `scripts/make-props.mjs` and `scripts/make-brand-mark.mjs` draw them as SVG and renders it with the Playwright/Chromium already used for the design previews. It is a build-time tool only, not an app dependency. The editor preview script uses Pillow and MediaPipe (desktop only), as the Spotlight preview does.

## Native engine libraries (Phase 4)

| Library | Version | Licence | Notes |
|---|---|---|---|
| stb_image | **v2.30** (latest) | Public domain / MIT | Vendored in `native/engine/third_party/stb`, SHA-256 `594c2fe35d49…` (`stb_image.h`). Only JPEG, PNG, BMP and GIF are compiled in; max 16 384 px per side; allocations go through the engine's memory budget |
| stb_image_write | **v1.16** (latest) | Public domain / MIT | SHA-256 `cbd5f0ad7a9c…`. PNG and JPEG encoding |
| OpenGL ES 3.1 + EGL | platform (NDK) | — | GPU previews; verified against the CPU at start-up |
| OpenCV | 5.0.0 (latest; **not shipped**) | Apache-2.0 | Evaluated for the app: the official Android SDK (4.12 AAR) is 118 MB. Used only on the development machine as the test oracle (`scripts/verify-engine-against-opencv.py`, `opencv-python-headless` 5.0.0.93). See PHASE4_REPORT "OpenCV decision" |
| ai-edge-litert (Python) | 2.3.0 | Apache-2.0 | Development only: host measurements in MODEL_EVALUATION.md |

## Portrait validation (Phase 5, development only)

No app dependencies were added in Phase 5. On the desktop, `scripts/validate-portrait.py` uses MediaPipe 1.2.2 (face landmarker and selfie segmenter) as a stand-in for ML Kit, plus pyarrow to read the FairFace validation set (CC BY 4.0: Kärkkäinen & Joo, "FairFace", WACV 2021). The dataset is used only locally and is not committed.

## Ads (Phase 3 update)

| Library | Version | Latest stable | Notes |
|---|---|---|---|
| com.google.android.gms:play-services-ads | **25.5.0** | 25.5.0 | Rewarded ads for the free plan's watermark-free unlock |
| com.google.android.ump:user-messaging-platform | **4.0.0** | 4.0.0 | Consent where the law requires it, before any ad request |

Debug builds always use Google's test ad unit. Release IDs come from the Gradle properties `photoplus.admobAppId` / `photoplus.admobRewardedId`; until they are set, the test IDs are used.

## Patched upstream: `@react-native/gradle-plugin` 0.88.1

`patches/@react-native+gradle-plugin+0.88.1.patch` is applied automatically by `postinstall`. It brings React Native's own Gradle plugin up to the toolchain above:

- Kotlin 2.2.0 → 2.4.21 and AGP 9.2.1 → 9.5.0 in its version catalog. Gradle 9.8 and later embed Kotlin 2.4, whose metadata the plugin's Kotlin 2.2 compiler can't read.
- JVM target 11 → 17, which AGP 9.5 requires.
- Kotlin API level 2.0 → 2.2 (2.0 is deprecated in Kotlin 2.4).
- `project.properties[...]` → `providers.gradleProperty(...)`, because `Project.getProperties` is deprecated and fails in Gradle 10.

**Remove the patch** once a React Native release ships these changes.

## Deprecations we can't remove (upstream library code)

The build is clean for **our** code: 0 Kotlin/Java compiler warnings, 0 Gradle deprecations from our build files, and Android Lint reports "No issues found". The remaining warnings come from inside third-party libraries that are already on their latest releases:

- **react-native-screens 4.30.1**: uses legacy-architecture classes and a deprecated `UIManagerHelper` overload; its `build.gradle` uses Groovy space-assignment and `Project.getProperties`.
- **react-native-safe-area-context 5.11.1**: same categories.
- **react-native-svg 15.16.0**: Groovy space-assignment in its `build.gradle`.

Those need fixing upstream. They don't affect the app, and we'll pick up fixes as new versions are released.

## On-device vision choice

ML Kit was chosen over MediaPipe Tasks Vision 1.2.2 for Spotlight. MediaPipe 1.2.2 was released two days before this work with a reworked API that could not be exercised on a device here, and it adds ~35 MB of native code plus a transitive ~20 MB LLM runtime. ML Kit's APIs are stable. The two segmentation artifacts are Google's latest releases, which Google only publishes as betas, and both have robust fallbacks (the bundled person model, then the engine's own tap cut-out). MediaPipe is used only by the preview script (`scripts/make-spotlight-assets.py`), never by the app.

The pose assistant does use MediaPipe Tasks Vision (1.2.3, now the latest stable release). ML Kit's pose detection is still beta, and MediaPipe reports each landmark's *presence* as well as its visibility, which the assistant needs in order to tell a limb outside the photo from one that is covered. The transitive LLM runtime is excluded. Spotlight still uses ML Kit.

## Experimental API opt-in

- **CameraX Camera2 interop** (`@ExperimentalCamera2Interop`) is still marked experimental in CameraX 1.6.2. It is the only supported route to manual ISO, shutter, focus distance and white-balance gains, and to per-frame metadata, so `CameraController` opts in explicitly (scoped to that one class, documented in its KDoc). `ImageProxyTransformFactory` is also experimental and is **not** used: overlay geometry is computed in `OverlayGeometry` instead.

## Platform-required deprecated member

- `MemoryPressureMonitor.onLowMemory()`: the `ComponentCallbacks` interface requires us to implement this method, even though Android deprecates it. Our override is an annotated no-op. Pressure handling uses only non-deprecated signals: `TRIM_MEMORY_BACKGROUND` and `ActivityManager.MemoryInfo.lowMemory`.

## Behaviour changes made to avoid deprecated APIs

- **WebP export needs Android 11+.** The only WebP encoder constant on Android 10 is deprecated. On Android 10 the capability probe doesn't list WebP, and the engine returns `UNSUPPORTED_FORMAT`.
- **No status/navigation bar colour attributes.** They're deprecated from API 35, and the app is edge-to-edge.
- **`EmitterSubscription` → `EventSubscription`** (React Native).
- **react-native-svg `<Rect x y>` → `<Path>`**, to avoid the deprecated `x`/`y` transform props.
- **Animated values via lazy `useState`** instead of reading refs during render (React Compiler rule).

## How to re-audit

```sh
npm outdated                         # JS packages
npm ci 2>&1 | grep -i deprecated     # deprecated transitive packages (expect none)
npm run lint                         # includes @typescript-eslint/no-deprecated
cd android && ./gradlew :app:compileDebugKotlin :app:lintDebug --warning-mode all
```

Android/Maven versions: check each artifact's `maven-metadata.xml` on Google Maven or Maven Central. Gradle: https://services.gradle.org/versions/current. SDK packages: `sdkmanager --list`.
