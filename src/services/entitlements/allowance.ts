import { ENFORCEMENT_ENABLED, type AccessDecision, type EntitlementState } from './EntitlementService';
import { FEATURES, FREE_DAILY_ALLOWANCE, type FeatureKey } from './features';

/** Uses of daily-limited features on one local day. */
export interface DailyUsage {
  day: string;
  counts: Partial<Record<FeatureKey, number>>;
}

export interface AllowanceDecision extends AccessDecision {
  /** Uses left today on the free plan; null when there is no limit. */
  remaining: number | null;
  /** The free plan's daily limit; null when there is none. */
  limit: number | null;
}

/** Local calendar day (YYYY-MM-DD): the allowance resets at the user's midnight. */
export function localDay(now: number): string {
  const d = new Date(now);
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
}

function usedToday(usage: DailyUsage | null, feature: FeatureKey, now: number): number {
  return usage && usage.day === localDay(now) ? (usage.counts[feature] ?? 0) : 0;
}

/**
 * Whether the feature may be used now. Premium (or a free feature): always,
 * without a limit. Free plan: while today's allowance lasts, if the feature
 * has one. Enforcement follows the rest of the plan checks (off until
 * billing ships), but `remaining` is always accurate so the UI can say it.
 */
export function checkAllowance(
  feature: FeatureKey,
  state: EntitlementState,
  usage: DailyUsage | null,
  now: number,
  enforce = ENFORCEMENT_ENABLED,
): AllowanceDecision {
  const requiredPlan = FEATURES[feature].plan;
  if (requiredPlan === 'free' || state.plan === 'premium') {
    return { allowed: true, requiredPlan, wouldBeBlocked: false, remaining: null, limit: null };
  }
  const limit = FREE_DAILY_ALLOWANCE[feature] ?? 0;
  const remaining = Math.max(0, limit - usedToday(usage, feature, now));
  const permitted = remaining > 0;
  return {
    allowed: enforce ? permitted : true,
    requiredPlan,
    wouldBeBlocked: !permitted,
    remaining: limit > 0 ? remaining : 0,
    limit: limit > 0 ? limit : null,
  };
}

/** Usage after one more use today (yesterday's counts are dropped). */
export function recordUse(usage: DailyUsage | null, feature: FeatureKey, now: number): DailyUsage {
  const day = localDay(now);
  const counts = usage && usage.day === day ? { ...usage.counts } : {};
  counts[feature] = (counts[feature] ?? 0) + 1;
  return { day, counts };
}

export function isDailyUsage(v: unknown): v is DailyUsage {
  if (!v || typeof v !== 'object') return false;
  const u = v as Record<string, unknown>;
  if (typeof u.day !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(u.day)) return false;
  if (!u.counts || typeof u.counts !== 'object') return false;
  return Object.values(u.counts as Record<string, unknown>).every(
    n => typeof n === 'number' && Number.isInteger(n) && n >= 0,
  );
}

/** Where the counts live: an internal settings row (not a user setting). */
export const USAGE_KEY = 'usage.daily';

/** Plain-language allowance line for the free plan, or null when unlimited. */
export function allowanceLabel(decision: AllowanceDecision, noun: string): string | null {
  if (decision.remaining === null || decision.limit === null) return null;
  if (decision.remaining === 0) return `You’ve used today’s free ${noun}s`;
  return `${decision.remaining} free ${noun}${decision.remaining === 1 ? '' : 's'} left today`;
}
