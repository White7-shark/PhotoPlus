import type {
  AspectRatio,
  CameraCapabilities,
  CaptureModeId,
  CapturePlan,
  DeviceClass,
  ResolutionId,
  ResolutionOption,
  WhiteBalanceId,
} from './types';

type Obj = Record<string, unknown>;

const isObj = (v: unknown): v is Obj => typeof v === 'object' && v !== null && !Array.isArray(v);
const num = (v: unknown, fallback = 0): number => (typeof v === 'number' && Number.isFinite(v) ? v : fallback);
const bool = (v: unknown): boolean => v === true;
const nums = (v: unknown): number[] =>
  Array.isArray(v) ? v.filter((x): x is number => typeof x === 'number' && Number.isFinite(x)) : [];
const pair = (v: unknown): [number, number] | null => {
  const n = nums(v);
  return n.length === 2 && n[0] <= n[1] ? [n[0], n[1]] : null;
};
const oneOf = <T extends string>(v: unknown, values: readonly T[]): v is T =>
  typeof v === 'string' && (values as readonly string[]).includes(v);

export const ASPECT_RATIOS = ['4:3', '16:9', '1:1'] as const satisfies readonly AspectRatio[];
export const RESOLUTION_IDS = ['max', '12mp', '8mp', '5mp', '2mp'] as const satisfies readonly ResolutionId[];
export const CAPTURE_MODES = [
  'standard',
  'enhance',
  'spotlight',
  'night',
  'superres',
  'raw',
] as const satisfies readonly CaptureModeId[];
const WB_MODES = [
  'auto',
  'incandescent',
  'fluorescent',
  'daylight',
  'cloudy',
  'shade',
  'kelvin',
] as const satisfies readonly WhiteBalanceId[];
const TIERS = ['entry', 'standard', 'performance'] as const satisfies readonly DeviceClass[];

function parseResolutions(v: unknown): Partial<Record<AspectRatio, ResolutionOption[]>> {
  const out: Partial<Record<AspectRatio, ResolutionOption[]>> = {};
  if (!isObj(v)) {
    return out;
  }
  for (const ratio of ASPECT_RATIOS) {
    const list = v[ratio];
    if (!Array.isArray(list)) {
      continue;
    }
    out[ratio] = list
      .filter(isObj)
      .flatMap(r =>
        oneOf(r.id, RESOLUTION_IDS) && num(r.width) > 0 && num(r.height) > 0
          ? [{ id: r.id, width: num(r.width), height: num(r.height), megapixels: num(r.megapixels) }]
          : [],
      );
  }
  return out;
}

function parsePlans(v: unknown): Partial<Record<CaptureModeId, CapturePlan>> {
  const out: Partial<Record<CaptureModeId, CapturePlan>> = {};
  if (!isObj(v)) {
    return out;
  }
  for (const mode of CAPTURE_MODES) {
    const p = v[mode];
    if (isObj(p)) {
      out[mode] = { frames: num(p.frames, 1), workingDimension: num(p.workingDimension), upscale: num(p.upscale, 1) };
    }
  }
  return out;
}

/**
 * Validates the native capability map. Anything missing or malformed is
 * treated as unsupported, so the UI simply hides that control.
 */
export function parseCameraCapabilities(raw: unknown): CameraCapabilities | null {
  if (!isObj(raw)) {
    return null;
  }
  const zoom = isObj(raw.zoom) ? raw.zoom : {};
  const ev = isObj(raw.exposureCompensation) ? raw.exposureCompensation : null;
  const m = isObj(raw.manual) ? raw.manual : {};
  const minFocus = num(m.minFocusDiopters, 0);
  const size = isObj(raw.captureSize) ? raw.captureSize : null;
  const zoomMin = Math.max(0.1, num(zoom.min, 1));
  const evMin = ev ? Math.round(num(ev.minIndex)) : 0;
  const evMax = ev ? Math.round(num(ev.maxIndex)) : 0;

  return {
    lensFacing: raw.lensFacing === 'front' ? 'front' : 'back',
    hardwareLevel: typeof raw.hardwareLevel === 'string' ? raw.hardwareLevel : 'unknown',
    zoom: { min: zoomMin, max: Math.max(zoomMin, num(zoom.max, 1)) },
    exposureCompensation:
      ev && evMax > evMin && num(ev.step) > 0 ? { minIndex: evMin, maxIndex: evMax, step: num(ev.step) } : null,
    flash: bool(raw.flash),
    torch: bool(raw.torch),
    tapToFocus: bool(raw.tapToFocus),
    manual: {
      exposure: bool(m.exposure) && pair(m.iso) !== null && pair(m.exposureTimeNs) !== null,
      iso: pair(m.iso),
      exposureTimeNs: pair(m.exposureTimeNs),
      isoStops: nums(m.isoStops),
      shutterStops: nums(m.shutterStops),
      focus: bool(m.focus) && minFocus > 0,
      minFocusDiopters: minFocus > 0 ? minFocus : null,
      whiteBalanceModes: Array.isArray(m.whiteBalanceModes)
        ? m.whiteBalanceModes.filter((x): x is WhiteBalanceId => oneOf(x, WB_MODES) && x !== 'kelvin')
        : ['auto'],
      kelvin: bool(m.kelvin),
      aeLock: bool(m.aeLock),
      awbLock: bool(m.awbLock),
    },
    raw: bool(raw.raw),
    aspectRatios: Array.isArray(raw.aspectRatios)
      ? raw.aspectRatios.filter((x): x is AspectRatio => oneOf(x, ASPECT_RATIOS))
      : ['4:3'],
    resolutions: parseResolutions(raw.resolutions),
    captureModes: Array.isArray(raw.captureModes)
      ? raw.captureModes.filter((x): x is CaptureModeId => oneOf(x, CAPTURE_MODES))
      : ['standard'],
    focusPeaking: bool(raw.focusPeaking),
    captureSize: size && num(size.width) > 0 ? { width: num(size.width), height: num(size.height) } : null,
    tier: oneOf(raw.tier, TIERS) ? raw.tier : 'entry',
    capturePlans: parsePlans(raw.capturePlans),
  };
}
