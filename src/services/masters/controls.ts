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
  /** 0 the photo's own background, painted .. 1 the artist's. */
  background: number;
}

export type ControlKey = keyof ArtControls;

export const ARTIST_WAY: Readonly<ArtControls> = {
  brush: 1,
  palette: 1,
  vivid: 0,
  likeness: 0.5,
  texture: 0.4,
  age: 1,
  background: 1,
};

/** Range and step of each control, in the order shown. */
export const CONTROL_RANGES: readonly { key: ControlKey; min: number; max: number; step: number }[] = [
  { key: 'brush', min: 0.6, max: 1.6, step: 0.05 },
  { key: 'palette', min: 0, max: 1, step: 0.05 },
  { key: 'vivid', min: -1, max: 1, step: 0.05 },
  { key: 'likeness', min: 0, max: 1, step: 0.05 },
  { key: 'texture', min: 0, max: 1.5, step: 0.05 },
  { key: 'age', min: 0, max: 1.5, step: 0.05 },
  { key: 'background', min: 0, max: 1, step: 0.05 },
];

export function isArtControls(v: unknown): v is ArtControls {
  if (!v || typeof v !== 'object') return false;
  const c = v as Record<string, unknown>;
  return CONTROL_RANGES.every(
    r =>
      typeof c[r.key] === 'number' &&
      Number.isFinite(c[r.key]) &&
      (c[r.key] as number) >= r.min &&
      (c[r.key] as number) <= r.max,
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
}

export const DEFAULT_PHOTO_ARTS: PhotoArtsSettings = {
  level: 1,
  remember: true,
  controls: { ...ARTIST_WAY },
  cardPreviews: true,
};

export function isPhotoArtsSettings(v: unknown): v is PhotoArtsSettings {
  if (!v || typeof v !== 'object') return false;
  const s = v as Record<string, unknown>;
  return (
    (s.level === 0 || s.level === 1 || s.level === 2) &&
    typeof s.remember === 'boolean' &&
    typeof s.cardPreviews === 'boolean' &&
    isArtControls(s.controls)
  );
}
