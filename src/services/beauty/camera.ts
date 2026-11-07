/**
 * Beauty in the camera: what the user chose (a preset and the sliders),
 * Portrait mode's own controls, and the recipe both resolve to. Pure logic,
 * unit-tested.
 */
import { BEAUTY_PRESETS, DEFAULT_PRESET_ID, findPreset } from './catalog';
import {
  DEFAULT_ADJUST,
  DEFAULT_PORTRAIT,
  GROUP_MAX,
  resolveRecipe,
  type BeautyAdjust,
  type BeautyPreset,
  type BeautyRecipe,
  type PortraitControls,
} from './recipe';

export interface CameraBeauty {
  /** Beauty mode on (the preset is shown live and applied to every photo). */
  on: boolean;
  preset: string;
  adjust: BeautyAdjust;
}

export const DEFAULT_CAMERA_BEAUTY: CameraBeauty = { on: false, preset: DEFAULT_PRESET_ID, adjust: DEFAULT_ADJUST };

const inRange = (v: unknown, lo: number, hi: number): v is number =>
  typeof v === 'number' && Number.isFinite(v) && v >= lo && v <= hi;

export function isBeautyAdjust(v: unknown): v is BeautyAdjust {
  if (!v || typeof v !== 'object') return false;
  const a = v as Record<string, unknown>;
  return (
    inRange(a.intensity, 0, 1) &&
    (['skin', 'face', 'lighting', 'background'] as const).every(k => inRange(a[k], 0, GROUP_MAX))
  );
}

export function isCameraBeauty(v: unknown): v is CameraBeauty {
  if (!v || typeof v !== 'object') return false;
  const b = v as Record<string, unknown>;
  return (
    typeof b.on === 'boolean' &&
    typeof b.preset === 'string' &&
    findPreset(b.preset) !== null &&
    isBeautyAdjust(b.adjust)
  );
}

export function isPortraitControls(v: unknown): v is PortraitControls {
  if (!v || typeof v !== 'object') return false;
  const p = v as Record<string, unknown>;
  return (
    inRange(p.depthBlur, 0, 1) &&
    inRange(p.subjectLight, -1, 1) &&
    inRange(p.backgroundLight, -1, 1) &&
    inRange(p.backgroundTone, -1, 1) &&
    inRange(p.edge, 0, 1)
  );
}

export { DEFAULT_PORTRAIT };

export function isPremiumPreset(id: string): boolean {
  return findPreset(id)?.tier === 'premium';
}

/**
 * The preset in effect: the chosen one, unless it is premium and the plan
 * does not include it (then the first free preset).
 */
export function effectivePreset(beauty: CameraBeauty, premiumAllowed: boolean): BeautyPreset | null {
  const p = findPreset(beauty.preset);
  if (p && (p.tier === 'free' || premiumAllowed)) return p;
  return BEAUTY_PRESETS.find(x => x.tier === 'free') ?? null;
}

/**
 * The recipe for the camera: the preset (when beauty is on) and, for a
 * Spotlight photo, Portrait mode's own controls — blur and background are
 * independent of the preset, so Natural Glow can have strong blur and
 * Midnight Glam none. Null when there is nothing to do.
 */
export function cameraRecipe(
  beauty: CameraBeauty,
  premiumAllowed: boolean,
  portrait: PortraitControls | null,
): BeautyRecipe | null {
  const preset = beauty.on ? effectivePreset(beauty, premiumAllowed) : null;
  if (!preset && !portrait) return null;
  return resolveRecipe(preset, beauty.adjust, portrait);
}

/** The short label on the camera's Beauty chip. */
export function beautyChipLabel(beauty: CameraBeauty, premiumAllowed: boolean): string | undefined {
  if (!beauty.on) return undefined;
  return effectivePreset(beauty, premiumAllowed)?.name;
}
