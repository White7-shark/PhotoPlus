import React, { useState } from 'react';
import { Image, StyleSheet, View, type AccessibilityActionEvent, type GestureResponderEvent } from 'react-native';

import { useTheme } from '../../theme';

const MAX_HEIGHT = 460;

/**
 * Before and after: the painting, with the photo revealed from the left as
 * far as a finger drags the divider (screen readers step it). With a frame,
 * the photo sits inside the frame's opening so the two line up.
 */
export function CompareSlider({
  painting,
  photo,
  inset,
  label,
  hint,
  testID,
}: {
  painting: { uri: string; width: number; height: number };
  photo: string;
  /** The painting's inset inside its frame, as a share of the framed width and height. */
  inset: { x: number; y: number };
  label: string;
  hint: string;
  testID?: string;
}) {
  const theme = useTheme();
  const [width, setWidth] = useState(0);
  const [split, setSplit] = useState(0);
  const aspect = painting.width / painting.height;
  const boxW = width > 0 ? Math.min(width, MAX_HEIGHT * aspect) : 0;
  const boxH = boxW / aspect;
  const inX = inset.x * boxW;
  const inY = inset.y * boxH;
  const innerW = boxW - 2 * inX;
  const innerH = boxH - 2 * inY;
  const reveal = Math.min(innerW, Math.max(0, split * boxW - inX));
  const at = (e: GestureResponderEvent) => {
    if (boxW > 0) setSplit(Math.min(1, Math.max(0, e.nativeEvent.locationX / boxW)));
  };
  const onAction = (e: AccessibilityActionEvent) =>
    setSplit(s => Math.min(1, Math.max(0, s + (e.nativeEvent.actionName === 'increment' ? 0.1 : -0.1))));
  const handleX = Math.max(10, Math.min(boxW - 10, split * boxW));

  return (
    <View
      style={{ width: '100%', alignItems: 'center' }}
      onLayout={e => setWidth(e.nativeEvent.layout.width)}
      testID={testID ? `${testID}-area` : undefined}
    >
      <View
        style={{ width: boxW || '100%', height: boxH || 260 }}
        accessible
        accessibilityRole="adjustable"
        accessibilityLabel={label}
        accessibilityHint={hint}
        accessibilityValue={{ min: 0, max: 100, now: Math.round(split * 100) }}
        accessibilityActions={[{ name: 'increment' }, { name: 'decrement' }]}
        onAccessibilityAction={onAction}
        onStartShouldSetResponder={() => true}
        onMoveShouldSetResponder={() => true}
        onResponderTerminationRequest={() => false}
        onResponderGrant={at}
        onResponderMove={at}
        testID={testID}
      >
        <Image
          pointerEvents="none"
          source={{ uri: painting.uri }}
          style={[StyleSheet.absoluteFill, { borderRadius: 14 }]}
          resizeMode="contain"
          testID="masters-painting"
        />
        {reveal > 0 ? (
          <View
            pointerEvents="none"
            style={{ position: 'absolute', left: inX, top: inY, width: reveal, height: innerH, overflow: 'hidden' }}
          >
            <Image
              source={{ uri: photo }}
              style={{ width: innerW, height: innerH }}
              resizeMode="cover"
              testID="masters-before"
            />
          </View>
        ) : null}
        {boxW > 0 ? (
          <View
            pointerEvents="none"
            style={[styles.divider, { left: handleX - 1, backgroundColor: theme.colors.accent }]}
          >
            <View style={[styles.knob, { borderColor: theme.colors.accent }]} />
          </View>
        ) : null}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  divider: { position: 'absolute', top: 0, bottom: 0, width: 2, alignItems: 'center', justifyContent: 'center' },
  knob: {
    width: 26,
    height: 26,
    borderRadius: 13,
    borderWidth: 2,
    backgroundColor: 'rgba(0,0,0,0.55)',
  },
});
