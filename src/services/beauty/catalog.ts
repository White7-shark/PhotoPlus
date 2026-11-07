/**
 * The 13 beauty presets (2 free, 11 premium) with their cover images.
 * Definitions live in presets.json (shared with the desktop validation
 * tool); this module types and checks them once, and adds the images.
 */
import type { ImageSourcePropType } from 'react-native';

import data from './presets.json';
import type { BeautyPreset, Tier } from './recipe';

/** Cover image per preset, in catalogue order (Metro needs static requires). */
const COVERS: Record<string, ImageSourcePropType> = {
  natural_glow: require('../../assets/beauty/01-natural-glow.jpg'),
  soft_radiance: require('../../assets/beauty/02-soft-radiance.jpg'),
  velvet_skin: require('../../assets/beauty/03-velvet-skin.jpg'),
  golden_goddess: require('../../assets/beauty/04-golden-goddess.jpg'),
  crystal_beauty: require('../../assets/beauty/05-crystal-beauty.jpg'),
  blush_romance: require('../../assets/beauty/06-blush-romance.jpg'),
  bronze_luxe: require('../../assets/beauty/07-bronze-luxe.jpg'),
  pure_elegance: require('../../assets/beauty/08-pure-elegance.jpg'),
  midnight_glam: require('../../assets/beauty/09-midnight-glam.jpg'),
  peach_perfection: require('../../assets/beauty/10-peach-perfection.jpg'),
  emerald_aura: require('../../assets/beauty/11-emerald-aura.jpg'),
  dreamlight: require('../../assets/beauty/12-dreamlight.jpg'),
  studio_royal: require('../../assets/beauty/13-studio-royal.jpg'),
};

const GROUPS = ['skin', 'face', 'lighting', 'background', 'color'] as const;

function isPreset(v: unknown): v is BeautyPreset {
  if (!v || typeof v !== 'object') {
    return false;
  }
  const p = v as Record<string, unknown>;
  return (
    typeof p.id === 'string' &&
    typeof p.name === 'string' &&
    typeof p.description === 'string' &&
    (p.tier === 'free' || p.tier === 'premium') &&
    GROUPS.every(g => p[g] !== null && typeof p[g] === 'object')
  );
}

/** Checked once at load; a malformed entry is dropped rather than crashing the camera. */
export const BEAUTY_PRESETS: readonly BeautyPreset[] = (data.presets as unknown[]).filter(isPreset);

export function findPreset(id: string | null | undefined): BeautyPreset | null {
  return id ? (BEAUTY_PRESETS.find(p => p.id === id) ?? null) : null;
}

export function presetCover(id: string): ImageSourcePropType | null {
  return COVERS[id] ?? null;
}

export function presetTier(id: string): Tier | null {
  return findPreset(id)?.tier ?? null;
}

/** The preset the camera starts with: the first free one. */
export const DEFAULT_PRESET_ID = BEAUTY_PRESETS.find(p => p.tier === 'free')?.id ?? 'natural_glow';
