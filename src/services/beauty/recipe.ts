/**
 * Beauty presets → the engine's recipe. Pure functions (no React Native),
 * shared by the app, its tests and the desktop validation tool
 * (scripts/beauty-demo.mjs runs this file with Node's type stripping).
 *
 * A preset holds its own processing profile, grouped by the camera's
 * sliders: Skin, Face, Lighting and Background. Its colour look is scaled
 * by Intensity only. Colours are hex; positions, sizes, hue widths and
 * skin protection are part of the look and are never scaled.
 */

export type Tier = 'free' | 'premium';
type Rgb = [number, number, number];

export interface PresetSkin {
  smooth?: number;
  blemish?: number;
  even?: number;
  brighten?: number;
}
export interface PresetFace {
  eyes?: number;
  teeth?: number;
  lips?: number;
  underEye?: number;
  shine?: number;
  blush?: number;
  blushColor?: string;
  contour?: number;
  highlight?: number;
}
export interface PresetLighting {
  faceLight?: number;
  studio?: number;
  subjectLight?: number;
  lightWarmth?: number;
  shadowLift?: number;
  rim?: number;
  rimColor?: string;
  bloom?: number;
  softFocus?: number;
  skinSheen?: number;
}
export interface PresetBackground {
  bgExposure?: number;
  bgSaturation?: number;
  bgTint?: string;
  bgTintAmount?: number;
  bgGlow?: number;
  bgGlowX?: number;
  bgGlowY?: number;
  bgGlowSize?: number;
  bgGlowColor?: string;
  bgVignette?: number;
  bgHaze?: number;
}
export interface PresetColor {
  temperature?: number;
  tint?: number;
  contrast?: number;
  blacks?: number;
  highlights?: number;
  saturation?: number;
  vibrance?: number;
  shadowColor?: string;
  shadowAmount?: number;
  highlightColor?: string;
  highlightAmount?: number;
  bandColor?: string;
  bandWidth?: number;
  bandHueShift?: number;
  bandSaturation?: number;
  skinProtect?: number;
  skinWarmth?: number;
  skinRosy?: number;
  skinLuminance?: number;
}

export interface BeautyPreset {
  id: string;
  name: string;
  tier: Tier;
  description: string;
  skin: PresetSkin;
  face: PresetFace;
  lighting: PresetLighting;
  background: PresetBackground;
  color: PresetColor;
}

/** The camera's sliders: Intensity 0..1, groups 0..1.5 (1 = the preset as designed). */
export interface BeautyAdjust {
  intensity: number;
  skin: number;
  face: number;
  lighting: number;
  background: number;
}

export const DEFAULT_ADJUST: BeautyAdjust = { intensity: 1, skin: 1, face: 1, lighting: 1, background: 1 };
export const GROUP_MAX = 1.5;

/** Portrait mode's own controls, independent of the preset. */
export interface PortraitControls {
  /** Background blur 0..1. */
  depthBlur: number;
  /** -1..1 */
  subjectLight: number;
  /** -1..1, stops */
  backgroundLight: number;
  /** -1 cool .. 1 warm */
  backgroundTone: number;
  /** 0..1 */
  edge: number;
}

export const DEFAULT_PORTRAIT: PortraitControls = {
  depthBlur: 0.6,
  subjectLight: 0,
  backgroundLight: 0,
  backgroundTone: 0,
  edge: 0.5,
};

/** The engine recipe (pp_beauty_params), with colours as gamma RGB 0..1. */
export interface BeautyRecipe {
  portrait: { blur: number; smooth: number; blemish: number; even: number; brighten: number; edge: number };
  face: {
    eyes: number;
    teeth: number;
    lips: number;
    underEye: number;
    shine: number;
    faceLight: number;
    studio: number;
    blush: number;
    blushColor: Rgb;
    contour: number;
    highlight: number;
  };
  grade: GradeRecipe;
  autoLight: boolean;
}

export interface GradeRecipe {
  temperature: number;
  tint: number;
  contrast: number;
  blacks: number;
  highlights: number;
  saturation: number;
  vibrance: number;
  shadowColor: Rgb;
  shadowAmount: number;
  highlightColor: Rgb;
  highlightAmount: number;
  bandColor: Rgb;
  bandWidth: number;
  bandHueShift: number;
  bandSaturation: number;
  skinProtect: number;
  skinWarmth: number;
  skinRosy: number;
  skinLuminance: number;
  subjectLight: number;
  lightWarmth: number;
  shadowLift: number;
  rim: number;
  rimColor: Rgb;
  bloom: number;
  softFocus: number;
  skinSheen: number;
  bgExposure: number;
  bgSaturation: number;
  bgTint: Rgb;
  bgTintAmount: number;
  bgGlow: number;
  bgGlowX: number;
  bgGlowY: number;
  bgGlowSize: number;
  bgGlowColor: Rgb;
  bgVignette: number;
  bgHaze: number;
  bgWarmth: number;
}

const GREY: Rgb = [0.5, 0.5, 0.5];

export function hexToRgb(hex: string | undefined, fallback: Rgb = GREY): Rgb {
  const m = typeof hex === 'string' ? /^#?([0-9a-f]{6})$/i.exec(hex.trim()) : null;
  if (!m) {
    return fallback;
  }
  const n = parseInt(m[1], 16);
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
}

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
const num = (v: unknown, fallback = 0) => (typeof v === 'number' && Number.isFinite(v) ? v : fallback);

/** Every value at 0: the photo as taken. */
export function neutralRecipe(): BeautyRecipe {
  return resolveRecipe(null, DEFAULT_ADJUST);
}

/**
 * The recipe for a preset at the user's slider settings, plus Portrait
 * mode's controls when the photo is a Spotlight one. `preset` null = no
 * beauty (Portrait controls still apply).
 */
export function resolveRecipe(
  preset: BeautyPreset | null,
  adjust: BeautyAdjust,
  portrait: PortraitControls | null = null,
): BeautyRecipe {
  const k = clamp(num(adjust.intensity, 1), 0, 1);
  const g = (v: number) => clamp(num(v, 1), 0, GROUP_MAX) * k;
  const ks = g(adjust.skin);
  const kf = g(adjust.face);
  const kl = g(adjust.lighting);
  const kb = g(adjust.background);
  const s = preset?.skin ?? {};
  const f = preset?.face ?? {};
  const l = preset?.lighting ?? {};
  const b = preset?.background ?? {};
  const c = preset?.color ?? {};
  const unit = (v: unknown, m: number) => clamp(num(v) * m, 0, 1);
  const signed = (v: unknown, m: number) => clamp(num(v) * m, -1, 1);
  return {
    portrait: {
      blur: portrait ? clamp(num(portrait.depthBlur), 0, 1) : 0,
      smooth: unit(s.smooth, ks),
      blemish: unit(s.blemish, ks),
      even: unit(s.even, ks),
      brighten: unit(s.brighten, ks),
      edge: portrait ? clamp(num(portrait.edge), 0, 1) : 0,
    },
    face: {
      eyes: unit(f.eyes, kf),
      teeth: unit(f.teeth, kf),
      lips: unit(f.lips, kf),
      underEye: unit(f.underEye, kf),
      shine: unit(f.shine, kf),
      faceLight: unit(l.faceLight, kl),
      studio: unit(l.studio, kl),
      blush: unit(f.blush, kf),
      blushColor: hexToRgb(f.blushColor, [0.86, 0.42, 0.47]),
      contour: unit(f.contour, kf),
      highlight: unit(f.highlight, kf),
    },
    grade: {
      temperature: signed(c.temperature, k),
      tint: signed(c.tint, k),
      contrast: signed(c.contrast, k),
      blacks: signed(c.blacks, k),
      highlights: signed(c.highlights, k),
      saturation: signed(c.saturation, k),
      vibrance: signed(c.vibrance, k),
      shadowColor: hexToRgb(c.shadowColor),
      shadowAmount: unit(c.shadowAmount, k),
      highlightColor: hexToRgb(c.highlightColor),
      highlightAmount: unit(c.highlightAmount, k),
      bandColor: hexToRgb(c.bandColor),
      bandWidth: clamp(num(c.bandWidth), 0, 90),
      bandHueShift: clamp(num(c.bandHueShift) * k, -60, 60),
      bandSaturation: signed(c.bandSaturation, k),
      skinProtect: clamp(num(c.skinProtect, 0.75), 0, 1),
      skinWarmth: signed(c.skinWarmth, k),
      skinRosy: unit(c.skinRosy, k),
      skinLuminance: signed(c.skinLuminance, k),
      subjectLight: clamp(num(l.subjectLight) * kl + (portrait ? num(portrait.subjectLight) : 0), -1, 1),
      lightWarmth: signed(l.lightWarmth, kl),
      shadowLift: unit(l.shadowLift, kl),
      rim: unit(l.rim, kl),
      rimColor: hexToRgb(l.rimColor, [1, 0.92, 0.8]),
      bloom: unit(l.bloom, kl),
      softFocus: unit(l.softFocus, kl),
      skinSheen: unit(l.skinSheen, kl),
      bgExposure: clamp(num(b.bgExposure) * kb + (portrait ? num(portrait.backgroundLight) : 0), -1, 1),
      bgSaturation: signed(b.bgSaturation, kb),
      bgTint: hexToRgb(b.bgTint),
      bgTintAmount: unit(b.bgTintAmount, kb),
      bgGlow: unit(b.bgGlow, kb),
      bgGlowX: clamp(num(b.bgGlowX, 0.8), 0, 1),
      bgGlowY: clamp(num(b.bgGlowY, 0.2), 0, 1),
      bgGlowSize: clamp(num(b.bgGlowSize, 0.35), 0.02, 1.5),
      bgGlowColor: hexToRgb(b.bgGlowColor, [1, 0.8, 0.5]),
      bgVignette: unit(b.bgVignette, kb),
      bgHaze: unit(b.bgHaze, kb),
      bgWarmth: portrait ? clamp(num(portrait.backgroundTone), -1, 1) : 0,
    },
    autoLight: true,
  };
}

/** The grade's fields in pp_beauty_grade order (the native bridge copies a float array). */
export const GRADE_ORDER = [
  'temperature',
  'tint',
  'contrast',
  'blacks',
  'highlights',
  'saturation',
  'vibrance',
  'shadowColor',
  'shadowAmount',
  'highlightColor',
  'highlightAmount',
  'bandColor',
  'bandWidth',
  'bandHueShift',
  'bandSaturation',
  'skinProtect',
  'skinWarmth',
  'skinRosy',
  'skinLuminance',
  'subjectLight',
  'lightWarmth',
  'shadowLift',
  'rim',
  'rimColor',
  'bloom',
  'softFocus',
  'skinSheen',
  'bgExposure',
  'bgSaturation',
  'bgTint',
  'bgTintAmount',
  'bgGlow',
  'bgGlowX',
  'bgGlowY',
  'bgGlowSize',
  'bgGlowColor',
  'bgVignette',
  'bgHaze',
  'bgWarmth',
] as const satisfies readonly (keyof GradeRecipe)[];

/** pp_beauty_grade's float count (with its reserved tail). */
export const GRADE_FLOATS = 58;

/** The grade flattened in native order (colours take three floats), zero-padded. */
export function gradeFloats(grade: GradeRecipe): number[] {
  const out: number[] = [];
  for (const key of GRADE_ORDER) {
    const v = grade[key];
    if (Array.isArray(v)) {
      out.push(...v);
    } else {
      out.push(v);
    }
  }
  while (out.length < GRADE_FLOATS) {
    out.push(0);
  }
  return out;
}

/** True when the recipe changes anything at all. */
export function isActive(r: BeautyRecipe): boolean {
  const p = r.portrait;
  const f = r.face;
  if (p.blur || p.smooth || p.blemish || p.even || p.brighten) {
    return true;
  }
  if (
    f.eyes ||
    f.teeth ||
    f.lips ||
    f.underEye ||
    f.shine ||
    f.faceLight ||
    f.studio ||
    f.blush ||
    f.contour ||
    f.highlight
  ) {
    return true;
  }
  const g = r.grade;
  return (
    [
      g.temperature,
      g.tint,
      g.contrast,
      g.blacks,
      g.highlights,
      g.saturation,
      g.vibrance,
      g.shadowAmount,
      g.highlightAmount,
      g.skinWarmth,
      g.skinRosy,
      g.skinLuminance,
      g.subjectLight,
      g.lightWarmth,
      g.shadowLift,
      g.rim,
      g.bloom,
      g.softFocus,
      g.skinSheen,
      g.bgExposure,
      g.bgSaturation,
      g.bgTintAmount,
      g.bgGlow,
      g.bgVignette,
      g.bgHaze,
      g.bgWarmth,
    ].some(v => v !== 0) ||
    (g.bandWidth > 0 && (g.bandHueShift !== 0 || g.bandSaturation !== 0))
  );
}
