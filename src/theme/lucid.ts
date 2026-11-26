/**
 * Lucid: the see-through theme (from Dr. File). The background is the
 * phone's wallpaper, a photo or a clip; cards are glass over it. Light or
 * dark isn't chosen — the picture decides — and the numbers are worked out
 * so every word stays readable on any picture:
 *
 *  - light picture (median brightness high, or the wallpaper says dark text
 *    suits it) → dark text, a white veil lifting the darkest tenth, milky glass;
 *  - dark picture → white text, a dark veil taming the brightest tenth, smoky glass;
 *  - veil: just enough that big titles straight on the picture reach 3:1
 *    where the picture is worst for them (WCAG large text);
 *  - glass: just enough tint that body text on a card reaches 4.5:1 over
 *    the worst part of the veiled picture (WCAG AA);
 *  - accent: the picture's most colourful colour, made light or dark enough
 *    to read (3:1).
 *
 * Brightness is WCAG relative luminance (0 black … 1 white). Pure; tested
 * in __tests__/lucid.test.tsx.
 */
import type { ColorTokens, ThemeMode } from './tokens';

export interface LookStats {
  mean: number;
  p10: number;
  p50: number;
  p90: number;
  /** Main colours, most common first ("#RRGGBB"). */
  colors: string[];
  saturation: number;
}

export interface LucidLook {
  scheme: ThemeMode;
  /** Full-screen veil over the picture: colour and strength (0–1). */
  veilColor: string;
  veil: number;
  /** Tint of the glass cards (0–1). */
  glass: number;
  accent: string;
  /** Colours for the theme (the rest come from the light or dark theme). */
  colors: Partial<ColorTokens>;
}

type RGB = [number, number, number];

export function hexToRgb(hex: string): RGB {
  const h = hex.replace('#', '');
  const n = parseInt(
    h.length === 3
      ? h
          .split('')
          .map(c => c + c)
          .join('')
      : h.slice(0, 6),
    16,
  );
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

const toHex = (c: RGB) =>
  `#${c
    .map(v =>
      Math.round(Math.max(0, Math.min(255, v)))
        .toString(16)
        .padStart(2, '0'),
    )
    .join('')
    .toUpperCase()}`;
const lin = (c: number) => {
  const s = c / 255;
  return s <= 0.04045 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
};
export const luminance = (c: RGB) => 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2]);
export const contrast = (a: number, b: number) => (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);

function hsl([r, g, b]: RGB): [number, number, number] {
  const R = r / 255;
  const G = g / 255;
  const B = b / 255;
  const max = Math.max(R, G, B);
  const min = Math.min(R, G, B);
  const l = (max + min) / 2;
  if (max === min) return [0, 0, l];
  const d = max - min;
  const s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
  const h = max === R ? (G - B) / d + (G < B ? 6 : 0) : max === G ? (B - R) / d + 2 : (R - G) / d + 4;
  return [h / 6, s, l];
}

function fromHsl(h: number, s: number, l: number): RGB {
  if (s === 0) return [l * 255, l * 255, l * 255];
  const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  const p = 2 * l - q;
  const f = (t: number) => {
    const x = t < 0 ? t + 1 : t > 1 ? t - 1 : t;
    return 255 * (x < 1 / 6 ? p + (q - p) * 6 * x : x < 1 / 2 ? q : x < 2 / 3 ? p + (q - p) * (2 / 3 - x) * 6 : p);
  };
  return [f(h + 1 / 3), f(h), f(h - 1 / 3)];
}

const rgba = (c: RGB, a: number) => `rgba(${c.map(Math.round).join(',')},${Math.round(a * 1000) / 1000})`;
const clamp = (x: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, x));
const mix = (a: RGB, b: RGB, t: number): RGB => [
  a[0] + (b[0] - a[0]) * t,
  a[1] + (b[1] - a[1]) * t,
  a[2] + (b[2] - a[2]) * t,
];

/** The accent: the most colourful main colour, moved to a lightness that reads on the glass (3:1). */
export function pickAccent(colors: string[], scheme: ThemeMode, cardLum: number): string {
  // Colourfulness is chroma (how far apart the channels are), not HSL
  // saturation: a pale cream has "saturation" but almost no colour.
  const chroma = (c: RGB) => (Math.max(...c) - Math.min(...c)) / 255;
  const score = (x: { h: [number, number, number]; k: number }) => x.k * (1 - Math.abs(x.h[2] - 0.5) * 0.6);
  const best = colors
    .map(hexToRgb)
    .map(c => ({ c, h: hsl(c), k: chroma(c) }))
    .sort((a, b) => score(b) - score(a))[0];
  // Too grey a picture: Photo Plus's ember.
  const [h, s0, l0] = best && best.k >= 0.06 ? best.h : [0.045, 0.85, 0.55];
  const s = Math.max(s0, 0.45);
  let l = l0;
  for (let i = 0; i < 40 && contrast(luminance(fromHsl(h, s, l)), cardLum) < 3.2; i++)
    l = clamp(l + (scheme === 'dark' ? 0.025 : -0.025), 0.08, 0.92);
  return toHex(fromHsl(h, s, l));
}

export function lucidLook(stats: LookStats, hintDarkText = false): LucidLook {
  const scheme: ThemeMode = stats.p50 > 0.4 || (hintDarkText && stats.p50 > 0.25) ? 'light' : 'dark';
  const dark = scheme === 'dark';
  const veilRgb: RGB = dark ? [8, 8, 12] : [255, 255, 255];
  const veilLum = luminance(veilRgb);
  // Veil: big titles straight on the picture at 3:1 against its worst tenth.
  const worst = dark ? stats.p90 : stats.p10;
  const target = dark ? 1.05 / 3 - 0.05 : 3 * (0.0085 + 0.05) - 0.05; // text ≈ white / ≈ #141418
  let veil = dark
    ? worst > target
      ? (worst - target) / (worst - veilLum)
      : 0
    : worst < target
      ? (target - worst) / (veilLum - worst)
      : 0;
  veil = clamp(veil + 0.06, dark ? 0.16 : 0.1, dark ? 0.72 : 0.6);
  const veiledWorst = worst * (1 - veil) + veilLum * veil;
  // Glass: body text on a card at 4.5:1 over the worst of the veiled picture.
  const tint: RGB = dark ? [16, 16, 22] : [255, 255, 255];
  const tintLum = luminance(tint);
  const cardTarget = dark ? 1.05 / 4.5 - 0.05 : 4.5 * (0.0085 + 0.05) - 0.05;
  let glass = dark
    ? veiledWorst > cardTarget
      ? (veiledWorst - cardTarget) / (veiledWorst - tintLum)
      : 0
    : veiledWorst < cardTarget
      ? (cardTarget - veiledWorst) / (tintLum - veiledWorst)
      : 0;
  glass = clamp(glass + 0.05, dark ? 0.26 : 0.38, dark ? 0.88 : 0.9);
  const cardLum = veiledWorst * (1 - glass) + tintLum * glass;

  const accent = pickAccent(stats.colors, scheme, cardLum);
  const a = hexToRgb(accent);
  const base = stats.colors[0] ? hexToRgb(stats.colors[0]) : veilRgb;
  const text: RGB = dark ? [255, 255, 255] : [20, 20, 24];

  const colors: Partial<ColorTokens> = {
    background: toHex(mix(base, veilRgb, dark ? 0.75 : 0.7)),
    backgroundRaised: toHex(mix(base, veilRgb, dark ? 0.68 : 0.8)),
    surface: rgba(tint, Math.min(0.95, glass + 0.12)),
    textPrimary: toHex(text),
    textSecondary: dark ? 'rgba(255,255,255,0.8)' : 'rgba(20,20,24,0.76)',
    textTertiary: dark ? 'rgba(255,255,255,0.66)' : 'rgba(20,20,24,0.62)',
    accent,
    accentAlt: toHex(mix(a, text, 0.25)),
    glassFill: rgba(tint, glass),
    glassFillStrong: rgba(tint, Math.min(0.95, glass + 0.12)),
    glassStroke: dark ? 'rgba(255,255,255,0.28)' : 'rgba(255,255,255,0.85)',
    glassHighlight: dark ? 'rgba(255,255,255,0.34)' : 'rgba(255,255,255,0.95)',
    divider: dark ? 'rgba(255,255,255,0.16)' : 'rgba(20,20,24,0.10)',
    overlay: rgba(tint, Math.min(0.92, glass + 0.2)),
    focusRing: accent,
  };
  return { scheme, veilColor: toHex(veilRgb), veil, glass, accent, colors };
}

/** The look from only the colours Android shares (a live or unreadable wallpaper): guessed on the safe side. */
export function statsFromColors(colors: string[], darkText: boolean): LookStats {
  const lums = colors.map(c => luminance(hexToRgb(c)));
  const mid = lums.length ? lums.reduce((x, y) => x + y, 0) / lums.length : darkText ? 0.6 : 0.1;
  return {
    mean: mid,
    p10: Math.min(...lums, mid) * 0.4,
    p50: darkText ? Math.max(mid, 0.45) : mid,
    p90: Math.min(1, Math.max(...lums, mid) * 1.8 + 0.1),
    colors,
    saturation: 0.3,
  };
}
