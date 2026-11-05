import React, { useState } from 'react';
import { View, type AccessibilityActionEvent, type GestureResponderEvent, type LayoutChangeEvent } from 'react-native';

import { useTheme } from '../theme';
import { AppText } from './AppText';

/**
 * A labelled slider (drag or tap anywhere on the track). Accessible as an
 * adjustable control: screen readers can step it up and down. `onChange`
 * fires while dragging; `onEnd` once the finger lifts (one undo step).
 */
export function ValueSlider({
  label,
  value,
  min = 0,
  max = 1,
  step = 0.05,
  onChange,
  onEnd,
  format,
  testID,
  disabled,
}: {
  label: string;
  value: number;
  min?: number;
  max?: number;
  step?: number;
  onChange: (v: number) => void;
  onEnd?: () => void;
  /** How the value reads (e.g. "+25"). */
  format?: (v: number) => string;
  testID?: string;
  disabled?: boolean;
}) {
  const theme = useTheme();
  const [width, setWidth] = useState(0);
  const clamp = (v: number) => Math.min(max, Math.max(min, v));
  const fromEvent = (e: GestureResponderEvent) =>
    width > 0 ? min + (max - min) * Math.min(1, Math.max(0, e.nativeEvent.locationX / width)) : min;
  const t = max > min ? (clamp(value) - min) / (max - min) : 0;
  const shown = format ? format(value) : `${Math.round(value * 100)}`;
  const onAction = (e: AccessibilityActionEvent) => {
    const next = clamp(value + (e.nativeEvent.actionName === 'increment' ? step : -step));
    onChange(next);
    onEnd?.();
  };
  return (
    <View
      style={{ gap: 6, opacity: disabled ? 0.45 : 1 }}
      accessible
      accessibilityRole="adjustable"
      accessibilityLabel={label}
      accessibilityValue={{ text: shown }}
      accessibilityActions={[{ name: 'increment' }, { name: 'decrement' }]}
      onAccessibilityAction={onAction}
      testID={testID}
    >
      <View style={{ flexDirection: 'row', justifyContent: 'space-between' }}>
        <AppText variant="caption" tone="secondary">
          {label}
        </AppText>
        <AppText variant="caption">{shown}</AppText>
      </View>
      <View
        onStartShouldSetResponder={() => !disabled}
        onMoveShouldSetResponder={() => !disabled}
        onResponderTerminationRequest={() => false}
        onResponderGrant={e => onChange(fromEvent(e))}
        onResponderMove={e => onChange(fromEvent(e))}
        onResponderRelease={() => onEnd?.()}
        onResponderTerminate={() => onEnd?.()}
        onLayout={(e: LayoutChangeEvent) => setWidth(e.nativeEvent.layout.width)}
        style={{ height: 28, justifyContent: 'center' }}
      >
        <View pointerEvents="none" style={{ height: 4, borderRadius: 2, backgroundColor: theme.colors.glassStroke }} />
        <View
          pointerEvents="none"
          style={{
            position: 'absolute',
            left: 0,
            width: Math.max(0, t * width),
            height: 4,
            borderRadius: 2,
            backgroundColor: theme.colors.accent,
          }}
        />
        <View
          pointerEvents="none"
          style={{
            position: 'absolute',
            left: Math.max(0, t * width - 11),
            width: 22,
            height: 22,
            borderRadius: 11,
            backgroundColor: '#fff',
            borderWidth: 2,
            borderColor: theme.colors.accent,
          }}
        />
      </View>
    </View>
  );
}
