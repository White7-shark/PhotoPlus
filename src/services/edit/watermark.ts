import type { WatermarkFont, WatermarkStyle } from './catalog';

export type WatermarkPosition = 'tl' | 'tc' | 'tr' | 'ml' | 'mc' | 'mr' | 'bl' | 'bc' | 'br';
export type WatermarkLayout = 'inline' | 'stacked';

/** Premium branding: own text, colour, logo, alignment. */
export interface CustomBranding {
  text: string;
  /** #RRGGBB */
  color: string;
  logoPath: string | null;
  align: 'left' | 'center' | 'right';
}

/**
 * The watermark the user designed (WatermarkSpec.kt resolves it against the
 * plan natively: on the free plan the Photo Plus mark is always drawn).
 */
export interface WatermarkSpec {
  style: WatermarkStyle;
  font: WatermarkFont;
  layout: WatermarkLayout;
  /** 0..1 slider within the plan's size range. */
  size: number;
  /** 0..1 slider within the plan's opacity range. */
  opacity: number;
  position: WatermarkPosition;
  /** false = no watermark (Premium only). */
  enabled: boolean;
  custom: CustomBranding | null;
}

/** The default: the "Created with PhotoPlus" signature, bottom right. */
export const DEFAULT_WATERMARK: WatermarkSpec = {
  style: 'signature',
  font: 'modern',
  layout: 'inline',
  size: 0.5,
  opacity: 0.85,
  position: 'br',
  enabled: true,
  custom: null,
};

export const DEFAULT_POSITION: WatermarkPosition = 'br';

export const POSITIONS: readonly WatermarkPosition[] = ['tl', 'tc', 'tr', 'ml', 'mc', 'mr', 'bl', 'bc', 'br'];

/** A saved branding look (Premium). */
export interface BrandingPreset {
  id: string;
  name: string;
  spec: WatermarkSpec;
}

const STYLES = ['classic', 'pill', 'signature', 'minimal', 'badge'];
const FONTS = ['modern', 'elegant', 'signature', 'bold', 'type'];
const unit = (v: unknown) => typeof v === 'number' && Number.isFinite(v) && v >= 0 && v <= 1;

export function isWatermarkSpec(v: unknown): v is WatermarkSpec {
  if (!v || typeof v !== 'object') return false;
  const s = v as Record<string, unknown>;
  const c = s.custom as Record<string, unknown> | null;
  return (
    STYLES.includes(s.style as string) &&
    FONTS.includes(s.font as string) &&
    (s.layout === 'inline' || s.layout === 'stacked') &&
    unit(s.size) &&
    unit(s.opacity) &&
    POSITIONS.includes(s.position as WatermarkPosition) &&
    typeof s.enabled === 'boolean' &&
    (c === null ||
      (!!c &&
        typeof c.text === 'string' &&
        c.text.length <= 40 &&
        typeof c.color === 'string' &&
        /^#[0-9a-fA-F]{6}$/.test(c.color) &&
        (c.logoPath === null || typeof c.logoPath === 'string') &&
        ['left', 'center', 'right'].includes(c.align as string)))
  );
}

export function isBrandingPresets(v: unknown): v is BrandingPreset[] {
  return (
    Array.isArray(v) &&
    v.length <= 12 &&
    v.every(
      p =>
        !!p &&
        typeof p === 'object' &&
        typeof (p as BrandingPreset).id === 'string' &&
        typeof (p as BrandingPreset).name === 'string' &&
        (p as BrandingPreset).name.length <= 30 &&
        isWatermarkSpec((p as BrandingPreset).spec),
    )
  );
}

/** What the free plan may change; the rest needs Premium. */
export function freeSpec(s: WatermarkSpec): WatermarkSpec {
  return { ...s, enabled: true, custom: null };
}
