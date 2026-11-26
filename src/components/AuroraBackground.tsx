import React from 'react';
import { Image, StyleSheet, View } from 'react-native';

import { useLucid } from '../services/lucid/LucidService';
import { useTheme } from '../theme';
import { VideoView } from './VideoView';

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
  if (theme.lucid) return <LucidBackground />;
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

/**
 * Lucid: the background is the chosen photo or clip (a clip loops, silent),
 * or nothing at all while the window shows the phone's wallpaper — under a
 * veil just strong enough that text on it stays readable (theme/lucid.ts).
 */
export function LucidBackground() {
  const theme = useTheme();
  const media = useLucid(s => s.media);
  const source = useLucid(s => s.source);
  const look = theme.lucid;
  if (!look) return null;
  return (
    <View
      pointerEvents="none"
      style={[StyleSheet.absoluteFill, { overflow: 'hidden' }]}
      accessibilityElementsHidden
      importantForAccessibility="no-hide-descendants"
      testID="lucid-background"
    >
      {media && source === 'video' ? (
        <VideoView source={media} muted loop cover style={StyleSheet.absoluteFill} testID="lucid-video" />
      ) : media ? (
        <Image
          source={{ uri: `file://${media}` }}
          style={StyleSheet.absoluteFill}
          resizeMode="cover"
          fadeDuration={0}
          testID="lucid-picture"
        />
      ) : null}
      <View
        style={[StyleSheet.absoluteFill, { backgroundColor: look.veilColor, opacity: look.veil }]}
        testID="lucid-veil"
      />
      {/* Titles and the status bar sit at the top, over whatever the picture has there: a soft
          fade in the veil's colour keeps them readable however busy that part is. */}
      <View
        style={[
          styles.headFade,
          {
            backgroundImage: `linear-gradient(180deg, ${fade(look.veilColor, 0.82)} 0%, ${fade(look.veilColor, 0)} 100%)`,
          },
        ]}
        testID="lucid-head-fade"
      />
    </View>
  );
}

const fade = (hex: string, a: number) => {
  const n = parseInt(hex.slice(1, 7), 16);
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a})`;
};

const styles = StyleSheet.create({
  headFade: { position: 'absolute', top: 0, left: 0, right: 0, height: 190 },
});
