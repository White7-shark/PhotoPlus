/**
 * Pink lips (Photoshoot Studio): the person's lips repainted in a pink shade.
 * The lips keep their own light, creases and gloss; dark lips are lifted so
 * the pink shows on deep skin, as in the looks people share.
 */
export interface LipShade {
  id: string;
  name: string;
  /** #RRGGBB */
  color: string;
}

export const LIP_SHADES: LipShade[] = [
  { id: 'soft', name: 'Soft pink', color: '#F4A6B8' },
  { id: 'baby', name: 'Baby pink', color: '#F8BBD0' },
  { id: 'nude', name: 'Nude pink', color: '#D99A9E' },
  { id: 'rose', name: 'Rose', color: '#E37B9A' },
  { id: 'hot', name: 'Hot pink', color: '#E8508A' },
  { id: 'berry', name: 'Berry', color: '#C2477A' },
];

/** The person's lips: a shade and how strongly it is applied (0..1), or none. */
export interface LipsChoice {
  shade: string;
  strength: number;
}

export const DEFAULT_LIPS: LipsChoice = { shade: 'soft', strength: 0.85 };

export const lipShade = (id: string): LipShade | undefined => LIP_SHADES.find(s => s.id === id);

/** What the device is asked for: a colour and strength, or nothing. */
export function lipsRequest(choice: LipsChoice | null | undefined): { color: string; strength: number } | undefined {
  if (!choice) return undefined;
  const shade = lipShade(choice.shade);
  const strength = Number.isFinite(choice.strength) ? Math.min(1, Math.max(0, choice.strength)) : 0;
  if (!shade || strength <= 0) return undefined;
  return { color: shade.color, strength: Math.round(strength * 100) / 100 };
}
