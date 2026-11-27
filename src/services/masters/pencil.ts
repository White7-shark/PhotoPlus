/**
 * Pencil Art: the masters' drawings (no paint) — the medium, the paper, how
 * bold the lines and how deep the shading — and the artist's signature
 * (Premium) that both drawings and paintings can carry.
 */
import { t, type StringKey } from '../../i18n';

export type ArtMode = 'paint' | 'pencil';

export const MEDIA = ['graphite', 'charcoal', 'red_chalk'] as const;
export type PencilMedium = (typeof MEDIA)[number];
export const PAPERS = ['white', 'cream', 'grey', 'tan'] as const;
export type PencilPaper = (typeof PAPERS)[number];

export interface PencilOptions {
  /** 0 graphite, 1 charcoal, 2 red chalk. */
  medium: number;
  /** 0 white, 1 cream, 2 grey toned, 3 tan toned. */
  paper: number;
  /** 0 close to the photo .. 1 the master's whole manner. */
  intensity: number;
  /** 0.5 .. 1.5 */
  lines: number;
  /** 0.5 .. 1.5 */
  shading: number;
}

export const PENCIL_DEFAULTS: PencilOptions = { medium: 0, paper: 0, intensity: 0.6, lines: 1, shading: 1 };

const clamp = (v: unknown, lo: number, hi: number, def: number) =>
  typeof v === 'number' && Number.isFinite(v) ? Math.min(hi, Math.max(lo, v)) : def;

export function clampPencil(p: Partial<PencilOptions>): PencilOptions {
  const d = PENCIL_DEFAULTS;
  return {
    medium: Math.round(clamp(p.medium, 0, MEDIA.length - 1, d.medium)),
    paper: Math.round(clamp(p.paper, 0, PAPERS.length - 1, d.paper)),
    intensity: clamp(p.intensity, 0, 1, d.intensity),
    lines: clamp(p.lines, 0.5, 1.5, d.lines),
    shading: clamp(p.shading, 0.5, 1.5, d.shading),
  };
}

export const mediumName = (m: PencilMedium) => t(`pencil_medium_${m}` as StringKey);
export const paperName = (p: PencilPaper) => t(`pencil_paper_${p}` as StringKey);

// ---- the signature ----------------------------------------------------------

export const SIG_STYLES = ['script', 'serif', 'print'] as const;
export type SigStyle = (typeof SIG_STYLES)[number];
export const SIG_POSITIONS = ['left', 'center', 'right'] as const;
export type SigPosition = (typeof SIG_POSITIONS)[number];

export interface Signature {
  enabled: boolean;
  text: string;
  line2: string;
  style: SigStyle;
  position: SigPosition;
  /** 0..1 */
  size: number;
  year: boolean;
}

export const SIGNATURE_DEFAULTS: Signature = {
  enabled: false,
  text: '',
  line2: '',
  style: 'script',
  position: 'right',
  size: 0.5,
  year: true,
};

export function isSignature(v: unknown): v is Signature {
  const s = v as Signature | null;
  return (
    !!s &&
    typeof s === 'object' &&
    typeof s.enabled === 'boolean' &&
    typeof s.text === 'string' &&
    s.text.length <= 40 &&
    typeof s.line2 === 'string' &&
    s.line2.length <= 40 &&
    (SIG_STYLES as readonly string[]).includes(s.style) &&
    (SIG_POSITIONS as readonly string[]).includes(s.position) &&
    typeof s.size === 'number' &&
    s.size >= 0 &&
    s.size <= 1 &&
    typeof s.year === 'boolean'
  );
}

/** What the device is sent (nothing when off or empty): at most 40 characters a line. */
export function signatureRequest(s: Signature): Record<string, unknown> | undefined {
  const text = s.text.replace(/\s+/g, ' ').trim().slice(0, 40);
  if (!s.enabled || !text) return undefined;
  const line2 = s.line2.replace(/\s+/g, ' ').trim().slice(0, 40);
  return {
    text,
    ...(line2 ? { line2 } : {}),
    style: (SIG_STYLES as readonly string[]).includes(s.style) ? s.style : 'script',
    position: (SIG_POSITIONS as readonly string[]).includes(s.position) ? s.position : 'right',
    size: clamp(s.size, 0, 1, 0.5),
    year: s.year,
  };
}

/** How long the live studio takes: long enough to enjoy, short enough not to wait. */
export const LIVE_MS: Record<ArtMode, number> = { pencil: 7000, paint: 8000 };
