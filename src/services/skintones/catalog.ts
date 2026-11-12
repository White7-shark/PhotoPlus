/**
 * Skin Tone: 13 shades for the person's skin (face and the visible body),
 * from Porcelain Rose to Ebony Glow. Unlike the beauty filters (light and
 * retouch), these change the colour of the skin itself; hair, eyes, lips,
 * clothes and the background keep theirs. Definitions are in
 * skintones.json (SkinTone.kt mirrors them; a unit test checks both).
 */
import type { ImageSourcePropType } from 'react-native';

import data from './skintones.json';

export interface SkinShadeDef {
  id: string;
  name: string;
  plan: 'free' | 'premium';
  /** A mid-tone of that skin, #RRGGBB. */
  color: string;
  /** 0..1 a soft sheen on the skin's highlights. */
  glow: number;
  note: string;
}

/** A shade as chosen: which one and how strongly (0..1). */
export interface SkinToneChoice {
  id: string;
  strength: number;
}

const COVERS: Record<string, ImageSourcePropType> = {
  porcelain_rose: require('../../assets/skintones/01-porcelain-rose.jpg'),
  ivory: require('../../assets/skintones/02-ivory.jpg'),
  peach: require('../../assets/skintones/03-peach.jpg'),
  golden_beige: require('../../assets/skintones/04-golden-beige.jpg'),
  honey: require('../../assets/skintones/05-honey.jpg'),
  olive: require('../../assets/skintones/06-olive.jpg'),
  sun_kissed: require('../../assets/skintones/07-sun-kissed.jpg'),
  caramel: require('../../assets/skintones/08-caramel.jpg'),
  bronze: require('../../assets/skintones/09-bronze.jpg'),
  cinnamon: require('../../assets/skintones/10-cinnamon.jpg'),
  mocha: require('../../assets/skintones/11-mocha.jpg'),
  espresso: require('../../assets/skintones/12-espresso.jpg'),
  ebony_glow: require('../../assets/skintones/13-ebony-glow.jpg'),
};

function isDef(v: unknown): v is SkinShadeDef {
  if (!v || typeof v !== 'object') return false;
  const s = v as Record<string, unknown>;
  return (
    typeof s.id === 'string' &&
    typeof s.name === 'string' &&
    typeof s.note === 'string' &&
    (s.plan === 'free' || s.plan === 'premium') &&
    typeof s.color === 'string' &&
    /^#[0-9A-Fa-f]{6}$/.test(s.color) &&
    typeof s.glow === 'number' &&
    s.glow >= 0 &&
    s.glow <= 1
  );
}

export const SKIN_SHADES: readonly SkinShadeDef[] = (data.shades as unknown[]).filter(isDef);

export function findShade(id: string | null | undefined): SkinShadeDef | null {
  return id ? (SKIN_SHADES.find(s => s.id === id) ?? null) : null;
}

export function shadeCover(id: string): ImageSourcePropType | null {
  return COVERS[id] ?? null;
}

export function isSkinToneChoice(v: unknown): v is SkinToneChoice {
  if (!v || typeof v !== 'object') return false;
  const c = v as Record<string, unknown>;
  return (
    typeof c.id === 'string' &&
    findShade(c.id) !== null &&
    typeof c.strength === 'number' &&
    Number.isFinite(c.strength) &&
    c.strength >= 0 &&
    c.strength <= 1
  );
}
