import React, { useRef } from 'react';
import { ActivityIndicator, Animated, Pressable, StyleSheet, View, type ViewStyle } from 'react-native';

import { useReducedMotion } from '../hooks/useReducedMotion';
import { useTheme } from '../theme';
import { AppText } from './AppText';
import { Icon, type IconName } from './Icon';

export type ButtonVariant = 'primary' | 'glass' | 'ghost' | 'premium' | 'danger';

export interface ButtonProps {
  label: string;
  onPress?: () => void;
  variant?: ButtonVariant;
  size?: 'md' | 'lg';
  icon?: IconName;
  loading?: boolean;
  disabled?: boolean;
  fullWidth?: boolean;
  accessibilityHint?: string;
  testID?: string;
  style?: ViewStyle;
}

export function usePressScale() {
  const reduced = useReducedMotion();
  const theme = useTheme();
  const scale = useRef(new Animated.Value(1)).current;
  const animate = (to: number) =>
    reduced
      ? undefined
      : Animated.spring(scale, { toValue: to, useNativeDriver: true, speed: 40, bounciness: 6 }).start();
  return {
    scale,
    onPressIn: () => animate(theme.motion.pressScale),
    onPressOut: () => animate(1),
  };
}

export function Button({
  label,
  onPress,
  variant = 'primary',
  size = 'md',
  icon,
  loading = false,
  disabled = false,
  fullWidth = false,
  accessibilityHint,
  testID,
  style,
}: ButtonProps) {
  const theme = useTheme();
  const { scale, onPressIn, onPressOut } = usePressScale();
  const inactive = disabled || loading;

  const surfaces: Record<ButtonVariant, ViewStyle> = {
    primary: { backgroundImage: theme.gradients.action, boxShadow: `0 10px 28px -10px ${theme.colors.accentAlt}` },
    premium: { backgroundImage: theme.gradients.premium, boxShadow: `0 10px 28px -12px ${theme.colors.premium}` },
    glass: {
      backgroundColor: theme.colors.glassFillStrong,
      borderWidth: StyleSheet.hairlineWidth * 2,
      borderColor: theme.colors.glassStroke,
      backgroundImage: theme.mode === 'dark' ? theme.gradients.glassSheen : theme.gradients.glassSheenLight,
    },
    ghost: { backgroundColor: 'transparent' },
    danger: { backgroundColor: 'rgba(255,107,107,0.14)', borderWidth: 1, borderColor: 'rgba(255,107,107,0.45)' },
  };
  const labelTone = variant === 'premium' ? 'inverse' : variant === 'danger' ? 'danger' : variant === 'ghost' ? 'accent' : 'primary';
  const labelColor =
    variant === 'premium'
      ? '#1A1206'
      : variant === 'primary'
        ? '#FFFFFF'
        : variant === 'danger'
          ? theme.colors.danger
          : variant === 'ghost'
            ? theme.colors.accent
            : theme.colors.textPrimary;

  return (
    <Animated.View style={[{ transform: [{ scale }] }, fullWidth && { alignSelf: 'stretch' }, style]}>
      <Pressable
        testID={testID}
        onPress={onPress}
        onPressIn={onPressIn}
        onPressOut={onPressOut}
        disabled={inactive}
        accessibilityRole="button"
        accessibilityLabel={label}
        accessibilityHint={accessibilityHint}
        accessibilityState={{ disabled: inactive, busy: loading }}
        android_ripple={{ color: 'rgba(255,255,255,0.18)', borderless: false }}
        style={[
          styles.base,
          {
            minHeight: size === 'lg' ? 56 : theme.layout.minTouchTarget,
            paddingHorizontal: size === 'lg' ? 26 : 20,
            borderRadius: theme.radii.pill,
            opacity: disabled ? 0.45 : 1,
          },
          surfaces[variant],
        ]}
      >
        <View style={styles.content}>
          {loading ? (
            <ActivityIndicator color={labelColor} size="small" />
          ) : icon ? (
            <Icon name={icon} size={size === 'lg' ? 20 : 18} color={labelColor} strokeWidth={2} />
          ) : null}
          <AppText
            variant={size === 'lg' ? 'bodyStrong' : 'callout'}
            tone={labelTone}
            style={{ color: labelColor, fontWeight: '700', letterSpacing: 0.2 }}
            numberOfLines={1}
          >
            {label}
          </AppText>
        </View>
      </Pressable>
    </Animated.View>
  );
}

export interface IconButtonProps {
  icon: IconName;
  label: string;
  onPress?: () => void;
  size?: number;
  active?: boolean;
  disabled?: boolean;
  tint?: string;
  testID?: string;
}

/** Circular glass control; always ≥ 48dp and always labelled. */
export function IconButton({ icon, label, onPress, size = 48, active, disabled, tint, testID }: IconButtonProps) {
  const theme = useTheme();
  const { scale, onPressIn, onPressOut } = usePressScale();
  const dim = Math.max(size, theme.layout.minTouchTarget);
  return (
    <Animated.View style={{ transform: [{ scale }] }}>
      <Pressable
        testID={testID}
        onPress={onPress}
        onPressIn={onPressIn}
        onPressOut={onPressOut}
        disabled={disabled}
        accessibilityRole="button"
        accessibilityLabel={label}
        accessibilityState={{ disabled: !!disabled, selected: active }}
        hitSlop={8}
        style={{
          width: dim,
          height: dim,
          borderRadius: dim / 2,
          alignItems: 'center',
          justifyContent: 'center',
          backgroundColor: active ? 'rgba(255,255,255,0.22)' : theme.colors.glassFill,
          borderWidth: StyleSheet.hairlineWidth * 2,
          borderColor: theme.colors.glassStroke,
          backgroundImage: theme.mode === 'dark' ? theme.gradients.glassSheen : theme.gradients.glassSheenLight,
          opacity: disabled ? 0.45 : 1,
        }}
      >
        <Icon name={icon} size={dim * 0.44} color={tint ?? theme.colors.textPrimary} />
      </Pressable>
    </Animated.View>
  );
}

const styles = StyleSheet.create({
  base: { justifyContent: 'center', alignItems: 'center', overflow: 'hidden' },
  content: { flexDirection: 'row', alignItems: 'center', gap: 8 },
});
