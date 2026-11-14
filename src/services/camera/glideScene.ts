/**
 * Glide in a new scene (Premium): the clip's person framed in a background
 * the way the Photoshoot Studio frames a photo — true to the scene's size
 * where they stand, from where the person usually is in the clip. Pure
 * (unit-tested); the native side cuts the person out of every frame.
 */
import { autoFrame, backgroundRequest, lookFor, resized, SIZE_MAX, SIZE_MIN, type Background, type CutoutInfo, type Framing } from '../studio/scene';

import type { GlideSceneRequest, GlideSubject } from './glide';

export function glideCutInfo(subject: GlideSubject): CutoutInfo {
  return {
    box: subject.box,
    cutBottom: subject.cutBottom,
    cutTop: subject.cutTop,
    cutSides: subject.cutLeft || subject.cutRight,
    aspect: subject.aspect,
    face: null,
  };
}

/**
 * The scene for a clip, or null (nobody in the clip, or no background).
 * `size` is the Size slider: 1 is the auto size (true to the scene).
 */
export function frameGlideScene(
  subject: GlideSubject,
  background: Background,
  size = 1,
): { scene: GlideSceneRequest; framing: Framing } | null {
  if (!subject.found || background.kind === 'transparent') return null;
  const cut = glideCutInfo(subject);
  const w = background.kind === 'plate' ? background.plate.width : Math.round(subject.aspect * 1000);
  const h = background.kind === 'plate' ? background.plate.height : 1000;
  const framing = autoFrame(cut, background, subject.aspect, w, h);
  const k = Math.min(SIZE_MAX, Math.max(SIZE_MIN, size));
  const placement = k === 1 ? framing.placement : resized(framing.placement, k, framing.placement.height);
  const look = { ...lookFor(background, framing.crop), bgBlur: framing.bgBlur };
  const bg = backgroundRequest(background) as GlideSceneRequest['background'];
  return {
    scene: { background: bg, crop: framing.crop, placement, look: { ...look }, box: subject.box },
    framing,
  };
}
