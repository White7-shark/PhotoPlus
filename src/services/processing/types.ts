import type { WatermarkSpec } from '../edit/watermark';

/** Mirrors ProcessRequest.kt / pp_op_type. */
export type EditOperation =
  | { type: 'tone'; exposure?: number; brightness?: number; contrast?: number }
  | { type: 'grayscale' }
  | { type: 'rotate'; quarterTurns: number }
  | { type: 'flip'; horizontal?: boolean; vertical?: boolean }
  | { type: 'crop'; x: number; y: number; width: number; height: number }
  | { type: 'resize'; width: number; height: number }
  | { type: 'sharpen'; amount: number }
  | { type: 'saturation'; amount: number };

export type OutputFormat = 'jpeg' | 'png' | 'webp';

export interface ProcessRequest {
  inputPath: string;
  outputPath: string;
  operations: EditOperation[];
  format?: OutputFormat;
  quality?: number;
  maxDimension?: number;
}

export interface ProcessResult {
  outputPath: string;
  width: number;
  height: number;
  sourceWidth: number;
  sourceHeight: number;
  format: OutputFormat;
  sizeBytes: number;
  timings: { decodeMs: number; processMs: number; totalMs: number };
}

export interface EngineInfo {
  available: boolean;
  version: string | null;
  loadError: string | null;
  workers: number;
  memoryBudgetBytes: number;
  maxDecodeDimension: number;
  operations: string[];
  outputFormats: string[];
}

export interface EngineMemoryStats {
  currentBytes: number;
  peakBytes: number;
  budgetBytes: number;
  liveAllocations: number;
  totalAllocations: number;
  failedAllocations: number;
  activeJobs: number;
}

export type JobState = 'queued' | 'running' | 'succeeded' | 'failed' | 'cancelled';

/** Restore a saved photo with the enhancement engine (pp_enhance). */
export interface EnhanceRequest {
  inputPath: string;
  outputPath: string;
  /** 2 = single-image super-resolution (doubles each side). */
  upscale?: 1 | 2;
  denoise?: number;
  sharpen?: number;
  detail?: number;
  vibrance?: number;
  autoTone?: boolean;
  /** Longest output edge; clamped natively to the device budget. */
  maxDimension?: number;
  quality?: number;
  /** Free plan: the result carries this watermark; its clean copy is reported. */
  watermark?: WatermarkSpec;
}

export interface EnhanceResult {
  outputPath: string;
  cleanPath?: string | null;
  width: number;
  height: number;
  sourceWidth: number;
  sourceHeight: number;
  sizeBytes: number;
  report: {
    noiseBefore: number;
    noiseAfter: number;
    blurBefore: number;
    deblurSigma: number;
    sharpnessAfter: number;
    upscale: number;
  };
  timings: { totalMs: number };
}

export interface SpotlightRequest {
  inputPath: string;
  outputPath: string;
  /** What should stand out, as taps on the photo as displayed (x0, y0, … in 0..1). */
  points?: number[];
  blur?: number;
  retouch?: number;
  brighten?: number;
  pop?: number;
  /** Clarity first (default true). */
  enhance?: boolean;
  look?: number[];
  lookStrength?: number;
  watermark?: WatermarkSpec;
}

export interface SpotlightResult {
  outputPath: string;
  cleanPath?: string | null;
  width: number;
  height: number;
  sizeBytes: number;
  portrait: { people: number; spots: number; skin: number; subject: number };
}

/** Transform: high-end lens quality, then the photo's effects re-applied. */
export interface TransformRequest {
  /** The photo to restore (the untouched base when it has one). */
  inputPath: string;
  outputPath: string;
  /** 2 = twice the detail. */
  upscale?: 1 | 2;
  /** The base already carries effects: keep its colours and any vignette. */
  keepStyle?: boolean;
  /** Spotlight to re-apply (taps in the upright photo). */
  spotlight?: { points: number[]; blur: number; retouch: number; brighten: number; pop: number } | null;
  look?: number[] | null;
  lookStrength?: number;
  maxDimension?: number;
  watermark?: WatermarkSpec;
}

export interface TransformReport {
  fringesFixedPx: number;
  deblur: number;
  deblurCorners: number;
  noiseBefore: number;
  noiseAfter: number;
  gradeBefore: number;
  upscale: number;
}

export interface TransformResult {
  outputPath: string;
  cleanPath?: string | null;
  width: number;
  height: number;
  sizeBytes: number;
  transform: TransformReport;
  portrait: { people: number; spots: number; skin: number; subject: number } | null;
}

export interface LookResult {
  outputPath: string;
  cleanPath?: string | null;
  width: number;
  height: number;
  sizeBytes: number;
}

export interface QualityReport {
  noise: number;
  /** Estimated blur in pixels at the analysis size (1600 px longest edge). */
  blurPx: number;
  sharpness: number;
  meanLuma: number;
  highlightsClipped: number;
  shadowsClipped: number;
}
