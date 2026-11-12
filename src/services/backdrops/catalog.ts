/**
 * Backdrops: 14 background designs behind the person. The camera uses one
 * (Signature, in Spotlight); the rest live in the editor so the camera stays
 * uncluttered. Definitions are in backdrops.json; the order is the engine's
 * (pp_backdrop_design) and Backdrop.kt's.
 */
import type { ImageSourcePropType } from 'react-native';

import data from './backdrops.json';

export interface BackdropDef {
  id: string;
  name: string;
  tier: 'free' | 'premium';
  description: string;
}

/** A backdrop as chosen: which design and how strongly (0..1). */
export interface BackdropChoice {
  id: string;
  strength: number;
}

const COVERS: Record<string, ImageSourcePropType> = {
  signature: require('../../assets/backdrops/01-signature.jpg'),
  soft_studio: require('../../assets/backdrops/02-soft-studio.jpg'),
  stage: require('../../assets/backdrops/03-stage.jpg'),
  noir: require('../../assets/backdrops/04-noir.jpg'),
  porcelain: require('../../assets/backdrops/05-porcelain.jpg'),
  canvas: require('../../assets/backdrops/06-canvas.jpg'),
  golden_hour: require('../../assets/backdrops/07-golden-hour.jpg'),
  city_lights: require('../../assets/backdrops/08-city-lights.jpg'),
  neon: require('../../assets/backdrops/09-neon.jpg'),
  color_pop: require('../../assets/backdrops/10-color-pop.jpg'),
  velocity: require('../../assets/backdrops/11-velocity.jpg'),
  swirl: require('../../assets/backdrops/12-swirl.jpg'),
  aurora: require('../../assets/backdrops/13-aurora.jpg'),
  dream: require('../../assets/backdrops/14-dream.jpg'),
};

function isDef(v: unknown): v is BackdropDef {
  if (!v || typeof v !== 'object') return false;
  const b = v as Record<string, unknown>;
  return (
    typeof b.id === 'string' &&
    typeof b.name === 'string' &&
    typeof b.description === 'string' &&
    (b.tier === 'free' || b.tier === 'premium')
  );
}

export const BACKDROPS: readonly BackdropDef[] = (data.backdrops as unknown[]).filter(isDef);

/** The camera's backdrop (Spotlight): right for any photo. */
export const DEFAULT_BACKDROP_ID = 'signature';

export function findBackdrop(id: string | null | undefined): BackdropDef | null {
  return id ? (BACKDROPS.find(b => b.id === id) ?? null) : null;
}

export function backdropCover(id: string): ImageSourcePropType | null {
  return COVERS[id] ?? null;
}

export function isBackdropChoice(v: unknown): v is BackdropChoice {
  if (!v || typeof v !== 'object') return false;
  const b = v as Record<string, unknown>;
  return (
    typeof b.id === 'string' &&
    findBackdrop(b.id) !== null &&
    typeof b.strength === 'number' &&
    Number.isFinite(b.strength) &&
    b.strength >= 0 &&
    b.strength <= 1
  );
}

/**
 * The camera's backdrop for a Spotlight photo: Signature, as strong as the
 * Depth blur slider (null when the slider is at zero).
 */
export function cameraBackdrop(depthBlur: number): BackdropChoice | null {
  const strength = Math.min(1, Math.max(0, depthBlur));
  return strength > 0 ? { id: DEFAULT_BACKDROP_ID, strength } : null;
}
