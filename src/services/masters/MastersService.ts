/**
 * Masters' bridge to the device (StudioModule.renderMasters): a photo painted
 * by one of the twenty artists, as a small card thumbnail, a preview, or the
 * finished painting (Premium, or one a day on the free plan). Results are
 * checked before use.
 */
import { errorCode, errorMessage, nativeModules, requireNative, type StudioSpec } from '../../native';
import { createId } from '../database/ids';
import { ARTISTS, type MastersLevel } from './catalog';
import { clampControls, type ArtControls } from './controls';

export type MastersQuality = 'thumb' | 'preview' | 'full';

export interface Painting {
  outputPath: string;
  width: number;
  height: number;
  sizeBytes: number;
  /** Finished paintings left today on the free plan; -1 when unlimited (Premium); absent when unknown. */
  remaining?: number;
}

export interface Allowance {
  /** -1 when unlimited. */
  remaining: number;
  limit: number;
}

export class MastersError extends Error {
  constructor(
    message: string,
    readonly code: string,
  ) {
    super(message);
    this.name = 'MastersError';
  }
}

const num = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);

export function isPainting(v: unknown): v is Painting {
  const p = v as Painting | null;
  return (
    !!p &&
    typeof p.outputPath === 'string' &&
    p.outputPath.length > 0 &&
    num(p.width) &&
    num(p.height) &&
    p.width > 0 &&
    p.height > 0
  );
}

/** The same photo and artist always paint the same picture; "Paint again" gives a new one. */
export const FIRST_SEED = 7;

export class MastersService {
  constructor(private readonly native: StudioSpec) {}

  /** jobId: to cancel it (a newer preview replaces an older one). */
  async paint(
    inputPath: string,
    artist: number,
    level: MastersLevel,
    quality: MastersQuality,
    seed = FIRST_SEED,
    jobId: string = createId('masters'),
    controls?: ArtControls,
  ): Promise<Painting> {
    if (!ARTISTS.some(a => a.id === artist)) throw new MastersError('Unknown artist', 'INVALID_ARGUMENT');
    let out: unknown;
    try {
      out = await this.native.renderMasters(jobId, {
        inputPath,
        artist,
        level,
        quality,
        seed,
        ...(controls ? { controls: clampControls(controls) } : {}),
      });
    } catch (e) {
      throw new MastersError(errorMessage(e), errorCode(e) ?? 'INTERNAL');
    }
    if (!isPainting(out)) throw new MastersError('The painting came back unexpected', 'INTERNAL');
    const remaining = num(out.remaining) ? out.remaining : undefined;
    return {
      outputPath: out.outputPath,
      width: out.width,
      height: out.height,
      sizeBytes: num(out.sizeBytes) ? out.sizeBytes : 0,
      remaining,
    };
  }

  cancel(jobId: string): void {
    try {
      this.native.cancel(jobId);
    } catch {
      // Already finished.
    }
  }

  async allowance(): Promise<Allowance> {
    try {
      const a = (await this.native.mastersAllowance()) as Partial<Allowance> | null;
      return { remaining: num(a?.remaining) ? a.remaining : 0, limit: num(a?.limit) ? a.limit : 1 };
    } catch {
      // Unknown: let the device decide when the painting is made.
      return { remaining: 1, limit: 1 };
    }
  }
}

let service: MastersService | null = null;
export function getMastersService(): MastersService {
  if (!service) service = new MastersService(requireNative(nativeModules.studio, 'PhotoPlusStudio'));
  return service;
}
