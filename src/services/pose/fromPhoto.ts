/**
 * A pose from the user's own photo (Premium): the photo is copied into the
 * app, the pose is read on the device (nothing leaves the phone), and it
 * joins "From my photos" like any library pose.
 */
import type { PosePhotoResult } from '../camera/types';
import { POSE_LANDMARKS, isPoseDef, type PoseDef, type PosePoint, type PoseShot } from './library';

export type FromPhotoOutcome = { kind: 'pose'; pose: PoseDef } | { kind: 'cancelled' } | { kind: 'noPerson' };

const SHOTS: PoseShot[] = ['headAndShoulders', 'halfBody', 'threeQuarter', 'fullBody'];

export function poseFromResult(result: PosePhotoResult, photoUri: string, index: number, now: number): PoseDef | null {
  if (!result.valid || !result.landmarks || result.landmarks.length !== POSE_LANDMARKS * 5) return null;
  const v = result.landmarks;
  const landmarks: PosePoint[] = [];
  for (let i = 0; i < POSE_LANDMARKS; i++) {
    const p = v.slice(i * 5, i * 5 + 5).map(n => Math.round(n * 1e4) / 1e4);
    landmarks.push([p[0], p[1], p[2], p[3], p[4]]);
  }
  // A face alone is a portrait too (head and shoulders framing).
  const shot = SHOTS.includes(result.shot as PoseShot) ? (result.shot as PoseShot) : 'headAndShoulders';
  const pose: PoseDef = {
    id: `mine-${now.toString(36)}`,
    name: `My pose ${index}`,
    category: 'mine',
    tier: 'premium',
    shot,
    aspect: result.aspect,
    unit: result.unit && result.unit > 0 ? Math.round(result.unit * 1e4) / 1e4 : 0.15,
    tips: ['Match the pose from your photo.', 'Line yourself up with the outline, then follow the arrows.'],
    landmarks,
    photoUri,
  };
  return isPoseDef(pose) ? pose : null;
}

export async function poseFromPhoto(
  pick: () => Promise<string | null>,
  analyze: (uri: string) => Promise<PosePhotoResult>,
  index: number,
  now: number = Date.now(),
): Promise<FromPhotoOutcome> {
  const path = await pick();
  if (!path) return { kind: 'cancelled' };
  const pose = poseFromResult(await analyze(path), path, index, now);
  return pose ? { kind: 'pose', pose } : { kind: 'noPerson' };
}
