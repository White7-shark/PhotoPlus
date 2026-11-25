import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * Rewards for the free plan (Kotlin: com.photoplus.rewards.RewardsModule):
 * points from check-ins, ads and tasks, spent on Premium time or a Premium
 * tool for a day; and the share campaign. Kept natively; ad points come
 * from the ad network's reward callback (PhotoPlusAds.showRewardedAdForPoints).
 */
export interface Spec extends TurboModule {
  getState(): Promise<Object>;
  checkIn(): Promise<Object>;
  /** Something the person did, counted toward tasks: photo, edit, studio, product, film, project, glide. */
  recordActivity(event: string): void;
  claimTask(id: string): Promise<Object>;
  /** item: tool, premium-1, premium-3, premium-7; tool: the tool for a tool unlock (else ""). */
  redeem(item: string, tool: string): Promise<Object>;
  claimSharePass(): Promise<Object>;
  /** Every app that takes a picture: [{ pkg, activity, label, app, icon }] (the in-app share sheet). */
  shareTargets(): Promise<Object[]>;
  /** Opens one app's share screen (package and its share activity) with a campaign picture and words. */
  share(target: string, activity: string, asset: string, text: string): Promise<Object>;
  addListener(eventName: string): void;
  removeListeners(count: number): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusRewards');
