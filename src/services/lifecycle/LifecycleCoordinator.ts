import { AppState, DeviceEventEmitter, type AppStateStatus } from 'react-native';

import { NativeEvents } from '../../native';
import { createLogger } from '../logging/logger';

const log = createLogger('lifecycle');

export type MemoryPressure = 'normal' | 'moderate' | 'critical';

export interface LifecycleHandlers {
  onForeground?: () => void;
  onBackground?: () => void;
  onMemoryPressure?: (level: MemoryPressure) => void;
}

interface AppStateLike {
  currentState: AppStateStatus | string | null | undefined;
  addEventListener(type: 'change', listener: (state: AppStateStatus) => void): { remove(): void };
}

interface EmitterLike {
  addListener(event: string, listener: (payload: { level: MemoryPressure }) => void): { remove(): void };
}

/**
 * Central place that reacts to app interruptions:
 *  - background: camera releases (CameraX lifecycle), long jobs can be
 *    cancelled by the subscriber, state is already persisted;
 *  - foreground: refresh volatile data (storage, permissions may have changed
 *    in system settings);
 *  - memory pressure: drop caches before the OS kills the process.
 */
export function startLifecycleCoordinator(
  handlers: LifecycleHandlers,
  appState: AppStateLike = AppState,
  emitter: EmitterLike = DeviceEventEmitter,
): () => void {
  let last = appState.currentState;
  const appSub = appState.addEventListener('change', next => {
    const wasActive = last === 'active';
    last = next;
    if (next === 'active' && !wasActive) {
      log.debug('App returned to foreground');
      handlers.onForeground?.();
    } else if (next === 'background') {
      log.debug('App moved to background');
      handlers.onBackground?.();
    }
  });
  const memSub = emitter.addListener(NativeEvents.memoryPressure, payload => {
    const level = payload?.level ?? 'normal';
    if (level !== 'normal') {
      log.warn(`Memory pressure: ${level}`);
    }
    handlers.onMemoryPressure?.(level);
  });
  return () => {
    appSub.remove();
    memSub.remove();
  };
}
