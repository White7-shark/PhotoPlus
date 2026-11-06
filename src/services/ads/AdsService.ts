import { errorCode, errorMessage, nativeModules, type AdsSpec } from '../../native';

/** A photo's progress towards a watermark-free export (four ads). */
export interface RewardProgress {
  watched: number;
  required: number;
  unlocked: boolean;
}

export class AdsError extends Error {
  constructor(
    message: string,
    readonly code: string,
  ) {
    super(message);
    this.name = 'AdsError';
  }
}

/**
 * Rewarded ads that unlock a free photo for export without the watermark.
 * The native side counts rewards (only from the ad network's callback) and
 * export checks them natively, so nothing here can grant an unlock.
 */
export class AdsService {
  private started: Promise<boolean> | null = null;

  constructor(private readonly native: AdsSpec | null | undefined = nativeModules.ads) {}

  get available(): boolean {
    return this.native != null;
  }

  /** Consent (where required) and the ad network, once per run. */
  start(): Promise<boolean> {
    const native = this.native;
    if (!native) return Promise.resolve(false);
    this.started ??= native
      .initialize()
      .then(r => (r as { canRequestAds?: boolean }).canRequestAds === true)
      .catch(() => {
        this.started = null;
        return false;
      });
    return this.started;
  }

  async progress(photoPath: string): Promise<RewardProgress> {
    const native = this.native;
    if (!native) return { watched: 0, required: 4, unlocked: false };
    try {
      const r = (await native.getRewardProgress(photoPath)) as unknown as RewardProgress;
      return { watched: r.watched ?? 0, required: r.required ?? 4, unlocked: r.unlocked === true };
    } catch {
      return { watched: 0, required: 4, unlocked: false };
    }
  }

  /** Shows one ad; resolves with the new progress (rewarded only if watched to the end). */
  async watch(photoPath: string): Promise<RewardProgress & { rewarded: boolean }> {
    const native = this.native;
    if (!native) throw new AdsError('Ads aren’t available on this device', 'ADS_UNAVAILABLE');
    await this.start();
    try {
      const r = (await native.showRewardedAd(photoPath)) as unknown as RewardProgress & { rewarded: boolean };
      return {
        watched: r.watched ?? 0,
        required: r.required ?? 4,
        unlocked: r.unlocked === true,
        rewarded: r.rewarded === true,
      };
    } catch (error) {
      throw new AdsError(errorMessage(error), errorCode(error) ?? 'AD_NOT_AVAILABLE');
    }
  }
}

let shared: AdsService | null = null;
export function getAdsService(): AdsService {
  shared ??= new AdsService();
  return shared;
}
