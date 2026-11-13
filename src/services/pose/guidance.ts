/**
 * The pose assistant's words and decisions (pure, unit-tested): what to tell
 * the person, where a part should go on screen, whether the photo is level,
 * where the face sits, and when automatic capture fires.
 *
 * Sides. The engine compares in display space and names parts with the
 * pose model's labels, in the image the user sees. On a mirrored selfie
 * preview those labels are the mirror image's: the "left hand" is the
 * user's real right hand. Image directions ("move left") are the screen's:
 * in a mirrored selfie that is the user's own left; for someone in front of
 * the rear camera, the screen's left is their right.
 */
import type { PoseDef, PosePoint } from './library';

export type PosePart =
  | 'body' | 'head' | 'leftHand' | 'rightHand' | 'leftElbow' | 'rightElbow' | 'leftFoot' | 'rightFoot'
  | 'leftKnee' | 'rightKnee' | 'shoulders' | 'camera';
export type PoseAction =
  | 'noPerson' | 'show' | 'closer' | 'back' | 'moveLeft' | 'moveRight' | 'cameraUp' | 'cameraDown'
  | 'turnLeft' | 'turnRight' | 'up' | 'down' | 'in' | 'out' | 'tiltLeft' | 'tiltRight' | 'lookLeft'
  | 'lookRight' | 'leanLeft' | 'leanRight' | 'straightenUp' | 'holdStill' | 'perfect';

export interface PoseAdvice {
  part: PosePart;
  action: PoseAction;
  amount: number;
}

export interface PoseMatch {
  score: number;
  coverage: number;
  pose: number;
  framing: number;
  offset: [number, number];
  size: number;
  bones: number[];
  hands: [number, number];
  advice: PoseAdvice[];
}

/** One PhotoPlusPose event. */
export interface LivePose {
  person: boolean;
  mirrored: boolean;
  stillness: number;
  shot: string | null;
  anchor: [number, number];
  unit: number;
  headTilt: number;
  /** The nose, when seen (preview fractions). */
  face: [number, number] | null;
  /** 33 × [x, y, reliability] in preview fractions. */
  points: [number, number, number][];
  match: PoseMatch | null;
}

const PART_WORDS: Record<string, string> = {
  Hand: 'hand',
  Elbow: 'elbow',
  Foot: 'foot',
  Knee: 'knee',
};

/** "your right hand" for a part label, as the user knows their own sides. */
export function partWords(part: PosePart, mirrored: boolean): string {
  if (part === 'body') return 'yourself';
  if (part === 'head') return 'your head';
  if (part === 'shoulders') return 'your shoulders';
  if (part === 'camera') return 'the camera';
  const m = /^(left|right)(Hand|Elbow|Foot|Knee)$/.exec(part);
  if (!m) return 'yourself';
  const labelSide = m[1] as 'left' | 'right';
  // On a mirrored preview the label belongs to the mirror image.
  const side = mirrored ? (labelSide === 'left' ? 'right' : 'left') : labelSide;
  return `your ${side} ${PART_WORDS[m[2]]}`;
}

/** The user's own side for an image direction ("the screen's left"). */
function ownSide(imageSide: 'left' | 'right', mirrored: boolean): 'left' | 'right' {
  if (mirrored) return imageSide; // a mirror: your left is on the left
  return imageSide === 'left' ? 'right' : 'left'; // facing the photographer
}

const SMALL = 0.12;

/** One piece of advice in plain words. */
export function adviceText(a: PoseAdvice, mirrored: boolean): string {
  const little = a.amount < SMALL ? ' a little' : '';
  const who = partWords(a.part, mirrored);
  const cap = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);
  switch (a.action) {
    case 'noPerson':
      return 'Step into the frame';
    case 'show':
      return a.part === 'body' ? 'Step back so more of you is in the picture' : `Show ${who}`;
    case 'closer':
      return a.amount < 0.25 ? 'Come a little closer' : 'Come closer';
    case 'back':
      return a.amount < 0.25 ? 'Step back a little' : 'Step back';
    case 'moveLeft':
      return `Move${a.amount < 0.12 ? ' a little' : ''} to your ${ownSide('left', mirrored)}`;
    case 'moveRight':
      return `Move${a.amount < 0.12 ? ' a little' : ''} to your ${ownSide('right', mirrored)}`;
    case 'cameraUp':
      return 'Aim the camera a little higher';
    case 'cameraDown':
      return 'Aim the camera a little lower';
    case 'turnLeft':
      return `Turn${a.amount < 0.6 ? ' slightly' : ''} to your ${ownSide('left', mirrored)}`;
    case 'turnRight':
      return `Turn${a.amount < 0.6 ? ' slightly' : ''} to your ${ownSide('right', mirrored)}`;
    case 'up':
      return `Raise ${who}${a.amount < 0.5 ? ' a little' : ''}`;
    case 'down':
      return `Lower ${who}${a.amount < 0.5 ? ' a little' : ''}`;
    case 'in':
      return `Bring ${who} in${a.amount < 0.5 ? ' a little' : ''}`;
    case 'out':
      return `Move ${who} out${a.amount < 0.5 ? ' a little' : ''}`;
    case 'tiltLeft':
      return `Tilt your head${little} to your ${ownSide('left', mirrored)}`;
    case 'tiltRight':
      return `Tilt your head${little} to your ${ownSide('right', mirrored)}`;
    case 'lookLeft':
      return `Turn your head${little} to your ${ownSide('left', mirrored)}`;
    case 'lookRight':
      return `Turn your head${little} to your ${ownSide('right', mirrored)}`;
    case 'leanLeft':
      return `Lean${little} to your ${ownSide('left', mirrored)}`;
    case 'leanRight':
      return `Lean${little} to your ${ownSide('right', mirrored)}`;
    case 'straightenUp':
      return 'Level your shoulders';
    case 'holdStill':
      return 'Hold still';
    case 'perfect':
      return 'Perfect, hold it';
    default:
      return cap(who);
  }
}

export function scoreWords(score: number): string {
  if (score >= 85) return 'Great match';
  if (score >= 70) return 'Almost there';
  if (score >= 50) return 'Getting closer';
  return 'Keep going';
}

/** The landmark a piece of advice is about (for the on-screen arrow). */
export const ADVICE_LANDMARK: Partial<Record<PosePart, number>> = {
  leftHand: 15, rightHand: 16, leftElbow: 13, rightElbow: 14, leftFoot: 27, rightFoot: 28, leftKnee: 25, rightKnee: 26,
};

/** Shoulder centre of a reference (or the nose, lower down, for a face). */
function anchorOf(p: PosePoint[], unit: number): [number, number] | null {
  const ok = (i: number) => p[i][4] >= 0.5 && p[i][3] >= 0.35;
  if (ok(11) && ok(12)) return [(p[11][0] + p[12][0]) / 2, (p[11][1] + p[12][1]) / 2];
  if (ok(0)) return [p[0][0], p[0][1] + 1.15 * unit];
  return null;
}

/**
 * Where a reference landmark falls on the person now: the reference's pose
 * carried onto the person's own position and size (for alignment arrows).
 */
export function targetOnPerson(ref: PoseDef, live: LivePose, index: number, previewAspect: number): [number, number] | null {
  const ra = anchorOf(ref.landmarks, ref.unit);
  if (!ra || !live.person || live.unit <= 0 || ref.unit <= 0) return null;
  const k = live.unit / ref.unit;
  const p = ref.landmarks[index];
  // Offsets in square units (height = 1) so the aspect is respected.
  const dx = (p[0] - ra[0]) * ref.aspect * k;
  const dy = (p[1] - ra[1]) * k;
  return [live.anchor[0] + dx / previewAspect, live.anchor[1] + dy];
}

// ------------------------------------------------------------------ framing

export interface FaceTip {
  text: string;
  ok: boolean;
}

/**
 * Free face positioning: eyes near the upper third line, the face inside
 * the frame with room around it.
 */
export function faceTip(face: [number, number] | null): FaceTip {
  if (!face) return { text: 'Point the camera at a face', ok: false };
  const [x, y] = face;
  if (x < 0.12) return { text: 'Move the face away from the edge', ok: false };
  if (x > 0.88) return { text: 'Move the face away from the edge', ok: false };
  // The nose sits a little below the eye line: aim for 0.3–0.45.
  if (y < 0.24) return { text: 'Tilt the phone up a little: leave room above the head', ok: false };
  if (y > 0.52) return { text: 'Tilt the phone down a little: eyes near the top third', ok: false };
  return { text: 'Nice framing', ok: true };
}

export interface LevelReading {
  roll: number;
  pitch: number;
  orientation: number;
}

export function levelTip(level: LevelReading | null): FaceTip {
  if (!level) return { text: '', ok: true };
  if (Math.abs(level.roll) < 1) return { text: 'Level', ok: true };
  // Gravity reads as a negative roll when the phone is turned clockwise.
  return { text: level.roll < 0 ? 'Turn the phone a little to the left' : 'Turn the phone a little to the right', ok: false };
}

// --------------------------------------------------------------- auto capture

export interface AutoCaptureSettings {
  enabled: boolean;
  /** Score to reach (0–100). */
  threshold: number;
  /** How long it must hold (ms) before the photo is taken. */
  holdMs: number;
}

export interface AutoCaptureState {
  /** When the conditions were first met (ms), or null. */
  since: number | null;
  /** 0..1 progress toward the shot. */
  progress: number;
  fire: boolean;
}

export const AUTO_CAPTURE_IDLE: AutoCaptureState = { since: null, progress: 0, fire: false };

/**
 * Automatic capture: fires once the match (pose and framing) has stayed at
 * or above the threshold, with the person still, for the hold time. Any slip
 * starts the count again.
 */
export function stepAutoCapture(
  prev: AutoCaptureState,
  live: LivePose | null,
  settings: AutoCaptureSettings,
  now: number,
): AutoCaptureState {
  if (!settings.enabled || !live?.person || !live.match) return AUTO_CAPTURE_IDLE;
  const m = live.match;
  const ready = m.score >= settings.threshold && m.coverage >= 0.5 && live.stillness >= 0.6;
  if (!ready) return AUTO_CAPTURE_IDLE;
  const since = prev.since ?? now;
  const hold = Math.max(200, settings.holdMs);
  const progress = Math.min(1, (now - since) / hold);
  return { since, progress, fire: progress >= 1 && !prev.fire };
}

// ------------------------------------------------------------------ guided

export type GuideStep = 'frame' | 'pose' | 'hold' | 'ready';

/** Guided capture: one thing at a time, framing first. */
export function guideStep(live: LivePose | null, threshold: number): GuideStep {
  if (!live?.person || !live.match) return 'frame';
  const m = live.match;
  if (m.advice.some(a => ['noPerson', 'closer', 'back', 'moveLeft', 'moveRight', 'cameraUp', 'cameraDown'].includes(a.action))) {
    return 'frame';
  }
  if (m.score < threshold) return 'pose';
  if (live.stillness < 0.6) return 'hold';
  return 'ready';
}

export const GUIDE_WORDS: Record<GuideStep, string> = {
  frame: 'Step 1 of 3: get into position',
  pose: 'Step 2 of 3: match the pose',
  hold: 'Step 3 of 3: hold still',
  ready: 'Ready',
};

// --------------------------------------------------------------- photography

/** Basic tips (free), shown one at a time. */
export const PHOTO_TIPS: readonly string[] = [
  'Face the light: a window in front of you is the most flattering light there is.',
  'Hold the phone at eye level, or a little above, for natural proportions.',
  'Put the eyes on the upper third line of the grid.',
  'Leave a little space in the direction the person is looking.',
  'Step back and zoom in a touch: faces look more natural than up close.',
  'Keep the horizon level: turn on the level to check.',
  'Tidy the background: one step to the side can remove a distraction.',
  'Relax the shoulders and breathe out just before the shot.',
];
