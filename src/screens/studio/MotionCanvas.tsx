import React, { useRef, useState } from 'react';
import { Image, StyleSheet, View, type GestureResponderEvent, type LayoutChangeEvent } from 'react-native';
import Svg, { Circle, Defs, G, Line, LinearGradient, Path, Polyline, Stop } from 'react-native-svg';

import { PIN_RADIUS, nearest, strokeToArrows, type MotionArrow, type MotionPin } from '../../services/motion/catalog';
import { useTheme } from '../../theme';

export type MotionTool = 'arrow' | 'anchor' | 'erase';

const HEAD = 10;

/** One arrow: a soft dark outline under a bright line, with a head at its end. */
function ArrowMark({ a, w, h, color }: { a: MotionArrow; w: number; h: number; color: string }) {
  const x0 = a.x0 * w;
  const y0 = a.y0 * h;
  const x1 = a.x1 * w;
  const y1 = a.y1 * h;
  const ang = Math.atan2(y1 - y0, x1 - x0);
  const head = (s: number) =>
    `M${x1 - HEAD * Math.cos(ang - 0.5 * s)} ${y1 - HEAD * Math.sin(ang - 0.5 * s)}L${x1} ${y1}L${x1 - HEAD * Math.cos(ang + 0.5 * s)} ${y1 - HEAD * Math.sin(ang + 0.5 * s)}`;
  return (
    <G>
      <Line x1={x0} y1={y0} x2={x1} y2={y1} stroke="rgba(0,0,0,0.45)" strokeWidth={6} strokeLinecap="round" />
      <Path d={head(1.1)} stroke="rgba(0,0,0,0.45)" strokeWidth={6} strokeLinecap="round" fill="none" />
      <Line x1={x0} y1={y0} x2={x1} y2={y1} stroke={color} strokeWidth={3} strokeLinecap="round" />
      <Path d={head(1)} stroke={color} strokeWidth={3} strokeLinecap="round" strokeLinejoin="round" fill="none" />
      <Circle cx={x0} cy={y0} r={3} fill={color} />
    </G>
  );
}

/**
 * The photo to draw on: drag to draw motion arrows (a curved stroke becomes
 * a chain of arrows that follows the curve), tap to pin a spot still, or tap
 * an arrow or pin to erase it. Everything is in 0..1 of the photo.
 */
export function MotionCanvas({
  photo,
  aspect,
  arrows,
  pins,
  tool,
  onArrows,
  onPin,
  onErase,
  label,
  testID = 'motion-canvas',
}: {
  photo: string;
  /** Width / height of the photo. */
  aspect: number;
  arrows: readonly MotionArrow[];
  pins: readonly MotionPin[];
  tool: MotionTool;
  onArrows: (added: MotionArrow[]) => void;
  onPin: (pin: MotionPin) => void;
  onErase: (hit: { kind: 'arrow' | 'pin'; index: number }) => void;
  label: string;
  testID?: string;
}) {
  const theme = useTheme();
  const [size, setSize] = useState({ w: 0, h: 0 });
  const [live, setLive] = useState<{ x: number; y: number }[]>([]);
  const stroke = useRef<{ x: number; y: number }[]>([]);

  const at = (e: GestureResponderEvent) => {
    const w = size.w || 1;
    const h = size.h || 1;
    return {
      x: Math.min(1, Math.max(0, e.nativeEvent.locationX / w)),
      y: Math.min(1, Math.max(0, e.nativeEvent.locationY / h)),
    };
  };
  const onLayout = (e: LayoutChangeEvent) => setSize({ w: e.nativeEvent.layout.width, h: e.nativeEvent.layout.height });

  const grant = (e: GestureResponderEvent) => {
    const p = at(e);
    stroke.current = [p];
    if (tool === 'arrow') setLive([p]);
  };
  const move = (e: GestureResponderEvent) => {
    if (tool !== 'arrow') return;
    const p = at(e);
    const last = stroke.current[stroke.current.length - 1];
    // Points a few pixels apart are enough for a smooth stroke.
    if (last && Math.hypot((p.x - last.x) * size.w, (p.y - last.y) * size.h) < 4) return;
    stroke.current = [...stroke.current, p];
    setLive(stroke.current);
  };
  const release = () => {
    const pts = stroke.current;
    stroke.current = [];
    setLive([]);
    const first = pts[0];
    if (!first) return;
    if (tool === 'arrow') {
      const added = strokeToArrows(pts);
      if (added.length) onArrows(added);
    } else if (tool === 'anchor') {
      onPin({ x: first.x, y: first.y, r: PIN_RADIUS });
    } else {
      const hit = nearest(arrows, pins, first.x, first.y);
      if (hit) onErase(hit);
    }
  };

  const short = Math.min(size.w, size.h);
  const accent = theme.colors.accent;
  return (
    <View
      style={[styles.box, { aspectRatio: aspect }]}
      onLayout={onLayout}
      onStartShouldSetResponder={() => true}
      onMoveShouldSetResponder={() => true}
      onResponderTerminationRequest={() => false}
      onResponderGrant={grant}
      onResponderMove={move}
      onResponderRelease={release}
      onResponderTerminate={release}
      accessible
      accessibilityLabel={label}
      testID={testID}
    >
      <Image source={{ uri: `file://${photo}` }} style={StyleSheet.absoluteFill} resizeMode="cover" />
      {size.w > 0 ? (
        <Svg width={size.w} height={size.h} style={StyleSheet.absoluteFill} pointerEvents="none">
          <Defs>
            <LinearGradient id="pin" x1="0" y1="0" x2="1" y2="1">
              <Stop offset="0" stopColor="#FFFFFF" stopOpacity={0.28} />
              <Stop offset="1" stopColor="#FFFFFF" stopOpacity={0.08} />
            </LinearGradient>
          </Defs>
          {pins.map((p, i) => (
            <G key={`p${i}`}>
              <Circle
                cx={p.x * size.w}
                cy={p.y * size.h}
                r={p.r * short}
                fill="url(#pin)"
                stroke="#FFFFFF"
                strokeWidth={2}
                strokeDasharray="5 4"
              />
              <Circle cx={p.x * size.w} cy={p.y * size.h} r={4} fill="#FFFFFF" />
            </G>
          ))}
          {arrows.map((a, i) => (
            <ArrowMark key={`a${i}`} a={a} w={size.w} h={size.h} color={accent} />
          ))}
          {live.length > 1 ? (
            <Polyline
              points={live.map(p => `${p.x * size.w},${p.y * size.h}`).join(' ')}
              stroke="#FFFFFF"
              strokeWidth={3}
              strokeOpacity={0.9}
              strokeLinecap="round"
              strokeLinejoin="round"
              fill="none"
            />
          ) : null}
        </Svg>
      ) : null}
    </View>
  );
}

const styles = StyleSheet.create({
  box: { width: '100%', borderRadius: 16, overflow: 'hidden', backgroundColor: '#000' },
});
