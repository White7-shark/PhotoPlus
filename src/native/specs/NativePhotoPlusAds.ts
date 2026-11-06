import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * Rewarded ads (Kotlin: com.photoplus.ads.AdsModule). On the free plan,
 * watching four ads for a photo lets that photo be exported, saved or shared
 * without the watermark. Progress is counted natively from the ad network's
 * reward callback; export checks it natively too.
 */
export interface Spec extends TurboModule {
  /** Asks for ad consent where the law requires it, then starts the ad network. */
  initialize(): Promise<Object>;
  /** { watched, required, unlocked } for a photo (its untouched copy's path). */
  getRewardProgress(photoPath: string): Promise<Object>;
  /** Shows one rewarded ad; resolves with { rewarded, watched, required, unlocked }. */
  showRewardedAd(photoPath: string): Promise<Object>;
  /** Whether a privacy (consent) settings entry must be offered. */
  privacyOptionsRequired(): Promise<boolean>;
  showPrivacyOptions(): Promise<boolean>;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusAds');
