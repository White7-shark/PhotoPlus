/**
 * Photo Plus raw palette — "Darkroom Aurora".
 *
 * The identity borrows from two photographic moments: the near-black of a
 * darkroom and the warm-to-violet spectrum of golden hour sliding into blue
 * hour. Components never use these directly; they consume semantic tokens
 * from `tokens.ts` so light/dark themes stay consistent.
 */
export const palette = {
  ink950: '#05050A',
  ink900: '#0A0911',
  ink850: '#100E19',
  ink800: '#171422',
  ink700: '#221E30',
  ink600: '#2E2940',

  paper50: '#FBFAFD',
  paper100: '#F3F1F8',
  paper200: '#E6E2EF',
  paper300: '#CFC9DC',

  ember: '#FF7A45',
  sunset: '#FF4F6E',
  rose: '#F2477E',
  orchid: '#C04DFF',
  violet: '#8B5CF6',
  indigo: '#5B5BF7',
  lagoon: '#2DD4BF',
  gold: '#F5C66B',
  goldDeep: '#C9922E',
  /** "Plus" lettering and swoosh in the brand logo. */
  brandGold: '#FFB01F',
  brandOrange: '#FF8A00',

  success: '#3DDC97',
  warning: '#FFC145',
  danger: '#FF6B6B',
  info: '#6CB4FF',

  white: '#FFFFFF',
  black: '#000000',
} as const;

export type PaletteColor = keyof typeof palette;
