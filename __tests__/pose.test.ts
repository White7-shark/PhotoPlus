import {
  CATEGORY_LABELS,
  POSES,
  POSE_LANDMARKS,
  canUsePose,
  findPose,
  isPoseDef,
  mirrorPose,
  posesByCategory,
  shown,
} from '../src/services/pose/library';
import {
  AUTO_CAPTURE_IDLE,
  PHOTO_TIPS,
  adviceText,
  faceTip,
  guideStep,
  levelTip,
  partWords,
  scoreWords,
  stepAutoCapture,
  targetOnPerson,
  type LivePose,
  type PoseMatch,
} from '../src/services/pose/guidance';

const match = (over: Partial<PoseMatch> = {}): PoseMatch => ({
  score: 90,
  coverage: 0.9,
  pose: 92,
  framing: 85,
  offset: [0, 0],
  size: 1,
  bones: [],
  hands: [1, 1],
  advice: [],
  ...over,
});
const live = (over: Partial<LivePose> = {}): LivePose => ({
  person: true,
  mirrored: true,
  stillness: 0.9,
  shot: 'halfBody',
  anchor: [0.5, 0.4],
  unit: 0.12,
  headTilt: 0,
  face: [0.5, 0.3],
  points: [],
  match: match(),
  ...over,
});

describe('pose library', () => {
  it('has the free demonstrations and the premium collection, all valid', () => {
    expect(POSES.length).toBeGreaterThanOrEqual(24);
    const free = POSES.filter(p => p.tier === 'free');
    expect(free.length).toBe(6);
    for (const p of POSES) {
      expect(isPoseDef(p)).toBe(true);
      expect(p.landmarks).toHaveLength(POSE_LANDMARKS);
      expect(p.tips.length).toBeGreaterThan(0);
      // The face and shoulders are in every pose's picture.
      expect(shown(p.landmarks[0])).toBe(true);
      expect(shown(p.landmarks[11]) && shown(p.landmarks[12])).toBe(true);
    }
    // Full-body poses show the feet; head-and-shoulders ones don't.
    const feet = (p: (typeof POSES)[number]) => shown(p.landmarks[27]) || shown(p.landmarks[28]);
    expect(POSES.filter(p => p.shot === 'fullBody').every(feet)).toBe(true);
    expect(POSES.filter(p => p.shot === 'headAndShoulders').some(feet)).toBe(false);
    expect(new Set(POSES.map(p => p.id)).size).toBe(POSES.length);
  });

  it('gates premium poses and groups by category', () => {
    const premium = POSES.find(p => p.tier === 'premium')!;
    const free = POSES.find(p => p.tier === 'free')!;
    expect(canUsePose(free, false)).toBe(true);
    expect(canUsePose(premium, false)).toBe(false);
    expect(canUsePose(premium, true)).toBe(true);
    const mine = { ...free, id: 'mine-1', category: 'mine' as const, tier: 'premium' as const };
    const groups = posesByCategory([mine]);
    expect(groups[0].category).toBe('mine');
    expect(groups.every(g => CATEGORY_LABELS[g.category])).toBe(true);
    expect(findPose('mine-1', [mine])?.id).toBe('mine-1');
    expect(findPose('nope')).toBeNull();
  });

  it('mirrors a pose: the other hand, the other side', () => {
    const p = findPose('chin-rest')!;
    const m = mirrorPose(p);
    expect(m.landmarks[16][0]).toBeCloseTo(1 - p.landmarks[15][0], 6);
    expect(m.landmarks[11][1]).toBeCloseTo(p.landmarks[12][1], 6);
    const back = mirrorPose(m).landmarks;
    back.forEach((pt, i) => pt.forEach((v, k) => expect(v).toBeCloseTo(p.landmarks[i][k], 9)));
  });

  it('rejects malformed poses', () => {
    const p = POSES[0];
    expect(isPoseDef({ ...p, landmarks: p.landmarks.slice(1) })).toBe(false);
    expect(isPoseDef({ ...p, aspect: 0 })).toBe(false);
    expect(isPoseDef({ ...p, tier: 'gold' })).toBe(false);
    expect(isPoseDef(null)).toBe(false);
  });
});

describe('pose guidance words', () => {
  it('names the user’s own side on a mirrored selfie and when facing the camera', () => {
    // Mirrored preview: the model's "left hand" is the user's right.
    expect(partWords('leftHand', true)).toBe('your right hand');
    expect(partWords('leftHand', false)).toBe('your left hand');
    expect(adviceText({ part: 'rightHand', action: 'up', amount: 0.8 }, true)).toBe('Raise your left hand');
    // "Move left" is the screen's left: your own left in a mirror, your right otherwise.
    expect(adviceText({ part: 'body', action: 'moveLeft', amount: 0.2 }, true)).toBe('Move to your left');
    expect(adviceText({ part: 'body', action: 'moveLeft', amount: 0.05 }, false)).toBe('Move a little to your right');
  });

  it('words every action plainly', () => {
    const actions = [
      'noPerson', 'show', 'closer', 'back', 'moveLeft', 'moveRight', 'cameraUp', 'cameraDown', 'turnLeft', 'turnRight',
      'up', 'down', 'in', 'out', 'tiltLeft', 'tiltRight', 'lookLeft', 'lookRight', 'leanLeft', 'leanRight',
      'straightenUp', 'holdStill', 'perfect',
    ] as const;
    for (const action of actions) {
      const t = adviceText({ part: 'leftHand', action, amount: 0.3 }, false);
      expect(t.length).toBeGreaterThan(3);
      expect(t).not.toMatch(/\b(landmark|yaw|pitch|roll|anchor|coverage|undefined)\b/i);
    }
    expect(adviceText({ part: 'leftHand', action: 'show', amount: 1 }, true)).toBe('Show your right hand');
    expect(adviceText({ part: 'body', action: 'back', amount: 0.1 }, true)).toBe('Step back a little');
    expect(scoreWords(90)).toBe('Great match');
    expect(scoreWords(30)).toBe('Keep going');
  });

  it('places the reference on the person for alignment arrows', () => {
    const ref = findPose('wave')!;
    // The same body, same place and size: the target is where the reference has it.
    const anchor: [number, number] = [
      (ref.landmarks[11][0] + ref.landmarks[12][0]) / 2,
      (ref.landmarks[11][1] + ref.landmarks[12][1]) / 2,
    ];
    const t = targetOnPerson(ref, live({ anchor, unit: ref.unit }), 15, ref.aspect)!;
    expect(t[0]).toBeCloseTo(ref.landmarks[15][0], 4);
    expect(t[1]).toBeCloseTo(ref.landmarks[15][1], 4);
    // Half the size, moved: the hand follows the person.
    const t2 = targetOnPerson(ref, live({ anchor: [0.3, 0.5], unit: ref.unit / 2 }), 15, ref.aspect)!;
    expect(t2[0]).toBeCloseTo(0.3 + (ref.landmarks[15][0] - anchor[0]) / 2, 4);
    expect(targetOnPerson(ref, live({ person: false }), 15, 0.75)).toBeNull();
  });
});

describe('free framing help', () => {
  it('guides the face to the upper third', () => {
    expect(faceTip(null).ok).toBe(false);
    expect(faceTip([0.5, 0.35]).ok).toBe(true);
    expect(faceTip([0.5, 0.65]).text).toMatch(/down/);
    expect(faceTip([0.5, 0.15]).text).toMatch(/up/);
    expect(faceTip([0.05, 0.35]).text).toMatch(/edge/);
  });

  it('reads the level', () => {
    expect(levelTip({ roll: 0.4, pitch: 0, orientation: 0 }).ok).toBe(true);
    // Turned clockwise (negative): turn back to the left.
    expect(levelTip({ roll: -3, pitch: 0, orientation: 0 }).text).toMatch(/left/);
    expect(levelTip({ roll: 3, pitch: 0, orientation: 0 }).text).toMatch(/right/);
    expect(PHOTO_TIPS.length).toBeGreaterThanOrEqual(6);
  });
});

describe('guided and automatic capture', () => {
  const settings = { enabled: true, threshold: 80, holdMs: 1000 };

  it('fires once the match has held, still, for the hold time', () => {
    let s = stepAutoCapture(AUTO_CAPTURE_IDLE, live(), settings, 0);
    expect(s.fire).toBe(false);
    s = stepAutoCapture(s, live(), settings, 600);
    expect(s.progress).toBeCloseTo(0.6, 5);
    s = stepAutoCapture(s, live(), settings, 1000);
    expect(s.fire).toBe(true);
    // Once only.
    expect(stepAutoCapture(s, live(), settings, 1100).fire).toBe(false);
  });

  it('starts over on any slip, and never fires when off or with no reference', () => {
    let s = stepAutoCapture(AUTO_CAPTURE_IDLE, live(), settings, 0);
    s = stepAutoCapture(s, live({ match: match({ score: 60 }) }), settings, 500);
    expect(s).toEqual(AUTO_CAPTURE_IDLE);
    s = stepAutoCapture(s, live({ stillness: 0.2 }), settings, 600);
    expect(s).toEqual(AUTO_CAPTURE_IDLE);
    s = stepAutoCapture(s, live({ match: match({ coverage: 0.3 }) }), settings, 700);
    expect(s).toEqual(AUTO_CAPTURE_IDLE);
    expect(stepAutoCapture(AUTO_CAPTURE_IDLE, live(), { ...settings, enabled: false }, 0)).toEqual(AUTO_CAPTURE_IDLE);
    expect(stepAutoCapture(AUTO_CAPTURE_IDLE, live({ match: null }), settings, 0)).toEqual(AUTO_CAPTURE_IDLE);
    expect(stepAutoCapture(AUTO_CAPTURE_IDLE, null, settings, 0)).toEqual(AUTO_CAPTURE_IDLE);
  });

  it('guides one step at a time, framing first', () => {
    expect(guideStep(null, 80)).toBe('frame');
    expect(guideStep(live({ match: match({ advice: [{ part: 'body', action: 'back', amount: 0.3 }] }) }), 80)).toBe('frame');
    expect(guideStep(live({ match: match({ score: 60 }) }), 80)).toBe('pose');
    expect(guideStep(live({ stillness: 0.3 }), 80)).toBe('hold');
    expect(guideStep(live(), 80)).toBe('ready');
  });
});

import { poseFromPhoto, poseFromResult } from '../src/services/pose/fromPhoto';

describe('a pose from my photo', () => {
  const flat = (findPose('chin-rest')!.landmarks as number[][]).flat();
  it('becomes a library pose of mine', async () => {
    const out = await poseFromPhoto(
      async () => '/data/poses/p.jpg',
      async () => ({ valid: true, aspect: 0.8, landmarks: flat, shot: 'halfBody', unit: 0.2 }),
      3,
      1700000000000,
    );
    if (out.kind !== 'pose') throw new Error(`expected a pose, got ${out.kind}`);
    expect(out.pose.category).toBe('mine');
    expect(out.pose.name).toBe('My pose 3');
    expect(out.pose.shot).toBe('halfBody');
    expect(out.pose.photoUri).toBe('/data/poses/p.jpg');
    expect(isPoseDef(out.pose)).toBe(true);
  });

  it('says so when there is nobody, and does nothing when cancelled', async () => {
    expect((await poseFromPhoto(async () => null, async () => ({ valid: false, aspect: 1 }), 1)).kind).toBe('cancelled');
    expect((await poseFromPhoto(async () => '/p.jpg', async () => ({ valid: false, aspect: 1 }), 1)).kind).toBe('noPerson');
    expect(poseFromResult({ valid: true, aspect: 1, landmarks: [1, 2, 3] }, '/p.jpg', 1, 1)).toBeNull();
    // A face-only photo is framed as head and shoulders.
    expect(poseFromResult({ valid: true, aspect: 1, landmarks: flat, shot: 'face', unit: 0.3 }, '/p.jpg', 1, 1)?.shot).toBe(
      'headAndShoulders',
    );
  });
});
