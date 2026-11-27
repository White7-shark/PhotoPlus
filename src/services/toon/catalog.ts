/**
 * Toon Studio's choices, in the engine's order (native/engine toon.hpp): the
 * styles (each with its own starting controls), the backgrounds, and the
 * controls a picture is made with.
 */
import type { IconName } from '../../components/Icon';
import { t, type StringKey } from '../../i18n';

export interface ToonSettings {
  style: number;
  background: number;
  /** 0 fine .. 1 bold ink lines */
  lines: number;
  /** cel-shading bands 2..6 */
  shades: number;
  /** -1 muted .. 1 vivid */
  vivid: number;
  /** 0 simplest shapes .. 1 most detail */
  detail: number;
  /** 0 the person's own eyes .. 1 big anime eyes */
  eyes: number;
  /** 0 the skin's own texture .. 1 smooth two-tone skin */
  skin: number;
  /** 0 .. 1 a soft glow around the lights */
  glow: number;
  /** 0 the photo .. 1 fully drawn: the picture blended with the photo */
  strength: number;
}

export interface ToonStyle {
  id: number;
  key: 'anime' | 'manga' | 'comic' | 'cartoon' | 'webtoon' | 'retro';
  icon: IconName;
  /** The style's own way: where its controls start. */
  start: Omit<ToonSettings, 'style' | 'background' | 'strength'>;
}

export const STYLES: readonly ToonStyle[] = [
  {
    id: 0,
    key: 'anime',
    icon: 'sparkles',
    start: { lines: 0.5, shades: 3, vivid: 0.3, detail: 0.5, eyes: 0.45, skin: 0.7, glow: 0.35 },
  },
  {
    id: 1,
    key: 'manga',
    icon: 'brush',
    start: { lines: 0.6, shades: 3, vivid: 0, detail: 0.55, eyes: 0.4, skin: 0.7, glow: 0 },
  },
  {
    id: 2,
    key: 'comic',
    icon: 'grid',
    start: { lines: 0.75, shades: 4, vivid: 0.5, detail: 0.45, eyes: 0.15, skin: 0.5, glow: 0 },
  },
  {
    id: 3,
    key: 'cartoon',
    icon: 'aperture',
    start: { lines: 0.8, shades: 2, vivid: 0.5, detail: 0.25, eyes: 0.5, skin: 0.8, glow: 0.2 },
  },
  {
    id: 4,
    key: 'webtoon',
    icon: 'drop',
    start: { lines: 0.35, shades: 4, vivid: 0.1, detail: 0.6, eyes: 0.35, skin: 0.8, glow: 0.55 },
  },
  {
    id: 5,
    key: 'retro',
    icon: 'film',
    start: { lines: 0.55, shades: 2, vivid: -0.2, detail: 0.45, eyes: 0.4, skin: 0.6, glow: 0.15 },
  },
];

export const BACKGROUNDS = ['keep', 'soft', 'sky', 'sunset', 'night', 'plain'] as const;
export type ToonBackground = (typeof BACKGROUNDS)[number];

export const DEFAULT_TOON: ToonSettings = { style: 0, background: 0, strength: 1, ...STYLES[0].start };

const clamp = (v: unknown, lo: number, hi: number, def: number) =>
  typeof v === 'number' && Number.isFinite(v) ? Math.min(hi, Math.max(lo, v)) : def;

export function clampToon(s: Partial<ToonSettings>): ToonSettings {
  const d = DEFAULT_TOON;
  return {
    style: Math.round(clamp(s.style, 0, STYLES.length - 1, d.style)),
    background: Math.round(clamp(s.background, 0, BACKGROUNDS.length - 1, d.background)),
    lines: clamp(s.lines, 0, 1, d.lines),
    shades: Math.round(clamp(s.shades, 2, 6, d.shades)),
    vivid: clamp(s.vivid, -1, 1, d.vivid),
    detail: clamp(s.detail, 0, 1, d.detail),
    eyes: clamp(s.eyes, 0, 1, d.eyes),
    skin: clamp(s.skin, 0, 1, d.skin),
    glow: clamp(s.glow, 0, 1, d.glow),
    strength: clamp(s.strength, 0, 1, d.strength),
  };
}

/** A style chosen: its own way for every control (the background and Strength stay). */
export const withStyle = (s: ToonSettings, style: number): ToonSettings =>
  clampToon({ ...s, style, ...(STYLES[style] ?? STYLES[0]).start });

export const styleName = (s: ToonStyle) => t(`toon_style_${s.key}` as StringKey);
export const styleAbout = (s: ToonStyle) => t(`toon_style_${s.key}_about` as StringKey);
export const backgroundName = (b: ToonBackground) => t(`toon_bg_${b}` as StringKey);
