import React, { useRef, useState } from 'react';
import { ActivityIndicator, Image, StyleSheet, View, type GestureResponderEvent, type LayoutChangeEvent } from 'react-native';

import { applyGesture, overlayRect, type Touch } from '../../services/studio/gesture';
import type { Placement } from '../../services/studio/scene';
import { containRect } from '../editor/geometry';

/**
 * The scene, with the person moved by hand: one finger moves them (their
 * size follows the scene's depth when asked), two fingers resize. While a
 * finger is down the person is drawn over the empty scene exactly where the
 * engine will put them; when it lifts, the finished scene (matched light,
 * shadows) comes back.
 */
export function PlacementView({
  sceneUri,
  emptyUri,
  cutoutUri,
  frameAspect,
  photoAspect,
  box,
  placement,
  horizon,
  follow,
  rendering,
  onChange,
}: {
  sceneUri: string | null;
  emptyUri: string | null;
  cutoutUri: string;
  frameAspect: number;
  photoAspect: number;
  box: readonly [number, number, number, number];
  placement: Placement;
  horizon: number;
  follow: boolean;
  rendering: boolean;
  onChange: (p: Placement) => void;
}) {
  const [view, setView] = useState({ w: 0, h: 0 });
  const [live, setLive] = useState<Placement | null>(null);
  // The gesture in progress: kept in handlers (the screen shows `live`).
  const gesture = useRef<{ start: Placement; from: Touch[]; now: Placement } | null>(null);
  const frame = containRect(view.w, view.h, frameAspect * 1000, 1000);
  const f = { x: frame.x, y: frame.y, w: frame.w, h: frame.h };

  const touches = (e: GestureResponderEvent) => e.nativeEvent.touches.map(t => ({ x: t.locationX, y: t.locationY }));
  const end = () => {
    const g = gesture.current;
    gesture.current = null;
    setLive(null);
    if (g) onChange(g.now);
  };
  const handlers = {
    onStartShouldSetResponder: () => true,
    onMoveShouldSetResponder: () => true,
    onResponderGrant: (e: GestureResponderEvent) => {
      gesture.current = { start: placement, from: touches(e), now: placement };
      setLive(placement);
    },
    onResponderMove: (e: GestureResponderEvent) => {
      const g = gesture.current;
      if (!g) return;
      const now = touches(e);
      // A finger added or lifted: carry on from here so nothing jumps.
      if (now.length !== g.from.length) {
        gesture.current = { start: g.now, from: now, now: g.now };
        return;
      }
      g.now = applyGesture(g.start, g.from, now, f, horizon, follow);
      setLive(g.now);
    },
    onResponderRelease: end,
    onResponderTerminate: end,
  };

  const moving = live !== null;
  const shown = live ?? placement;
  const rect = overlayRect(shown, box, photoAspect, f);
  const base = moving || !sceneUri ? emptyUri ?? sceneUri : sceneUri;
  return (
    <View
      style={styles.wrap}
      onLayout={(e: LayoutChangeEvent) => setView({ w: e.nativeEvent.layout.width, h: e.nativeEvent.layout.height })}
      accessibilityLabel="Your scene. Drag to move yourself, pinch to resize"
      testID="placement-view"
      {...handlers}
    >
      <View pointerEvents="none" style={[styles.frame, { left: frame.x, top: frame.y, width: frame.w, height: frame.h }]}>
        {base ? <Image source={{ uri: base }} style={StyleSheet.absoluteFill} resizeMode="stretch" fadeDuration={0} /> : null}
      </View>
      {moving || !sceneUri ? (
        <View pointerEvents="none" style={[styles.frame, { left: frame.x, top: frame.y, width: frame.w, height: frame.h }]}>
          <Image
            source={{ uri: cutoutUri }}
            style={{
              position: 'absolute',
              left: rect.left - frame.x,
              top: rect.top - frame.y,
              width: rect.width,
              height: rect.height,
              transform: shown.flip ? [{ scaleX: -1 }] : [],
            }}
            resizeMode="stretch"
            fadeDuration={0}
          />
        </View>
      ) : null}
      {rendering ? <ActivityIndicator style={styles.spinner} color="#FFFFFF" accessibilityLabel="Updating the scene" /> : null}
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: { width: '100%', aspectRatio: 0.85, borderRadius: 18, overflow: 'hidden', backgroundColor: '#0B0B0E' },
  frame: { position: 'absolute', overflow: 'hidden' },
  spinner: { position: 'absolute', right: 12, top: 12 },
});
