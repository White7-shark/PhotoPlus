import type { QualityReport } from './types';

/** Thresholds on the engine's 0..1 luma scale, matched to its estimators. */
const NOISY = 0.012;
const SOFT_PX = 1.2;

export interface EnhanceSuggestion {
  suggested: boolean;
  reasons: string[];
}

/** Explains, in plain words, why a photo would benefit from Enhance. */
export function enhanceSuggestion(q: QualityReport | null): EnhanceSuggestion {
  if (!q) {
    return { suggested: false, reasons: [] };
  }
  const reasons: string[] = [];
  if (q.noise >= NOISY) {
    reasons.push('visible noise');
  }
  if (q.blurPx >= SOFT_PX) {
    reasons.push('soft focus');
  }
  if (q.meanLuma < 0.25 && q.shadowsClipped < 0.2) {
    reasons.push('underexposed');
  }
  return { suggested: reasons.length > 0, reasons };
}

/** Sibling path for an enhanced copy: PP_x.jpg → PP_x_enhanced.jpg (or _superres). */
export function enhancedPathFor(path: string, upscale: 1 | 2, attempt = 0): string {
  const dot = path.lastIndexOf('.');
  const slash = path.lastIndexOf('/');
  const stem = dot > slash ? path.slice(0, dot) : path;
  const suffix = upscale === 2 ? '_superres' : '_enhanced';
  return `${stem}${suffix}${attempt > 0 ? `_${attempt}` : ''}.jpg`;
}

/** Percentage of noise removed, for the result summary. */
export function noiseReduction(before: number, after: number): number {
  if (!(before > 0) || !(after >= 0)) {
    return 0;
  }
  return Math.max(0, Math.min(100, Math.round((1 - after / before) * 100)));
}

export function formatMegapixels(width: number, height: number): string {
  const mp = (width * height) / 1e6;
  return `${mp >= 10 ? Math.round(mp) : Math.round(mp * 10) / 10} MP`;
}
