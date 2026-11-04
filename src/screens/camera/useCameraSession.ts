import { useCallback, useEffect, useRef, useState } from 'react';

import { createLogger } from '../../services/logging/logger';
import {
  CameraEvents,
  getCameraService,
  type CameraCapabilities,
  type CameraEventPayloads,
  type CameraStateName,
} from '../../services/camera/CameraService';

const log = createLogger('camera');

export type Metadata = CameraEventPayloads[typeof CameraEvents.metadata];

export interface CameraSessionState {
  capabilities: CameraCapabilities | null;
  zoom: number;
  torch: boolean;
  /** Clockwise degrees to counter-rotate icons so they stay upright. */
  orientation: 0 | 90 | 180 | 270;
  cameraState: CameraStateName | null;
  recoveringAttempt: number | null;
  metadata: Metadata | null;
  sharpness: number | null;
  focus: { x: number; y: number; status: 'focusing' | 'locked' | 'failed'; key: number } | null;
}

/**
 * Subscribes to the camera's live event stream for as long as the screen
 * is mounted, and refreshes capabilities whenever a session (re)binds:
 * switching lens, changing aspect ratio or recovering from an error can
 * all change what the camera supports.
 */
export function useCameraSession() {
  const [state, setState] = useState<CameraSessionState>({
    capabilities: null,
    zoom: 1,
    torch: false,
    orientation: 0,
    cameraState: null,
    recoveringAttempt: null,
    metadata: null,
    sharpness: null,
    focus: null,
  });
  const focusKey = useRef(0);

  useEffect(() => {
    const camera = getCameraService();
    const subs = [
      camera.on(CameraEvents.zoom, z => setState(s => ({ ...s, zoom: z.ratio }))),
      camera.on(CameraEvents.torch, t => setState(s => ({ ...s, torch: t.on }))),
      camera.on(CameraEvents.orientation, o => setState(s => ({ ...s, orientation: o.degrees }))),
      camera.on(CameraEvents.metadata, m => setState(s => ({ ...s, metadata: m }))),
      camera.on(CameraEvents.peaking, p => setState(s => ({ ...s, sharpness: p.sharpness }))),
      camera.on(CameraEvents.state, e =>
        setState(s => ({
          ...s,
          cameraState: e.state,
          recoveringAttempt:
            e.state === 'recovering' ? (e.attempt ?? 1) : e.state === 'open' ? null : s.recoveringAttempt,
        })),
      ),
      camera.on(CameraEvents.focus, f =>
        setState(s =>
          s.focus && Math.abs(s.focus.x - f.x) < 1e-3 && Math.abs(s.focus.y - f.y) < 1e-3
            ? { ...s, focus: { ...s.focus, status: f.success ? 'locked' : 'failed' } }
            : s,
        ),
      ),
    ];
    return () => subs.forEach(sub => sub.remove());
  }, []);

  const refreshCapabilities = useCallback(async () => {
    try {
      const caps = await getCameraService().getCapabilities();
      setState(s => ({ ...s, capabilities: caps }));
      return caps;
    } catch (e) {
      log.warn('capabilities unavailable', e);
      return null;
    }
  }, []);

  /** Shows the focus ring immediately; the native focus event resolves it. */
  const markFocus = useCallback((x: number, y: number) => {
    focusKey.current += 1;
    setState(s => ({ ...s, focus: { x, y, status: 'focusing', key: focusKey.current } }));
  }, []);

  const clearFocus = useCallback(() => setState(s => ({ ...s, focus: null })), []);

  return { ...state, refreshCapabilities, markFocus, clearFocus };
}
