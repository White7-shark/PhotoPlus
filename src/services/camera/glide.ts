/**
 * Glide: a 7-second pro video from the camera, made steady, clean and sharp,
 * with optional Spotlight (background blur) and Glow (beauty). Pure logic —
 * request building, timing and labels — so every rule is unit-tested.
 */
import type { WatermarkSpec } from '../edit/watermark';
import { BLUR_LEVELS, beautySettings, type BeautySettings, type BlurLevel, type GlowLevel } from './model';

/** The longest Glide clip (the native recorder enforces the same limit). */
export const GLIDE_MAX_MS = 7_000;
/** Shorter clips have too little motion to steady; they are not processed. */
export const GLIDE_MIN_MS = 1_000;

export type GlideSpotlight = 'off' | BlurLevel;

export const GLIDE_SPOTLIGHT_LEVELS: readonly { id: GlideSpotlight; label: string }[] = [
  { id: 'off', label: 'Off' },
  ...BLUR_LEVELS.map(l => ({ id: l.id, label: l.label })),
];

/** What the native `glide` job takes. */
export interface GlideRequest {
  inputPath: string;
  outputPath: string;
  /** 0..1: how smooth the camera path becomes (1 is a gimbal-like glide). */
  steady: number;
  /** Largest crop the steadying may use, 1..1.25. */
  maxZoom: number;
  denoise: number;
  sharpen: number;
  /** Spotlight background blur 0..1 (0 = off) and subject pop. */
  blur: number;
  pop: number;
  /** Glow and face touches (null = off). */
  beauty: BeautySettings | null;
  /** Free-plan clips carry the mark in every frame, as photos do. */
  watermark?: WatermarkSpec;
}

export interface GlideResult {
  outputPath: string;
  width: number;
  height: number;
  frames: number;
  fps: number;
  durationMs: number;
  zoom: number;
  shakeBefore: number;
  shakeAfter: number;
  processMs: number;
  watermarked: boolean;
}

/** A finished Glide clip: the result, the untouched recording, and its gallery copy. */
export interface GlideClip extends GlideResult {
  rawPath: string;
  galleryUri: string | null;
}

export type GlideStage = 'analysing' | 'measuring' | 'crafting' | 'done';

export function glideRequest(args: {
  inputPath: string;
  outputPath: string;
  spotlight: GlideSpotlight;
  glow: GlowLevel;
  watermark?: WatermarkSpec;
}): GlideRequest {
  const blur = args.spotlight === 'off' ? 0 : (BLUR_LEVELS.find(l => l.id === args.spotlight)?.value ?? 0);
  return {
    inputPath: args.inputPath,
    outputPath: args.outputPath,
    steady: 1,
    maxZoom: 1.12,
    denoise: 1,
    sharpen: 1,
    blur,
    pop: blur > 0 ? 0.4 : 0,
    beauty: beautySettings(args.glow),
    ...(args.watermark ? { watermark: args.watermark } : {}),
  };
}

/** Where a finished clip is written (the exports area, so it can be shared). */
export function glideFileName(now: Date): string {
  const p = (n: number, w = 2) => String(n).padStart(w, '0');
  return (
    `PhotoPlus_Glide_${now.getFullYear()}${p(now.getMonth() + 1)}${p(now.getDate())}_` +
    `${p(now.getHours())}${p(now.getMinutes())}${p(now.getSeconds())}.mp4`
  );
}

/** Seconds left on the recording badge (counts down 7 → 0). */
export function secondsLeft(elapsedMs: number): number {
  return Math.max(0, Math.ceil((GLIDE_MAX_MS - Math.max(0, elapsedMs)) / 1000));
}

/** 0..1 of the 7 seconds used, for the progress ring. */
export function recordedFraction(elapsedMs: number): number {
  return Math.min(1, Math.max(0, elapsedMs / GLIDE_MAX_MS));
}

export function glideStageLabel(stage: GlideStage | null, fraction: number): string {
  const pct = `${Math.round(Math.min(1, Math.max(0, fraction)) * 100)}%`;
  switch (stage) {
    case 'measuring':
      return 'Reading the light…';
    case 'crafting':
      return `Crafting your Glide · ${pct}`;
    case 'done':
      return 'Saving…';
    case 'analysing':
    default:
      return `Finding the shake · ${pct}`;
  }
}

/**
 * How much steadier the clip became, from the engine's own measure of
 * frame-to-frame jitter (px). Null when there was nothing to steady.
 */
export function steadierPercent(shakeBefore: number, shakeAfter: number): number | null {
  if (!Number.isFinite(shakeBefore) || !Number.isFinite(shakeAfter) || shakeBefore < 0.5) {
    return null;
  }
  const pct = Math.round((1 - Math.max(0, shakeAfter) / shakeBefore) * 100);
  return pct > 0 ? Math.min(pct, 99) : null;
}

export function glideSummary(r: Pick<GlideResult, 'shakeBefore' | 'shakeAfter' | 'durationMs'>): string {
  const seconds = `${(r.durationMs / 1000).toFixed(1)} s`;
  const steadier = steadierPercent(r.shakeBefore, r.shakeAfter);
  return steadier ? `${seconds} · ${steadier}% steadier` : seconds;
}

export function isGlideStage(value: unknown): value is GlideStage {
  return value === 'analysing' || value === 'measuring' || value === 'crafting' || value === 'done';
}
