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
import { clampPencil, type ArtMode, type PencilOptions } from './pencil';
import { getAdsService } from '../ads/AdsService';
import { readToolAllowance, readToolUnlock, unknownAllowance, type ToolAllowance, type ToolUnlock } from '../ads/toolAllowance';

export type MastersQuality = 'thumb' | 'preview' | 'full';

export interface Painting {
  outputPath: string;
  width: number;
  height: number;
  sizeBytes: number;
  /** Finished paintings left today on the free plan; -1 when unlimited (Premium); absent when unknown. */
  remaining?: number;
  /** The picture inside its frame (x0, y0, x1, y1, 0..1). */
  inner?: [number, number, number, number];
  /** The live studio: the blank sheet or primed ground the work starts from. */
  blankPath?: string;
}

/** What else a work is made with: drawing or painting, the pencil's options, a signature, the live studio's blank. */
export interface PaintExtras {
  mode?: ArtMode;
  pencil?: PencilOptions;
  signature?: Record<string, unknown>;
  live?: boolean;
}

/** Photo Arts' day on the free plan (paintings and drawings together): each work after an ad. */
export type AdAllowance = ToolAllowance;
/** The same. */
export type Allowance = ToolAllowance;

/** Five works a day on the free plan. */
export const ARTS_FALLBACK: ToolAllowance = unknownAllowance(5);

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
    frame = 0,
    extras: PaintExtras = {},
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
        ...(frame > 0 ? { frame } : {}),
        ...(extras.mode === 'pencil' ? { mode: 'pencil', pencil: clampPencil(extras.pencil ?? {}) } : {}),
        ...(extras.signature ? { signature: extras.signature } : {}),
        ...(extras.live ? { live: true } : {}),
      });
    } catch (e) {
      throw new MastersError(errorMessage(e), errorCode(e) ?? 'INTERNAL');
    }
    if (!isPainting(out)) throw new MastersError('The painting came back unexpected', 'INTERNAL');
    const remaining = num(out.remaining) ? out.remaining : undefined;
    const inner =
      Array.isArray(out.inner) && out.inner.length === 4 && out.inner.every(num)
        ? (out.inner as Painting['inner'])
        : undefined;
    return {
      outputPath: out.outputPath,
      width: out.width,
      height: out.height,
      sizeBytes: num(out.sizeBytes) ? out.sizeBytes : 0,
      remaining,
      ...(inner ? { inner } : {}),
      ...(typeof out.blankPath === 'string' && out.blankPath ? { blankPath: out.blankPath } : {}),
    };
  }

  cancel(jobId: string): void {
    try {
      this.native.cancel(jobId);
    } catch {
      // Already finished.
    }
  }

  /** Photo Arts today on the free plan (Premium: remaining -1). */
  async allowance(): Promise<Allowance> {
    try {
      return readToolAllowance(await this.native.mastersAllowance(), ARTS_FALLBACK);
    } catch {
      return ARTS_FALLBACK;
    }
  }

  /** Pencil Art shares Photo Arts' allowance. */
  async pencilAllowance(): Promise<AdAllowance> {
    return this.allowance();
  }

  /** A rewarded ad for a free work (offline: unlocked on credit, the ad owed). */
  async unlockArts(ads = nativeModules.ads): Promise<ToolUnlock> {
    if (!ads) throw new MastersError('Ads aren’t available on this device', 'ADS_UNAVAILABLE');
    try {
      await getAdsService()
        .start()
        .catch(() => false);
      const r = await ads.showRewardedAdForTool('arts');
      return readToolUnlock(r, await this.allowance());
    } catch (e) {
      throw new MastersError(errorMessage(e), errorCode(e) ?? 'AD_NOT_AVAILABLE');
    }
  }

  /** The same (the name the pencil flow used). */
  unlockPencil(ads = nativeModules.ads): Promise<ToolUnlock> {
    return this.unlockArts(ads);
  }
}

let service: MastersService | null = null;
export function getMastersService(): MastersService {
  if (!service) service = new MastersService(requireNative(nativeModules.studio, 'PhotoPlusStudio'));
  return service;
}
