/**
 * The pose library: reference poses built from a simple skeleton
 * (scripts/pose-library.py → poses.json), and poses taken from the user's own
 * photos. Both are the 33 landmarks of the on-device pose model (normalised
 * image coordinates, the person's own left/right), so the camera compares
 * them the same way.
 */
import data from './poses.json';

export type PoseCategory = 'portrait' | 'standing' | 'sitting' | 'fashion' | 'fun' | 'mine';
export type PoseShot = 'headAndShoulders' | 'halfBody' | 'threeQuarter' | 'fullBody';

/** One landmark: x, y (0..1 of the image), z (relative depth), visibility, presence. */
export type PosePoint = [number, number, number, number, number];

export const POSE_LANDMARKS = 33;

export interface PoseDef {
  id: string;
  name: string;
  category: PoseCategory;
  tier: 'free' | 'premium';
  shot: PoseShot;
  /** Image width / height of the reference. */
  aspect: number;
  /** One body unit (a face-on shoulder width) as a share of the image height: for drawing. */
  unit: number;
  tips: string[];
  landmarks: PosePoint[];
  /** For a pose from the user's photo: where the photo is. */
  photoUri?: string;
}

function isPoint(v: unknown): v is PosePoint {
  return Array.isArray(v) && v.length === 5 && v.every(n => typeof n === 'number' && Number.isFinite(n));
}

export function isPoseDef(v: unknown): v is PoseDef {
  if (!v || typeof v !== 'object') return false;
  const p = v as Record<string, unknown>;
  return (
    typeof p.id === 'string' &&
    p.id.length > 0 &&
    typeof p.name === 'string' &&
    typeof p.category === 'string' &&
    ['portrait', 'standing', 'sitting', 'fashion', 'fun', 'mine'].includes(p.category) &&
    (p.tier === 'free' || p.tier === 'premium') &&
    typeof p.shot === 'string' &&
    ['headAndShoulders', 'halfBody', 'threeQuarter', 'fullBody'].includes(p.shot) &&
    typeof p.aspect === 'number' &&
    p.aspect > 0.1 &&
    p.aspect < 10 &&
    typeof p.unit === 'number' &&
    p.unit > 0 &&
    p.unit < 2 &&
    Array.isArray(p.tips) &&
    p.tips.every(t => typeof t === 'string') &&
    Array.isArray(p.landmarks) &&
    p.landmarks.length === POSE_LANDMARKS &&
    p.landmarks.every(isPoint) &&
    (p.photoUri === undefined || typeof p.photoUri === 'string')
  );
}

export const POSES: readonly PoseDef[] = ((data as { poses: unknown[] }).poses ?? []).filter(isPoseDef);

export const CATEGORY_LABELS: Record<PoseCategory, string> = {
  portrait: 'Portrait',
  standing: 'Standing',
  sitting: 'Sitting',
  fashion: 'Fashion',
  fun: 'Fun',
  mine: 'From my photos',
};

export const SHOT_LABELS: Record<PoseShot, string> = {
  headAndShoulders: 'Head and shoulders',
  halfBody: 'Half body',
  threeQuarter: 'Three-quarter',
  fullBody: 'Full body',
};

export function findPose(id: string | null | undefined, mine: readonly PoseDef[] = []): PoseDef | null {
  if (!id) return null;
  return POSES.find(p => p.id === id) ?? mine.find(p => p.id === id) ?? null;
}

/** Free users see the simple demonstrations; the rest is Premium. */
export function canUsePose(pose: PoseDef, premium: boolean): boolean {
  return premium || pose.tier === 'free';
}

/** Poses grouped by category, in a stable order. */
export function posesByCategory(mine: readonly PoseDef[] = []): { category: PoseCategory; poses: PoseDef[] }[] {
  const order: PoseCategory[] = ['mine', 'portrait', 'standing', 'fashion', 'sitting', 'fun'];
  const all = [...mine, ...POSES];
  return order
    .map(category => ({ category, poses: all.filter(p => p.category === category) }))
    .filter(g => g.poses.length > 0);
}

/** Mirrored pose (the other hand): x flipped, left and right swapped. */
const MIRROR = [0, 4, 5, 6, 1, 2, 3, 8, 7, 10, 9, 12, 11, 14, 13, 16, 15, 18, 17, 20, 19, 22, 21, 24, 23, 26, 25, 28, 27, 30, 29, 32, 31];
export function mirrorPose(pose: PoseDef): PoseDef {
  const landmarks = MIRROR.map(j => {
    const [x, y, z, v, p] = pose.landmarks[j];
    return [1 - x, y, z, v, p] as PosePoint;
  });
  return { ...pose, landmarks };
}

/** A landmark the silhouette may draw: in the picture and present. */
export function shown(p: PosePoint): boolean {
  return p[4] >= 0.5 && p[3] >= 0.35 && p[0] >= -0.02 && p[0] <= 1.02 && p[1] >= -0.02 && p[1] <= 1.02;
}
