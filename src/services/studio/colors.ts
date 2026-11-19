/**
 * Backdrop colours for the Photoshoot Studio: conversions for the colour
 * picker, the studio paper palette, and colours that suit an outfit.
 *
 * Suggestions follow what portrait photographers do with seamless paper:
 *  - the outfit must stand apart from the paper in lightness, or the person
 *    melts into it;
 *  - a colour opposite the outfit's on the colour wheel (navy on mustard)
 *    or a quieter shade of the same family (tonal) looks deliberate; colours
 *    a third of the wheel apart, both strong, tend to clash;
 *  - plain outfits (white, black, grey, denim) carry a bold, warm paper well;
 *  - never the outfit's own colour.
 * Pure (unit-tested).
 */

export interface Rgb {
  r: number;
  g: number;
  b: number;
}

/** Hue 0..360, saturation and value 0..1. */
export interface Hsv {
  h: number;
  s: number;
  v: number;
}

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

/** "#abc", "abc", "#aabbcc" or "aabbcc" → "#AABBCC", or null when it is not a colour. */
export function normalizeHex(input: string): string | null {
  const t = input.trim().replace(/^#/, '');
  if (/^[0-9a-f]{3}$/i.test(t)) {
    return `#${t
      .split('')
      .map(c => c + c)
      .join('')
      .toUpperCase()}`;
  }
  return /^[0-9a-f]{6}$/i.test(t) ? `#${t.toUpperCase()}` : null;
}

export function hexToRgb(hex: string): Rgb {
  const h = normalizeHex(hex) ?? '#000000';
  return { r: parseInt(h.slice(1, 3), 16), g: parseInt(h.slice(3, 5), 16), b: parseInt(h.slice(5, 7), 16) };
}

export function rgbToHex({ r, g, b }: Rgb): string {
  const c = (v: number) => Math.round(Math.min(255, Math.max(0, v))).toString(16).padStart(2, '0');
  return `#${c(r)}${c(g)}${c(b)}`.toUpperCase();
}

export function rgbToHsv({ r, g, b }: Rgb): Hsv {
  const R = r / 255,
    G = g / 255,
    B = b / 255;
  const max = Math.max(R, G, B),
    min = Math.min(R, G, B),
    d = max - min;
  let h = 0;
  if (d > 0) {
    if (max === R) h = 60 * (((G - B) / d) % 6);
    else if (max === G) h = 60 * ((B - R) / d + 2);
    else h = 60 * ((R - G) / d + 4);
  }
  return { h: (h + 360) % 360, s: max > 0 ? d / max : 0, v: max };
}

export function hsvToRgb({ h, s, v }: Hsv): Rgb {
  const H = (((h % 360) + 360) % 360) / 60,
    S = clamp01(s),
    V = clamp01(v);
  const c = V * S,
    x = c * (1 - Math.abs((H % 2) - 1)),
    m = V - c;
  const [r, g, b] = H < 1 ? [c, x, 0] : H < 2 ? [x, c, 0] : H < 3 ? [0, c, x] : H < 4 ? [0, x, c] : H < 5 ? [x, 0, c] : [c, 0, x];
  return { r: (r + m) * 255, g: (g + m) * 255, b: (b + m) * 255 };
}

export const hsvToHex = (hsv: Hsv) => rgbToHex(hsvToRgb(hsv));
export const hexToHsv = (hex: string) => rgbToHsv(hexToRgb(hex));

/** Relative luminance (sRGB, 0..1), as the eye judges lightness. */
export function luminance(hex: string): number {
  const { r, g, b } = hexToRgb(hex);
  const lin = (v: number) => {
    const c = v / 255;
    return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

/** Lightness (0..1, perceptual: the cube root of luminance, as in CIELAB). */
const lightness = (hex: string) => Math.cbrt(luminance(hex));

/** Distance between two hues on the colour wheel, 0..180. */
export function hueDistance(a: number, b: number): number {
  const d = Math.abs((((a - b) % 360) + 360) % 360);
  return d > 180 ? 360 - d : d;
}

/** Studio paper colours, as photographers buy them. */
export interface StudioColour {
  id: string;
  name: string;
  color: string;
}

export const STUDIO_COLOURS: readonly StudioColour[] = [
  { id: 'mustard', name: 'Mustard', color: '#C4962F' },
  { id: 'terracotta', name: 'Terracotta', color: '#B8684A' },
  { id: 'sage', name: 'Sage', color: '#9CAA8C' },
  { id: 'dusty-blue', name: 'Dusty blue', color: '#8098B2' },
  { id: 'peach', name: 'Peach', color: '#E8B697' },
  { id: 'lilac', name: 'Lilac', color: '#B5A3C8' },
  { id: 'olive', name: 'Olive', color: '#6F6A3A' },
  { id: 'burgundy', name: 'Burgundy', color: '#6B2737' },
  { id: 'teal', name: 'Teal', color: '#2F6A6C' },
  { id: 'navy', name: 'Navy', color: '#22304F' },
  { id: 'sand', name: 'Sand', color: '#D8C3A0' },
  { id: 'charcoal', name: 'Charcoal', color: '#2B2D31' },
];

export interface OutfitColour {
  /** "#rrggbb" */
  color: string;
  /** Its share of the clothes, 0..1. */
  share: number;
}

/** How well a paper colour suits an outfit (higher is better). */
export function suitScore(paper: string, outfit: readonly OutfitColour[]): number {
  if (outfit.length === 0) return 0;
  const p = hexToHsv(paper);
  const lp = lightness(paper);
  let total = 0,
    weights = 0;
  for (const o of outfit) {
    const c = hexToHsv(o.color);
    const lc = lightness(o.color);
    let s = 0;
    // Stand apart from the paper in lightness.
    s += 2.2 * Math.min(Math.abs(lp - lc), 0.35);
    const strongPaper = p.s > 0.25 && p.v > 0.2;
    const plainOutfit = c.s < 0.2 || c.v < 0.18 || (c.s < 0.45 && c.h > 190 && c.h < 240); // white, black, grey, denim
    if (plainOutfit) {
      // A bold, warm paper behind a plain outfit.
      if (strongPaper && (p.h < 50 || p.h > 340)) s += 0.45;
      else if (strongPaper) s += 0.2;
    } else if (strongPaper) {
      const dh = hueDistance(p.h, c.h);
      if (dh >= 140) s += 0.6; // opposite
      else if (dh <= 35) s += 0.3; // tonal
      else if (dh >= 60 && dh <= 120 && c.s > 0.45) s -= 0.4; // a clash
    }
    // Never the outfit's own colour.
    const a = hexToRgb(paper),
      b = hexToRgb(o.color);
    if (Math.hypot(a.r - b.r, a.g - b.g, a.b - b.b) < 60) s -= 1.2;
    total += s * o.share;
    weights += o.share;
  }
  return weights > 0 ? total / weights : 0;
}

/** The studio colours that best suit an outfit, best first. */
export function suggestForOutfit(outfit: readonly OutfitColour[], count = 4): StudioColour[] {
  if (outfit.length === 0) return [];
  return STUDIO_COLOURS.map(c => ({ c, s: suitScore(c.color, outfit) }))
    .sort((x, y) => y.s - x.s)
    .slice(0, count)
    .map(x => x.c);
}

/** True when an outfit colour list from the device is usable. */
export function isOutfit(v: unknown): v is OutfitColour[] {
  return (
    Array.isArray(v) &&
    v.every(
      o =>
        o &&
        typeof o === 'object' &&
        typeof (o as OutfitColour).color === 'string' &&
        normalizeHex((o as OutfitColour).color) !== null &&
        typeof (o as OutfitColour).share === 'number',
    )
  );
}
