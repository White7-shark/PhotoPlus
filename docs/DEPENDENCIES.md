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
| Gradle | **9.8.2** | 9.8.2 |
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
| androidx.camera (camera2 / lifecycle / view) | **1.6.2** | 1.6.2 |
| androidx.exifinterface | **1.4.2** | 1.4.2 |
| androidx.core:core-ktx | **1.19.1** | 1.19.1 |
| androidx.appcompat | **1.8.0** | 1.8.0 |
| androidx.activity:activity-ktx | **1.13.0** | 1.13.0 (1.14 is alpha) |
| ML Kit face detection (bundled) | **16.1.7** | 16.1.7 |
| ML Kit selfie segmentation (bundled) | **16.0.0-beta6** | 16.0.0-beta6 (Google ships it only as beta) |
| ML Kit subject segmentation (Play services) | **16.0.0-beta1** | 16.0.0-beta1 (Google ships it only as beta) |
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

## Patched upstream: `@react-native/gradle-plugin` 0.88.1

`patches/@react-native+gradle-plugin+0.88.1.patch` is applied automatically by `postinstall`. It brings React Native's own Gradle plugin up to the toolchain above:

- Kotlin 2.2.0 → 2.4.21 and AGP 9.2.1 → 9.5.0 in its version catalog. Gradle 9.8.2 embeds Kotlin 2.4, whose metadata the plugin's Kotlin 2.2 compiler can't read.
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
