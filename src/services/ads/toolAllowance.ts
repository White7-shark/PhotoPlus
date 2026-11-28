/**
 * A tool's free-plan day, as the device counts it (AdAllowance.kt): uses left,
 * uses unlocked by ads and not yet made, and ads owed from offline unlocks.
 *
 * Offline, no ad can load: the device still unlocks the use but the ad is
 * owed. With `maxOwed` owed the tool is frozen until the phone is online and
 * every owed ad has been watched; while any are owed, each ad watched pays one
 * off before it unlocks a new use.
 */
import { t } from '../../i18n';

export interface ToolAllowance {
  /** Uses left today; -1 unlimited (Premium). */
  remaining: number;
  limit: number;
  /** Uses unlocked by ads and not made yet. */
  credits: number;
  /** The next use needs an ad first. */
  needsAd: boolean;
  /** Ads owed from uses unlocked offline. */
  owed: number;
  maxOwed: number;
  /** Owed reached maxOwed: the tool waits until they're watched online. */
  frozen: boolean;
}

/** What an ad (or an offline unlock) did. */
export interface ToolUnlock {
  /** A use was unlocked or an owed ad paid off. */
  rewarded: boolean;
  /** Offline: unlocked on credit, the ad owed. */
  offline: boolean;
  allowance: ToolAllowance;
}

const num = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);

export const MAX_OWED = 3;

export function unknownAllowance(limit: number): ToolAllowance {
  return { remaining: limit, limit, credits: 0, needsAd: true, owed: 0, maxOwed: MAX_OWED, frozen: false };
}

/** The device's answer, checked; anything missing comes from `fallback`. */
export function readToolAllowance(v: unknown, fallback: ToolAllowance): ToolAllowance {
  const a = v as Partial<ToolAllowance> | null;
  if (!a || !num(a.remaining)) return fallback;
  const credits = num(a.credits) ? Math.max(0, a.credits) : 0;
  const maxOwed = num(a.maxOwed) && a.maxOwed > 0 ? a.maxOwed : MAX_OWED;
  const owed = a.remaining === -1 ? 0 : num(a.owed) ? Math.min(maxOwed, Math.max(0, a.owed)) : 0;
  return {
    remaining: a.remaining,
    limit: num(a.limit) ? a.limit : fallback.limit,
    credits,
    needsAd:
      a.remaining !== -1 &&
      (typeof a.needsAd === 'boolean' ? a.needsAd : (a.remaining > 0 || owed > 0) && credits === 0),
    owed,
    maxOwed,
    frozen: a.remaining !== -1 && (a.frozen === true || owed >= maxOwed),
  };
}

export function readToolUnlock(v: unknown, fallback: ToolAllowance): ToolUnlock {
  const r = v as { rewarded?: unknown; offline?: unknown; allowance?: unknown } | null;
  return {
    rewarded: r?.rewarded === true,
    offline: r?.offline === true,
    allowance: readToolAllowance(r?.allowance, fallback),
  };
}

/** Whether a finished one can be made now without another ad. */
export const unlockedNow = (a: ToolAllowance) => a.remaining === -1 || (a.credits > 0 && a.remaining > 0);

/** A line about the owed ads (null when none). */
export function owedLine(a: ToolAllowance): string | null {
  if (a.remaining === -1 || a.owed <= 0) return null;
  return a.frozen ? t('ads_owed_frozen', { n: a.owed }) : t('ads_owed_some', { n: a.owed, max: a.maxOwed });
}

/** What to say after an ad or an offline unlock. */
export function unlockNotice(u: ToolUnlock): string | null {
  if (u.offline) return t('ads_offline_unlocked', { n: u.allowance.owed, max: u.allowance.maxOwed });
  if (u.rewarded && !unlockedNow(u.allowance) && u.allowance.owed > 0)
    return t('ads_owed_paid', { n: u.allowance.owed });
  return null;
}
