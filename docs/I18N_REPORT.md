# Languages: the app, the share pictures and the captions

Photo Plus speaks English and 8 more languages. They were chosen as the most-spoken languages with the largest Android audiences:

| Language | Tag | Android folder | Notes |
| --- | --- | --- | --- |
| English | `en` | `values` | The source of every word |
| Spanish (Español) | `es` | `values-es` | |
| Portuguese, Brazil (Português) | `pt-BR` | `values-pt-rBR` | |
| French (Français) | `fr` | `values-fr` | |
| Arabic (العربية) | `ar` | `values-ar` | Right to left |
| Hindi (हिन्दी) | `hi` | `values-hi` | |
| Indonesian (Bahasa Indonesia) | `id` | `values-in` | Android's folder name for Indonesian |
| Russian (Русский) | `ru` | `values-ru` | |
| Chinese, Simplified (简体中文) | `zh-CN` | `values-zh-rCN` | |

## How the words work

- **One source.**
  - `src/i18n/strings/en.json` holds every word the translated screens show.
  - `src/i18n/strings/<tag>.json` holds the hand translations.
  - `node tools/i18n/build-strings.mjs` writes `res/values*/strings.xml` and `StringTable.kt` from them. `--check` fails when the files are out of date, and a Jest test runs that check.
  - The generator also refuses a translation with a key English doesn't have, or with placeholders different from English.
- **Placeholders.** Placeholders such as `{days}` are wrapped in `<xliff:g>`, so translators and Google Play leave them alone. `app_name` is marked `translatable="false"`.
- **At run time** (`src/i18n`, plus `PhotoPlusI18n` in Kotlin):
  - The JS side reads the strings from Android's resources for the app's language. Translations Google Play adds to the app bundle therefore show up with no code change.
  - Any word not yet translated is English.
  - `t(key, params)` and `useT()` read the words. Components redraw when the language changes.
- **Choosing a language.**
  - Settings → Language lists "Phone's language" and the 9 languages, each named in its own language.
  - The choice is Android's per-app language:
    - `AppCompatDelegate.setApplicationLocales`;
    - the same setting as the phone's Settings → Apps → Photo Plus → Language;
    - stored by the system on Android 13+, and by AppCompat's `AppLocalesMetadataHolderService` on Android 10–12.
  - `generateLocaleConfig` builds the language list from the `values-*` folders.
  - Arabic lays the app out right to left. The manifest already has `supportsRtl`, and Android recreates the screen in the new direction.
- **Every language is installed.**
  - Google Play normally installs only the phone's own languages from an app bundle.
  - Because the language can be changed inside the app, `bundle.language.enableSplit = false` keeps all of them.
  - The strings add only a few kilobytes.

### What is translated by hand, and what Play will translate

- **By hand (51 strings per language):**
  - the tab bar;
  - the Language screen;
  - Settings' section titles, Premium and Rewards rows, and theme;
  - the integrity block screen's titles, buttons and "no internet" message, and the update banner;
  - the Premium sheet's buttons;
  - the share sheet.
- **Still English in `values/strings.xml` (120 strings in all), for Google Play to translate:** the remaining Settings, integrity and share-sheet text.
- **Not yet moved into `strings.xml`:** the other screens (Studio, the editor, Product Studio, Rewards and Premium pages, and so on). They are still written in English in the JS code. They move into `en.json` screen by screen with the same `t()` call, and each one moved becomes translatable.

### Google Play's app-string translation: what to know

From Google's page ([Translate and localize your app](https://support.google.com/googleplay/android-developer/answer/9844778)):

- **Setup.** Play Console → Grow users → Translations → App strings → Add languages. Play translates the `strings.xml` in each new app bundle with Gemini, at no cost.
- **Play replaces your translations.** For a language you select, Play replaces any translations already in the bundle: "Any existing translations for these languages will be overridden."
  - Select a language to let Play translate everything in it, including the 69 strings per language not done by hand. Then compare Play's text with the hand translations in `src/i18n/strings/<tag>.json`, and fix any differences in the Console. Console edits carry over to later releases.
  - To keep the hand translations exactly, don't select that language in Play. It then shows the hand-translated words and English for the rest.
- **Brand names.** Uncheck "Translate" in the Console for any string that is only a brand or tool name. Play's page doesn't say whether it honours `translatable="false"`.
- **Right-to-left.** Arabic is right to left, and the app is ready for it.
- **The store listing.** The store listing has its own translation service.

## The share campaign in every language

- **Headline.** It is now **"You can now do a photoshoot right on your phone!"**. "Photoshoot" is a noun, so "do a photoshoot" is standard English; "you can now photoshoot" uses it as a verb, which is informal.
- **Captions.** The captions are written by hand in all 9 languages (`src/services/rewards/captions.ts`):
  - two post captions and two notes to friends (English keeps its three of each);
  - each caption starts with the headline and puts the link on its own line;
  - posts end with the brand hashtags plus three hashtags people use in that language (for example `#SesiónDeFotos`, `#फ़ोटोशूट`, `#写真`).
- **Pictures.** The pictures are rendered in every language: 27 JPEGs, 4.4 MB.
  - English stays in `assets/promo`; the others are in `assets/promo/<es|pt-br|fr|ar|hi|id|ru|zh-cn>/`.
  - `tools/promo/copy.mjs` holds their words.
  - `node tools/promo/render-promo.mjs [langs…]` renders them:
    - Arabic is laid out right to left;
    - fonts for Arabic, Devanagari, Cyrillic and Chinese (Noto) are fetched from Google Fonts into `tools/promo/.fonts`, which is not committed;
    - long words shrink the headline and the feature list a little until everything fits.
- **What gets shared.** The Rewards page shares the picture and caption in the app's language, and the native side accepts the language folders.

### Photos of people in the pictures

Photos from a real photoshoot made with the app would sell it best. They need photos the business has the rights to, and the permission (a model release) of everyone shown. The renderer is ready for them:

- Put the photos in `tools/promo/people/*.jpg`. This folder is ignored by git and is never committed.
- Render again. They take the place of the studio scenes in the phone screen and the photo strips, framed on the face.
- Without them, the pictures use the app's own studio scenes, as before.

## Tests

- **Jest (`__tests__/i18n.test.tsx`, 9 tests):**
  - the generator check;
  - every translation's keys and placeholders, and that the most-seen words really are translated;
  - every key is in `values/strings.xml`, with xliff placeholders;
  - `format` and the English fallback;
  - matching phone languages (`pt-PT`→`pt-BR`, `in-ID`→`id`, `zh-Hans-CN`→`zh-CN`);
  - following the phone, then choosing Spanish;
  - the Language screen (lists all 9, redraws in Spanish, returns to the phone's language);
  - every language's captions (headline first, link on its own line, hashtags last on posts, none to friends);
  - every language's 3 pictures exist and match the path the native side accepts.
- **Kotlin:** `StringTableTest` checks that `STRING_TABLE` has exactly the keys of `en.json`, each with its own resource.
- **Android lint:** clean for the new code. The "missing translation" check is turned off on purpose, because partial translations are expected; it is set in `values/strings.xml`.
