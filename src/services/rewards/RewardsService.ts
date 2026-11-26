/**
 * Rewards (free plan): the bridge to the device's rewards ledger, where
 * points, check-ins, tasks, redemptions and the share campaign are kept, and
 * to rewarded ads for points. Nothing here can grant points or Premium: the
 * device decides and answers with the new state.
 */
import { errorCode, errorMessage, nativeModules, type AdsSpec, type RewardsSpec } from '../../native';
import { t as tx } from '../../i18n';

export type ActivityEvent = 'photo' | 'edit' | 'studio' | 'product' | 'film' | 'project' | 'glide';
export type RewardItem = 'tool' | 'premium-1' | 'premium-3' | 'premium-7';
export type RewardTool = 'sceneSwap' | 'relight' | 'productPro' | 'exportHd' | 'studioLight' | 'hair' | 'batch';

export interface TaskState {
  id: string;
  points: number;
  daily: boolean;
  goal: number;
  progress: number;
  claimed: boolean;
}

export interface RewardsState {
  subscribed: boolean;
  points: number;
  earned: number;
  now: number;
  checkIn: { day: number; done: boolean; schedule: number[] };
  ads: { today: number; perDay: number; points: number; wait: number };
  tasks: TaskState[];
  shares: {
    count: number;
    goal: number;
    days: number;
    lockHours: number;
    ready: boolean;
    opensAt: number;
    locks: Record<string, number>;
    /** Shares made to each app (all time). */
    counts: Record<string, number>;
  };
  premiumUntil: number;
  tools: Partial<Record<RewardTool, number>>;
  daysUsed: number;
  capDays: number;
  store: { id: RewardItem; cost: number; days: number; hours: number }[];
}

/** One app's share screen (an app can have several, such as a chat and a status). */
export interface ShareTarget {
  pkg: string;
  activity: string;
  /** The share screen's name ("Status", "Chats"…). */
  label: string;
  /** The app's name. */
  app: string;
  /** A PNG of its icon, or "". */
  icon: string;
}

export function isShareTarget(v: unknown): v is ShareTarget {
  if (!v || typeof v !== 'object') return false;
  const t = v as Record<string, unknown>;
  return (
    typeof t.pkg === 'string' &&
    typeof t.activity === 'string' &&
    typeof t.label === 'string' &&
    typeof t.app === 'string' &&
    typeof t.icon === 'string'
  );
}

export class RewardsError extends Error {
  constructor(
    message: string,
    readonly code: string,
  ) {
    super(message);
    this.name = 'RewardsError';
  }
}

export function isRewardsState(v: unknown): v is RewardsState {
  if (!v || typeof v !== 'object') return false;
  const s = v as Record<string, unknown>;
  return (
    typeof s.points === 'number' &&
    !!s.checkIn &&
    !!s.ads &&
    Array.isArray(s.tasks) &&
    !!s.shares &&
    Array.isArray(s.store) &&
    typeof s.premiumUntil === 'number'
  );
}

export const TASK_TEXT: Record<string, { title: string; hint: string }> = {
  'first-photo': { title: tx('rewards_take_your_first_photo'), hint: tx('rewards_with_the_photo_plus_camera') },
  'first-edit': { title: tx('rewards_edit_and_save_a_photo'), hint: tx('rewards_any_edit_in_the_editor') },
  'first-product': { title: tx('rewards_make_a_product_shot'), hint: tx('rewards_in_product_studio') },
  'first-film': { title: tx('rewards_make_a_film_strip_or'), hint: tx('rewards_in_film_collage') },
  'first-project': { title: tx('rewards_start_a_project'), hint: tx('rewards_organise_a_shoot_in_projects') },
  'first-glide': { title: tx('rewards_record_a_glide_video'), hint: tx('rewards_glide_in_the_camera') },
  'daily-photos': { title: tx('rewards_take_3_photos_today'), hint: tx('rewards_any_mode_in_the_camera') },
  'daily-edit': { title: tx('rewards_save_an_edit_today'), hint: tx('rewards_the_editor_or_any_studio') },
  'daily-studio': {
    title: tx('rewards_create_in_the_photoshoot_studio'),
    hint: tx('rewards_scene_swap_relight_film_product'),
  },
};

export const TOOL_TEXT: Record<RewardTool, string> = {
  sceneSwap: 'Scene Swap',
  relight: 'Relight',
  productPro: 'Pro product setups',
  exportHd: 'HD & 4K downloads',
  studioLight: 'Studio light & film looks',
  hair: 'Hair colours & styles',
  batch: 'Batch editing',
};

export class RewardsService {
  constructor(
    private readonly native: RewardsSpec | null | undefined = nativeModules.rewards,
    private readonly ads: AdsSpec | null | undefined = nativeModules.ads,
  ) {}

  get available() {
    return this.native != null;
  }

  private async call(run: () => Promise<unknown>): Promise<Record<string, unknown>> {
    try {
      return (await run()) as Record<string, unknown>;
    } catch (error) {
      throw new RewardsError(errorMessage(error), errorCode(error) ?? 'INTERNAL');
    }
  }

  private need(): RewardsSpec {
    if (!this.native) throw new RewardsError('Rewards aren’t available on this device', 'UNAVAILABLE');
    return this.native;
  }

  private static stateOf(r: Record<string, unknown>): RewardsState {
    const s = 'state' in r ? r.state : r;
    if (!isRewardsState(s)) throw new RewardsError('Rewards returned something unexpected', 'INTERNAL');
    return s;
  }

  async state(): Promise<RewardsState> {
    const n = this.need();
    return RewardsService.stateOf(await this.call(() => n.getState()));
  }

  async checkIn(): Promise<{ points: number; state: RewardsState }> {
    const n = this.need();
    const r = await this.call(() => n.checkIn());
    return { points: Number(r.points) || 0, state: RewardsService.stateOf(r) };
  }

  async claimTask(id: string): Promise<{ points: number; state: RewardsState }> {
    const n = this.need();
    const r = await this.call(() => n.claimTask(id));
    return { points: Number(r.points) || 0, state: RewardsService.stateOf(r) };
  }

  async redeem(item: RewardItem, tool?: RewardTool): Promise<{ until: number; state: RewardsState }> {
    const n = this.need();
    const r = await this.call(() => n.redeem(item, tool ?? ''));
    return { until: Number(r.until) || 0, state: RewardsService.stateOf(r) };
  }

  async claimSharePass(): Promise<{ until: number; state: RewardsState }> {
    const n = this.need();
    const r = await this.call(() => n.claimSharePass());
    return { until: Number(r.until) || 0, state: RewardsService.stateOf(r) };
  }

  /** One rewarded ad for points (counted only when watched to the end). */
  async watchAd(): Promise<{ rewarded: boolean; points: number; state: RewardsState }> {
    const ads = this.ads;
    if (!ads) throw new RewardsError('Ads aren’t available on this device', 'ADS_UNAVAILABLE');
    try {
      await ads.initialize();
    } catch {
      // Consent could not be checked: the ad call says whether ads can be shown.
    }
    const r = await this.call(() => ads.showRewardedAdForPoints());
    return { rewarded: r.rewarded === true, points: Number(r.points) || 0, state: RewardsService.stateOf(r) };
  }

  /** Every app that takes a picture (the in-app share sheet). */
  async shareTargets(): Promise<ShareTarget[]> {
    if (!this.native) return [];
    try {
      const list = (await this.native.shareTargets()) as unknown[];
      return list.filter(isShareTarget);
    } catch {
      return [];
    }
  }

  async share(
    target: ShareTarget,
    asset: string,
    text: string,
  ): Promise<{ counted: boolean; target: string; state: RewardsState }> {
    const n = this.need();
    const r = await this.call(() => n.share(target.pkg, target.activity, asset, text));
    return {
      counted: r.counted === true,
      target: typeof r.target === 'string' ? r.target : '',
      state: RewardsService.stateOf(r),
    };
  }
}

let shared: RewardsService | null = null;
export function getRewardsService(): RewardsService {
  shared ??= new RewardsService();
  return shared;
}

/** Counts something the person did toward tasks (never fails, never waits). */
export function recordActivity(...events: ActivityEvent[]) {
  const n = nativeModules.rewards;
  if (!n) return;
  for (const e of events) {
    try {
      n.recordActivity(e);
    } catch {
      // Rewards are a bonus: never in the way of the photo.
    }
  }
}
