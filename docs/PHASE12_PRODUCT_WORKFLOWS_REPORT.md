# Phase 12: Product photography and professional workflows

**Goal:** make Photo Plus useful to photographers, content creators, retailers and small businesses, not only to portrait enthusiasts. A seller should be able to shoot products on a phone and get consistent catalogue shots. A photographer should be able to organise shoots, reuse their setups and their brand mark, and deliver every size a client needs. Originals are never changed.

## What the person can do

| Feature | Where | Free | Premium |
|---|---|---|---|
| Object cut-out (its own shadow and the old paper left behind; keep/remove brushes; edge settings) | Product Studio › Cut-out | ✓ | ✓ |
| Backdrops: Plain and Paper sweep, in 12 colours | Product Studio › Backdrop | ✓ | ✓ |
| Backdrops: Tabletop, Spotlit, Scene (studio photos), Transparent | Product Studio › Backdrop | | ✓ |
| Shadows: contact and soft | Product Studio › Shadow | ✓ | ✓ |
| Shadows: cast shadow (direction, length, softness) and reflection | Product Studio › Shadow | | ✓ |
| Colour correction: auto white balance, auto brightness, exposure, contrast, colour, warmth, tint | Product Studio › Colour | ✓ | ✓ |
| Composition templates: Centred, Fill the frame (85%), Standing | Product Studio › Layout | ✓ | ✓ |
| Composition templates: Left/Right third, Hero close-up, Banner | Product Studio › Layout | | ✓ |
| Catalogue presets: Marketplace white, Soft paper, Studio grey | Product Studio › Setups, Batch | ✓ | ✓ |
| Catalogue presets: Tabletop, Glossy black, Pastel pop, Cut-out PNG | Product Studio › Setups, Batch | | ✓ |
| Export profiles: Square 1:1, Post 4:5, Story 9:16, Catalogue 3:4, Landscape 3:2, Banner 16:9 | Export | ✓ | ✓ |
| Export profile: PNG (transparent when the backdrop is) | Export | | ✓ |
| Download sizes 360p and 480p / 720p, 1080p, HD and 4K | Export | 360p, 480p | all |
| Before and after | Product Studio (Before/After), Batch results, Project grid | ✓ | ✓ |
| Batch editing: a setup or a look on many photos | Batch edit | 3 at a time | 50 at a time |
| Saved setups and looks (a look can be saved from the editor) | Product Studio, Editor, Batch | | ✓ |
| Brand mark (saved brandings from the editor's Watermark tool) on product shots and batches | Export, Batch | Photo Plus mark | own mark |
| Projects: kind (products, portraits, both), defaults reused by every tool, originals/edited filter, select, move, delete | Projects › a project | ✓ | ✓ |
| Packs: Catalogue essentials, Everyday looks | Project › Pack | ✓ | ✓ |
| Packs: Lifestyle tabletop, Boutique, Editorial looks | Project › Pack | | ✓ |

The free and Premium split is checked in two places. In the app, locked choices show a crown and open Premium. On the device, `ProductModule` refuses a Premium save with `PREMIUM_REQUIRED` (backdrop, cast shadow, reflection, PNG or a size above 480p), and a Kotlin test keeps the rules fixed.

## Workflows

- **Saving reusable setups.** Setups and looks are stored in the library (`presets` table) and listed in Product Studio, Batch and the project. A damaged row is skipped, never applied.
- **Reapplying a look to many photos.** Batch edit runs one photo after another on the device, with progress and Stop. A product setup makes each photo a catalogue shot; a look edits each photo's colour, optionally cropped to one shape, starting from its untouched photo. One failure doesn't stop the batch; a Premium refusal does, since every photo would fail.
- **Organising shoots into projects.** Projects open to a grid of their photos. Photos can be filtered (originals or edited), selected, moved, batch edited or deleted, and imported straight into the project.
- **Exporting in different aspect ratios.** Export profiles set the frame's shape and format; the download size sets its short edge. Looks can crop to 1:1, 4:5, 9:16, 3:2 or 16:9.
- **Keeping originals.** Every result is a new photo that records its original (`sourceImageId`, and `editBase` for looks). Originals are only read; deleting an edit leaves its original.
- **Reusing brand watermarks.** Brandings saved in the editor apply to product shots and batches, and a project can remember one.
- **Packs across projects.** A pack is a set of setups (product packs) or looks (look packs). Product packs suit product and mixed projects; look packs suit any project. Applying a pack makes its first item the project's default, and Product Studio then lists that pack's setups.

## How it works

### Cut-out (`native/engine/src/studio/product.cpp`, `pp_product_cutout`)

1. **The backdrop, everywhere.** A quadratic surface is fitted to the photo's border, reweighted so the product counts for little where it touches the edge. This follows paper that is brighter in the middle, as studio paper and phone shots are. Pixels that match it count as backdrop; the estimate is made again from them three times, spread under the product by a normalised blur. Shadowed paper doesn't count as backdrop, so under a shadow the estimate is the unshadowed paper.
2. **What differs from it.** The distance in colour from the backdrop, with the product's own shadow counting for little. A shadow is the backdrop darker in the same colour, and rarely takes more than half its light. A black product on white is still the product.
3. **The object mask.** On the phone, ML Kit subject segmentation provides it (taps choose objects, and every object is kept for a set). It keeps out specks and decides on a busy backdrop.
4. **Objects and holes.** Specks are dropped; a set of products stays a set. An enclosed area is filled when the object mask says it is the product, or, without a mask, when a solid product edge rings it (a white label on a bottle). Paper caught between the product and its shadow isn't filled.
5. **Edges at full size.** Each edge pixel's alpha is where its colour lies between the product's own colour just inside and the paper's just outside. Its colour is then freed of the paper (no fringe).

### Catalogue shot (`pp_product_render`)

- **Placement.** The product's box fills the asked share of the frame, centred or standing on its base, and can be mirrored.
- **Backdrops.** Solid, sweep (wall curving into floor), surface (a table with a horizon), radial, a scene photo or transparent, with a pool of light and darker corners, dithered.
- **Shadows.** Contact (the silhouette pressed onto the base line), soft (a wider pool), cast (the silhouette laid on the floor, leaning and foreshortened, softer and fainter with distance) and reflection (mirrored, fading out). In linear light; on a transparent frame they are soft black.
- **Colour.** White balance from the old backdrop's colour, and brightness up to white for a light paper, grey card or wall, then manual exposure, warmth, tint, contrast and colour. Highlights roll off.

### Device and app

- **Android.** `ProductModule`: open, refine, render, close; one worker, at most two sessions. `Product.kt` parses and checks setups, profiles and edge settings. JNI: `nativeProductCutout`, `nativeProductRender`.
- **App.**
  - Catalogue and rules: `src/services/product/setup.ts`.
  - Native bridge: `ProductService.ts`.
  - Batch runner: `batch.ts`.
  - Screens: `ProductStudioScreen`, `BatchScreen`, `ProjectScreen` (`src/screens/product`).
  - Library: migration v2 (presets, project kind and defaults), `PresetsRepository`, `ProjectsRepository.setKind/setDefaults`, `ImagesRepository.moveToProject/listDerived`.

## Validation

- **Engine:** 8 new tests (237 in all, under AddressSanitizer). They cover:
  - the shadow left out;
  - edge alpha within 0.01 of the truth, with no paper fringe;
  - real gaps kept open and specks dropped;
  - a set of products kept;
  - a busy backdrop following the mask;
  - brushes;
  - placement to the pixel;
  - shadows where the light sends them;
  - smooth backdrops;
  - colour correction;
  - bad input refused.
- **Real products:** 15 catalogue photos from the Amazon Berkeley Objects set (benchmark only, kept local), and the same products as simulated phone shots (warm grey paper a stop under, light fall-off, their own shadow, sensor noise), through every preset. Products come out whole, without their shadow, and the warm, dim cast is corrected.
- **Honest limit:** without the object mask, a part the same colour as the paper and touching the outline can't be told from it: a cream lampshade on cream paper, a white sole on white. Enclosed areas are kept. On the phone, the ML Kit object mask covers these; offline there was no object model to test that path on real photos (it is covered by tests). Keep and Remove brushes fix the rest.
- **Android:** 5 Kotlin tests (setup parsing and clamping, refusals, Premium rules, export frames, edge settings); build and lint pass.
- **App:** 14 new Jest tests (353 in all). They cover the catalogue rules, the migration on real SQLite, the batch runner (originals untouched, failures, Premium stop, Stop, crops), and the three screens driven against a real database.

## Speed

On the test machine (desktop CPU, one core per task): cut-out about 0.25–0.6 s for a 1600 × 1200 photo, catalogue shot about 30–110 ms at 900 px. The preview uses a 1280 px copy; saves cut out from up to 4096 px.
