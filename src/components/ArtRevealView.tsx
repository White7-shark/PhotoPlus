import React from 'react';
import { StyleSheet, View, type ViewStyle } from 'react-native';

import NativeArtRevealView from '../native/specs/PhotoPlusArtRevealViewNativeComponent';

export interface ArtRevealViewProps {
  work: string;
  blank: string;
  tool: 'pencil' | 'brush';
  artist: number;
  inner?: readonly [number, number, number, number];
  durationMs: number;
  playing?: boolean;
  /** Jump to the finished work. */
  finish?: boolean;
  onReady?: (strokes: number) => void;
  onDone?: () => void;
  onError?: (message: string) => void;
  accessibilityLabel: string;
  style?: ViewStyle;
  testID?: string;
}

/** The live studio: the work drawn or painted stroke by stroke (Kotlin: com.photoplus.studio.ArtRevealView). */
export function ArtRevealView({
  work,
  blank,
  tool,
  artist,
  inner,
  durationMs,
  playing = true,
  finish = false,
  onReady,
  onDone,
  onError,
  accessibilityLabel,
  style,
  testID = 'art-reveal',
}: ArtRevealViewProps) {
  return (
    <View style={[styles.box, style]} accessible accessibilityLabel={accessibilityLabel}>
      <NativeArtRevealView
        testID={testID}
        style={StyleSheet.absoluteFill}
        work={work}
        blank={blank}
        tool={tool === 'brush' ? 1 : 0}
        artist={artist}
        inner={inner ? [...inner] : undefined}
        durationMs={durationMs}
        playing={playing}
        finish={finish}
        onRevealReady={e => onReady?.(e.nativeEvent.strokes)}
        onRevealDone={() => onDone?.()}
        onRevealError={e => onError?.(e.nativeEvent.message)}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  box: { overflow: 'hidden' },
});
