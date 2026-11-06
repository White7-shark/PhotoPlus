import React, { useEffect, useState } from 'react';
import { Image, ScrollView, View, type GestureResponderEvent } from 'react-native';

import { AppText, PreviewTile, SegmentedControl } from '../../components';
import type { CameraService } from '../../services/camera/CameraService';
import { CameraEvents, type CameraPropRequest } from '../../services/camera/types';
import { PROP_CATEGORIES, propSource, type PropCategory, type PropDef } from '../../services/edit/catalog';
import { fitToFace, mainFace, type FaceGeometry } from '../../services/edit/placement';

/** The preview frame in screen pixels. */
export interface Frame {
  width: number;
  height: number;
}

/** A prop on the preview, in preview pixels (centre, width, rotation clockwise). */
export interface LivePlacement {
  cx: number;
  cy: number;
  width: number;
  rotation: number;
}

/**
 * The person's adjustment on top of the face fit, so the prop keeps following
 * the face: a shift along the face's own axes (in face widths), a size factor
 * and an extra turn. Mirrors PropFit.Adjust natively.
 */
export interface PropAdjust {
  du: number;
  dv: number;
  scale: number;
  turn: number;
}

export const NO_ADJUST: PropAdjust = { du: 0, dv: 0, scale: 1, turn: 0 };

/** Faces jitter a little frame to frame: blend each new reading with the last. */
export function smoothFace(prev: FaceGeometry | null, next: FaceGeometry, amount = 0.55): FaceGeometry {
  if (!prev) return next;
  const mix = (a: number, b: number) => a + (b - a) * amount;
  const pt = (a: { x: number; y: number }, b: { x: number; y: number }) => ({ x: mix(a.x, b.x), y: mix(a.y, b.y) });
  // A different face (far away) is taken as is, not blended.
  if (Math.hypot(prev.top.x - next.top.x, prev.top.y - next.top.y) > prev.width) return next;
  return {
    top: pt(prev.top, next.top),
    chin: pt(prev.chin, next.chin),
    eyeL: pt(prev.eyeL, next.eyeL),
    eyeR: pt(prev.eyeR, next.eyeR),
    width: mix(prev.width, next.width),
  };
}

/** The face's own axes in preview pixels: right along the eyes, and its width. */
function faceAxes(face: FaceGeometry, frame: Frame) {
  let rx = (face.eyeR.x - face.eyeL.x) * frame.width;
  let ry = (face.eyeR.y - face.eyeL.y) * frame.height;
  const d = Math.hypot(rx, ry) || 1e-6;
  rx /= d;
  ry /= d;
  return { rx, ry, faceW: Math.max(1, face.width * frame.width) };
}

/** Where the prop sits: fitted to the face plus the person's adjustment, or where it was put. */
export function livePlacement(
  def: PropDef,
  face: FaceGeometry | null,
  frame: Frame,
  adjust: PropAdjust,
  free: LivePlacement | null,
): LivePlacement {
  if (!face) {
    return (
      free ?? {
        cx: frame.width / 2,
        cy: frame.height * (def.anchor === 'neck' ? 0.62 : 0.4),
        width: frame.width * 0.45,
        rotation: 0,
      }
    );
  }
  const base = fitToFace(def, face, frame.width / frame.height);
  const { rx, ry, faceW } = faceAxes(face, frame);
  return {
    cx: base.cx * frame.width + (rx * adjust.du - ry * adjust.dv) * faceW,
    cy: base.cy * frame.height + (ry * adjust.du + rx * adjust.dv) * faceW,
    width: base.width * frame.width * adjust.scale,
    rotation: base.rotation + adjust.turn,
  };
}

/** A drag of (dx, dy) screen pixels, as a shift along the face (so it keeps following it). */
export function dragAdjust(adjust: PropAdjust, face: FaceGeometry, frame: Frame, dx: number, dy: number): PropAdjust {
  const { rx, ry, faceW } = faceAxes(face, frame);
  return { ...adjust, du: adjust.du + (dx * rx + dy * ry) / faceW, dv: adjust.dv + (-dx * ry + dy * rx) / faceW };
}

/** What the camera needs to put the prop into the photo. */
export function propRequest(
  def: PropDef,
  at: LivePlacement,
  frame: Frame,
  face: FaceGeometry | null,
  adjust: PropAdjust,
): CameraPropRequest {
  return {
    id: def.id,
    cx: at.cx / frame.width,
    cy: at.cy / frame.height,
    width: at.width / frame.width,
    rotation: at.rotation,
    flip: false,
    anchor: def.anchor,
    ax: def.ax,
    ay: def.ay,
    fit: def.fit,
    drop: def.drop,
    aspect: def.aspect,
    tracked: face !== null,
    ...(face ? { du: adjust.du, dv: adjust.dv, scale: adjust.scale, turn: adjust.turn } : {}),
  };
}

/**
 * The prop worn in the camera: follows the main face, can be dragged (one
 * finger) and resized or turned (two fingers) on the prop itself. Camera
 * gestures (focus, pinch-to-zoom) keep working everywhere else.
 */
export function useLiveProp(camera: CameraService, def: PropDef | null, active: boolean) {
  const [tracked, setTracked] = useState<FaceGeometry | null>(null);
  // Adjustments belong to the prop they were made on: a new prop starts fresh.
  const [adj, setAdj] = useState<{ id: string | null; value: PropAdjust }>({ id: null, value: NO_ADJUST });
  const [placed, setPlaced] = useState<{ id: string | null; value: LivePlacement | null }>({ id: null, value: null });
  const tracking = active && def !== null;
  const id = def?.id ?? null;

  useEffect(() => {
    if (!tracking) return;
    camera.setFaceTracking(true);
    let lostAt = 0;
    const sub = camera.on(CameraEvents.faces, ({ faces }) => {
      const main = mainFace(Array.isArray(faces) ? faces : []);
      if (main) {
        lostAt = 0;
        setTracked(prev => smoothFace(prev, main));
      } else {
        // Keep the last position briefly so a blink or a turn doesn't drop the prop.
        lostAt ||= Date.now();
        if (Date.now() - lostAt > 700) setTracked(null);
      }
    });
    return () => {
      sub.remove();
      camera.setFaceTracking(false);
      setTracked(null);
    };
  }, [camera, tracking]);

  return {
    face: tracking ? tracked : null,
    adjust: adj.id === id ? adj.value : NO_ADJUST,
    setAdjust: (value: PropAdjust) => setAdj({ id, value }),
    free: placed.id === id ? placed.value : null,
    setFree: (value: LivePlacement) => setPlaced({ id, value }),
  };
}

export function LivePropOverlay({
  def,
  frame,
  face,
  adjust,
  free,
  onAdjust,
  onFree,
}: {
  def: PropDef;
  frame: Frame;
  face: FaceGeometry | null;
  adjust: PropAdjust;
  free: LivePlacement | null;
  onAdjust: (a: PropAdjust) => void;
  onFree: (p: LivePlacement) => void;
}) {
  const at = livePlacement(def, face, frame, adjust, free);
  const w = at.width;
  const h = at.width * def.aspect;
  const [touch, setTouch] = useState<{ x: number; y: number; dist: number; angle: number } | null>(null);
  // The touch area covers the prop, turned, with a little room around it.
  const box = Math.max(w, h) + 32;

  const read = (e: GestureResponderEvent) => {
    const t = e.nativeEvent.touches;
    if (t.length >= 2) {
      const dx = t[1].pageX - t[0].pageX;
      const dy = t[1].pageY - t[0].pageY;
      return {
        x: (t[0].pageX + t[1].pageX) / 2,
        y: (t[0].pageY + t[1].pageY) / 2,
        dist: Math.hypot(dx, dy),
        angle: Math.atan2(dy, dx),
      };
    }
    return { x: e.nativeEvent.pageX, y: e.nativeEvent.pageY, dist: 0, angle: 0 };
  };

  const move = (e: GestureResponderEvent) => {
    const now = read(e);
    if (!touch) {
      setTouch(now);
      return;
    }
    const twoFingers = now.dist > 0 && touch.dist > 0;
    const scale = twoFingers ? now.dist / touch.dist : 1;
    const turn = twoFingers ? ((now.angle - touch.angle) * 180) / Math.PI : 0;
    const dx = now.x - touch.x;
    const dy = now.y - touch.y;
    if (face) {
      const moved = dragAdjust(adjust, face, frame, dx, dy);
      onAdjust({ ...moved, scale: Math.min(4, Math.max(0.3, moved.scale * scale)), turn: moved.turn + turn });
    } else {
      onFree({
        cx: at.cx + dx,
        cy: at.cy + dy,
        width: Math.min(frame.width * 2, Math.max(24, at.width * scale)),
        rotation: at.rotation + turn,
      });
    }
    setTouch(now);
  };

  return (
    <View
      onStartShouldSetResponder={() => true}
      onMoveShouldSetResponder={() => true}
      onResponderTerminationRequest={() => false}
      onResponderGrant={e => setTouch(read(e))}
      onResponderMove={move}
      onResponderRelease={() => setTouch(null)}
      onResponderTerminate={() => setTouch(null)}
      accessibilityLabel={`${def.name}. Drag to move it, pinch to resize`}
      testID="live-prop"
      style={{ position: 'absolute', left: at.cx - box / 2, top: at.cy - box / 2, width: box, height: box }}
    >
      <Image
        source={{ uri: propSource(def) }}
        resizeMode="stretch"
        style={{
          position: 'absolute',
          left: (box - w) / 2,
          top: (box - h) / 2,
          width: w,
          height: h,
          transform: [{ rotate: `${at.rotation}deg` }],
        }}
      />
    </View>
  );
}

/** The props tray: a preview of every prop, by category; tap to wear it. */
export function PropTray({
  props,
  category,
  onCategory,
  selected,
  locked,
  onSelect,
  onCreate,
}: {
  props: readonly PropDef[];
  category: PropCategory;
  onCategory: (c: PropCategory) => void;
  selected: string | null;
  /** Premium props are locked on the free plan. */
  locked: boolean;
  onSelect: (def: PropDef | null) => void;
  onCreate: () => void;
}) {
  const shown = props.filter(p => p.category === category);
  return (
    <View
      style={{ marginHorizontal: 12, padding: 12, gap: 10, borderRadius: 22, backgroundColor: 'rgba(10,9,17,0.66)' }}
      testID="prop-tray"
    >
      <SegmentedControl
        label="Props"
        value={category}
        onChange={onCategory}
        options={PROP_CATEGORIES.map(c => ({ value: c.id, label: c.label }))}
      />
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 10 }}>
        <PreviewTile
          label="None"
          placeholderIcon="close"
          width={64}
          height={64}
          selected={selected === null}
          onPress={() => onSelect(null)}
        />
        {category === 'mine' ? (
          <PreviewTile
            label="Make one"
            placeholderIcon="plus"
            width={64}
            height={64}
            onPress={onCreate}
            testID="camera-prop-create"
          />
        ) : null}
        {shown.map(p => (
          <PreviewTile
            key={p.id}
            label={p.name}
            uri={propSource(p)}
            width={64}
            height={64}
            dark
            locked={p.premium && locked}
            selected={selected === p.id}
            onPress={() => onSelect(p)}
            testID={`camera-prop-${p.id}`}
          />
        ))}
      </ScrollView>
      <AppText variant="caption" tone="secondary" align="center">
        {selected ? 'It follows your face. Drag it to move it, pinch to resize.' : 'Tap a prop to try it on.'}
      </AppText>
    </View>
  );
}
