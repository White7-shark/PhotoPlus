/**
 * Photo Motion's bridge to the device (StudioModule.startMotion /
 * renderMotion, AdsModule.showRewardedAdForMotion). A photo is read once (its
 * person, faces, depth and sky); every preview and the finished video are
 * made from that. Previews are free; finished videos are Premium, or two a
 * day on the free plan, each unlocked by a rewarded ad. The device counts
 * the videos and the ads (from the ad network's own callback), so nothing
 * here can grant one. Results are checked before use.
 */
import { errorCode, errorMessage, nativeModules, requireNative, type AdsSpec, type StudioSpec } from '../../native';
import { getAdsService } from '../ads/AdsService';
import { createId } from '../database/ids';
import { clampSettings, packArrows, packPins, type MotionArrow, type MotionPin, type MotionSettings } from './catalog';

export type MotionQuality = 'preview' | 'full';

export interface MotionScene {
  sessionId: string;
  width: number;
  height: number;
  /** Share of the photo that is sky (0..1). */
  sky: number;
  /** Whether the depth of the scene was found (else it is estimated from the layout). */
  depth: boolean;
  faces: number;
}

export interface MotionVideo {
  outputPath: string;
  posterPath: string;
  width: number;
  height: number;
  durationMs: number;
  /** Free videos left today; -1 when unlimited; absent when unknown. */
  remaining?: number;
}

export interface MotionAllowance {
  /** Videos left today; -1 when unlimited (Premium). */
  remaining: number;
  limit: number;
  /** Videos unlocked by an ad and not made yet. */
  credits: number;
  /** The next video needs an ad first. */
  needsAd: boolean;
}

export class MotionError extends Error {
  constructor(
    message: string,
    readonly code: string,
  ) {
    super(message);
    this.name = 'MotionError';
  }
}

const num = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
const str = (v: unknown): v is string => typeof v === 'string' && v.length > 0;

export function isVideo(v: unknown): v is MotionVideo {
  const o = v as MotionVideo | null;
  return !!o && str(o.outputPath) && num(o.width) && num(o.height) && o.width > 0 && o.height > 0;
}

export function readAllowance(v: unknown, fallback: MotionAllowance): MotionAllowance {
  const a = v as Partial<MotionAllowance> | null;
  if (!a || !num(a.remaining)) return fallback;
  const limit = num(a.limit) ? a.limit : 2;
  const credits = num(a.credits) ? Math.max(0, a.credits) : 0;
  return {
    remaining: a.remaining,
    limit,
    credits,
    needsAd: a.remaining !== -1 && (typeof a.needsAd === 'boolean' ? a.needsAd : a.remaining > 0 && credits === 0),
  };
}

/** What the free plan has before the device answers: a video, after an ad. */
export const UNKNOWN_ALLOWANCE: MotionAllowance = { remaining: 2, limit: 2, credits: 0, needsAd: true };

export class MotionService {
  constructor(
    private readonly native: StudioSpec,
    private readonly ads: AdsSpec | null | undefined = nativeModules.ads,
    // Consent and the ad network, once per run (shared with the other ads).
    private readonly startAds: () => Promise<boolean> = () => getAdsService().start(),
  ) {}

  private async call<T>(run: () => Promise<unknown>, check: (v: unknown) => v is T, what: string): Promise<T> {
    let out: unknown;
    try {
      out = await run();
    } catch (e) {
      throw new MotionError(errorMessage(e), errorCode(e) ?? 'INTERNAL');
    }
    if (!check(out)) throw new MotionError(`${what} came back unexpected`, 'INTERNAL');
    return out;
  }

  /** Reads a photo once: its person, faces, depth and sky. */
  async start(inputPath: string, jobId: string = createId('motion')): Promise<MotionScene> {
    const isScene = (v: unknown): v is MotionScene => {
      const s = v as MotionScene | null;
      return !!s && str(s.sessionId) && num(s.width) && num(s.height) && s.width > 0 && s.height > 0;
    };
    const s = await this.call(() => this.native.startMotion(jobId, { inputPath }), isScene, 'The photo');
    return {
      sessionId: s.sessionId,
      width: s.width,
      height: s.height,
      sky: num(s.sky) ? Math.min(1, Math.max(0, s.sky)) : 0,
      depth: s.depth === true,
      faces: num(s.faces) ? s.faces : 0,
    };
  }

  /** A preview (small, quick, free) or the finished video. jobId: to cancel it. */
  async render(
    sessionId: string,
    settings: MotionSettings,
    arrows: readonly MotionArrow[],
    pins: readonly MotionPin[],
    quality: MotionQuality,
    jobId: string = createId('motion'),
  ): Promise<MotionVideo> {
    const s = clampSettings(settings);
    const a = packArrows(arrows);
    const p = packPins(pins);
    const v = await this.call(
      () =>
        this.native.renderMotion(jobId, {
          sessionId,
          quality,
          ...s,
          ...(a.length ? { arrows: a } : {}),
          ...(p.length ? { anchors: p } : {}),
        }),
      isVideo,
      'The video',
    );
    return {
      outputPath: v.outputPath,
      posterPath: str(v.posterPath) ? v.posterPath : '',
      width: v.width,
      height: v.height,
      durationMs: num(v.durationMs) ? v.durationMs : s.duration * 1000,
      remaining: num(v.remaining) ? v.remaining : undefined,
    };
  }

  cancel(jobId: string): void {
    try {
      this.native.cancel(jobId);
    } catch {
      // Already finished.
    }
  }

  end(sessionId: string): void {
    try {
      this.native.endMotion(sessionId);
    } catch {
      // Already gone.
    }
  }

  async allowance(): Promise<MotionAllowance> {
    try {
      return readAllowance(await this.native.motionAllowance(), UNKNOWN_ALLOWANCE);
    } catch {
      // Unknown: the device decides when the video is made.
      return UNKNOWN_ALLOWANCE;
    }
  }

  /** Shows a rewarded ad; watched to the end, it unlocks one of today's free videos. */
  async unlock(): Promise<{ rewarded: boolean; allowance: MotionAllowance }> {
    const ads = this.ads;
    if (!ads) throw new MotionError('Ads aren’t available on this device', 'ADS_UNAVAILABLE');
    try {
      await this.startAds().catch(() => false);
      const r = (await ads.showRewardedAdForTool('motion')) as { rewarded?: boolean; allowance?: unknown } | null;
      return { rewarded: r?.rewarded === true, allowance: readAllowance(r?.allowance, await this.allowance()) };
    } catch (e) {
      if (e instanceof MotionError) throw e;
      throw new MotionError(errorMessage(e), errorCode(e) ?? 'AD_NOT_AVAILABLE');
    }
  }
}

let service: MotionService | null = null;
export function getMotionService(): MotionService {
  if (!service) service = new MotionService(requireNative(nativeModules.studio, 'PhotoPlusStudio'));
  return service;
}
