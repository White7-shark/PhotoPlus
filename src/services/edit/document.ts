import type { PropPlacement } from './placement';

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
  props: PropPlacement[];
  frame: { id: string; zoom: number; x: number; y: number } | null;
}

export const EMPTY_DOCUMENT: EditDocument = {
  quarterTurns: 0,
  flipH: false,
  straighten: 0,
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
  props: [],
  frame: null,
};

const isNum = (v: unknown, min: number, max: number): v is number =>
  typeof v === 'number' && Number.isFinite(v) && v >= min && v <= max;

/** True for a well-formed document (e.g. read back from a saved photo). */
export function isEditDocument(v: unknown): v is EditDocument {
  if (!v || typeof v !== 'object') return false;
  const d = v as Record<string, unknown>;
  const crop = d.crop as Record<string, unknown> | null;
  return (
    [0, 1, 2, 3].includes(d.quarterTurns as number) &&
    typeof d.flipH === 'boolean' &&
    isNum(d.straighten, -45, 45) &&
    (crop === null || (!!crop && ['x', 'y', 'w', 'h'].every(k => isNum(crop[k], 0, 1)))) &&
    ['exposure'].every(k => isNum(d[k], -2, 2)) &&
    ['brightness', 'contrast', 'saturation', 'warmth'].every(k => isNum(d[k], -1, 1)) &&
    isNum(d.sharpen, 0, 1) &&
    isNum(d.retouch, 0, 1) &&
    typeof d.transform === 'boolean' &&
    Array.isArray(d.props) &&
    d.props.length <= 12
  );
}

/** Whether anything was changed. */
export function hasEdits(d: EditDocument): boolean {
  return JSON.stringify(d) !== JSON.stringify(EMPTY_DOCUMENT);
}

/** The geometry part (what face positions and props are measured against). */
export function geometryOf(d: EditDocument): Pick<EditDocument, 'quarterTurns' | 'flipH' | 'straighten' | 'crop'> {
  return { quarterTurns: d.quarterTurns, flipH: d.flipH, straighten: d.straighten, crop: d.crop };
}
