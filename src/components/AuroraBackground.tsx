import React from 'react';
import { StyleSheet, View } from 'react-native';

import { useTheme } from '../theme';

interface Blob {
  color: string;
  size: number;
  top?: number | `${number}%`;
  left?: number | `${number}%`;
  right?: number | `${number}%`;
  bottom?: number | `${number}%`;
  opacity: number;
}

/**
 * Soft light leaks behind the glass — the "golden hour into blue hour"
 * atmosphere. Each blob is a radial gradient; on Android 12+ a native blur
 * filter softens them further. Purely decorative, hidden from screen readers.
 */
export function AuroraBackground({ variant = 'default' }: { variant?: 'default' | 'camera' | 'calm' }) {
  const theme = useTheme();
  const { auroraA, auroraB, auroraC } = theme.colors;
  const strength = theme.mode === 'dark' ? 1 : 0.8;
  const blobs: Blob[] =
    variant === 'calm'
      ? [
          { color: auroraB, size: 420, top: -160, right: -140, opacity: 0.35 * strength },
          { color: auroraC, size: 380, bottom: -160, left: -120, opacity: 0.3 * strength },
        ]
      : [
          { color: auroraA, size: 460, top: -170, left: -150, opacity: 0.55 * strength },
          { color: auroraB, size: 400, top: '30%', right: -190, opacity: 0.42 * strength },
          { color: auroraC, size: 480, bottom: -220, left: '10%', opacity: 0.45 * strength },
        ];
  return (
    <View
      pointerEvents="none"
      style={[StyleSheet.absoluteFill, { backgroundColor: theme.colors.background, overflow: 'hidden' }]}
      accessibilityElementsHidden
      importantForAccessibility="no-hide-descendants"
    >
      {blobs.map((b, i) => (
        <View
          key={i}
          style={{
            position: 'absolute',
            width: b.size,
            height: b.size,
            top: b.top,
            left: b.left,
            right: b.right,
            bottom: b.bottom,
            opacity: b.opacity,
            borderRadius: b.size / 2,
            backgroundImage: `radial-gradient(circle at center, ${b.color} 0%, ${b.color} 18%, transparent 70%)`,
            filter: [{ blur: 40 }],
          }}
        />
      ))}
      {/* Film grain-like vignette to keep text contrast stable at the edges */}
      <View
        style={[
          StyleSheet.absoluteFill,
          {
            backgroundImage:
              theme.mode === 'dark'
                ? 'radial-gradient(ellipse at center, rgba(5,5,10,0) 40%, rgba(5,5,10,0.55) 100%)'
                : 'radial-gradient(ellipse at center, rgba(243,241,248,0) 50%, rgba(243,241,248,0.6) 100%)',
          },
        ]}
      />
    </View>
  );
}
