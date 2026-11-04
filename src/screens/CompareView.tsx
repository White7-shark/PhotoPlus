import React, { useRef, useState } from 'react';
import { Image, StyleSheet, View, type GestureResponderEvent, type LayoutChangeEvent } from 'react-native';

import { AppText } from '../components';
import { useTheme } from '../theme';

export interface CompareViewProps {
  beforeUri: string;
  afterUri: string;
  /** Display aspect ratio of the photo (width / height). */
  aspect: number;
  beforeLabel?: string;
  afterLabel?: string;
}

/** Clamps the divider so both labels stay reachable. */
export function clampSplit(value: number): number {
  return Math.min(0.98, Math.max(0.02, value));
}

/**
 * Before / after comparison: drag the divider (or use screen-reader
 * increment / decrement) to reveal the enhanced photo over the original.
 */
export function CompareView({
  beforeUri,
  afterUri,
  aspect,
  beforeLabel = 'Before',
  afterLabel = 'After',
}: CompareViewProps) {
  const theme = useTheme();
  const [width, setWidth] = useState(0);
  const [split, setSplit] = useState(0.5);
  const widthRef = useRef(0);

  const grant = useRef({ pageX: 0, split: 0.5 });

  // The View responder system is enough for one horizontal drag.
  const onGrant = (e: GestureResponderEvent) => {
    const w = widthRef.current;
    if (w > 0) {
      const next = clampSplit(e.nativeEvent.locationX / w);
      grant.current = { pageX: e.nativeEvent.pageX, split: next };
      setSplit(next);
    }
  };
  const onMove = (e: GestureResponderEvent) => {
    const w = widthRef.current;
    if (w > 0) {
      setSplit(clampSplit(grant.current.split + (e.nativeEvent.pageX - grant.current.pageX) / w));
    }
  };

  const onLayout = (e: LayoutChangeEvent) => {
    widthRef.current = e.nativeEvent.layout.width;
    setWidth(e.nativeEvent.layout.width);
  };
  const height = width > 0 ? width / aspect : 0;

  return (
    <View
      onLayout={onLayout}
      style={[styles.root, { height: height || undefined, aspectRatio: height ? undefined : aspect }]}
      accessible
      accessibilityRole="adjustable"
      accessibilityLabel="Before and after comparison"
      accessibilityValue={{ text: `${Math.round(split * 100)} percent original` }}
      accessibilityActions={[{ name: 'increment' }, { name: 'decrement' }]}
      onAccessibilityAction={e =>
        setSplit(s => clampSplit(s + (e.nativeEvent.actionName === 'increment' ? 0.1 : -0.1)))
      }
      onStartShouldSetResponder={() => true}
      onMoveShouldSetResponder={() => true}
      onResponderTerminationRequest={() => false}
      onResponderGrant={onGrant}
      onResponderMove={onMove}
    >
      {width > 0 ? (
        <>
          <Image source={{ uri: afterUri }} style={{ width, height }} resizeMode="contain" />
          <View style={[styles.clip, { width: width * split, height }]}>
            <Image source={{ uri: beforeUri }} style={{ width, height }} resizeMode="contain" />
          </View>
          <View pointerEvents="none" style={[styles.divider, { left: width * split - 1, height }]}>
            <View style={[styles.handle, { borderColor: theme.colors.premium }]}>
              <AppText variant="caption" style={{ color: '#FFFFFF', fontWeight: '800' }}>
                ‹ ›
              </AppText>
            </View>
          </View>
          <View pointerEvents="none" style={[styles.label, { left: 10 }]}>
            <AppText variant="overline" style={styles.labelText}>
              {beforeLabel}
            </AppText>
          </View>
          <View pointerEvents="none" style={[styles.label, { right: 10 }]}>
            <AppText variant="overline" style={[styles.labelText, { color: theme.colors.premium }]}>
              {afterLabel}
            </AppText>
          </View>
        </>
      ) : null}
    </View>
  );
}

const styles = StyleSheet.create({
  root: { width: '100%', overflow: 'hidden', borderRadius: 18, backgroundColor: '#000' },
  clip: { position: 'absolute', left: 0, top: 0, overflow: 'hidden' },
  divider: {
    position: 'absolute',
    top: 0,
    width: 2,
    backgroundColor: '#FFFFFF',
    alignItems: 'center',
    justifyContent: 'center',
  },
  handle: {
    width: 40,
    height: 40,
    borderRadius: 20,
    borderWidth: 2,
    backgroundColor: 'rgba(10,9,17,0.75)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  label: {
    position: 'absolute',
    top: 10,
    paddingHorizontal: 8,
    paddingVertical: 3,
    borderRadius: 10,
    backgroundColor: 'rgba(10,9,17,0.6)',
  },
  labelText: { color: '#FFFFFF' },
});
