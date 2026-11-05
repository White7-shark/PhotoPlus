/**
 * Pure camera-UI logic: which controls to offer for the active camera,
 * dial values and labels, and capture presets. No React, no native calls,
 * so every rule here is unit-tested.
 */
import type { FeatureKey } from '../entitlements/features';
import type {
  AspectRatio,
  CameraCapabilities,
  CaptureModeId,
  CaptureOptions,
  CaptureStage,
  ManualSettings,
  ResolutionOption,
  SpotlightSettings,
  WhiteBalanceId,
} from './types';

// ---------------------------------------------------------------- modes

export type UiMode = 'photo' | 'enhance' | 'spotlight' | 'night' | 'superres' | 'pro';

export interface ModeDefinition {
  id: UiMode;
  /** Brand name shown in the app. */
  label: string;
  description: string;
  feature: FeatureKey;
}

/** The mode the camera opens in: single-photo Clarity, free for everyone. */
export const DEFAULT_MODE: UiMode = 'enhance';

export const MODES: readonly ModeDefinition[] = [
  { id: 'night', label: 'Moonlight', description: 'Bright, clean photos after dark', feature: 'multiShot' },
  {
    id: 'spotlight',
    label: 'Spotlight',
    description: 'Your subject shines, the background melts away',
    feature: 'spotlight',
  },
  { id: 'enhance', label: 'Clarity', description: 'Sharper, cleaner, brighter in one tap', feature: 'clarity' },
  { id: 'photo', label: 'Photo', description: 'Just the shot, untouched', feature: 'cameraCapture' },
  {
    id: 'superres',
    label: 'Ultra Detail',
    description: 'Twice the detail from a few quick shots',
    feature: 'multiShot',
  },
  { id: 'pro', label: 'Pro', description: 'Full manual control', feature: 'advancedCamera' },
];

export function modeLabel(mode: string): string {
  switch (mode) {
    case 'standard':
    case 'photo':
      return 'Photo';
    case 'raw':
      return 'RAW';
    case 'enhance':
      return 'Clarity';
    case 'spotlight':
      return 'Spotlight';
    case 'night':
      return 'Moonlight';
    case 'superres':
      return 'Ultra Detail';
    case 'pro':
      return 'Pro';
    case 'transform':
      return 'Transform';
    default:
      return 'Photo';
  }
}

/** Modes the active camera can actually run (capability detection, step 2.4). */
export function availableModes(caps: CameraCapabilities | null): ModeDefinition[] {
  if (!caps) {
    return MODES.filter(m => m.id === 'photo');
  }
  const supports = (id: CaptureModeId) => caps.captureModes.includes(id);
  const pro =
    caps.manual.exposure ||
    caps.manual.focus ||
    caps.manual.kelvin ||
    caps.raw ||
    caps.manual.whiteBalanceModes.length > 1;
  return MODES.filter(m => {
    switch (m.id) {
      case 'photo':
        return true;
      case 'enhance':
        return supports('enhance');
      case 'spotlight':
        return supports('spotlight');
      case 'night':
        return supports('night');
      case 'superres':
        return supports('superres');
      case 'pro':
        return pro;
    }
  });
}

export function captureModeFor(mode: UiMode, rawEnabled: boolean): CaptureModeId {
  switch (mode) {
    case 'photo':
      return 'standard';
    case 'pro':
      return rawEnabled ? 'raw' : 'standard';
    default:
      return mode;
  }
}

// ----------------------------------------------------------------- zoom

/** Quick zoom pills within the camera's range: ultra-wide (if any), 1×, 2×, 5×. */
export function zoomPresets(min: number, max: number): number[] {
  const out: number[] = [];
  if (min < 0.95) {
    out.push(Math.round(min * 10) / 10);
  }
  for (const z of [1, 2, 5, 10]) {
    if (z >= min && z <= max + 1e-6 && out.length < 4) {
      out.push(z);
    }
  }
  return out.length ? out : [1];
}

export function formatZoom(ratio: number): string {
  const rounded = ratio >= 10 ? Math.round(ratio) : Math.round(ratio * 10) / 10;
  return `${Number.isInteger(rounded) ? rounded.toFixed(0) : rounded.toFixed(1)}×`;
}

/** The pill that should look selected for a live ratio. */
export function activeZoomPreset(presets: number[], ratio: number): number {
  let best = presets[0];
  for (const p of presets) {
    if (ratio + 0.05 >= p) {
      best = p;
    }
  }
  return best;
}

// ------------------------------------------------------------- exposure

export function exposureIndices(ev: CameraCapabilities['exposureCompensation']): number[] {
  if (!ev) {
    return [];
  }
  // Show at most ~1/3 EV granularity so the dial stays usable.
  const stride = Math.max(1, Math.round(1 / 3 / ev.step));
  const out: number[] = [];
  for (let i = 0; i <= ev.maxIndex; i += stride) {
    out.push(i);
  }
  for (let i = -stride; i >= ev.minIndex; i -= stride) {
    out.unshift(i);
  }
  return out;
}

export function formatEv(index: number, step: number): string {
  const ev = Math.round(index * step * 10) / 10;
  if (ev === 0) {
    return '0';
  }
  return `${ev > 0 ? '+' : '−'}${Math.abs(ev).toFixed(1)}`;
}

// --------------------------------------------------------- manual dials

export function formatShutter(ns: number): string {
  if (ns >= 1e9) {
    const s = Math.round((ns / 1e9) * 10) / 10;
    return `${Number.isInteger(s) ? s.toFixed(0) : s.toFixed(1)}″`;
  }
  return `1/${Math.round(1e9 / ns)}`;
}

export function formatIso(iso: number): string {
  return `ISO ${Math.round(iso)}`;
}

/** Diopters (1/metres) to a distance label; 0 is infinity. */
export function formatFocusDistance(diopters: number): string {
  if (diopters <= 0.001) {
    return '∞';
  }
  const metres = 1 / diopters;
  if (metres >= 1) {
    return `${metres >= 10 ? Math.round(metres) : Math.round(metres * 10) / 10} m`;
  }
  return `${Math.round(metres * 100)} cm`;
}

/** Focus dial stops from infinity to the lens's closest distance. */
export function focusStops(minFocusDiopters: number | null): number[] {
  if (!minFocusDiopters || minFocusDiopters <= 0) {
    return [];
  }
  const metres = [Infinity, 10, 5, 3, 2, 1.5, 1, 0.7, 0.5, 0.35, 0.25, 0.2, 0.15, 0.12, 0.1, 0.08, 0.06, 0.05, 0.04];
  const stops = metres.map(m => (m === Infinity ? 0 : 1 / m)).filter(d => d <= minFocusDiopters + 1e-6);
  if (stops[stops.length - 1] < minFocusDiopters - 0.5) {
    stops.push(minFocusDiopters); // the lens's own closest focus
  }
  return stops;
}

export const KELVIN_STOPS: readonly number[] = Array.from({ length: 29 }, (_, i) => 2000 + i * 250).filter(
  k => k <= 9000,
);

export function nearestIndex(values: readonly number[], value: number): number {
  let best = 0;
  for (let i = 1; i < values.length; i++) {
    if (Math.abs(values[i] - value) < Math.abs(values[best] - value)) {
      best = i;
    }
  }
  return best;
}

/**
 * Keeps brightness constant when the user picks a shutter speed: the
 * camera's auto-exposure product (ISO × time) is preserved, so choosing
 * 1/1000 s for sport raises ISO instead of producing a black frame.
 */
export function compensatedIso(
  liveIso: number | null,
  liveExposureNs: number | null,
  targetExposureNs: number,
  range: [number, number] | null,
): number | null {
  if (!liveIso || !liveExposureNs || !range || targetExposureNs <= 0) {
    return null;
  }
  const iso = (liveIso * liveExposureNs) / targetExposureNs;
  return Math.round(Math.min(range[1], Math.max(range[0], iso)));
}

export const WHITE_BALANCE_LABELS: Record<WhiteBalanceId, string> = {
  auto: 'Auto',
  incandescent: 'Tungsten',
  fluorescent: 'Fluorescent',
  daylight: 'Daylight',
  cloudy: 'Cloudy',
  shade: 'Shade',
  kelvin: 'Kelvin',
};

// ------------------------------------------------------------ resolution

export function resolutionLabel(option: ResolutionOption): string {
  const mp = option.megapixels >= 10 ? Math.round(option.megapixels) : Math.round(option.megapixels * 10) / 10;
  return option.id === 'max' ? `Max · ${mp} MP` : `${mp} MP`;
}

export function resolutionsFor(caps: CameraCapabilities | null, ratio: AspectRatio): ResolutionOption[] {
  return caps?.resolutions[ratio] ?? [];
}

/** Aspect ratio of the preview frame as width / height in portrait. */
export function portraitAspect(ratio: AspectRatio): number {
  switch (ratio) {
    case '16:9':
      return 9 / 16;
    case '1:1':
      return 1;
    default:
      return 3 / 4;
  }
}

// --------------------------------------------------------------- presets

export type PresetId = 'portrait' | 'night' | 'sport' | 'landscape' | 'document';

// ------------------------------------------------------------- spotlight

export type BlurLevel = 'light' | 'medium' | 'strong';
export type GlowLevel = 'off' | 'natural' | 'smooth' | 'flawless';

export const BLUR_LEVELS: readonly { id: BlurLevel; label: string; value: number }[] = [
  { id: 'light', label: 'Light', value: 0.35 },
  { id: 'medium', label: 'Medium', value: 0.6 },
  { id: 'strong', label: 'Strong', value: 0.9 },
];

export const GLOW_LEVELS: readonly { id: GlowLevel; label: string; retouch: number; brighten: number }[] = [
  { id: 'off', label: 'Off', retouch: 0, brighten: 0 },
  { id: 'natural', label: 'Natural', retouch: 0.5, brighten: 0.3 },
  { id: 'smooth', label: 'Smooth', retouch: 0.75, brighten: 0.4 },
  { id: 'flawless', label: 'Flawless', retouch: 1, brighten: 0.5 },
];

export function spotlightSettings(blur: BlurLevel, glow: GlowLevel): SpotlightSettings {
  const b = BLUR_LEVELS.find(l => l.id === blur) ?? BLUR_LEVELS[1];
  const g = GLOW_LEVELS.find(l => l.id === glow) ?? GLOW_LEVELS[2];
  return { blur: b.value, retouch: g.retouch, brighten: g.brighten, pop: 0.5 };
}

export const MAX_SPOTLIGHT_POINTS = 4;

/**
 * Tapping in Spotlight chooses what stands out: a tap near an existing
 * marker removes it, otherwise a new marker is added (oldest dropped past
 * the limit). Points are normalised 0..1.
 */
export function toggleSpotlightPoint(
  points: readonly { x: number; y: number }[],
  tap: { x: number; y: number },
  radius = 0.06,
): { x: number; y: number }[] {
  const hit = points.findIndex(p => Math.hypot(p.x - tap.x, p.y - tap.y) < radius);
  if (hit >= 0) {
    return points.filter((_, i) => i !== hit);
  }
  return [...points, tap].slice(-MAX_SPOTLIGHT_POINTS);
}

export const flattenPoints = (points: readonly { x: number; y: number }[]): number[] => points.flatMap(p => [p.x, p.y]);

// ------------------------------------------------------------ vibe match

/** Engine look numbers (pp_style order): 9 tone quantiles, 3+3 zone colours, 2 spreads, vignette, grain, mean. */
export const LOOK_LENGTH = 20;

export interface LookSummary {
  /** -1 (dark, moody) … +1 (bright, airy). */
  brightness: number;
  /** -1 (cool, blue) … +1 (warm, golden). */
  warmth: number;
  /** -1 (muted) … +1 (vivid). */
  vividness: number;
}

export function isLook(v: unknown): v is number[] {
  return Array.isArray(v) && v.length === LOOK_LENGTH && v.every(n => typeof n === 'number' && Number.isFinite(n));
}

export function summariseLook(look: readonly number[]): LookSummary {
  const median = look[4];
  const midCb = look[10];
  const midCr = look[13];
  const spread = (look[15] + look[16]) / 2;
  const clamp = (v: number) => Math.max(-1, Math.min(1, v));
  return {
    brightness: clamp((median - 0.45) / 0.25),
    warmth: clamp((midCr - midCb - 0.02) / 0.08),
    vividness: clamp((spread - 0.06) / 0.04),
  };
}

export function describeLook(s: LookSummary): string {
  const light = s.brightness > 0.35 ? 'Bright' : s.brightness < -0.35 ? 'Moody' : 'Balanced';
  const tone = s.warmth > 0.35 ? 'warm' : s.warmth < -0.35 ? 'cool' : 'neutral';
  const colour = s.vividness > 0.35 ? 'vivid' : s.vividness < -0.35 ? 'soft' : '';
  return [light, tone, colour].filter(Boolean).join(', ');
}

/**
 * Camera settings that get the shot close to a reference look before the
 * look itself is applied: exposure follows the reference's brightness, and
 * white balance its warmth (Kelvin where supported, else a preset).
 */
export function lookCameraSettings(
  look: readonly number[],
  caps: CameraCapabilities | null,
): { evIndex: number; whiteBalance: WhiteBalanceId; kelvin: number | null } {
  const median = Math.max(0.05, Math.min(0.95, look[4]));
  // sRGB-ish median -> linear, relative to the 18% grey auto-exposure aims for.
  const ev = Math.max(-2, Math.min(2, Math.log2(Math.pow(median, 2.2) / 0.18)));
  const ec = caps?.exposureCompensation;
  // `|| 0` folds -0 into 0 so it never shows as "-0".
  const evIndex = ec ? Math.max(ec.minIndex, Math.min(ec.maxIndex, Math.round(ev / ec.step))) || 0 : 0;
  const warmth = look[13] - look[10];
  if (caps?.manual.kelvin) {
    // Higher white-balance Kelvin renders warmer, lower renders cooler.
    const kelvin = Math.round(Math.max(3500, Math.min(8500, 5500 + (warmth - 0.02) * 25000)) / 250) * 250;
    return { evIndex, whiteBalance: Math.abs(kelvin - 5500) < 400 ? 'auto' : 'kelvin', kelvin };
  }
  const modes = caps?.manual.whiteBalanceModes ?? ['auto'];
  const pick = (id: WhiteBalanceId): WhiteBalanceId => (modes.includes(id) ? id : 'auto');
  const w = warmth - 0.02;
  const whiteBalance =
    w > 0.06
      ? pick('shade')
      : w > 0.03
        ? pick('cloudy')
        : w < -0.06
          ? pick('incandescent')
          : w < -0.03
            ? pick('fluorescent')
            : 'auto';
  return { evIndex, whiteBalance, kelvin: null };
}

export interface CapturePreset {
  id: PresetId;
  title: string;
  description: string;
  mode: UiMode;
  aspectRatio?: AspectRatio;
  /** Engine tuning for processed modes. */
  tuning?: Pick<CaptureOptions, 'denoise' | 'sharpen' | 'detail' | 'vibrance' | 'autoTone'>;
  /** Fast shutter for motion, resolved against live exposure. */
  shutterNs?: number;
}

/** Additional capture presets (premium). */
export const PRESETS: readonly CapturePreset[] = [
  {
    id: 'portrait',
    title: 'Portrait',
    description: 'Soft background, glowing skin',
    mode: 'spotlight',
    aspectRatio: '4:3',
  },
  {
    id: 'night',
    title: 'Night',
    description: 'Bright, clean photos after dark',
    mode: 'night',
    tuning: { denoise: 1, sharpen: 0.8, detail: 0.3, vibrance: 0.2, autoTone: true },
  },
  {
    id: 'sport',
    title: 'Sport',
    description: '1/1000 s freezes motion; ISO follows',
    mode: 'pro',
    shutterNs: 1_000_000,
  },
  {
    id: 'landscape',
    title: 'Landscape',
    description: 'Wide frame, crisp detail, rich colour',
    mode: 'enhance',
    aspectRatio: '16:9',
    tuning: { denoise: 0.8, sharpen: 1, detail: 0.55, vibrance: 0.35, autoTone: true },
  },
  {
    id: 'document',
    title: 'Document',
    description: 'Sharp, clean text on a neutral page',
    mode: 'enhance',
    aspectRatio: '4:3',
    tuning: { denoise: 1, sharpen: 1, detail: 0.7, vibrance: 0, autoTone: true },
  },
];

export function availablePresets(caps: CameraCapabilities | null): CapturePreset[] {
  const modes = new Set(availableModes(caps).map(m => m.id));
  return PRESETS.filter(p => {
    if (!modes.has(p.mode)) {
      return false;
    }
    if (p.shutterNs !== undefined) {
      const range = caps?.manual.exposureTimeNs;
      return !!caps?.manual.exposure && !!range && p.shutterNs >= range[0] && p.shutterNs <= range[1];
    }
    return true;
  });
}

/** True when any manual override is active (for the "M" indicator and reset button). */
export function isManualActive(m: ManualSettings): boolean {
  return (
    m.iso != null ||
    m.exposureTimeNs != null ||
    m.focusDiopters != null ||
    (m.whiteBalance !== undefined && m.whiteBalance !== 'auto') ||
    !!m.aeLock ||
    !!m.awbLock
  );
}

// ------------------------------------------------------------- progress

export function progressLabel(stage: CaptureStage, frames: number): string {
  switch (stage) {
    case 'capturing':
      return frames > 1 ? 'Hold still…' : 'Capturing…';
    case 'processing':
      return frames > 1 ? 'Blending your shots…' : 'Perfecting your photo…';
    case 'styling':
      return 'Adding the spotlight…';
    case 'saving':
      return 'Saving…';
  }
}
