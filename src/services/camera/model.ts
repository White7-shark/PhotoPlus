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
  ManualSettings,
  ResolutionOption,
  WhiteBalanceId,
} from './types';

// ---------------------------------------------------------------- modes

export type UiMode = 'photo' | 'enhance' | 'night' | 'superres' | 'pro';

export interface ModeDefinition {
  id: UiMode;
  label: string;
  description: string;
  feature: FeatureKey;
}

export const MODES: readonly ModeDefinition[] = [
  { id: 'night', label: 'Night', description: 'Fuses a burst to clean up low light', feature: 'advancedCamera' },
  { id: 'enhance', label: 'Enhance', description: 'Restores detail, removes noise and blur', feature: 'cameraCapture' },
  { id: 'photo', label: 'Photo', description: 'The camera’s own photo', feature: 'cameraCapture' },
  {
    id: 'superres',
    label: 'Super-res',
    description: 'Burst super-resolution: twice the detail',
    feature: 'advancedCamera',
  },
  { id: 'pro', label: 'Pro', description: 'Manual ISO, shutter, focus and white balance', feature: 'advancedCamera' },
];

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
    description: 'Gentle detail and natural skin tones',
    mode: 'enhance',
    aspectRatio: '4:3',
    tuning: { denoise: 1, sharpen: 0.6, detail: 0.15, vibrance: 0.1, autoTone: true },
  },
  {
    id: 'night',
    title: 'Night',
    description: 'Multi-frame fusion for low light',
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

export function progressLabel(stage: 'capturing' | 'processing' | 'saving', frames: number): string {
  switch (stage) {
    case 'capturing':
      return frames > 1 ? `Hold still · capturing ${frames} frames` : 'Capturing';
    case 'processing':
      return frames > 1 ? `Fusing ${frames} frames` : 'Enhancing';
    case 'saving':
      return 'Saving';
  }
}
