/**
 * The app's integrity (IntegrityModule): the phone's time must be right,
 * the app must go online at least once a week, and new versions must be
 * installed within 7 days. The device decides; this reads and asks.
 */
import { DeviceEventEmitter } from 'react-native';

import { nativeModules, NativeEvents, type IntegritySpec } from '../../native';

export type BlockReason = 'time' | 'offline' | 'update';

export interface IntegrityState {
  blocked: boolean;
  reason: BlockReason | null;
  /** How far the phone's clock is off (minutes; + ahead), when known. */
  skewMinutes: number | null;
  offlineDays: number;
  maxOfflineDays: number;
  lastOnline: number;
  /** The true time. */
  now: number;
  /** The last online check reached the internet. */
  online: boolean;
  update: { pending: boolean; required: boolean; banner: boolean; deadline: number };
}

export const OK_STATE: IntegrityState = {
  blocked: false,
  reason: null,
  skewMinutes: null,
  offlineDays: 0,
  maxOfflineDays: 7,
  lastOnline: 0,
  now: 0,
  online: true,
  update: { pending: false, required: false, banner: false, deadline: 0 },
};

export function isIntegrityState(v: unknown): v is IntegrityState {
  if (!v || typeof v !== 'object') return false;
  const s = v as Record<string, unknown>;
  const u = s.update as Record<string, unknown> | undefined;
  return (
    typeof s.blocked === 'boolean' &&
    (s.reason === null || s.reason === 'time' || s.reason === 'offline' || s.reason === 'update') &&
    typeof s.offlineDays === 'number' &&
    !!u &&
    typeof u.banner === 'boolean' &&
    typeof u.deadline === 'number'
  );
}

/** A state that can't be read is treated as fine (never lock someone out by a fault in the bridge). */
const read = (v: unknown): IntegrityState => (isIntegrityState(v) ? v : OK_STATE);

export class IntegrityService {
  constructor(private readonly native: IntegritySpec | null | undefined = nativeModules.integrity) {}

  get available() {
    return this.native != null;
  }

  async state(): Promise<IntegrityState> {
    if (!this.native) return OK_STATE;
    try {
      return read(await this.native.getState());
    } catch {
      return OK_STATE;
    }
  }

  /** An online check (time and updates). */
  async check(force: boolean): Promise<IntegrityState> {
    if (!this.native) return OK_STATE;
    return read(await this.native.check(force));
  }

  async later(): Promise<IntegrityState> {
    if (!this.native) return OK_STATE;
    return read(await this.native.later());
  }

  async startUpdate(): Promise<string> {
    if (!this.native) return 'none';
    return this.native.startUpdate();
  }

  openDateSettings() {
    this.native?.openDateSettings();
  }

  onChange(listener: (s: IntegrityState) => void): () => void {
    const sub = DeviceEventEmitter.addListener(NativeEvents.integrityChanged, (e: unknown) => {
      if (isIntegrityState(e)) listener(e);
    });
    return () => sub.remove();
  }
}

let shared: IntegrityService | null = null;
export function getIntegrityService(): IntegrityService {
  shared ??= new IntegrityService();
  return shared;
}

export const formatDay = (ms: number) =>
  new Date(ms).toLocaleDateString(undefined, { weekday: 'short', day: 'numeric', month: 'short' });

/** How far off the clock is, in words. */
export function skewWords(minutes: number | null): string {
  if (minutes === null) return 'set to a different date or time';
  const abs = Math.abs(minutes);
  const amount =
    abs >= 2 * 24 * 60
      ? `${Math.round(abs / (24 * 60))} days`
      : abs >= 120
        ? `${Math.round(abs / 60)} hours`
        : `${Math.round(abs)} minutes`;
  return `${amount} ${minutes > 0 ? 'ahead' : 'behind'}`;
}
