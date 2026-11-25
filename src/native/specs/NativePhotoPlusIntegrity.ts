import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * The app's integrity (Kotlin: com.photoplus.integrity.IntegrityModule):
 * the phone's time must be right (checked against the internet), the app
 * must go online at least once a week, and a new version must be installed
 * within 7 days. Blocked until put right.
 */
export interface Spec extends TurboModule {
  /** { blocked, reason, skewMinutes, offlineDays, maxOfflineDays, lastOnline, now, online, update: { pending, required, banner, deadline } } */
  getState(): Promise<Object>;
  /** An online check now (time and updates); force skips the 10-minute gap. */
  check(force: boolean): Promise<Object>;
  /** Later on the update banner (it returns at the next reminder). */
  later(): Promise<Object>;
  /** Google Play's in-app update, else the Play listing. */
  startUpdate(): Promise<string>;
  /** The phone's date & time settings. */
  openDateSettings(): void;
  addListener(eventName: string): void;
  removeListeners(count: number): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusIntegrity');
