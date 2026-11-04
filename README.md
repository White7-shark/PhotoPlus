# Photo Plus

Your photoshoot studio on Android: a pro camera, an AI director and a darkroom, all in one app. It works offline first, and photos stay on the device.

- **Platform:** Android 10+ (API 29), targeting Android 37. Built with React Native 0.88 + TypeScript, Kotlin 2.4, C++23.
- **Product spec:** [`docs/PRODUCT_SPEC.md`](docs/PRODUCT_SPEC.md)
- **Architecture:** [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- **Toolchain & dependency versions (latest-stable policy):** [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md)
- **Phase 1 report (testing & review evidence):** [`docs/PHASE1_REPORT.md`](docs/PHASE1_REPORT.md)
- **Phase 2 report: professional camera:** [`docs/PHASE2_REPORT.md`](docs/PHASE2_REPORT.md)
- **Enhancement benchmark (weak camera → strong photo):** [`docs/benchmarks/README.md`](docs/benchmarks/README.md)

## Getting started

```sh
npm install          # also applies patches/ via patch-package
npm start            # Metro bundler
npm run android      # build & install a debug build on a device/emulator
```

**Requires:**

- Node 26.13 LTS (`.nvmrc`) with npm 11.20+
- JDK 25 LTS. The Gradle daemon is pinned to 25 in `android/gradle/gradle-daemon-jvm.properties`.
- Android SDK:
  - platform 37.2
  - build-tools 37.0.0
  - NDK 30.0.16248370
  - CMake 4.1.2

Gradle 9.8.2 is provided by the wrapper.

## Verification

| Command | What it checks |
|---|---|
| `npm run typecheck` | TypeScript across app and tests |
| `npm run lint` | ESLint 10, type-aware, with `no-deprecated` as an error |
| `npm run format:check` | Prettier 3 |
| `npm test` | Jest: services, SQLite schema (real SQLite via sql.js), UI accessibility |
| `npm run test:engine` | C++ engine tests under AddressSanitizer + UBSan |
| `npm run test:android` | Kotlin JVM unit tests + Android Lint |
| `npm run verify` | Everything above except the Android checks |

## Repository layout

```
src/                 React Native app (TypeScript)
  app/               root component and startup sequence
  assets/brand/      logo images bundled in the app (emblem + full logo)
  components/        design-system components (glass, buttons, states…)
  navigation/        stack + glass tab bar
  screens/           Studio, Camera (camera/), Photo preview, Projects, Packs, Settings, Device, Engine Lab
  services/          capabilities, permissions, database, processing, storage…
  state/             zustand store
  theme/             tokens, contrast utilities, ThemeProvider
  native/specs/      TurboModule / Fabric codegen specs
android/             Kotlin native layer + JNI bridge
native/engine/       platform-agnostic C/C++ processing engine (+ host tests)
scripts/             dev tooling (brand assets, design previews, enhancement benchmark)
assets/brand/        master logo artwork (source/) and exported brand assets
docs/                spec, architecture, phase reports
```
