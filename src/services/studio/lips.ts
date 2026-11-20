/**
 * Lips in the Photoshoot Studio.
 *
 * Pink lips: a natural soft pink that adapts to the person. Lips that are
 * already pink (as most fair people's are) get almost nothing and fair faces
 * only a faint touch, while brown or greyish lips get the full soft pink, so
 * it reads as their own lips, not make-up.
 *
 * Lipstick: a chosen colour, applied as asked. The lips keep their own light,
 * lines and shine either way.
 */
export interface LipShade {
  id: string;
  name: string;
  /** #RRGGBB */
  color: string;
}

/** The soft pink natural pink lips are drawn toward. */
export const NATURAL_PINK = '#F4A6B8';

/** Lipstick shades people wear most. */
export const LIPSTICKS: LipShade[] = [
  { id: 'red', name: 'Classic red', color: '#C21E2F' },
  { id: 'ruby', name: 'Ruby', color: '#9B111E' },
  { id: 'wine', name: 'Wine', color: '#722F37' },
  { id: 'berry', name: 'Berry', color: '#8E2A4F' },
  { id: 'plum', name: 'Plum', color: '#6E2A4A' },
  { id: 'mauve', name: 'Mauve', color: '#B07384' },
  { id: 'nude', name: 'Nude', color: '#C48A76' },
  { id: 'brown', name: 'Brown nude', color: '#8D5A4A' },
  { id: 'coral', name: 'Coral', color: '#E8614D' },
  { id: 'rose', name: 'Rose pink', color: '#D9668A' },
  { id: 'hot', name: 'Hot pink', color: '#E0408A' },
  { id: 'orange', name: 'Orange red', color: '#D9381E' },
];

/** The person's lips: natural pink, or a lipstick shade, and how strong (0..1). */
export type LipsChoice = { kind: 'natural'; strength: number } | { kind: 'lipstick'; shade: string; strength: number };

export const DEFAULT_NATURAL: LipsChoice = { kind: 'natural', strength: 0.85 };
export const DEFAULT_LIPSTICK_STRENGTH = 0.9;

export const lipstick = (id: string): LipShade | undefined => LIPSTICKS.find(s => s.id === id);

/** What the device is asked for, or nothing. */
export function lipsRequest(
  choice: LipsChoice | null | undefined,
): { color: string; strength: number; natural: boolean } | undefined {
  if (!choice) return undefined;
  const strength = Number.isFinite(choice.strength) ? Math.min(1, Math.max(0, choice.strength)) : 0;
  if (strength <= 0) return undefined;
  const rounded = Math.round(strength * 100) / 100;
  if (choice.kind === 'natural') return { color: NATURAL_PINK, strength: rounded, natural: true };
  const shade = lipstick(choice.shade);
  return shade ? { color: shade.color, strength: rounded, natural: false } : undefined;
}
