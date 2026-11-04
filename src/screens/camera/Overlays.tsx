import React, { useEffect, useState } from 'react';
import { ActivityIndicator, Animated, Pressable, StyleSheet, View } from 'react-native';

import { AppText, Icon } from '../../components';
import { useReducedMotion } from '../../hooks/useReducedMotion';
import { useTheme } from '../../theme';

export function GridOverlay() {
  return (
    <View pointerEvents="none" style={StyleSheet.absoluteFill} importantForAccessibility="no-hide-descendants">
      {[1, 2].map(i => (
        <View
          key={`v${i}`}
          style={[styles.line, { left: `${(i * 100) / 3}%`, top: 0, bottom: 0, width: StyleSheet.hairlineWidth }]}
        />
      ))}
      {[1, 2].map(i => (
        <View
          key={`h${i}`}
          style={[styles.line, { top: `${(i * 100) / 3}%`, left: 0, right: 0, height: StyleSheet.hairlineWidth }]}
        />
      ))}
    </View>
  );
}

const RING = 76;

/** Tap-to-focus feedback at the touched point; turns gold when focus locks. */
export function FocusRing({
  x,
  y,
  status,
  frame,
}: {
  x: number;
  y: number;
  status: 'focusing' | 'locked' | 'failed';
  frame: { width: number; height: number };
}) {
  const theme = useTheme();
  const reduced = useReducedMotion();
  const [scale] = useState(() => new Animated.Value(reduced ? 1 : 1.35));
  useEffect(() => {
    if (!reduced) {
      Animated.spring(scale, { toValue: 1, useNativeDriver: true, speed: 18, bounciness: 8 }).start();
    }
  }, [scale, reduced]);
  const color = status === 'locked' ? theme.colors.premium : status === 'failed' ? theme.colors.danger : '#FFFFFF';
  return (
    <Animated.View
      pointerEvents="none"
      accessibilityLiveRegion="polite"
      accessibilityLabel={status === 'locked' ? 'Focus locked' : status === 'failed' ? 'Could not focus' : 'Focusing'}
      style={[
        styles.ring,
        {
          left: x * frame.width - RING / 2,
          top: y * frame.height - RING / 2,
          borderColor: color,
          transform: [{ scale }],
        },
      ]}
    >
      <View style={[styles.ringDot, { backgroundColor: color }]} />
    </Animated.View>
  );
}

/** Large countdown for the self-timer. */
export function Countdown({ value }: { value: number }) {
  return (
    <View pointerEvents="none" style={[StyleSheet.absoluteFill, styles.centre]} accessibilityLiveRegion="assertive">
      <AppText variant="display" style={styles.countdown} accessibilityLabel={`${value}`}>
        {value}
      </AppText>
    </View>
  );
}

/** Status pill shown at the top of the viewfinder (recovery, processing). */
export function StatusPill({
  label,
  tone = 'neutral',
  busy,
  onCancel,
}: {
  label: string;
  tone?: 'neutral' | 'warning' | 'premium';
  busy?: boolean;
  onCancel?: () => void;
}) {
  const theme = useTheme();
  const color = tone === 'warning' ? theme.colors.warning : tone === 'premium' ? theme.colors.premium : '#FFFFFF';
  return (
    <View style={styles.pill} accessibilityLiveRegion="polite">
      {busy ? (
        <ActivityIndicator size="small" color={color} />
      ) : (
        <Icon name="info" size={14} color={color} strokeWidth={2} />
      )}
      <AppText variant="caption" style={{ color, fontWeight: '700' }} numberOfLines={1}>
        {label}
      </AppText>
      {onCancel ? (
        <Pressable onPress={onCancel} accessibilityRole="button" accessibilityLabel="Cancel processing" hitSlop={12}>
          <Icon name="close" size={14} color="#FFFFFF" strokeWidth={2.2} />
        </Pressable>
      ) : null}
    </View>
  );
}

/** Brief white flash when the shutter fires (skipped with reduced motion). */
export function ShutterFlash({ trigger }: { trigger: number }) {
  const reduced = useReducedMotion();
  const [opacity] = useState(() => new Animated.Value(0));
  useEffect(() => {
    if (trigger === 0 || reduced) {
      return;
    }
    opacity.setValue(0.7);
    Animated.timing(opacity, { toValue: 0, duration: 220, useNativeDriver: true }).start();
  }, [trigger, reduced, opacity]);
  return <Animated.View pointerEvents="none" style={[StyleSheet.absoluteFill, { backgroundColor: '#000', opacity }]} />;
}

const styles = StyleSheet.create({
  line: { position: 'absolute', backgroundColor: 'rgba(255,255,255,0.35)' },
  ring: {
    position: 'absolute',
    width: RING,
    height: RING,
    borderRadius: RING / 2,
    borderWidth: 1.5,
    alignItems: 'center',
    justifyContent: 'center',
  },
  ringDot: { width: 4, height: 4, borderRadius: 2 },
  centre: { alignItems: 'center', justifyContent: 'center' },
  countdown: {
    color: '#FFFFFF',
    fontSize: 120,
    lineHeight: 130,
    textShadowColor: 'rgba(0,0,0,0.5)',
    textShadowRadius: 18,
  },
  pill: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
    paddingHorizontal: 14,
    height: 34,
    borderRadius: 17,
    backgroundColor: 'rgba(10,9,17,0.72)',
    borderWidth: StyleSheet.hairlineWidth * 2,
    borderColor: 'rgba(255,255,255,0.16)',
    maxWidth: 320,
  },
});
