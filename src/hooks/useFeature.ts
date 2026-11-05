import { checkFeature, type AccessDecision } from '../services/entitlements/EntitlementService';
import type { FeatureKey } from '../services/entitlements/features';
import { useAppStore } from '../state/store';

/** Whether the current plan may use a feature (enforcement is off until billing ships). */
export function useFeature(feature: FeatureKey): AccessDecision {
  const state = useAppStore(s => s.entitlements);
  return checkFeature(feature, state);
}

export function useFeatureCheck(): (feature: FeatureKey) => AccessDecision {
  const state = useAppStore(s => s.entitlements);
  return feature => checkFeature(feature, state);
}
