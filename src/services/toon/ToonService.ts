/**
 * Toon Studio's bridge to the device (StudioModule.renderToon): the photo as
 * anime or a cartoon — a card thumbnail, a quick preview, or the finished
 * picture (Premium, or two a day on the free plan, each unlocked by a
 * rewarded ad the device counts). Results are checked before use.
 */
import { errorCode, errorMessage, nativeModules, requireNative, type AdsSpec, type StudioSpec } from '../../native';
import { getAdsService } from '../ads/AdsService';
import { createId } from '../database/ids';
import { clampToon, type ToonSettings } from './catalog';

export type ToonQuality = 'thumb' | 'preview' | 'full';

export interface ToonPicture {
  outputPath: string;
  width: number;
  height: number;
  /** Free pictures left today; -1 unlimited; absent when unknown. */
  remaining?: number;
}

export interface ToonAllowance {
  remaining: number;
  limit: number;
  credits: number;
  needsAd: boolean;
}

export class ToonError extends Error {
  constructor(
    message: string,
    readonly code: string,
  ) {
    super(message);
    this.name = 'ToonError';
  }
}

const num = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
const FALLBACK: ToonAllowance = { remaining: 2, limit: 2, credits: 0, needsAd: true };

export class ToonService {
  constructor(
    private readonly native: StudioSpec,
    private readonly ads: AdsSpec | null | undefined = nativeModules.ads,
    private readonly startAds: () => Promise<boolean> = () => getAdsService().start(),
  ) {}

  async render(
    inputPath: string,
    settings: ToonSettings,
    quality: ToonQuality,
    seed = 7,
    jobId: string = createId('toon'),
  ): Promise<ToonPicture> {
    let out: unknown;
    try {
      out = await this.native.renderToon(jobId, { inputPath, quality, seed, ...clampToon(settings) });
    } catch (e) {
      throw new ToonError(errorMessage(e), errorCode(e) ?? 'INTERNAL');
    }
    const p = out as Partial<ToonPicture> | null;
    if (
      !p ||
      typeof p.outputPath !== 'string' ||
      !p.outputPath ||
      !num(p.width) ||
      !num(p.height) ||
      p.width <= 0 ||
      p.height <= 0
    )
      throw new ToonError('The picture came back unexpected', 'INTERNAL');
    return {
      outputPath: p.outputPath,
      width: p.width,
      height: p.height,
      remaining: num(p.remaining) ? p.remaining : undefined,
    };
  }

  cancel(jobId: string): void {
    try {
      this.native.cancel(jobId);
    } catch {
      // Already finished.
    }
  }

  async allowance(): Promise<ToonAllowance> {
    try {
      const a = (await this.native.toonAllowance()) as Partial<ToonAllowance> | null;
      if (!a || !num(a.remaining)) return FALLBACK;
      const credits = num(a.credits) ? Math.max(0, a.credits) : 0;
      return {
        remaining: a.remaining,
        limit: num(a.limit) ? a.limit : 2,
        credits,
        needsAd: a.remaining !== -1 && (typeof a.needsAd === 'boolean' ? a.needsAd : a.remaining > 0 && credits === 0),
      };
    } catch {
      return FALLBACK;
    }
  }

  /** A rewarded ad for a free picture; resolves whether it was watched to the end. */
  async unlock(): Promise<{ rewarded: boolean; allowance: ToonAllowance }> {
    const ads = this.ads;
    if (!ads) throw new ToonError('Ads aren’t available on this device', 'ADS_UNAVAILABLE');
    try {
      await this.startAds().catch(() => false);
      const r = (await ads.showRewardedAdForTool('toon')) as { rewarded?: boolean } | null;
      return { rewarded: r?.rewarded === true, allowance: await this.allowance() };
    } catch (e) {
      throw new ToonError(errorMessage(e), errorCode(e) ?? 'AD_NOT_AVAILABLE');
    }
  }
}

let service: ToonService | null = null;
export function getToonService(): ToonService {
  if (!service) service = new ToonService(requireNative(nativeModules.studio, 'PhotoPlusStudio'));
  return service;
}
