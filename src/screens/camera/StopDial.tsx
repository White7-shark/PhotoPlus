import React, { useEffect, useRef, useState } from 'react';
import {
  ScrollView,
  StyleSheet,
  View,
  type AccessibilityActionEvent,
  type LayoutChangeEvent,
  type NativeScrollEvent,
  type NativeSyntheticEvent,
  type ScrollViewInstance,
} from 'react-native';

import { AppText } from '../../components';
import { nearestIndex } from '../../services/camera/model';
import { useTheme } from '../../theme';

const ITEM = 58;

export interface StopDialProps {
  /** Accessible name, e.g. "Shutter speed". */
  label: string;
  values: readonly number[];
  /** Current value, or null when the control is on automatic. */
  value: number | null;
  format: (value: number) => string;
  onChange: (value: number) => void;
  /** Shown in the centre when value is null (e.g. live auto reading). */
  autoLabel?: string;
  testID?: string;
}

/**
 * A camera-style ruler dial: swipe to a stop, it snaps under the centre mark.
 * Natively scrolled (no JS per frame) and exposed to screen readers as an
 * adjustable control with increment / decrement actions.
 */
export function StopDial({ label, values, value, format, onChange, autoLabel, testID }: StopDialProps) {
  const theme = useTheme();
  const scroll = useRef<ScrollViewInstance>(null);
  const [width, setWidth] = useState(0);
  const dragging = useRef(false);
  const index = value === null || values.length === 0 ? -1 : nearestIndex(values, value);

  // Follow external changes (presets, reset, clamping) when not being dragged.
  useEffect(() => {
    if (!dragging.current && width > 0 && index >= 0) {
      scroll.current?.scrollTo({ x: index * ITEM, animated: true });
    }
  }, [index, width]);

  const settle = (e: NativeSyntheticEvent<NativeScrollEvent>) => {
    dragging.current = false;
    const i = Math.max(0, Math.min(values.length - 1, Math.round(e.nativeEvent.contentOffset.x / ITEM)));
    if (i !== index) {
      onChange(values[i]);
    }
  };

  const onAction = (e: AccessibilityActionEvent) => {
    const from = index < 0 ? Math.floor(values.length / 2) : index;
    const to = e.nativeEvent.actionName === 'increment' ? from + 1 : from - 1;
    if (to >= 0 && to < values.length) {
      onChange(values[to]);
    }
  };

  const current = index >= 0 ? format(values[index]) : (autoLabel ?? 'Auto');

  return (
    <View
      testID={testID}
      accessible
      accessibilityRole="adjustable"
      accessibilityLabel={label}
      accessibilityValue={{ text: current }}
      accessibilityActions={[{ name: 'increment' }, { name: 'decrement' }]}
      onAccessibilityAction={onAction}
      onLayout={(e: LayoutChangeEvent) => setWidth(e.nativeEvent.layout.width)}
      style={styles.root}
    >
      <AppText variant="bodyStrong" align="center" style={{ color: index >= 0 ? theme.colors.premium : '#FFFFFF' }}>
        {current}
      </AppText>
      <View style={styles.track}>
        {width > 0 ? (
          <ScrollView
            ref={scroll}
            horizontal
            showsHorizontalScrollIndicator={false}
            snapToInterval={ITEM}
            decelerationRate="fast"
            contentContainerStyle={{ paddingHorizontal: (width - ITEM) / 2 }}
            contentOffset={{ x: Math.max(0, index) * ITEM, y: 0 }}
            onScrollBeginDrag={() => {
              dragging.current = true;
            }}
            onMomentumScrollEnd={settle}
            importantForAccessibility="no-hide-descendants"
          >
            {values.map((v, i) => (
              <View key={`${v}`} style={styles.item}>
                <View style={[styles.tick, i % 3 === 0 ? styles.tickMajor : null]} />
                <AppText
                  variant="caption"
                  numberOfLines={1}
                  style={{ color: i === index ? '#FFFFFF' : 'rgba(255,255,255,0.55)', fontSize: 11 }}
                >
                  {format(v)}
                </AppText>
              </View>
            ))}
          </ScrollView>
        ) : null}
        <View
          pointerEvents="none"
          style={[styles.centre, { left: width / 2 - 1, backgroundColor: theme.colors.premium }]}
        />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: { gap: 6, paddingVertical: 4 },
  track: { height: 44, justifyContent: 'center' },
  item: { width: ITEM, alignItems: 'center', gap: 4 },
  tick: { width: 1.5, height: 10, borderRadius: 1, backgroundColor: 'rgba(255,255,255,0.45)' },
  tickMajor: { height: 16, backgroundColor: 'rgba(255,255,255,0.8)' },
  centre: { position: 'absolute', top: 0, width: 2, height: 20, borderRadius: 1 },
});
