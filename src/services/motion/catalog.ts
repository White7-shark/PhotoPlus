/**
 * Photo Motion's choices, in the engine's order (native/engine motion.hpp):
 * the camera moves, the effects, the video lengths, and the settings a
 * video is made with.
 */
import type { IconName } from '../../components/Icon';
import { t, type StringKey } from '../../i18n';

export interface CameraMove {
  /** The engine's number. */
  id: number;
  key: 'still' | 'push_in' | 'pull_out' | 'orbit' | 'pan' | 'dolly_zoom' | 'float' | 'rise';
  icon: IconName;
}

export const CAMERAS: readonly CameraMove[] = [
  { id: 0, key: 'still', icon: 'target' },
  { id: 1, key: 'push_in', icon: 'plus' },
  { id: 2, key: 'pull_out', icon: 'minus' },
  { id: 3, key: 'orbit', icon: 'refresh' },
  { id: 4, key: 'pan', icon: 'compare' },
  { id: 5, key: 'dolly_zoom', icon: 'aperture' },
  { id: 6, key: 'float', icon: 'drop' },
  { id: 7, key: 'rise', icon: 'chevronLeft' },
];

export interface MotionEffect {
  id: number;
  key: 'none' | 'snow' | 'rain' | 'bokeh' | 'sparkles' | 'petals' | 'fireflies' | 'light_leak' | 'film' | 'sun_rays';
  icon: IconName;
}

export const EFFECTS: readonly MotionEffect[] = [
  { id: 0, key: 'none', icon: 'close' },
  { id: 1, key: 'snow', icon: 'sparkles' },
  { id: 2, key: 'rain', icon: 'drop' },
  { id: 3, key: 'bokeh', icon: 'aperture' },
  { id: 4, key: 'sparkles', icon: 'sparkles' },
  { id: 5, key: 'petals', icon: 'brush' },
  { id: 6, key: 'fireflies', icon: 'flash' },
  { id: 7, key: 'light_leak', icon: 'sun' },
  { id: 8, key: 'film', icon: 'film' },
  { id: 9, key: 'sun_rays', icon: 'sun' },
];

/** Video lengths offered, in seconds (the device makes 2..10). */
export const DURATIONS = [3, 5, 8, 10] as const;
export type Duration = (typeof DURATIONS)[number];

/** Motion arrows and pins the person draws (0..1 of the photo's width and height). */
export interface MotionArrow {
  x0: number;
  y0: number;
  x1: number;
  y1: number;
}
export interface MotionPin {
  x: number;
  y: number;
  /** Radius, a share of the photo's short side. */
  r: number;
}
/** The device takes up to 32 of each (faces are pinned on top of these). */
export const MAX_ARROWS = 32;
export const MAX_PINS = 32;
export const PIN_RADIUS = 0.06;

export interface MotionSettings {
  camera: number;
  /** 0..1 */
  cameraAmount: number;
  /** 0..2: how fast the drawn motion flows. */
  flowSpeed: number;
  sky: boolean;
  /** -1..1: left or right, and how fast. */
  skySpeed: number;
  effect: number;
  /** 0..1 */
  effectAmount: number;
  loop: boolean;
  keepFaces: boolean;
  duration: Duration;
}

/** A gentle float with faces kept still: a good first look for any photo. */
export const DEFAULT_SETTINGS: MotionSettings = {
  camera: 6,
  cameraAmount: 0.5,
  flowSpeed: 1,
  sky: false,
  skySpeed: 0.5,
  effect: 0,
  effectAmount: 0.6,
  loop: true,
  keepFaces: true,
  duration: 5,
};

/** Below this share of the photo, no sky is offered. */
export const SKY_MIN = 0.03;

const clamp = (v: unknown, lo: number, hi: number, def: number) =>
  typeof v === 'number' && Number.isFinite(v) ? Math.min(hi, Math.max(lo, v)) : def;
const clamp01 = (v: number) => clamp(v, 0, 1, 0);

export function clampSettings(s: Partial<MotionSettings>): MotionSettings {
  const d = DEFAULT_SETTINGS;
  return {
    camera: Math.round(clamp(s.camera, 0, CAMERAS.length - 1, d.camera)),
    cameraAmount: clamp(s.cameraAmount, 0, 1, d.cameraAmount),
    flowSpeed: clamp(s.flowSpeed, 0, 2, d.flowSpeed),
    sky: s.sky === true,
    skySpeed: clamp(s.skySpeed, -1, 1, d.skySpeed),
    effect: Math.round(clamp(s.effect, 0, EFFECTS.length - 1, d.effect)),
    effectAmount: clamp(s.effectAmount, 0, 1, d.effectAmount),
    loop: s.loop !== false,
    keepFaces: s.keepFaces !== false,
    duration: (DURATIONS as readonly number[]).includes(s.duration as number) ? (s.duration as Duration) : d.duration,
  };
}

/** Arrows as the device takes them ([x0, y0, x1, y1, …]); too-short ones are dropped. */
export function packArrows(arrows: readonly MotionArrow[]): number[] {
  return arrows
    .filter(a => Math.hypot(a.x1 - a.x0, a.y1 - a.y0) >= 0.01)
    .slice(0, MAX_ARROWS)
    .flatMap(a => [clamp01(a.x0), clamp01(a.y0), clamp01(a.x1), clamp01(a.y1)]);
}

export function packPins(pins: readonly MotionPin[]): number[] {
  return pins.slice(0, MAX_PINS).flatMap(p => [clamp01(p.x), clamp01(p.y), clamp(p.r, 0.01, 0.5, PIN_RADIUS)]);
}

/**
 * A drawn stroke (points 0..1) becomes arrows: one per stretch of about a
 * tenth of the picture, so a curved stroke (a river's bend, a wave of hair)
 * flows along its curve.
 */
export function strokeToArrows(points: readonly { x: number; y: number }[], step = 0.1): MotionArrow[] {
  if (points.length < 2) return [];
  const out: MotionArrow[] = [];
  let start = points[0];
  let run = 0;
  for (let i = 1; i < points.length; i++) {
    run += Math.hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
    if (run >= step || i === points.length - 1) {
      const end = points[i];
      if (Math.hypot(end.x - start.x, end.y - start.y) >= 0.01)
        out.push({ x0: start.x, y0: start.y, x1: end.x, y1: end.y });
      start = end;
      run = 0;
    }
  }
  return out;
}

/** The arrow or pin nearest a tap (within reach, 0..1 units), for erasing. */
export function nearest(
  arrows: readonly MotionArrow[],
  pins: readonly MotionPin[],
  x: number,
  y: number,
  reach = 0.06,
): { kind: 'arrow' | 'pin'; index: number } | null {
  let best: { kind: 'arrow' | 'pin'; index: number } | null = null;
  let bestD = reach;
  arrows.forEach((a, i) => {
    const dx = a.x1 - a.x0;
    const dy = a.y1 - a.y0;
    const len2 = dx * dx + dy * dy || 1e-9;
    const t = Math.min(1, Math.max(0, ((x - a.x0) * dx + (y - a.y0) * dy) / len2));
    const d = Math.hypot(x - (a.x0 + t * dx), y - (a.y0 + t * dy));
    if (d < bestD) {
      bestD = d;
      best = { kind: 'arrow', index: i };
    }
  });
  pins.forEach((p, i) => {
    const d = Math.hypot(x - p.x, y - p.y);
    if (d < bestD) {
      bestD = d;
      best = { kind: 'pin', index: i };
    }
  });
  return best;
}

export const cameraName = (c: CameraMove) => t(`motion_camera_${c.key}` as StringKey);
export const cameraAbout = (c: CameraMove) => t(`motion_camera_${c.key}_about` as StringKey);
export const effectName = (e: MotionEffect) => t(`motion_effect_${e.key}` as StringKey);
