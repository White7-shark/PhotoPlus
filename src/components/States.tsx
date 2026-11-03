import React, { useEffect, useState } from 'react';
import { ActivityIndicator, Animated, View, type ViewStyle } from 'react-native';

import { useReducedMotion } from '../hooks/useReducedMotion';
import { useTheme } from '../theme';
import { AppText } from './AppText';
import { Button } from './Button';
import { GlassSurface } from './GlassSurface';
import { Icon, type IconName } from './Icon';
import { LogoMark } from './Logo';

/** Standard loading state; announces itself to screen readers. */
export function LoadingState({ label = 'Loading…', style }: { label?: string; style?: ViewStyle }) {
  const theme = useTheme();
  return (
    <View
      style={[{ alignItems: 'center', justifyContent: 'center', padding: 32, gap: 12 }, style]}
      accessible
      accessibilityRole="progressbar"
      accessibilityLabel={label}
      accessibilityLiveRegion="polite"
    >
      <ActivityIndicator color={theme.colors.accent} size="large" />
      <AppText variant="callout" tone="secondary">
        {label}
      </AppText>
    </View>
  );
}

/** Shimmering placeholder block for content that is still loading. */
export function Skeleton({
  height = 16,
  width = '100%',
  radius = 8,
  style,
}: {
  height?: number;
  width?: number | `${number}%`;
  radius?: number;
  style?: ViewStyle;
}) {
  const theme = useTheme();
  const reduced = useReducedMotion();
  const [pulse] = useState(() => new Animated.Value(0.5));
  useEffect(() => {
    if (reduced) {
      return;
    }
    const loop = Animated.loop(
      Animated.sequence([
        Animated.timing(pulse, { toValue: 1, duration: 700, useNativeDriver: true }),
        Animated.timing(pulse, { toValue: 0.5, duration: 700, useNativeDriver: true }),
      ]),
    );
    loop.start();
    return () => loop.stop();
  }, [pulse, reduced]);
  return (
    <Animated.View
      importantForAccessibility="no"
      style={[
        { height, width, borderRadius: radius, backgroundColor: theme.colors.glassFillStrong, opacity: pulse },
        style,
      ]}
    />
  );
}

interface MessageStateProps {
  icon?: IconName;
  title: string;
  message?: string;
  actionLabel?: string;
  onAction?: () => void;
  secondaryLabel?: string;
  onSecondary?: () => void;
  tone?: 'neutral' | 'error';
  style?: ViewStyle;
}

function MessageState({
  icon,
  title,
  message,
  actionLabel,
  onAction,
  secondaryLabel,
  onSecondary,
  tone = 'neutral',
  style,
}: MessageStateProps) {
  const theme = useTheme();
  const tint = tone === 'error' ? theme.colors.danger : theme.colors.accent;
  return (
    <GlassSurface
      style={[{ alignItems: 'center', padding: 28, gap: 12 }, style ?? {}]}
      accessibilityLiveRegion={tone === 'error' ? 'assertive' : 'none'}
    >
      {icon ? (
        <View
          style={{
            width: 64,
            height: 64,
            borderRadius: 32,
            alignItems: 'center',
            justifyContent: 'center',
            backgroundColor: tone === 'error' ? 'rgba(255,107,107,0.12)' : 'rgba(255,122,69,0.12)',
            borderWidth: 1,
            borderColor: tone === 'error' ? 'rgba(255,107,107,0.35)' : 'rgba(255,122,69,0.35)',
          }}
        >
          <Icon name={icon} size={28} color={tint} />
        </View>
      ) : null}
      <AppText variant="title3" align="center" accessibilityRole="header">
        {title}
      </AppText>
      {message ? (
        <AppText variant="body" tone="secondary" align="center" style={{ maxWidth: 360 }}>
          {message}
        </AppText>
      ) : null}
      {actionLabel && onAction ? (
        <Button
          label={actionLabel}
          onPress={onAction}
          variant={tone === 'error' ? 'glass' : 'primary'}
          style={{ marginTop: 8 }}
        />
      ) : null}
      {secondaryLabel && onSecondary ? <Button label={secondaryLabel} onPress={onSecondary} variant="ghost" /> : null}
    </GlassSurface>
  );
}

export function EmptyState(props: Omit<MessageStateProps, 'tone'>) {
  return <MessageState {...props} tone="neutral" />;
}

export function ErrorState({
  retryLabel = 'Try again',
  onRetry,
  ...props
}: Omit<MessageStateProps, 'tone' | 'actionLabel' | 'onAction'> & { onRetry?: () => void; retryLabel?: string }) {
  return (
    <MessageState
      icon="alert"
      {...props}
      actionLabel={onRetry ? retryLabel : undefined}
      onAction={onRetry}
      tone="error"
    />
  );
}

/** Full-screen branded splash used while the app boots. */
export function SplashState({ label }: { label: string }) {
  return (
    <View
      style={{ flex: 1, alignItems: 'center', justifyContent: 'center', gap: 20 }}
      accessible
      accessibilityLabel={label}
      accessibilityLiveRegion="polite"
    >
      <LogoMark size={96} />
      <ActivityIndicator color="#FFFFFF" />
      <AppText variant="callout" tone="secondary">
        {label}
      </AppText>
    </View>
  );
}
