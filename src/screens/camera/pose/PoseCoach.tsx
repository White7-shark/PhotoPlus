import React from 'react';
import { Pressable, StyleSheet, View, type ViewStyle } from 'react-native';
import Svg, { Circle } from 'react-native-svg';

import { AppText, GlassSurface, Icon } from '../../../components';
import { GUIDE_WORDS, PHOTO_TIPS, adviceText, scoreWords } from '../../../services/pose/guidance';
import { shown } from '../../../services/pose/library';
import type { PoseAssistantSettings } from '../../../services/pose/settings';
import { PoseSilhouette } from './PoseSilhouette';
import type { PoseAssistant } from './usePoseAssistant';

const GOOD = '#5BE49B';
const NEAR = '#F5C66B';
const OFF = '#FF6B6B';

function ScoreRing({ score, progress }: { score: number; progress: number }) {
  const r = 22;
  const c = 2 * Math.PI * r;
  const colour = score >= 80 ? GOOD : score >= 55 ? NEAR : OFF;
  return (
    <View style={{ width: 56, height: 56, alignItems: 'center', justifyContent: 'center' }}>
      <Svg width={56} height={56} style={StyleSheet.absoluteFill}>
        <Circle cx={28} cy={28} r={r} stroke="rgba(255,255,255,0.18)" strokeWidth={4} fill="none" />
        <Circle
          cx={28}
          cy={28}
          r={r}
          stroke={colour}
          strokeWidth={4}
          fill="none"
          strokeDasharray={`${(c * Math.min(100, score)) / 100} ${c}`}
          strokeLinecap="round"
          transform="rotate(-90 28 28)"
        />
        {progress > 0 ? (
          <Circle
            cx={28}
            cy={28}
            r={r - 7}
            stroke="#FFFFFF"
            strokeWidth={3}
            fill="none"
            strokeDasharray={`${2 * Math.PI * (r - 7) * progress} ${2 * Math.PI * (r - 7)}`}
            transform="rotate(-90 28 28)"
          />
        ) : null}
      </Svg>
      <AppText variant="bodyStrong" style={{ color: '#FFFFFF' }}>
        {Math.round(score)}
      </AppText>
    </View>
  );
}

/**
 * What the assistant says, in one small panel: for Premium the score, the
 * next thing to change and the guided step (and the countdown to an
 * automatic photo); for everyone else the pose to copy with its tips, and
 * where to put the face. Words only, no jargon.
 */
export function PoseCoach({
  assistant,
  settings,
  tipIndex,
  onUpgrade,
  rotation = 0,
}: {
  assistant: PoseAssistant;
  settings: PoseAssistantSettings;
  tipIndex: number;
  onUpgrade: () => void;
  rotation?: number;
}) {
  const { reference, matching, live, available, face } = assistant;
  if (!reference && !settings.faceGuide) return null;
  const turn: ViewStyle = rotation ? { transform: [{ rotate: `${rotation}deg` }] } : {};
  if (!available) {
    return (
      <GlassSurface radius={18} style={[styles.panel, turn]}>
        <AppText variant="caption" style={styles.text}>
          Pose guidance isn’t available on this phone. The grid and level still work.
        </AppText>
      </GlassSurface>
    );
  }
  if (matching && reference) {
    const m = live?.person ? live.match : null;
    const mirrored = live?.mirrored ?? false;
    const first = m?.advice[0];
    const second = m?.advice[1];
    return (
      <GlassSurface radius={18} style={[styles.panel, styles.row, turn]}>
        {m ? <ScoreRing score={m.score} progress={assistant.autoProgress} /> : null}
        <View style={{ flex: 1, gap: 2 }} accessibilityLiveRegion="polite">
          <AppText variant="caption" style={{ color: 'rgba(255,255,255,0.7)', fontWeight: '700' }}>
            {settings.guided ? GUIDE_WORDS[assistant.step] : m ? scoreWords(m.score) : reference.name}
          </AppText>
          <AppText variant="bodyStrong" style={styles.text}>
            {!live?.person ? 'Step into the frame' : first ? adviceText(first, mirrored) : 'Hold it there'}
          </AppText>
          {second ? (
            <AppText variant="caption" style={{ color: 'rgba(255,255,255,0.8)' }}>
              {adviceText(second, mirrored)}
            </AppText>
          ) : null}
          {assistant.autoProgress > 0 ? (
            <AppText variant="caption" style={{ color: GOOD, fontWeight: '700' }}>
              Taking the photo…
            </AppText>
          ) : null}
        </View>
      </GlassSurface>
    );
  }
  // Free: a simple demonstration of the pose, and the face guide.
  return (
    <GlassSurface radius={18} style={[styles.panel, styles.row, turn]}>
      {reference ? (
        <Svg width={44} height={58}>
          <PoseSilhouette
            points={reference.landmarks.map(p => ({ x: p[0], y: p[1], ok: shown(p) }))}
            unit={reference.unit}
            width={44}
            height={58}
            color="#FFFFFF"
          />
        </Svg>
      ) : (
        <Icon name="figure" size={22} color={face.ok ? GOOD : '#FFFFFF'} />
      )}
      <View style={{ flex: 1, gap: 2 }} accessibilityLiveRegion="polite">
        {reference ? (
          <>
            <AppText variant="bodyStrong" style={styles.text}>
              {reference.name}
            </AppText>
            <AppText variant="caption" style={{ color: 'rgba(255,255,255,0.85)' }}>
              {reference.tips[tipIndex % reference.tips.length]}
            </AppText>
          </>
        ) : null}
        {settings.faceGuide ? (
          <AppText variant="caption" style={{ color: face.ok ? GOOD : 'rgba(255,255,255,0.85)', fontWeight: '600' }}>
            {face.text}
          </AppText>
        ) : (
          <AppText variant="caption" style={{ color: 'rgba(255,255,255,0.7)' }}>
            {PHOTO_TIPS[tipIndex % PHOTO_TIPS.length]}
          </AppText>
        )}
        {reference ? (
          <Pressable
            onPress={onUpgrade}
            accessibilityRole="button"
            accessibilityLabel="Live pose matching with Premium"
            hitSlop={8}
          >
            <AppText variant="caption" style={{ color: NEAR, fontWeight: '700' }}>
              See how well you match: Premium
            </AppText>
          </Pressable>
        ) : null}
      </View>
    </GlassSurface>
  );
}

const styles = StyleSheet.create({
  panel: { paddingHorizontal: 14, paddingVertical: 10, maxWidth: 360, alignSelf: 'center' },
  row: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  text: { color: '#FFFFFF' },
});
