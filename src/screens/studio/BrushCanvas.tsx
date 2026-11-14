import React, { useRef, useState } from 'react';
import { Image, StyleSheet, View, type GestureResponderEvent, type LayoutChangeEvent } from 'react-native';
import Svg, { Path } from 'react-native-svg';

import { thin, toPhoto, type Touch } from '../../services/studio/gesture';
import type { Brush, Stroke } from '../../services/studio/StudioService';
import { containRect } from '../editor/geometry';

export type Backing = 'dim' | 'checks' | 'white' | 'black';

export const BRUSH_COLOURS: Record<Brush, string> = {
  keep: 'rgba(91,228,155,0.55)',
  remove: 'rgba(255,107,107,0.55)',
  hair: 'rgba(245,198,107,0.6)',
};

/**
 * The cut-out to review and correct: the person in full colour over their
 * photo dimmed (or over checks, white or black), and a finger paints keep,
 * remove or "fix hair" strokes. `tapMode` turns a tap into a choice of
 * person instead.
 */
export function BrushCanvas({
  photoUri,
  cutoutUri,
  photoWidth,
  photoHeight,
  brush,
  radius,
  backing,
  tapMode,
  onStroke,
  onTap,
}: {
  photoUri: string;
  cutoutUri: string;
  photoWidth: number;
  photoHeight: number;
  brush: Brush;
  /** Share of the photo's short side. */
  radius: number;
  backing: Backing;
  tapMode: boolean;
  onStroke: (s: Stroke) => void;
  onTap: (p: Touch) => void;
}) {
  const [view, setView] = useState({ w: 0, h: 0 });
  const [live, setLive] = useState<Touch[]>([]);
  // The stroke being drawn: collected in handlers (the screen shows `live`).
  const stroke = useRef<Touch[]>([]);
  const area = containRect(view.w, view.h, photoWidth, photoHeight);
  const frame = { x: area.x, y: area.y, w: area.w, h: area.h };
  const at = (e: GestureResponderEvent) => toPhoto(e.nativeEvent.locationX, e.nativeEvent.locationY, frame);
  const finish = () => {
    const pts = stroke.current;
    stroke.current = [];
    setLive([]);
    if (pts.length === 0) return;
    if (tapMode) onTap(pts[0]);
    else onStroke({ brush, radius, points: thin(pts, radius) });
  };
  const handlers = {
    onStartShouldSetResponder: () => true,
    onMoveShouldSetResponder: () => true,
    onResponderGrant: (e: GestureResponderEvent) => {
      const p = at(e);
      stroke.current = p ? [p] : [];
      setLive(stroke.current);
    },
    onResponderMove: (e: GestureResponderEvent) => {
      if (tapMode) return;
      const p = at(e);
      if (!p) return;
      stroke.current = [...stroke.current, p];
      setLive(stroke.current);
    },
    onResponderRelease: finish,
    onResponderTerminate: () => {
      stroke.current = [];
      setLive([]);
    },
  };

  const shortPx = Math.min(area.w, area.h);
  const path = live.map((p, i) => `${i ? 'L' : 'M'}${(area.x + p.x * area.w).toFixed(1)} ${(area.y + p.y * area.h).toFixed(1)}`).join(' ');
  return (
    <View
      style={styles.wrap}
      onLayout={(e: LayoutChangeEvent) => setView({ w: e.nativeEvent.layout.width, h: e.nativeEvent.layout.height })}
      accessibilityLabel={tapMode ? 'Photo. Tap the person to keep' : 'Your cut-out. Paint with a finger to correct it'}
      testID="brush-canvas"
      {...handlers}
    >
      <View pointerEvents="none" style={[styles.area, { left: area.x, top: area.y, width: area.w, height: area.h }]}>
        {backing === 'checks' ? <Checks /> : null}
        {backing === 'white' || backing === 'black' ? (
          <View style={[StyleSheet.absoluteFill, { backgroundColor: backing === 'white' ? '#F4F4F2' : '#060607' }]} />
        ) : null}
        {backing === 'dim' ? (
          <Image source={{ uri: photoUri }} style={[StyleSheet.absoluteFill, { opacity: 0.28 }]} resizeMode="stretch" />
        ) : null}
        <Image source={{ uri: cutoutUri }} style={StyleSheet.absoluteFill} resizeMode="stretch" />
      </View>
      {live.length > 0 && !tapMode ? (
        <Svg pointerEvents="none" style={StyleSheet.absoluteFill}>
          <Path
            d={live.length === 1 ? `${path} l0.1 0` : path}
            stroke={BRUSH_COLOURS[brush]}
            strokeWidth={Math.max(4, radius * 2 * shortPx)}
            strokeLinecap="round"
            strokeLinejoin="round"
            fill="none"
          />
        </Svg>
      ) : null}
    </View>
  );
}

/** A transparency checkerboard. */
function Checks() {
  const cells = [];
  for (let y = 0; y < 24; y++)
    for (let x = 0; x < 24; x++)
      if ((x + y) % 2 === 0) cells.push(<View key={`${x}-${y}`} style={{ position: 'absolute', left: `${(x * 100) / 24}%`, top: `${(y * 100) / 24}%`, width: `${100 / 24}%`, height: `${100 / 24}%`, backgroundColor: '#CFCFD4' }} />);
  return <View style={[StyleSheet.absoluteFill, { backgroundColor: '#F2F2F5' }]}>{cells}</View>;
}

const styles = StyleSheet.create({
  wrap: { width: '100%', aspectRatio: 0.85, borderRadius: 18, overflow: 'hidden', backgroundColor: '#0B0B0E' },
  area: { position: 'absolute', overflow: 'hidden' },
});
