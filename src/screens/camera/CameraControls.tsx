import React from 'react';
import { ActivityIndicator, Animated, Image, Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, Icon, usePressScale, type IconName } from '../../components';
import { activeZoomPreset, formatZoom, type ModeDefinition, type UiMode } from '../../services/camera/model';
import { FEATURES } from '../../services/entitlements/features';
import { useTheme } from '../../theme';

const GLASS = 'rgba(16,14,25,0.55)';
const STROKE = 'rgba(255,255,255,0.16)';

/**
 * Keeps icons upright while the UI itself stays portrait-locked. `degrees`
 * comes from the native orientation event: 270 when the phone is turned
 * clockwise (left edge up), so the icon turns 270° (= 90° anticlockwise).
 */
export const uprightStyle = (degrees: number) => ({ transform: [{ rotate: `${degrees}deg` }] });
const rotate = uprightStyle;

export interface ChipProps {
  label?: string;
  icon?: IconName;
  active?: boolean;
  tint?: string;
  accessibilityLabel: string;
  onPress: () => void;
  rotation?: number;
  testID?: string;
}

/** Compact glass control for the camera top bar; ≥ 48dp touch target via hitSlop. */
export function Chip({ label, icon, active, tint, accessibilityLabel, onPress, rotation = 0, testID }: ChipProps) {
  const { scale, onPressIn, onPressOut } = usePressScale();
  const color = tint ?? (active ? '#FFFFFF' : 'rgba(255,255,255,0.86)');
  return (
    <Animated.View style={{ transform: [{ scale }] }}>
      <Pressable
        testID={testID}
        onPress={onPress}
        onPressIn={onPressIn}
        onPressOut={onPressOut}
        accessibilityRole="button"
        accessibilityLabel={accessibilityLabel}
        accessibilityState={{ selected: !!active }}
        hitSlop={{ top: 6, bottom: 6, left: 4, right: 4 }}
        style={[styles.chip, { backgroundColor: active ? 'rgba(255,255,255,0.2)' : GLASS, minWidth: label ? 48 : 40 }]}
      >
        <View style={[styles.chipInner, rotate(rotation)]}>
          {icon ? <Icon name={icon} size={18} color={color} strokeWidth={2} /> : null}
          {label ? (
            <AppText variant="caption" style={{ color, fontWeight: '700' }} maxFontSizeMultiplier={1.3}>
              {label}
            </AppText>
          ) : null}
        </View>
      </Pressable>
    </Animated.View>
  );
}

export function ZoomPills({
  presets,
  zoom,
  onSelect,
  rotation,
}: {
  presets: number[];
  zoom: number;
  onSelect: (ratio: number) => void;
  rotation: number;
}) {
  const theme = useTheme();
  const active = activeZoomPreset(presets, zoom);
  return (
    <View style={styles.zoomRow} accessibilityRole="radiogroup" accessibilityLabel="Zoom">
      {presets.map(p => {
        const selected = p === active;
        return (
          <Pressable
            key={p}
            onPress={() => onSelect(p)}
            accessibilityRole="radio"
            accessibilityLabel={`Zoom ${formatZoom(p)}`}
            accessibilityState={{ selected }}
            hitSlop={8}
            style={[styles.zoomPill, selected ? { width: 44, height: 44, borderColor: theme.colors.premium } : null]}
          >
            <AppText
              variant="caption"
              style={[{ color: selected ? theme.colors.premium : '#FFFFFF', fontWeight: '800' }, rotate(rotation)]}
              maxFontSizeMultiplier={1.2}
            >
              {selected ? formatZoom(zoom) : formatZoom(p).replace('×', '')}
            </AppText>
          </Pressable>
        );
      })}
    </View>
  );
}

export function ModeSwitcher({
  modes,
  value,
  onChange,
}: {
  modes: ModeDefinition[];
  value: UiMode;
  onChange: (mode: UiMode) => void;
}) {
  const theme = useTheme();
  return (
    <ScrollView
      horizontal
      showsHorizontalScrollIndicator={false}
      contentContainerStyle={styles.modes}
      accessibilityRole="tablist"
      accessibilityLabel="Capture mode"
    >
      {modes.map(m => {
        const selected = m.id === value;
        const premium = FEATURES[m.feature].plan === 'premium';
        return (
          <Pressable
            key={m.id}
            testID={`mode-${m.id}`}
            onPress={() => onChange(m.id)}
            accessibilityRole="tab"
            accessibilityLabel={`${m.label}${premium ? ', premium' : ''}`}
            accessibilityHint={m.description}
            accessibilityState={{ selected }}
            hitSlop={{ top: 12, bottom: 12 }}
            style={[styles.mode, selected ? { backgroundColor: 'rgba(255,255,255,0.14)' } : null]}
          >
            {premium ? <Icon name="crown" size={11} color={theme.colors.premium} strokeWidth={2.2} /> : null}
            <AppText
              variant="overline"
              numberOfLines={1}
              style={{ color: selected ? theme.colors.premium : 'rgba(255,255,255,0.78)', letterSpacing: 1.4 }}
              maxFontSizeMultiplier={1.3}
            >
              {m.label.toUpperCase()}
            </AppText>
          </Pressable>
        );
      })}
    </ScrollView>
  );
}

const RECORD_RED = '#FF3B4E';

export function ShutterButton({
  mode,
  disabled,
  busy,
  onPress,
  countdown,
  recording = false,
}: {
  mode: UiMode;
  disabled: boolean;
  busy: boolean;
  onPress: () => void;
  countdown: number | null;
  /** Glide: a clip is recording (the button stops it). */
  recording?: boolean;
}) {
  const theme = useTheme();
  const { scale, onPressIn, onPressOut } = usePressScale();
  const centreIcon: IconName | null =
    mode === 'night' ? 'moon' : mode === 'superres' ? 'layers' : mode === 'enhance' ? 'sparkles' : null;
  return (
    <Animated.View style={{ transform: [{ scale }] }}>
      <Pressable
        testID="shutter"
        accessibilityRole="button"
        accessibilityLabel={
          countdown !== null
            ? 'Cancel timer'
            : mode === 'glide'
              ? recording
                ? 'Stop recording'
                : 'Record a Glide clip'
              : 'Take photo'
        }
        accessibilityState={{ disabled, busy }}
        disabled={disabled}
        onPress={onPress}
        onPressIn={onPressIn}
        onPressOut={onPressOut}
        style={[
          styles.shutterOuter,
          { borderColor: mode === 'pro' ? theme.colors.premium : '#FFFFFF', opacity: disabled ? 0.45 : 1 },
        ]}
      >
        <View
          style={[
            styles.shutterInner,
            mode === 'glide'
              ? {
                  backgroundColor: RECORD_RED,
                  // A red dot to record; a rounded square to stop.
                  borderRadius: recording ? 10 : 32,
                  transform: [{ scale: recording ? 0.55 : busy ? 0.84 : 1 }],
                }
              : {
                  backgroundImage: mode === 'photo' || mode === 'pro' ? undefined : theme.gradients.spectrum,
                  transform: [{ scale: busy ? 0.84 : 1 }],
                },
          ]}
        >
          {countdown !== null ? (
            <AppText variant="title2" style={{ color: '#0A0911' }}>
              {countdown}
            </AppText>
          ) : busy ? (
            <ActivityIndicator color={mode === 'photo' || mode === 'pro' ? '#0A0911' : '#FFFFFF'} />
          ) : centreIcon ? (
            <Icon name={centreIcon} size={24} color="#FFFFFF" strokeWidth={2} />
          ) : null}
        </View>
      </Pressable>
    </Animated.View>
  );
}

export function LastShot({
  path,
  progress,
  onPress,
  rotation,
}: {
  path: string | null;
  progress: number | null;
  onPress: () => void;
  rotation: number;
}) {
  const theme = useTheme();
  return (
    <Pressable
      onPress={onPress}
      disabled={!path}
      accessibilityRole="button"
      accessibilityLabel={path ? 'Open last photo' : 'No photos yet'}
      accessibilityState={{ disabled: !path, busy: progress !== null }}
      hitSlop={8}
      style={[styles.thumb, rotate(rotation)]}
    >
      {path ? (
        <Image source={{ uri: `file://${path}` }} style={StyleSheet.absoluteFill} resizeMode="cover" />
      ) : (
        <Icon name="image" size={20} color="rgba(255,255,255,0.6)" />
      )}
      {progress !== null ? (
        <View style={[StyleSheet.absoluteFill, styles.thumbBusy]}>
          <ActivityIndicator color={theme.colors.premium} />
        </View>
      ) : null}
    </Pressable>
  );
}

const styles = StyleSheet.create({
  chip: {
    height: 36,
    paddingHorizontal: 10,
    borderRadius: 18,
    borderWidth: StyleSheet.hairlineWidth * 2,
    borderColor: STROKE,
    alignItems: 'center',
    justifyContent: 'center',
  },
  chipInner: { flexDirection: 'row', alignItems: 'center', gap: 5 },
  zoomRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
    paddingHorizontal: 6,
    paddingVertical: 4,
    borderRadius: 26,
    backgroundColor: 'rgba(10,9,17,0.45)',
  },
  zoomPill: {
    width: 36,
    height: 36,
    borderRadius: 22,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(255,255,255,0.12)',
    borderWidth: 1,
    borderColor: 'transparent',
  },
  modes: { paddingHorizontal: 24, gap: 4, alignItems: 'center', flexGrow: 1, justifyContent: 'center' },
  mode: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 4,
    paddingHorizontal: 12,
    paddingVertical: 8,
    borderRadius: 16,
    minHeight: 32,
  },
  shutterOuter: {
    width: 82,
    height: 82,
    borderRadius: 41,
    borderWidth: 4,
    alignItems: 'center',
    justifyContent: 'center',
  },
  shutterInner: {
    width: 64,
    height: 64,
    borderRadius: 32,
    backgroundColor: '#FFFFFF',
    alignItems: 'center',
    justifyContent: 'center',
  },
  thumb: {
    width: 50,
    height: 50,
    borderRadius: 14,
    overflow: 'hidden',
    borderWidth: 1.5,
    borderColor: 'rgba(255,255,255,0.6)',
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(255,255,255,0.08)',
  },
  thumbBusy: { alignItems: 'center', justifyContent: 'center', backgroundColor: 'rgba(5,5,10,0.55)' },
});
