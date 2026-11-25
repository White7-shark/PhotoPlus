import { createNavigationContainerRef } from '@react-navigation/native';

import type { RootStackParamList } from './types';

/** For places outside a screen (the Premium sheet) that open a screen. */
export const navigationRef = createNavigationContainerRef<RootStackParamList>();

export function openScreen(name: 'Premium' | 'Rewards') {
  if (navigationRef.isReady()) navigationRef.navigate(name);
}
