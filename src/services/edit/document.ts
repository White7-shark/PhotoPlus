import { isBackdropChoice, type BackdropChoice } from '../backdrops/catalog';
import { isBeautyAdjust } from '../beauty/camera';
import { isSkinToneChoice, type SkinToneChoice } from '../skintones/catalog';
import type { BeautyAdjust, BeautyRecipe } from '../beauty/recipe';
import type { PropPlacement } from './placement';

/**
 * Advanced portrait controls (Premium), mirrored by EditDocument.kt's
 * PortraitChoice. All 0..1 except lightAngle, bgLight and bgColor (-1..1).
 */
export interface PortraitAdvanced {
  // Skin (replace the Glow preset's single strength)
  smooth: number;
  blemish: number;
  even: number;
  shine: number;
  underEye: number;
  // Face
  eyes: number;
  teeth: number;
  lips: number;
  slim: number;
  // Light
  faceLight: number;
  studio: number;
  /** Studio light from picture left (-1) to right (1). */
  lightAngle: number;
  // Background (with Spotlight's subject)
  bgLight: number;
  bgColor: number;
  /** Finer edges (hair) and no fringe of the old background. */
  edge: number;
}

export const NEUTRAL_PORTRAIT: PortraitAdvanced = {
  smooth: 0,
  blemish: 0,
  even: 0,
  shine: 0,
  underEye: 0,
  eyes: 0,
  teeth: 0,
  lips: 0,
  slim: 0,
  faceLight: 0,
  studio: 0,
  lightAngle: -0.5,
  bgLight: 0,
  bgColor: 0,
  edge: 0,
};

/** Advanced controls that start where a Glow preset left the photo (so switching changes nothing). */
export function portraitFromGlow(retouch: number): PortraitAdvanced {
  return { ...NEUTRAL_PORTRAIT, smooth: retouch, blemish: retouch, even: retouch };
}

const SIGNED_PORTRAIT_KEYS = new Set<keyof PortraitAdvanced>(['lightAngle', 'bgLight', 'bgColor']);

export function isPortraitAdvanced(v: unknown): v is PortraitAdvanced {
  if (!v || typeof v !== 'object') return false;
  const p = v as Record<string, unknown>;
  return (Object.keys(NEUTRAL_PORTRAIT) as (keyof PortraitAdvanced)[]).every(k => {
    const x = p[k];
    const min = SIGNED_PORTRAIT_KEYS.has(k) ? -1 : 0;
    return typeof x === 'number' && Number.isFinite(x) && x >= min && x <= 1;
  });
}

/**
 * A photo's edits (mirrored and validated natively by EditDocument.kt). The
 * original photo is never changed: this document is replayed on it every
 * time, so every edit can be undone or changed later.
 */
export interface EditDocument {
  quarterTurns: 0 | 1 | 2 | 3;
  flipH: boolean;
  /** Degrees, -45..45. */
  straighten: number;
  /** Perspective fix, -1..1: vertical > 0 widens the top (buildings shot from below), horizontal > 0 the right. */
  perspectiveV: number;
  perspectiveH: number;
  /** Fractions of the turned and straightened photo, or null for none. */
  crop: { x: number; y: number; w: number; h: number } | null;
  exposure: number;
  brightness: number;
  contrast: number;
  saturation: number;
  warmth: number;
  sharpen: number;
  look: { id: string; strength: number } | null;
  /** Glow skin retouch, 0 (off) .. 1. */
  retouch: number;
  /** Spotlight: what should shine (taps on the photo) and how much the rest melts. */
  spotlight: { points: number[]; blur: number } | null;
  focus: { kind: 'radial' | 'tilt'; x: number; y: number; size: number; angle: number; strength: number } | null;
  /** The high-end lens look (Premium; a few a day on the free plan). */
  transform: boolean;
  /** Advanced portrait controls (Premium), or null for the Glow presets only. */
  portrait: PortraitAdvanced | null;
  props: PropPlacement[];
  frame: { id: string; zoom: number; x: number; y: number } | null;
  /** A beauty filter: the preset, its sliders, and the recipe they resolve to (what the engine applies). */
  beauty: { preset: string; adjust: BeautyAdjust; recipe: BeautyRecipe } | null;
  /** A backdrop: a new background behind the person. */
  backdrop: BackdropChoice | null;
  /** A Skin Tone shade for the person's skin. */
  skinTone: SkinToneChoice | null;
}

export const EMPTY_DOCUMENT: EditDocument = {
  quarterTurns: 0,
  flipH: false,
  straighten: 0,
  perspectiveV: 0,
  perspectiveH: 0,
  crop: null,
  exposure: 0,
  brightness: 0,
  contrast: 0,
  saturation: 0,
  warmth: 0,
  sharpen: 0,
  look: null,
  retouch: 0,
  spotlight: null,
  focus: null,
  transform: false,
  portrait: null,
  props: [],
  frame: null,
  beauty: null,
  backdrop: null,
  skinTone: null,
};

const isNum = (v: unknown, min: number, max: number): v is number =>
  typeof v === 'number' && Number.isFinite(v) && v >= min && v <= max;

function isBeautyChoice(v: unknown): boolean {
  if (!v || typeof v !== 'object') return false;
  const b = v as Record<string, unknown>;
  return typeof b.preset === 'string' && isBeautyAdjust(b.adjust) && !!b.recipe && typeof b.recipe === 'object';
}

/** True for a well-formed document (e.g. read back from a saved photo). */
export function isEditDocument(v: unknown): v is EditDocument {
  if (!v || typeof v !== 'object') return false;
  const d = v as Record<string, unknown>;
  const crop = d.crop as Record<string, unknown> | null;
  return (
    [0, 1, 2, 3].includes(d.quarterTurns as number) &&
    typeof d.flipH === 'boolean' &&
    isNum(d.straighten, -45, 45) &&
    // Added later: older saved edits have no perspective (see withDefaults).
    [d.perspectiveV, d.perspectiveH].every(v => v === undefined || isNum(v, -1, 1)) &&
    (crop === null || (!!crop && ['x', 'y', 'w', 'h'].every(k => isNum(crop[k], 0, 1)))) &&
    ['exposure'].every(k => isNum(d[k], -2, 2)) &&
    ['brightness', 'contrast', 'saturation', 'warmth'].every(k => isNum(d[k], -1, 1)) &&
    isNum(d.sharpen, 0, 1) &&
    isNum(d.retouch, 0, 1) &&
    typeof d.transform === 'boolean' &&
    // Added later (older saved edits have none: see withDefaults).
    (d.portrait === undefined || d.portrait === null || isPortraitAdvanced(d.portrait)) &&
    (d.beauty === undefined || d.beauty === null || isBeautyChoice(d.beauty)) &&
    (d.backdrop === undefined || d.backdrop === null || isBackdropChoice(d.backdrop)) &&
    (d.skinTone === undefined || d.skinTone === null || isSkinToneChoice(d.skinTone)) &&
    Array.isArray(d.props) &&
    d.props.length <= 12
  );
}

/** A saved document with any fields added since it was saved filled in. */
export function withDefaults(d: EditDocument): EditDocument {
  return { ...EMPTY_DOCUMENT, ...d };
}

/** Whether anything was changed. */
export function hasEdits(d: EditDocument): boolean {
  return JSON.stringify(d) !== JSON.stringify(EMPTY_DOCUMENT);
}

/** The geometry part (what face positions and props are measured against). */
export function geometryOf(
  d: EditDocument,
): Pick<EditDocument, 'quarterTurns' | 'flipH' | 'perspectiveV' | 'perspectiveH' | 'straighten' | 'crop'> {
  return {
    quarterTurns: d.quarterTurns,
    flipH: d.flipH,
    perspectiveV: d.perspectiveV,
    perspectiveH: d.perspectiveH,
    straighten: d.straighten,
    crop: d.crop,
  };
}
