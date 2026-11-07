import { useCallback, useEffect, useRef, useState } from 'react';

import { errorCode } from '../../native';
import type { CameraService } from '../../services/camera/CameraService';
import {
  GLIDE_MAX_MS,
  GLIDE_MIN_MS,
  glideFileName,
  glideRequest,
  type GlideClip,
  type GlideSpotlight,
  type GlideStage,
} from '../../services/camera/glide';
import type { BeautyRecipe } from '../../services/beauty/recipe';
import type { WatermarkSpec } from '../../services/edit/watermark';
import { createLogger } from '../../services/logging/logger';
import { getPermissionService } from '../../services/permissions/PermissionService';
import { getProcessingService } from '../../services/processing/ImageProcessingService';
import { getStorageService, joinPath } from '../../services/storage/StorageService';

const log = createLogger('glide');

export type { GlideClip };

export type GlidePhase = 'idle' | 'recording' | 'crafting';

export interface GlideSettings {
  spotlight: GlideSpotlight;
  /** The beauty preset (with Spotlight's blur when on), resolved; null = a plain steady clip. */
  recipe: BeautyRecipe | null;
  watermark: WatermarkSpec;
}

/**
 * The Glide flow in the camera: record (up to 7 s), then craft the clip
 * (steady, clean, sharp, plus Spotlight and Glow), then save it to the
 * gallery. One clip at a time; recording stops when the camera goes away.
 */
export function useGlide({
  camera,
  enabled,
  active,
  settings,
  onDone,
  onError,
}: {
  camera: CameraService;
  /** Glide mode is on (asks for the microphone once). */
  enabled: boolean;
  /** The camera is running (recording stops when it is not). */
  active: boolean;
  settings: GlideSettings;
  onDone: (clip: GlideClip) => void;
  /** An error code for friendlyMessage (never called for a cancel). */
  onError: (code: string) => void;
}) {
  const [phase, setPhase] = useState<GlidePhase>('idle');
  const [elapsedMs, setElapsedMs] = useState(0);
  const [stage, setStage] = useState<GlideStage | null>(null);
  const [fraction, setFraction] = useState(0);
  const [audio, setAudio] = useState(false);
  const busy = useRef(false);
  const ticker = useRef<ReturnType<typeof setInterval> | null>(null);
  const abort = useRef<AbortController | null>(null);
  // Latest settings and callbacks, read when a clip finishes recording.
  const latest = useRef({ settings, onDone, onError });
  useEffect(() => {
    latest.current = { settings, onDone, onError };
  });

  // Sound needs the microphone: asked once when Glide is chosen; without it
  // clips are silent (never blocked).
  useEffect(() => {
    if (!enabled) {
      return;
    }
    let cancelled = false;
    getPermissionService()
      .ensure('microphone')
      .then(s => !cancelled && setAudio(s === 'granted'))
      .catch(() => !cancelled && setAudio(false));
    return () => {
      cancelled = true;
    };
  }, [enabled]);

  const stopTicker = () => {
    if (ticker.current) {
      clearInterval(ticker.current);
      ticker.current = null;
    }
  };

  const craft = useCallback(async (rawPath: string) => {
    const { settings: s } = latest.current;
    const storage = getStorageService();
    const paths = await storage.getPaths();
    const outputPath = joinPath(paths.exports, glideFileName(new Date()));
    const controller = new AbortController();
    abort.current = controller;
    try {
      const result = await getProcessingService().glide(
        glideRequest({
          inputPath: rawPath,
          outputPath,
          spotlight: s.spotlight,
          recipe: s.recipe,
          watermark: s.watermark,
        }),
        { signal: controller.signal, onProgress: setFraction, onStage: setStage },
      );
      // Clips are kept in the gallery (like any camera's videos); a failed
      // save still leaves the clip to share from the preview.
      const galleryUri = await storage.saveVideoToGallery(result.outputPath).catch(e => {
        log.warn('gallery save failed', e);
        return null;
      });
      latest.current.onDone({ ...result, rawPath, galleryUri });
    } finally {
      abort.current = null;
    }
  }, []);

  const start = useCallback(async () => {
    if (busy.current) {
      return;
    }
    busy.current = true;
    setPhase('recording');
    setElapsedMs(0);
    setStage(null);
    setFraction(0);
    const began = Date.now();
    ticker.current = setInterval(() => setElapsedMs(Date.now() - began), 100);
    try {
      const clip = await camera.startVideo({ audio, maxMs: GLIDE_MAX_MS });
      stopTicker();
      if (clip.durationMs < GLIDE_MIN_MS) {
        getStorageService()
          .deleteFile(clip.path)
          .catch(() => undefined);
        latest.current.onError('CLIP_TOO_SHORT');
        return;
      }
      setPhase('crafting');
      await craft(clip.path);
    } catch (e) {
      const code = errorCode(e) ?? 'RECORDING_FAILED';
      if (code !== 'CANCELLED') {
        log.error('glide failed', e);
        latest.current.onError(code);
      }
    } finally {
      stopTicker();
      busy.current = false;
      setPhase('idle');
      setStage(null);
    }
  }, [camera, audio, craft]);

  const stop = useCallback(() => camera.stopVideo(), [camera]);
  const cancel = useCallback(() => abort.current?.abort(), []);

  // Leaving the camera ends a recording (the clip so far is still crafted).
  useEffect(() => {
    if (!active && phase === 'recording') {
      camera.stopVideo();
    }
  }, [active, phase, camera]);

  useEffect(
    () => () => {
      stopTicker();
    },
    [],
  );

  return { phase, elapsedMs, stage, fraction, audio, start, stop, cancel };
}
