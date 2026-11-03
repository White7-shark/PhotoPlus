import { palette } from './palette';

export type ThemeMode = 'dark' | 'light';
export type ThemePreference = ThemeMode | 'system';

export interface ColorTokens {
  background: string;
  backgroundRaised: string;
  surface: string;
  textPrimary: string;
  textSecondary: string;
  textTertiary: string;
  textInverse: string;
  accent: string;
  accentAlt: string;
  accentCool: string;
  premium: string;
  premiumDeep: string;
  success: string;
  warning: string;
  danger: string;
  info: string;
  glassFill: string;
  glassFillStrong: string;
  glassStroke: string;
  glassHighlight: string;
  glassShadow: string;
  divider: string;
  focusRing: string;
  overlay: string;
  auroraA: string;
  auroraB: string;
  auroraC: string;
}

export interface Theme {
  mode: ThemeMode;
  colors: ColorTokens;
  gradients: typeof gradients;
  spacing: typeof spacing;
  radii: typeof radii;
  typography: typeof typography;
  motion: typeof motion;
  layout: typeof layout;
}

const darkColors: ColorTokens = {
  background: palette.ink950,
  backgroundRaised: palette.ink900,
  surface: palette.ink850,
  textPrimary: '#F7F5FB',
  textSecondary: 'rgba(247,245,251,0.74)',
  textTertiary: 'rgba(247,245,251,0.56)',
  textInverse: palette.ink950,
  accent: palette.ember,
  accentAlt: palette.rose,
  accentCool: palette.lagoon,
  premium: palette.gold,
  premiumDeep: palette.goldDeep,
  success: palette.success,
  warning: palette.warning,
  danger: palette.danger,
  info: palette.info,
  glassFill: 'rgba(255,255,255,0.065)',
  glassFillStrong: 'rgba(255,255,255,0.11)',
  glassStroke: 'rgba(255,255,255,0.14)',
  glassHighlight: 'rgba(255,255,255,0.28)',
  glassShadow: 'rgba(0,0,0,0.45)',
  divider: 'rgba(255,255,255,0.08)',
  focusRing: palette.lagoon,
  overlay: 'rgba(5,5,10,0.72)',
  auroraA: palette.ember,
  auroraB: palette.orchid,
  auroraC: palette.indigo,
};

const lightColors: ColorTokens = {
  background: palette.paper100,
  backgroundRaised: palette.paper50,
  surface: palette.white,
  textPrimary: '#14111D',
  textSecondary: 'rgba(20,17,29,0.72)',
  textTertiary: 'rgba(20,17,29,0.58)',
  textInverse: palette.white,
  accent: '#E5531F',
  accentAlt: '#D42A64',
  accentCool: '#0F9C8C',
  premium: '#B07A16',
  premiumDeep: '#8A5E0E',
  success: '#12935F',
  warning: '#A86A00',
  danger: '#D33A3A',
  info: '#1F6FD1',
  glassFill: 'rgba(255,255,255,0.58)',
  glassFillStrong: 'rgba(255,255,255,0.78)',
  glassStroke: 'rgba(255,255,255,0.9)',
  glassHighlight: 'rgba(255,255,255,1)',
  glassShadow: 'rgba(56,40,92,0.16)',
  divider: 'rgba(20,17,29,0.08)',
  focusRing: '#0F9C8C',
  overlay: 'rgba(243,241,248,0.8)',
  auroraA: '#FFB08A',
  auroraB: '#E3A6FF',
  auroraC: '#A9B4FF',
};

/** Signature gradients as CSS strings for `backgroundImage`. */
export const gradients = {
  /** Decorative only (no text on top): logo, glows, icon tiles. */
  spectrum: 'linear-gradient(135deg, #FF7A45 0%, #F2477E 48%, #8B5CF6 100%)',
  /** Same hues, deepened so white labels meet WCAG AA (≥ 4.5:1) at every stop. */
  action: 'linear-gradient(135deg, #CC4128 0%, #C2306A 50%, #7445E0 100%)',
  spectrumSoft: 'linear-gradient(135deg, rgba(255,122,69,0.9) 0%, rgba(242,71,126,0.9) 50%, rgba(139,92,246,0.9) 100%)',
  premium: 'linear-gradient(135deg, #FFE3A3 0%, #F5C66B 45%, #C9922E 100%)',
  lagoon: 'linear-gradient(135deg, #2DD4BF 0%, #5B5BF7 100%)',
  glassSheen:
    'linear-gradient(160deg, rgba(255,255,255,0.20) 0%, rgba(255,255,255,0.04) 38%, rgba(255,255,255,0.0) 60%, rgba(255,255,255,0.05) 100%)',
  glassSheenLight:
    'linear-gradient(160deg, rgba(255,255,255,0.95) 0%, rgba(255,255,255,0.55) 45%, rgba(255,255,255,0.35) 100%)',
  scrim: 'linear-gradient(180deg, rgba(5,5,10,0) 0%, rgba(5,5,10,0.85) 100%)',
} as const;

export const spacing = {
  xxs: 2,
  xs: 4,
  sm: 8,
  md: 12,
  lg: 16,
  xl: 24,
  xxl: 32,
  xxxl: 48,
  gutter: 16,
} as const;

export const radii = {
  xs: 8,
  sm: 12,
  md: 18,
  lg: 24,
  xl: 32,
  pill: 999,
} as const;

/** Type ramp. Display styles use tighter tracking for an editorial feel. */
export const typography = {
  display: { fontSize: 40, lineHeight: 44, fontWeight: '800' as const, letterSpacing: -1.2 },
  title1: { fontSize: 30, lineHeight: 36, fontWeight: '800' as const, letterSpacing: -0.8 },
  title2: { fontSize: 22, lineHeight: 28, fontWeight: '700' as const, letterSpacing: -0.4 },
  title3: { fontSize: 18, lineHeight: 24, fontWeight: '700' as const, letterSpacing: -0.2 },
  body: { fontSize: 15, lineHeight: 22, fontWeight: '400' as const, letterSpacing: 0 },
  bodyStrong: { fontSize: 15, lineHeight: 22, fontWeight: '600' as const, letterSpacing: 0 },
  callout: { fontSize: 13, lineHeight: 18, fontWeight: '500' as const, letterSpacing: 0.1 },
  caption: { fontSize: 12, lineHeight: 16, fontWeight: '500' as const, letterSpacing: 0.2 },
  overline: { fontSize: 11, lineHeight: 14, fontWeight: '700' as const, letterSpacing: 1.6 },
  mono: { fontSize: 12, lineHeight: 16, fontWeight: '500' as const, letterSpacing: 0.3, fontFamily: 'monospace' },
} as const;

export type TypographyVariant = keyof typeof typography;

export const motion = {
  fast: 140,
  base: 220,
  slow: 360,
  pressScale: 0.97,
} as const;

export const layout = {
  /** Minimum touch target in dp (Material + WCAG 2.5.8). */
  minTouchTarget: 48,
  maxContentWidth: 720,
  tabBarHeight: 68,
} as const;

export function createTheme(mode: ThemeMode): Theme {
  return {
    mode,
    colors: mode === 'dark' ? darkColors : lightColors,
    gradients,
    spacing,
    radii,
    typography,
    motion,
    layout,
  };
}

export const darkTheme = createTheme('dark');
export const lightTheme = createTheme('light');
