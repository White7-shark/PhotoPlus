import { useEffect, useMemo, useRef, useState } from 'react';

import type { CameraService } from '../../../services/camera/CameraService';
import { CameraEvents } from '../../../services/camera/types';
import {
  AUTO_CAPTURE_IDLE,
  faceTip,
  guideStep,
  levelTip,
  stepAutoCapture,
  type FaceTip,
  type GuideStep,
  type LevelReading,
  type LivePose,
} from '../../../services/pose/guidance';
import { canUsePose, findPose, mirrorPose, type PoseDef } from '../../../services/pose/library';
import type { PoseAssistantSettings } from '../../../services/pose/settings';

export interface PoseAssistant {
  /** The chosen pose (mirrored if asked), when this plan can use it. */
  reference: PoseDef | null;
  /** Premium: the live pose is compared with the reference. */
  matching: boolean;
  live: LivePose | null;
  level: LevelReading | null;
  /** False when pose detection cannot run on this phone. */
  available: boolean;
  face: FaceTip;
  levelHint: FaceTip;
  step: GuideStep;
  /** 0..1 toward an automatic photo. */
  autoProgress: number;
}

/**
 * The pose assistant's state for the camera screen: what the native side
 * follows (the person, and the reference pose for Premium), the live pose,
 * the level, and automatic capture.
 */
export function usePoseAssistant({
  camera,
  settings,
  myPoses,
  premium,
  active,
  busy,
  onAutoCapture,
}: {
  camera: CameraService;
  settings: PoseAssistantSettings;
  myPoses: readonly PoseDef[];
  premium: boolean;
  /** The camera is ready for photos (not Glide, not in the background). */
  active: boolean;
  /** A photo is being taken or processed. */
  busy: boolean;
  onAutoCapture: () => void;
}): PoseAssistant {
  const reference = useMemo(() => {
    const p = findPose(settings.poseId, myPoses);
    if (!p || !canUsePose(p, premium)) return null;
    return settings.mirror ? mirrorPose(p) : p;
  }, [settings.poseId, settings.mirror, myPoses, premium]);
  const matching = premium && reference !== null;
  const following = active && (settings.faceGuide || matching);

  // What the native side follows.
  useEffect(() => {
    if (!following) {
      camera.setPoseGuide(null);
      return;
    }
    camera.setPoseGuide({
      reference: matching && reference ? { landmarks: reference.landmarks, aspect: reference.aspect } : undefined,
      framing: true,
      strictness: settings.strictness,
    });
  }, [camera, following, matching, reference, settings.strictness]);
  useEffect(() => () => camera.setPoseGuide(null), [camera]);

  useEffect(() => {
    camera.setLevel(active && settings.level);
  }, [camera, active, settings.level]);
  useEffect(() => () => camera.setLevel(false), [camera]);

  const [live, setLive] = useState<LivePose | null>(null);
  const [level, setLevel] = useState<LevelReading | null>(null);
  const [available, setAvailable] = useState(true);
  useEffect(() => {
    const subs = [
      camera.on(CameraEvents.pose, setLive),
      camera.on(CameraEvents.level, setLevel),
      camera.on(CameraEvents.poseState, s => setAvailable(s.running)),
    ];
    return () => subs.forEach(s => s.remove());
  }, [camera]);

  // Automatic capture: the match held, still, for the hold time.
  const auto = useRef(AUTO_CAPTURE_IDLE);
  const [autoProgress, setAutoProgress] = useState(0);
  const fire = useRef(onAutoCapture);
  useEffect(() => {
    fire.current = onAutoCapture;
  }, [onAutoCapture]);
  useEffect(() => {
    const next = stepAutoCapture(
      auto.current,
      live,
      { enabled: matching && settings.autoCapture && !busy, threshold: settings.threshold, holdMs: settings.holdMs },
      Date.now(),
    );
    auto.current = next.fire ? AUTO_CAPTURE_IDLE : next;
    setAutoProgress(next.fire ? 0 : next.progress);
    if (next.fire) fire.current();
  }, [live, matching, settings.autoCapture, settings.threshold, settings.holdMs, busy]);

  // Readings from before the feature was turned off are not shown.
  const shownLive = following ? live : null;
  const shownLevel = settings.level ? level : null;
  return {
    reference,
    matching,
    live: shownLive,
    level: shownLevel,
    available,
    face: faceTip(shownLive?.person ? shownLive.face : null),
    levelHint: levelTip(level),
    step: guideStep(shownLive, settings.threshold),
    autoProgress,
  };
}
