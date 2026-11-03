import React from 'react';
import { Pressable, StyleSheet, Switch, View, type ViewStyle } from 'react-native';

import { useTheme } from '../theme';
import { AppText } from './AppText';
import { usePressScale } from './Button';
import { GlassSurface } from './GlassSurface';
import { Icon, type IconName } from './Icon';
import { Animated } from 'react-native';

export function SectionHeader({
  title,
  action,
  overline,
}: {
  title: string;
  overline?: string;
  action?: React.ReactNode;
}) {
  return (
    <View style={styles.sectionHeader}>
      <View style={{ flex: 1 }}>
        {overline ? (
          <AppText variant="overline" tone="tertiary" style={{ marginBottom: 2 }}>
            {overline.toUpperCase()}
          </AppText>
        ) : null}
        <AppText variant="title3" accessibilityRole="header">
          {title}
        </AppText>
      </View>
      {action}
    </View>
  );
}

/** `media` is for badges placed on photos/artwork: a dark chip that stays legible on any image. */
export function Badge({
  label,
  tone = 'neutral',
  icon,
}: {
  label: string;
  tone?: 'neutral' | 'premium' | 'success' | 'warning' | 'danger' | 'accent' | 'media';
  icon?: IconName;
}) {
  const theme = useTheme();
  const palette = {
    neutral: { bg: theme.colors.glassFillStrong, fg: theme.colors.textSecondary, border: theme.colors.glassStroke },
    premium: { bg: 'rgba(245,198,107,0.16)', fg: theme.colors.premium, border: 'rgba(245,198,107,0.45)' },
    success: { bg: 'rgba(61,220,151,0.14)', fg: theme.colors.success, border: 'rgba(61,220,151,0.4)' },
    warning: { bg: 'rgba(255,193,69,0.14)', fg: theme.colors.warning, border: 'rgba(255,193,69,0.4)' },
    danger: { bg: 'rgba(255,107,107,0.14)', fg: theme.colors.danger, border: 'rgba(255,107,107,0.4)' },
    accent: { bg: 'rgba(255,122,69,0.14)', fg: theme.colors.accent, border: 'rgba(255,122,69,0.4)' },
    media: { bg: 'rgba(10,9,17,0.62)', fg: '#FFFFFF', border: 'rgba(255,255,255,0.22)' },
  }[tone];
  return (
    <View
      style={{
        flexDirection: 'row',
        alignItems: 'center',
        gap: 4,
        alignSelf: 'flex-start',
        paddingHorizontal: 9,
        paddingVertical: 4,
        borderRadius: theme.radii.pill,
        backgroundColor: palette.bg,
        borderWidth: 1,
        borderColor: palette.border,
      }}
    >
      {icon ? <Icon name={icon} size={12} color={palette.fg} strokeWidth={2.2} /> : null}
      <AppText variant="caption" style={{ color: palette.fg, fontWeight: '700' }}>
        {label}
      </AppText>
    </View>
  );
}

export function PremiumBadge() {
  return <Badge label="Premium" tone="premium" icon="crown" />;
}

export interface ListRowProps {
  icon?: IconName;
  iconTint?: string;
  title: string;
  subtitle?: string;
  value?: string;
  onPress?: () => void;
  right?: React.ReactNode;
  accessibilityHint?: string;
  last?: boolean;
}

export function ListRow({
  icon,
  iconTint,
  title,
  subtitle,
  value,
  onPress,
  right,
  accessibilityHint,
  last,
}: ListRowProps) {
  const theme = useTheme();
  const content = (
    <View
      style={[
        styles.row,
        { minHeight: theme.layout.minTouchTarget + 8 },
        !last && { borderBottomWidth: StyleSheet.hairlineWidth, borderBottomColor: theme.colors.divider },
      ]}
    >
      {icon ? (
        <View style={[styles.rowIcon, { backgroundColor: theme.colors.glassFillStrong }]}>
          <Icon name={icon} size={18} color={iconTint ?? theme.colors.textPrimary} />
        </View>
      ) : null}
      <View style={{ flex: 1 }}>
        <AppText variant="bodyStrong">{title}</AppText>
        {subtitle ? (
          <AppText variant="caption" tone="secondary" style={{ marginTop: 2 }}>
            {subtitle}
          </AppText>
        ) : null}
      </View>
      {value ? (
        <AppText variant="callout" tone="secondary" style={{ maxWidth: '45%' }} numberOfLines={1}>
          {value}
        </AppText>
      ) : null}
      {right}
      {onPress ? <Icon name="chevronRight" size={18} color={theme.colors.textTertiary} /> : null}
    </View>
  );
  if (!onPress) {
    return (
      <View accessible accessibilityLabel={[title, subtitle, value].filter(Boolean).join(', ')}>
        {content}
      </View>
    );
  }
  return (
    <Pressable
      onPress={onPress}
      accessibilityRole="button"
      accessibilityLabel={[title, value].filter(Boolean).join(', ')}
      accessibilityHint={accessibilityHint ?? subtitle}
      android_ripple={{ color: 'rgba(255,255,255,0.08)' }}
    >
      {content}
    </Pressable>
  );
}

export function ToggleRow({
  title,
  subtitle,
  value,
  onChange,
  icon,
  last,
}: {
  title: string;
  subtitle?: string;
  value: boolean;
  onChange: (v: boolean) => void;
  icon?: IconName;
  last?: boolean;
}) {
  const theme = useTheme();
  return (
    <ListRow
      icon={icon}
      title={title}
      subtitle={subtitle}
      last={last}
      right={
        <Switch
          value={value}
          onValueChange={onChange}
          accessibilityLabel={title}
          trackColor={{ false: theme.colors.glassFillStrong, true: theme.colors.accent }}
          thumbColor="#FFFFFF"
        />
      }
    />
  );
}

export interface SegmentOption<T extends string> {
  value: T;
  label: string;
  icon?: IconName;
}

export function SegmentedControl<T extends string>({
  options,
  value,
  onChange,
  label,
}: {
  options: SegmentOption<T>[];
  value: T;
  onChange: (v: T) => void;
  label: string;
}) {
  const theme = useTheme();
  return (
    <View
      accessibilityRole="radiogroup"
      accessibilityLabel={label}
      style={{
        flexDirection: 'row',
        padding: 4,
        borderRadius: theme.radii.pill,
        backgroundColor: theme.colors.glassFill,
        borderWidth: 1,
        borderColor: theme.colors.glassStroke,
      }}
    >
      {options.map(opt => {
        const selected = opt.value === value;
        return (
          <Pressable
            key={opt.value}
            onPress={() => onChange(opt.value)}
            accessibilityRole="radio"
            accessibilityLabel={opt.label}
            accessibilityState={{ selected, checked: selected }}
            style={{
              flex: 1,
              minHeight: 40,
              flexDirection: 'row',
              gap: 6,
              alignItems: 'center',
              justifyContent: 'center',
              borderRadius: theme.radii.pill,
              backgroundImage: selected ? theme.gradients.action : undefined,
            }}
          >
            {opt.icon ? (
              <Icon name={opt.icon} size={16} color={selected ? '#FFFFFF' : theme.colors.textSecondary} />
            ) : null}
            <AppText
              variant="callout"
              style={{ color: selected ? '#FFFFFF' : theme.colors.textSecondary, fontWeight: '700' }}
            >
              {opt.label}
            </AppText>
          </Pressable>
        );
      })}
    </View>
  );
}

export function ProgressBar({ value, label, tint }: { value: number; label: string; tint?: string }) {
  const theme = useTheme();
  const pct = Math.max(0, Math.min(1, Number.isFinite(value) ? value : 0));
  return (
    <View
      accessible
      accessibilityRole="progressbar"
      accessibilityLabel={label}
      accessibilityValue={{ min: 0, max: 100, now: Math.round(pct * 100) }}
      style={{ height: 8, borderRadius: 4, backgroundColor: theme.colors.glassFillStrong, overflow: 'hidden' }}
    >
      <View
        style={{
          width: `${pct * 100}%`,
          height: '100%',
          borderRadius: 4,
          backgroundColor: tint,
          backgroundImage: tint ? undefined : theme.gradients.spectrum,
        }}
      />
    </View>
  );
}

export function StatTile({
  label,
  value,
  icon,
  tint,
  style,
}: {
  label: string;
  value: string;
  icon: IconName;
  tint?: string;
  style?: ViewStyle;
}) {
  const theme = useTheme();
  return (
    <GlassSurface
      style={[{ flex: 1, padding: 14, gap: 10 }, style ?? {}]}
      radius={theme.radii.md}
      accessible
      accessibilityLabel={`${label}: ${value}`}
    >
      <Icon name={icon} size={18} color={tint ?? theme.colors.accent} />
      <View>
        <AppText variant="title3" numberOfLines={1} adjustsFontSizeToFit>
          {value}
        </AppText>
        <AppText variant="caption" tone="tertiary">
          {label}
        </AppText>
      </View>
    </GlassSurface>
  );
}

/** Pressable glass card with press-scale feedback. */
export function PressableCard({
  onPress,
  label,
  hint,
  children,
  style,
  radius,
}: {
  onPress: () => void;
  label: string;
  hint?: string;
  children: React.ReactNode;
  style?: ViewStyle;
  radius?: number;
}) {
  const { scale, onPressIn, onPressOut } = usePressScale();
  return (
    <Animated.View style={[{ transform: [{ scale }] }, style]}>
      <Pressable
        onPress={onPress}
        onPressIn={onPressIn}
        onPressOut={onPressOut}
        accessibilityRole="button"
        accessibilityLabel={label}
        accessibilityHint={hint}
        style={{ flex: 1 }}
      >
        <GlassSurface radius={radius} style={{ flex: 1 }}>
          {children}
        </GlassSurface>
      </Pressable>
    </Animated.View>
  );
}

const styles = StyleSheet.create({
  sectionHeader: { flexDirection: 'row', alignItems: 'flex-end', marginBottom: 12, marginTop: 8 },
  row: { flexDirection: 'row', alignItems: 'center', gap: 12, paddingVertical: 10, paddingHorizontal: 16 },
  rowIcon: { width: 34, height: 34, borderRadius: 10, alignItems: 'center', justifyContent: 'center' },
});
