/** The pose assistant's saved choices (validated, so a bad value never sticks). */
import { isPoseDef, type PoseDef } from './library';

export type GridStyle = 'thirds' | 'golden' | 'cross' | 'diagonals';

export interface PoseAssistantSettings {
  /** The level line on the camera. */
  level: boolean;
  /** Free: help placing the face (eyes on the upper third). */
  faceGuide: boolean;
  /** The pose to follow (library id or one of my poses), or null. */
  poseId: string | null;
  /** Do the pose with the other hand. */
  mirror: boolean;
  /** Premium: the step-by-step guide and the score. */
  guided: boolean;
  /** Premium: take the photo by itself once the pose matches. */
  autoCapture: boolean;
  /** Score to reach for automatic capture (50–95). */
  threshold: number;
  /** How long to hold it (ms). */
  holdMs: number;
  /** 0..1: how exact the match must be. */
  strictness: number;
}

export const DEFAULT_POSE_ASSISTANT: PoseAssistantSettings = {
  level: false,
  faceGuide: false,
  poseId: null,
  mirror: false,
  guided: true,
  autoCapture: false,
  threshold: 80,
  holdMs: 1200,
  strictness: 0.5,
};

export const MAX_MY_POSES = 30;

export function isGridStyle(v: unknown): v is GridStyle {
  return v === 'thirds' || v === 'golden' || v === 'cross' || v === 'diagonals';
}

export function isPoseAssistantSettings(v: unknown): v is PoseAssistantSettings {
  if (!v || typeof v !== 'object') return false;
  const s = v as Record<string, unknown>;
  const num = (k: string, lo: number, hi: number) => {
    const v = s[k];
    return typeof v === 'number' && Number.isFinite(v) && v >= lo && v <= hi;
  };
  return (
    typeof s.level === 'boolean' &&
    typeof s.faceGuide === 'boolean' &&
    (s.poseId === null || (typeof s.poseId === 'string' && s.poseId.length > 0 && s.poseId.length <= 80)) &&
    typeof s.mirror === 'boolean' &&
    typeof s.guided === 'boolean' &&
    typeof s.autoCapture === 'boolean' &&
    num('threshold', 50, 95) &&
    num('holdMs', 300, 5000) &&
    num('strictness', 0, 1)
  );
}

export function isMyPoses(v: unknown): v is PoseDef[] {
  return Array.isArray(v) && v.length <= MAX_MY_POSES && v.every(p => isPoseDef(p) && p.category === 'mine');
}
