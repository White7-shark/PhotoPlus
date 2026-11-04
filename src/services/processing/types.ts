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
}

export interface EnhanceResult {
  outputPath: string;
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

export interface QualityReport {
  noise: number;
  blurPx: number;
  sharpness: number;
  meanLuma: number;
  highlightsClipped: number;
  shadowsClipped: number;
}
