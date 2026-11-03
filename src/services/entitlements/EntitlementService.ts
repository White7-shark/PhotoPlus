import { FEATURES, type FeatureKey, type Plan } from './features';

/**
 * Entitlement decisions. Phase 1 builds the infrastructure only: purchases
 * are not wired to Google Play Billing yet and enforcement is OFF, so every
 * feature is usable during development. Call sites already ask the right
 * question (`check`) so turning enforcement on later is a one-line change
 * plus a real `EntitlementState` source.
 */

export interface EntitlementState {
  plan: Plan;
  /** Pack ids purchased separately (retained even if Premium lapses). */
  ownedPackIds: string[];
  /** Pack ids included with the Premium subscription. */
  includedPackIds: string[];
}

export const ENFORCEMENT_ENABLED = false;

export interface AccessDecision {
  allowed: boolean;
  requiredPlan: Plan;
  /** True when the user would be blocked if enforcement were on. */
  wouldBeBlocked: boolean;
}

export function checkFeature(feature: FeatureKey, state: EntitlementState, enforce = ENFORCEMENT_ENABLED): AccessDecision {
  const requiredPlan = FEATURES[feature].plan;
  const permitted = requiredPlan === 'free' || state.plan === 'premium';
  return { allowed: enforce ? permitted : true, requiredPlan, wouldBeBlocked: !permitted };
}

export type PackAccess = 'apply' | 'locked-subscription-required' | 'locked-purchase-required';

/**
 * Pack rules from the spec:
 *  - Applying any pack requires active Premium.
 *  - Included packs are usable while Premium is active.
 *  - Separately sold packs need a purchase; ownership survives a lapse, but
 *    applying still requires Premium.
 */
export function packAccess(packId: string, state: EntitlementState, enforce = ENFORCEMENT_ENABLED): PackAccess {
  const decision = ((): PackAccess => {
    if (state.plan !== 'premium') {
      return 'locked-subscription-required';
    }
    if (state.includedPackIds.includes(packId) || state.ownedPackIds.includes(packId)) {
      return 'apply';
    }
    return 'locked-purchase-required';
  })();
  return enforce ? decision : 'apply';
}

export const FREE_STATE: EntitlementState = { plan: 'free', ownedPackIds: [], includedPackIds: [] };
