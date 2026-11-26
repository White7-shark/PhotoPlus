import { t as tx } from '../../i18n';
/**
 * Studio light, film looks, gradient backdrops and composition templates
 * for the Photoshoot Studio (Phase 11).
 *
 * A photo has no depth, so studio light is simulated: the person's shape is
 * estimated from their outline (and their face), and the light is laid on
 * relative to what the photo already has. It looks consistent, not
 * physically exact: every look has a strength to ease it back, and the
 * direction and softness can be changed when something looks wrong.
 *
 * Free: the soft studio and bright looks, gradient backdrops, vignette,
 * warmth, tint, and the basic grades (vivid, soft, black and white), and two
 * composition templates. Premium: directional, rim and coloured lights,
 * changing a light's direction or softness, film looks (faded blacks, split
 * toning, grain), and the other templates. The device checks the same rules.
 * Pure (unit-tested).
 */

// ------------------------------------------------------------------ light

/** Linear colour gains for a light (1, 1, 1 is white). */
export type Gains = readonly [number, number, number];

export interface LightParams {
  /** Degrees around the person: 0 from the camera, -90 from the frame's left, 90 from its right. */
  angle: number;
  /** Degrees above the eyes. */
  height: number;
  /** 0 a bare bulb (hard shadows) .. 1 a big softbox. */
  softness: number;
  /** The key light's intensity (1 normal). */
  key: number;
  /** The fill from the other side, 0..1 of the key. */
  fill: number;
  keyColour?: Gains;
  fillColour?: Gains;
  rimLeft?: number;
  rimRight?: number;
  rimColour?: Gains;
}

export interface GradientDef {
  id: string;
  name: string;
  /** #RRGGBB */
  from: string;
  to: string;
  /** A pool of `from` behind the person, falling to `to`; else top to bottom. */
  radial: boolean;
}

export const GRADIENTS: readonly GradientDef[] = [
  { id: 'grad-studio-grey', name: tx('light_studio_grey'), from: '#D9D4CC', to: '#8F8A84', radial: true },
  { id: 'grad-charcoal', name: tx('light_charcoal'), from: '#5A5650', to: '#1E1C1A', radial: true },
  { id: 'grad-midnight', name: tx('light_midnight'), from: '#2A2C33', to: '#08090C', radial: true },
  { id: 'grad-warm-sand', name: tx('light_warm_sand'), from: '#E3D6C4', to: '#9A8770', radial: true },
  { id: 'grad-sunset', name: tx('light_sunset'), from: '#F2C58F', to: '#B4664A', radial: true },
  { id: 'grad-deep-teal', name: tx('light_deep_teal'), from: '#3F6F78', to: '#14262B', radial: true },
  { id: 'grad-violet-night', name: tx('light_violet_night'), from: '#3B1D5E', to: '#0B1030', radial: true },
  { id: 'grad-blush', name: tx('light_blush'), from: '#F3DCDD', to: '#C99598', radial: true },
  { id: 'grad-sky', name: tx('light_sky'), from: '#CFE3F5', to: '#6E95BF', radial: false },
  { id: 'grad-forest', name: tx('light_forest'), from: '#4F6B4C', to: '#1A2619', radial: false },
  { id: 'grad-high-key', name: tx('light_bright_white'), from: '#FBFAF8', to: '#E4E2DE', radial: true },
];

// ------------------------------------------------------------------ finish

export interface FinishParams {
  vignette?: number;
  vignetteSize?: number;
  warmth?: number;
  tint?: number;
  contrast?: number;
  saturation?: number;
  fade?: number;
  shadowTone?: Gains;
  highlightTone?: Gains;
  toneAmount?: number;
  grain?: number;
  grainSize?: number;
  mono?: boolean;
}

/** A finish uses film-look settings (Premium). */
export const isFilmFinish = (f: FinishParams) => (f.fade ?? 0) > 0 || (f.toneAmount ?? 0) > 0 || (f.grain ?? 0) > 0 || f.mono === true;

export interface FilmGrade {
  id: string;
  name: string;
  premium: boolean;
  finish: FinishParams;
}

export const FILM_GRADES: readonly FilmGrade[] = [
  { id: 'none', name: tx('light_none'), premium: false, finish: {} },
  { id: 'vivid', name: tx('light_vivid'), premium: false, finish: { contrast: 0.15, saturation: 0.15 } },
  { id: 'soft', name: tx('light_soft'), premium: false, finish: { contrast: -0.12, saturation: -0.05 } },
  { id: 'black-white', name: tx('light_black_white'), premium: false, finish: { saturation: -1, contrast: 0.15 } },
  {
    id: 'warm-film',
    name: tx('light_warm_film'),
    premium: true,
    finish: { warmth: 0.15, fade: 0.15, saturation: -0.12, highlightTone: [0.15, 0.08, -0.05], shadowTone: [-0.05, 0.05, 0.08], toneAmount: 0.6, grain: 0.3 },
  },
  { id: 'teal-orange', name: tx('light_teal_orange'), premium: true, finish: { shadowTone: [-0.35, 0.08, 0.35], highlightTone: [0.35, 0.12, -0.3], toneAmount: 0.85, contrast: 0.15 } },
  { id: 'matte', name: tx('light_matte'), premium: true, finish: { fade: 0.35, contrast: -0.1, saturation: -0.15 } },
  { id: 'classic-chrome', name: tx('light_classic_chrome'), premium: true, finish: { contrast: 0.25, saturation: -0.25, shadowTone: [0, 0.04, 0.08], toneAmount: 0.5, grain: 0.2 } },
  { id: 'noir', name: tx('light_noir'), premium: true, finish: { mono: true, contrast: 0.45, grain: 0.5, vignette: 0.5 } },
  { id: 'cinema', name: tx('light_cinema'), premium: true, finish: { contrast: 0.2, fade: 0.08, shadowTone: [-0.2, 0.05, 0.25], highlightTone: [0.2, 0.1, -0.1], toneAmount: 0.6, vignette: 0.3 } },
];

// ------------------------------------------------------------------ looks

export interface LightPreset {
  id: string;
  name: string;
  /** In plain words, what it does. */
  about: string;
  premium: boolean;
  /** Null: the photo's own light. */
  light: LightParams | null;
  /** The backdrop it is shot against (applied with the look; the person can change it after). */
  gradient?: string;
  /** Its grade (applied with the look). */
  finish?: FinishParams;
}

export const LIGHT_PRESETS: readonly LightPreset[] = [
  { id: 'as-shot', name: tx('light_as_shot'), about: tx('light_the_light_of_your_photo'), premium: false, light: null },
  {
    id: 'soft-studio',
    name: tx('light_soft_studio'),
    about: tx('light_a_big_soft_light_just'),
    premium: false,
    light: { angle: -20, height: 40, softness: 0.85, key: 1, fill: 0.6 },
    gradient: 'grad-studio-grey',
  },
  {
    id: 'high-key',
    name: tx('light_bright'),
    about: tx('light_bright_soft_light_from_the'),
    premium: false,
    light: { angle: -10, height: 35, softness: 0.95, key: 1, fill: 0.85 },
    gradient: 'grad-high-key',
    finish: { contrast: -0.1 },
  },
  {
    id: 'rembrandt',
    name: tx('light_rembrandt'),
    about: tx('light_light_from_high_to_one'),
    premium: true,
    light: { angle: -50, height: 40, softness: 0.35, key: 1, fill: 0.15 },
    gradient: 'grad-charcoal',
    finish: { vignette: 0.35 },
  },
  { id: 'loop', name: tx('light_loop'), about: tx('light_slightly_to_one_side_and'), premium: true, light: { angle: -35, height: 30, softness: 0.5, key: 1, fill: 0.3 }, gradient: 'grad-warm-sand' },
  { id: 'butterfly', name: tx('light_butterfly'), about: tx('light_high_in_front_shadows_under'), premium: true, light: { angle: 0, height: 55, softness: 0.45, key: 1, fill: 0.35 }, gradient: 'grad-studio-grey' },
  {
    id: 'split',
    name: tx('light_split'),
    about: tx('light_from_the_side_half_the'),
    premium: true,
    light: { angle: -88, height: 10, softness: 0.3, key: 1, fill: 0.04 },
    gradient: 'grad-midnight',
    finish: { vignette: 0.4, contrast: 0.15 },
  },
  { id: 'clamshell', name: tx('light_clamshell'), about: tx('light_soft_light_above_and_below'), premium: true, light: { angle: 0, height: 45, softness: 0.9, key: 1, fill: 0.75 }, gradient: 'grad-high-key' },
  {
    id: 'rim-light',
    name: tx('light_rim_light'),
    about: tx('light_lights_behind_on_both_sides'),
    premium: true,
    light: { angle: -20, height: 30, softness: 0.7, key: 0.75, fill: 0.3, rimLeft: 0.75, rimRight: 0.75 },
    gradient: 'grad-midnight',
    finish: { vignette: 0.3 },
  },
  {
    id: 'low-key',
    name: tx('light_low_key'),
    about: tx('light_one_light_from_the_side'),
    premium: true,
    light: { angle: -55, height: 40, softness: 0.4, key: 1, fill: 0.08 },
    gradient: 'grad-midnight',
    finish: { vignette: 0.55, contrast: 0.25 },
  },
  {
    id: 'golden-hour',
    name: tx('light_golden_hour'),
    about: tx('light_low_warm_sun_from_the'),
    premium: true,
    light: { angle: 60, height: 15, softness: 0.6, key: 1, fill: 0.35, keyColour: [1.25, 0.98, 0.7], fillColour: [0.85, 0.95, 1.15] },
    gradient: 'grad-sunset',
    finish: { warmth: 0.3, vignette: 0.2 },
  },
  {
    id: 'neon-gels',
    name: tx('light_neon'),
    about: tx('light_magenta_and_cyan_coloured_lights'),
    premium: true,
    light: {
      angle: -70,
      height: 15,
      softness: 0.55,
      key: 1,
      fill: 0.75,
      keyColour: [1.45, 0.55, 1.25],
      fillColour: [0.45, 1.1, 1.5],
      rimRight: 0.6,
      rimColour: [0.4, 1.2, 1.4],
    },
    gradient: 'grad-violet-night',
    finish: { contrast: 0.2, saturation: 0.15 },
  },
  {
    id: 'noir',
    name: tx('light_film_noir'),
    about: tx('light_hard_side_light_black_and'),
    premium: true,
    light: { angle: -80, height: 30, softness: 0.25, key: 1, fill: 0.06 },
    gradient: 'grad-charcoal',
    finish: FILM_GRADES.find(g => g.id === 'noir')!.finish,
  },
];

/** What the person chose: a look, how strong, and (Premium) its direction, softness and rim changed. */
export interface LightChoice {
  preset: string;
  /** 0..1 */
  strength: number;
  angle?: number;
  softness?: number;
  rim?: number;
}

export const lightPreset = (id: string) => LIGHT_PRESETS.find(p => p.id === id);

/** The look's light was changed by hand (direction, softness or rim): Premium. */
export const isCustomLight = (c: LightChoice) => c.angle != null || c.softness != null || c.rim != null;

export function isPremiumLight(c: LightChoice | null): boolean {
  if (!c) return false;
  const p = lightPreset(c.preset);
  return !p || p.premium || isCustomLight(c);
}

/** How the device is told about the light, or undefined for the photo's own. */
export function lightRequest(c: LightChoice | null): Record<string, unknown> | undefined {
  if (!c) return undefined;
  const p = lightPreset(c.preset);
  if (!p?.light) return undefined;
  const l = p.light;
  const rim = c.rim;
  const req: Record<string, unknown> = {
    preset: isCustomLight(c) ? 'custom' : p.id,
    strength: clamp01(c.strength),
    angle: c.angle ?? l.angle,
    height: l.height,
    softness: c.softness != null ? clamp01(c.softness) : l.softness,
    key: l.key,
    fill: l.fill,
    rimLeft: rim != null ? clamp01(rim) : (l.rimLeft ?? 0),
    rimRight: rim != null ? clamp01(rim) : (l.rimRight ?? 0),
  };
  if (l.keyColour) req.keyColour = [...l.keyColour];
  if (l.fillColour) req.fillColour = [...l.fillColour];
  if (l.rimColour) req.rimColour = [...l.rimColour];
  return req;
}

// ------------------------------------------------------------------ finish choice

/** A grade and the person's own vignette, warmth and tint on top. */
export interface FinishChoice {
  grade: string;
  vignette: number;
  warmth: number;
  tint: number;
}

export const NO_FINISH: FinishChoice = { grade: 'none', vignette: 0, warmth: 0, tint: 0 };

export const filmGrade = (id: string) => FILM_GRADES.find(g => g.id === id);

export function isPremiumFinish(c: FinishChoice): boolean {
  const g = filmGrade(c.grade);
  return !g || g.premium;
}

/** The finish the device applies (the grade, then the person's own settings), or undefined for none. */
export function finishRequest(c: FinishChoice, look?: FinishParams): Record<string, unknown> | undefined {
  const g = filmGrade(c.grade)?.finish ?? {};
  const base: FinishParams = { ...(look ?? {}), ...g };
  const f: FinishParams = {
    ...base,
    vignette: clamp(c.vignette + (base.vignette ?? 0), -1, 1),
    warmth: clamp(c.warmth + (base.warmth ?? 0), -1, 1),
    tint: clamp(c.tint + (base.tint ?? 0), -1, 1),
  };
  const out: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(f)) {
    if (v == null) continue;
    if (typeof v === 'number' && v === 0) continue;
    out[k] = Array.isArray(v) ? [...v] : v;
  }
  return Object.keys(out).length > 0 ? out : undefined;
}

// ------------------------------------------------------------------ composition templates

/** Where the person stands in the frame: the bottom centre of their box (x, y) and its height, all 0..1 of the frame. */
export interface CompositionTemplate {
  id: string;
  name: string;
  about: string;
  premium: boolean;
  x: number;
  y: number;
  height: number;
}

export const TEMPLATES: readonly CompositionTemplate[] = [
  { id: 'centre', name: tx('light_centre'), about: tx('light_centred_with_room_around_you'), premium: false, x: 0.5, y: 1, height: 0.82 },
  { id: 'close-up', name: tx('light_close_up'), about: tx('light_head_and_shoulders_filling_the'), premium: false, x: 0.5, y: 1, height: 1.18 },
  { id: 'left-third', name: tx('light_left_third'), about: tx('light_on_the_left_third_looking'), premium: true, x: 0.36, y: 1, height: 0.8 },
  { id: 'right-third', name: tx('light_right_third'), about: tx('light_on_the_right_third_looking'), premium: true, x: 0.64, y: 1, height: 0.8 },
  { id: 'editorial', name: tx('light_editorial'), about: tx('light_small_in_a_large_frame'), premium: true, x: 0.5, y: 1, height: 0.6 },
  { id: 'cover', name: tx('light_cover'), about: tx('light_tall_and_centred_with_space'), premium: true, x: 0.5, y: 1, height: 0.72 },
];

export const template = (id: string) => TEMPLATES.find(t => t.id === id);

function clamp(v: number, lo: number, hi: number) {
  return Math.min(hi, Math.max(lo, v));
}
function clamp01(v: number) {
  return clamp(Number.isFinite(v) ? v : 0, 0, 1);
}
