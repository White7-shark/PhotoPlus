import { useEffect, useState } from 'react';
import { AccessibilityInfo } from 'react-native';

import { useAppStore } from '../state/store';

/** Honour the in-app setting first, then the OS "remove animations" flag. */
export function useReducedMotion(): boolean {
  const preference = useAppStore(s => s.settings.reduceMotion);
  const [system, setSystem] = useState(false);
  useEffect(() => {
    let mounted = true;
    AccessibilityInfo.isReduceMotionEnabled()
      .then(v => mounted && setSystem(v))
      .catch(() => undefined);
    const sub = AccessibilityInfo.addEventListener('reduceMotionChanged', setSystem);
    return () => {
      mounted = false;
      sub.remove();
    };
  }, []);
  if (preference === 'on') {
    return true;
  }
  if (preference === 'off') {
    return false;
  }
  return system;
}
