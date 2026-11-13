import React from 'react';
import { StyleSheet, View } from 'react-native';
import Svg, { Circle, G, Line, Path, Polygon } from 'react-native-svg';

import { ADVICE_LANDMARK, targetOnPerson, type LevelReading, type LivePose } from '../../../services/pose/guidance';
import { shown, type PoseDef } from '../../../services/pose/library';
import type { GridStyle } from '../../../services/pose/settings';
import { PoseSilhouette } from './PoseSilhouette';

const GOOD = '#5BE49B';
const NEAR = '#F5C66B';
const OFF = '#FF6B6B';
const GOLD = '#F5C66B';
const LINE = 'rgba(255,255,255,0.45)';

/** Composition grids: rule of thirds, golden ratio (phi grid), centre cross, diagonals. */
export function CompositionGrid({ style, width, height }: { style: GridStyle; width: number; height: number }) {
  const lines: [number, number, number, number][] = [];
  if (style === 'thirds' || style === 'golden') {
    const a = style === 'thirds' ? 1 / 3 : 0.382;
    for (const f of [a, 1 - a]) {
      lines.push([f * width, 0, f * width, height], [0, f * height, width, f * height]);
    }
  } else if (style === 'cross') {
    lines.push([width / 2, 0, width / 2, height], [0, height / 2, width, height / 2]);
  } else {
    lines.push([0, 0, width, height], [width, 0, 0, height]);
    // The diagonal method: perpendiculars from the corners meet the diagonals at strong points.
    lines.push([width / 3, 0, width / 3, height], [(2 * width) / 3, 0, (2 * width) / 3, height]);
  }
  return (
    <Svg
      width={width}
      height={height}
      style={StyleSheet.absoluteFill}
      pointerEvents="none"
      importantForAccessibility="no-hide-descendants"
    >
      {lines.map(([x1, y1, x2, y2], i) => (
        <Line key={i} x1={x1} y1={y1} x2={x2} y2={y2} stroke={LINE} strokeWidth={StyleSheet.hairlineWidth * 1.5} />
      ))}
      {style === 'cross' ? (
        <Path
          d={`M${width / 2 - 14} ${height / 2 - 14}h28v28h-28z`}
          stroke={LINE}
          fill="none"
        />
      ) : null}
    </Svg>
  );
}

/**
 * The level: the horizon as the phone sees it, with fixed marks at the ends;
 * it lines up with them (and turns green) when the phone is level.
 */
export function LevelLine({ level, width, height }: { level: LevelReading; width: number; height: number }) {
  const ok = Math.abs(level.roll) < 1;
  const angle = level.roll - level.orientation;
  const half = width * 0.22;
  const cx = width / 2;
  const cy = height / 2;
  const rad = (angle * Math.PI) / 180;
  const dx = Math.cos(rad) * half;
  const dy = Math.sin(rad) * half;
  const colour = ok ? GOOD : '#FFFFFF';
  const portrait = level.orientation === 0 || level.orientation === 180;
  return (
    <Svg width={width} height={height} style={StyleSheet.absoluteFill} pointerEvents="none">
      {portrait ? (
        <>
          <Line x1={cx - half - 22} y1={cy} x2={cx - half - 6} y2={cy} stroke={colour} strokeWidth={2} />
          <Line x1={cx + half + 6} y1={cy} x2={cx + half + 22} y2={cy} stroke={colour} strokeWidth={2} />
        </>
      ) : (
        <>
          <Line x1={cx} y1={cy - half - 22} x2={cx} y2={cy - half - 6} stroke={colour} strokeWidth={2} />
          <Line x1={cx} y1={cy + half + 6} x2={cx} y2={cy + half + 22} stroke={colour} strokeWidth={2} />
        </>
      )}
      <Line x1={cx - dx} y1={cy - dy} x2={cx + dx} y2={cy + dy} stroke={colour} strokeWidth={ok ? 2.5 : 2} strokeLinecap="round" />
      <Circle cx={cx} cy={cy} r={3} fill={colour} />
    </Svg>
  );
}

const BONES: [number, number, number][] = [
  // [from, to, engine bone index (−1: not scored)]
  [11, 13, 0], [12, 14, 1], [13, 15, 2], [14, 16, 3], [23, 25, 4], [24, 26, 5], [25, 27, 6], [26, 28, 7],
  [11, 12, 9], [23, 24, 10], [11, 23, -1], [12, 24, -1],
];

function boneColour(score: number | undefined): string {
  if (score === undefined || score < 0) return 'rgba(255,255,255,0.75)';
  if (score >= 0.75) return GOOD;
  if (score >= 0.45) return NEAR;
  return OFF;
}

function Arrow({ from, to }: { from: [number, number]; to: [number, number] }) {
  const dx = to[0] - from[0];
  const dy = to[1] - from[1];
  const len = Math.hypot(dx, dy);
  if (len < 14) return null;
  const ux = dx / len;
  const uy = dy / len;
  const head = 12;
  const tip: [number, number] = [to[0] - ux * 6, to[1] - uy * 6];
  const left: [number, number] = [tip[0] - ux * head - uy * head * 0.6, tip[1] - uy * head + ux * head * 0.6];
  const right: [number, number] = [tip[0] - ux * head + uy * head * 0.6, tip[1] - uy * head - ux * head * 0.6];
  return (
    <G>
      <Line x1={from[0]} y1={from[1]} x2={tip[0]} y2={tip[1]} stroke={GOLD} strokeWidth={3} strokeLinecap="round" strokeDasharray="6 5" />
      <Polygon points={`${tip[0]},${tip[1]} ${left[0]},${left[1]} ${right[0]},${right[1]}`} fill={GOLD} />
      <Circle cx={to[0]} cy={to[1]} r={9} stroke={GOLD} strokeWidth={2.5} fill="rgba(245,198,107,0.25)" />
    </G>
  );
}

/**
 * The pose assistant over the preview:
 *  - the reference pose as a soft silhouette where it should be (Premium:
 *    live silhouette overlay);
 *  - the person's own pose, each bone coloured by how well it matches
 *    (green, gold, red), and arrows from a part to where it should go
 *    (Premium: landmark alignment);
 *  - free: the eye-line guide for placing the face.
 */
export function PoseOverlay({
  width,
  height,
  live,
  reference,
  showReference,
  alignment,
  faceGuide,
  faceOk,
}: {
  width: number;
  height: number;
  live: LivePose | null;
  reference: PoseDef | null;
  showReference: boolean;
  alignment: boolean;
  faceGuide: boolean;
  faceOk: boolean;
}) {
  const aspect = width / Math.max(height, 1);
  const pts = live?.person ? live.points : [];
  const bones = live?.match?.bones ?? [];
  const px = (i: number): [number, number] => [pts[i][0] * width, pts[i][1] * height];
  const ok = (i: number) => pts.length > i && pts[i][2] >= 0.5;
  const arrows = alignment && reference && live?.match
    ? live.match.advice
        .map(a => ADVICE_LANDMARK[a.part])
        .filter((i): i is number => i !== undefined && ok(i))
        .slice(0, 2)
        .map(i => {
          const t = targetOnPerson(reference, live, i, aspect);
          return t ? { from: px(i), to: [t[0] * width, t[1] * height] as [number, number], key: i } : null;
        })
        .filter((a): a is { from: [number, number]; to: [number, number]; key: number } => a !== null)
    : [];
  return (
    <View pointerEvents="none" style={StyleSheet.absoluteFill} importantForAccessibility="no-hide-descendants">
      <Svg width={width} height={height}>
        {showReference && reference ? (
          <PoseSilhouette
            points={reference.landmarks.map(p => ({ x: p[0], y: p[1], ok: shown(p) }))}
            unit={reference.unit}
            width={width}
            height={height}
            color="rgba(255,255,255,0.22)"
            outline="rgba(255,255,255,0.55)"
          />
        ) : null}
        {faceGuide ? (
          <>
            <Line
              x1={width * 0.18}
              y1={height / 3}
              x2={width * 0.82}
              y2={height / 3}
              stroke={faceOk ? GOOD : 'rgba(255,255,255,0.6)'}
              strokeWidth={1.5}
              strokeDasharray="8 6"
            />
            {live?.face ? (
              <Circle
                cx={live.face[0] * width}
                cy={live.face[1] * height}
                r={Math.max(16, (live.unit || 0.1) * height * 0.32)}
                stroke={faceOk ? GOOD : 'rgba(255,255,255,0.8)'}
                strokeWidth={2}
                fill="none"
              />
            ) : null}
          </>
        ) : null}
        {alignment && pts.length === 33 ? (
          <G>
            {BONES.map(([a, b, bone]) =>
              ok(a) && ok(b) ? (
                <Line
                  key={`${a}-${b}`}
                  x1={px(a)[0]}
                  y1={px(a)[1]}
                  x2={px(b)[0]}
                  y2={px(b)[1]}
                  stroke={boneColour(bone >= 0 ? bones[bone] : undefined)}
                  strokeWidth={4}
                  strokeLinecap="round"
                />
              ) : null,
            )}
            {[0, 11, 12, 13, 14, 15, 16, 23, 24, 25, 26, 27, 28].map(i =>
              ok(i) ? <Circle key={i} cx={px(i)[0]} cy={px(i)[1]} r={4.5} fill="#FFFFFF" /> : null,
            )}
          </G>
        ) : null}
        {arrows.map(a => (
          <Arrow key={a.key} from={a.from} to={a.to} />
        ))}
      </Svg>
    </View>
  );
}
