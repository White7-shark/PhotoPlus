# Photo Plus

Your photoshoot studio on Android: a pro camera, an AI director and a darkroom, all in one app. It works offline first, and photos stay on the device.

- **Platform:** Android 10+ (API 29), React Native 0.88 + TypeScript, Kotlin, C/C++
- **Product spec:** [`docs/PRODUCT_SPEC.md`](docs/PRODUCT_SPEC.md)
- **Architecture:** [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- **Phase 1 report (testing & review evidence):** [`docs/PHASE1_REPORT.md`](docs/PHASE1_REPORT.md)

## Getting started

```sh
npm install
npm start            # Metro bundler
npm run android      # build & install a debug build on a device/emulator
```

Requires Node ≥ 22.11, JDK 17+, Android SDK (platform 37, build-tools 37.0.0), NDK 27.1.12297006 and CMake 3.22.1.

## Verification

| Command | What it checks |
|---|---|
| `npm run typecheck` | TypeScript across app and tests |
| `npm run lint` | ESLint (React Native config) |
| `npm test` | Jest: services, SQLite schema (real SQLite via sql.js), UI accessibility |
| `npm run test:engine` | C++ engine tests under AddressSanitizer + UBSan |
| `npm run test:android` | Kotlin JVM unit tests + Android Lint |
| `npm run verify` | All of the above except Android |

## Repository layout

```
src/                 React Native app (TypeScript)
  app/               root component and startup sequence
  brand/             logo geometry (shared by the app, icons and previews)
  components/        design-system components (glass, buttons, states…)
  navigation/        stack + glass tab bar
  screens/           Studio, Camera, Projects, Packs, Settings, Device, Engine Lab
  services/          capabilities, permissions, database, processing, storage…
  state/             zustand store
  theme/             tokens, contrast utilities, ThemeProvider
  native/specs/      TurboModule / Fabric codegen specs
android/             Kotlin native layer + JNI bridge
native/engine/       platform-agnostic C/C++ processing engine (+ host tests)
scripts/             dev tooling (icon generation)
docs/                spec, architecture, phase reports
```
