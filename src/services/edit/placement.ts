import type { PropDef } from './catalog';

/** A point in photo fractions (0..1 of width / height). */
export interface Pt {
  x: number;
  y: number;
}

/**
 * A face found in the photo (photo fractions). `eyeL`/`eyeR` are the eyes on
 * the left and right of the picture (not the person's left and right).
 */
export interface FaceGeometry {
  /** Top of the face outline (upper forehead). */
  top: Pt;
  /** Bottom of the chin. */
  chin: Pt;
  eyeL: Pt;
  eyeR: Pt;
  /** Face width as a fraction of the photo width. */
  width: number;
}

/** A prop on the photo: centre and width in photo fractions, rotation clockwise. */
export interface PropPlacement {
  id: string;
  cx: number;
  cy: number;
  width: number;
  rotation: number;
  flip: boolean;
}

const len = (x: number, y: number) => Math.hypot(x, y);

/**
 * Fits a prop to a face so it looks worn: hats sit where the crown meets the
 * head, glasses centre between the eyes, outfit pieces sit below the chin,
 * sized to the face and turned with the head. `aspect` is the photo's
 * width / height.
 */
export function fitToFace(prop: PropDef, face: FaceGeometry, aspect: number): PropPlacement {
  // Work in units where both axes have the same scale (photo height = 1).
  const px = (p: Pt) => ({ x: p.x * aspect, y: p.y });
  const eL = px(face.eyeL);
  const eR = px(face.eyeR);
  const top = px(face.top);
  const chin = px(face.chin);
  // The face's own axes: right along the eye line, down perpendicular to it.
  let rx = eR.x - eL.x;
  let ry = eR.y - eL.y;
  const eyeDist = len(rx, ry) || 1e-6;
  rx /= eyeDist;
  ry /= eyeDist;
  const dx = -ry;
  const dy = rx;
  const faceH = Math.max(1e-6, (chin.x - top.x) * dx + (chin.y - top.y) * dy);
  const faceW = face.width * aspect;
  let anchor: { x: number; y: number };
  let width: number;
  if (prop.anchor === 'eyes') {
    anchor = { x: (eL.x + eR.x) / 2, y: (eL.y + eR.y) / 2 };
    width = prop.fit * eyeDist;
  } else {
    anchor = prop.anchor === 'head' ? top : chin;
    width = prop.fit * faceW;
  }
  anchor = { x: anchor.x + dx * prop.drop * faceH, y: anchor.y + dy * prop.drop * faceH };
  const height = width * prop.aspect;
  const ox = (0.5 - prop.ax) * width;
  const oy = (0.5 - prop.ay) * height;
  const c = { x: anchor.x + rx * ox + dx * oy, y: anchor.y + ry * ox + dy * oy };
  return {
    id: prop.id,
    cx: c.x / aspect,
    cy: c.y,
    width: width / aspect,
    rotation: (Math.atan2(ry, rx) * 180) / Math.PI,
    flip: false,
  };
}

/** Without a face: centred, a third of the photo wide. */
export function defaultPlacement(prop: PropDef): PropPlacement {
  return { id: prop.id, cx: 0.5, cy: prop.anchor === 'neck' ? 0.6 : 0.4, width: 0.35, rotation: 0, flip: false };
}

/** The face a new prop should go on: the largest one. */
export function mainFace(faces: readonly FaceGeometry[]): FaceGeometry | null {
  return faces.reduce<FaceGeometry | null>((best, f) => (best === null || f.width > best.width ? f : best), null);
}
