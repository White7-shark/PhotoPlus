/**
 * Prints every beauty preset as an engine recipe line for the beauty_demo
 * tool, resolved by the app's own resolver (src/services/beauty/recipe.ts):
 *
 *   node --experimental-strip-types scripts/beauty-recipes.mts [--intensity 0.8] [--depth-blur 0.6] [--none] [--grade-only]
 *
 * --grade-only keeps only the lighting / background / colour grade (skin and
 * face off), which the live shader computes exactly as the engine does.
 *
 * Line: id, 6 portrait floats, 13 face floats, 58 grade floats.
 */
import { readFileSync } from 'node:fs';

import {
  DEFAULT_ADJUST,
  DEFAULT_PORTRAIT,
  gradeFloats,
  resolveRecipe,
  type BeautyPreset,
  type BeautyRecipe,
} from '../src/services/beauty/recipe.ts';

const args = process.argv.slice(2);
const opt = (name: string) => {
  const i = args.indexOf(name);
  return i >= 0 ? Number(args[i + 1]) : undefined;
};
const intensity = opt('--intensity') ?? 1;
const depthBlur = opt('--depth-blur');
const portrait = depthBlur === undefined ? null : { ...DEFAULT_PORTRAIT, depthBlur };
const data = JSON.parse(readFileSync(new URL('../src/services/beauty/presets.json', import.meta.url), 'utf8')) as {
  presets: BeautyPreset[];
};

const gradeOnly = args.includes('--grade-only');

function line(id: string, recipe: BeautyRecipe): string {
  const neutral = resolveRecipe(null, DEFAULT_ADJUST, portrait);
  const r = gradeOnly ? { ...neutral, grade: recipe.grade } : recipe;
  const p = r.portrait;
  const f = r.face;
  const nums = [
    p.blur, p.smooth, p.blemish, p.even, p.brighten, p.edge,
    f.eyes, f.teeth, f.lips, f.underEye, f.shine, f.faceLight, f.studio, f.blush, ...f.blushColor, f.contour, f.highlight,
    ...gradeFloats(r.grade),
  ];
  return [id, ...nums.map(n => Number(n.toFixed(5)))].join(' ');
}

const adjust = { ...DEFAULT_ADJUST, intensity };
if (args.includes('--none')) {
  console.log(line('none', resolveRecipe(null, adjust, portrait)));
}
for (const preset of data.presets) {
  console.log(line(preset.id, resolveRecipe(preset, adjust, portrait)));
}
