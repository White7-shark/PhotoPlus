/**
 * Writes android/app/src/test/resources/beauty_fixture.json: recipes as JS
 * sends them (and their engine order), and live-shader uniforms, so the
 * Kotlin bridge is pinned to the app's resolver and the desktop parity tool.
 *
 *   node --experimental-strip-types scripts/beauty-fixture.mts
 */
import { readFileSync, writeFileSync } from 'node:fs';

import { DEFAULT_ADJUST, DEFAULT_PORTRAIT, gradeFloats, resolveRecipe, type BeautyPreset, type BeautyRecipe } from '../src/services/beauty/recipe.ts';
// @ts-expect-error plain JS module
import { blurWeights, faceUniforms, lowSize, studioMean } from './beauty-live.mjs';

const data = JSON.parse(readFileSync(new URL('../src/services/beauty/presets.json', import.meta.url), 'utf8')) as {
  presets: BeautyPreset[];
};
const flat = (r: BeautyRecipe) => [
  r.portrait.blur, r.portrait.smooth, r.portrait.blemish, r.portrait.even, r.portrait.brighten, r.portrait.edge,
  r.face.eyes, r.face.teeth, r.face.lips, r.face.underEye, r.face.shine, r.face.faceLight, r.face.studio, r.face.blush,
  ...r.face.blushColor, r.face.contour, r.face.highlight, ...gradeFloats(r.grade), r.autoLight ? 1 : 0,
];
const recipes = [
  ...data.presets.map(p => ({ id: p.id, recipe: resolveRecipe(p, DEFAULT_ADJUST) })),
  {
    id: 'golden_goddess+portrait@0.6',
    recipe: resolveRecipe(
      data.presets.find(p => p.id === 'golden_goddess') ?? null,
      { ...DEFAULT_ADJUST, intensity: 0.6, skin: 1.5, background: 0 },
      { ...DEFAULT_PORTRAIT, depthBlur: 0.8, backgroundTone: -0.4 },
    ),
  },
].map(r => ({ ...r, flat: flat(r.recipe) }));

// A frontal face (design units of the engine's test face, 240 x 300).
const pts: number[] = new Array(262).fill(0);
const put = (i: number, x: number, y: number) => {
  pts[2 * i] = x / 240;
  pts[2 * i + 1] = y / 300;
};
const cx = 120, cy = 130, rx = 52, ry = 68;
for (let i = 0; i < 36; i++) {
  const t = -Math.PI / 2 + (2 * Math.PI * i) / 36;
  put(i, cx + rx * Math.cos(t), cy + ry * Math.sin(t));
}
for (let i = 0; i < 16; i++) {
  const t = (2 * Math.PI * i) / 16;
  put(56 + i, cx - 22 + 11 * Math.cos(t), cy - 12 + 5 * Math.sin(t));
  put(72 + i, cx + 22 + 11 * Math.cos(t), cy - 12 + 5 * Math.sin(t));
}
for (let i = 0; i < 11; i++) put(88 + i, cx - 19 + (38 * i) / 10, cy + 38 - 4 * Math.sin((Math.PI * i) / 10));
for (let i = 0; i < 9; i++) {
  put(99 + i, cx - 14 + (28 * i) / 8, cy + 37);
  put(108 + i, cx + 14 - (28 * i) / 8, cy + 39);
  put(117 + i, cx + 17 - (34 * i) / 8, cy + 46);
}
put(126, cx, cy - 10);
put(127, cx, cy + 8);
put(128, cx - 7, cy + 14);
put(129, cx, cy + 16);
put(130, cx + 7, cy + 14);

const fixture = {
  recipes,
  face: { points: pts, w: 480, h: 600, expected: faceUniforms(pts, 480, 600) },
  studioMean: [-1, -0.35, 0, 0.35, 1].map(angle => ({ angle, value: studioMean(angle) })),
  blur: [1.2, 3.84, 6].map(sigma => ({ sigma, ...blurWeights(sigma) })),
  low: [[1920, 1080], [1280, 720], [4000, 3000], [200, 100]].map(([inW, inH]) => ({ inW, inH, ...lowSize(inW, inH) })),
};
writeFileSync(
  new URL('../android/app/src/test/resources/beauty_fixture.json', import.meta.url),
  JSON.stringify(fixture, null, 1) + '\n',
);
console.log(`${recipes.length} recipes`);
