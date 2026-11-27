/**
 * Photo Arts: the person's say over the painting (pp_art_controls), and
 * the tool's saved settings. Every default is the artist's own way.
 */
import type { MastersLevel } from './catalog';

export interface ArtControls {
  /** 0.6 finer marks .. 1.6 bolder marks. */
  brush: number;
  /** 0 the photo's own colours .. 1 the artist's palette. */
  palette: number;
  /** -1 muted .. +1 vivid. */
  vivid: number;
  /** 0 the artist's freedom .. 1 true to the sitter. */
  likeness: number;
  /** 0 smooth .. 1 the artist's full texture .. 1.5 heavy (canvas, paper, paint relief, bristle marks). Smooth by default. */
  texture: number;
  /** 0 fresh paint .. 1.5 old varnish and cracks. */
  age: number;
  /** How far the photo is transformed: 0 natural .. 1 the full transformation. */
  intensity: number;
  /** What it is painted on (SURFACES index; 0 the artist's own). */
  surface: number;
  /** Behind the person (BACKDROPS index; 0 the artist's). */
  backdrop: number;
}

/** The touch's sliders. */
export type ControlKey = 'brush' | 'palette' | 'vivid' | 'likeness' | 'texture' | 'age';

export const ARTIST_WAY: Readonly<ArtControls> = {
  brush: 1,
  palette: 1,
  vivid: 0,
  likeness: 0.5,
  texture: 0.4,
  age: 1,
  intensity: 0.6,
  surface: 0,
  backdrop: 0,
};

/** How far the painting goes. */
export const INTENSITIES = [
  { id: 'natural', value: 0.3 },
  { id: 'balanced', value: 0.6 },
  { id: 'full', value: 1 },
] as const;
export type IntensityId = (typeof INTENSITIES)[number]['id'];
/** The choice nearest a stored value. */
export const intensityOf = (v: number): IntensityId =>
  INTENSITIES.reduce((best, i) => (Math.abs(i.value - v) < Math.abs(best.value - v) ? i : best)).id;

/** Painted on (engine order, pp_art_controls.surface). */
export const SURFACES = ['artist', 'linen', 'paper', 'watercolour', 'rough', 'parchment', 'board'] as const;
/** Behind the person (engine order, pp_art_controls.backdrop). */
export const BACKDROPS = ['artist', 'scene', 'studio', 'umber', 'sky', 'landscape', 'gold'] as const;
/** Museum frames (engine order, pp_art_frame) and each one's border as a share of the painting's short side. */
export const FRAMES = ['none', 'gilded', 'baroque', 'black', 'walnut', 'mat'] as const;
const FRAME_BORDER = [0, 0.085, 0.13, 0.05, 0.075, 0.122];

/**
 * Where the painting sits inside a framed picture of [w] x [h]: its inset
 * on every side, as a share of the framed width and height (for comparing
 * with the photo).
 */
export function frameInset(frame: number, w: number, h: number): { x: number; y: number } {
  const r = FRAME_BORDER[frame] ?? 0;
  if (r <= 0 || w <= 0 || h <= 0) return { x: 0, y: 0 };
  // b = r * min(W, H), with W = w - 2b and H = h - 2b.
  const b = (r * Math.min(w, h)) / (1 + 2 * r);
  return { x: b / w, y: b / h };
}

/** Range and step of each control, in the order shown. */
export const CONTROL_RANGES: readonly { key: ControlKey; min: number; max: number; step: number }[] = [
  { key: 'brush', min: 0.6, max: 1.6, step: 0.05 },
  { key: 'palette', min: 0, max: 1, step: 0.05 },
  { key: 'vivid', min: -1, max: 1, step: 0.05 },
  { key: 'likeness', min: 0, max: 1, step: 0.05 },
  { key: 'texture', min: 0, max: 1.5, step: 0.05 },
  { key: 'age', min: 0, max: 1.5, step: 0.05 },
];

const choice = (v: unknown, n: number) => typeof v === 'number' && Number.isInteger(v) && v >= 0 && v < n;

export function isArtControls(v: unknown): v is ArtControls {
  if (!v || typeof v !== 'object') return false;
  const c = v as Record<string, unknown>;
  return (
    CONTROL_RANGES.every(
      r =>
        typeof c[r.key] === 'number' &&
        Number.isFinite(c[r.key]) &&
        (c[r.key] as number) >= r.min &&
        (c[r.key] as number) <= r.max,
    ) &&
    typeof c.intensity === 'number' &&
    c.intensity >= 0 &&
    c.intensity <= 1 &&
    choice(c.surface, SURFACES.length) &&
    choice(c.backdrop, BACKDROPS.length)
  );
}

/** Whether the person has changed anything from the artist's way. */
export const isArtistWay = (c: ArtControls) => CONTROL_RANGES.every(r => Math.abs(c[r.key] - ARTIST_WAY[r.key]) < 1e-3);

/** Kept within range (a slider's float steps, an older saved value). */
export function clampControls(c: Partial<ArtControls>): ArtControls {
  const out = { ...ARTIST_WAY };
  for (const r of CONTROL_RANGES) {
    const v = c[r.key];
    if (typeof v === 'number' && Number.isFinite(v))
      out[r.key] = Math.min(r.max, Math.max(r.min, Math.round(v * 1000) / 1000));
  }
  if (typeof c.intensity === 'number' && Number.isFinite(c.intensity))
    out.intensity = Math.min(1, Math.max(0, c.intensity));
  if (choice(c.surface, SURFACES.length)) out.surface = c.surface as number;
  if (choice(c.backdrop, BACKDROPS.length)) out.backdrop = c.backdrop as number;
  return out;
}

/** Photo Arts's settings (Settings, and remembered from the tool). */
export interface PhotoArtsSettings {
  /** The finish a painting starts at. */
  level: MastersLevel;
  /** Keep the person's touch (the controls) for next time. */
  remember: boolean;
  /** The person's touch, kept when `remember` is on. */
  controls: ArtControls;
  /** Paint the artist cards with the person's photo (off: names only, lighter on the battery). */
  cardPreviews: boolean;
  /** The museum frame (FRAMES index; 0 none). */
  frame: number;
}

export const DEFAULT_PHOTO_ARTS: PhotoArtsSettings = {
  level: 1,
  remember: true,
  controls: { ...ARTIST_WAY },
  cardPreviews: true,
  frame: 0,
};

export function isPhotoArtsSettings(v: unknown): v is PhotoArtsSettings {
  if (!v || typeof v !== 'object') return false;
  const s = v as Record<string, unknown>;
  return (
    (s.level === 0 || s.level === 1 || s.level === 2) &&
    typeof s.remember === 'boolean' &&
    typeof s.cardPreviews === 'boolean' &&
    choice(s.frame, FRAMES.length) &&
    isArtControls(s.controls)
  );
}
