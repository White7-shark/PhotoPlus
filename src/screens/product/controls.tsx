import React from 'react';
import { Pressable, StyleSheet, View } from 'react-native';

import { AppText, Icon } from '../../components';
import type { IconName } from '../../components/Icon';
import { useTheme } from '../../theme';
import { t as tx } from '../../i18n';

/** A choice chip: a crown when it needs Premium, an optional colour swatch or icon. */
export function Choice({
  label,
  selected,
  onPress,
  testID,
  locked,
  swatch,
  icon,
}: {
  label: string;
  selected: boolean;
  onPress: () => void;
  testID?: string;
  locked?: boolean;
  swatch?: string;
  icon?: IconName;
}) {
  const theme = useTheme();
  return (
    <Pressable
      onPress={onPress}
      accessibilityRole="radio"
      accessibilityLabel={locked ? tx('controls_premium', { label }) : label}
      accessibilityState={{ selected }}
      testID={testID}
      style={[styles.chip, { borderColor: selected ? theme.colors.accent : 'rgba(255,255,255,0.14)' }]}
    >
      {locked ? <Icon name="crown" size={11} color={theme.colors.accent} /> : null}
      {icon ? <Icon name={icon} size={13} color={theme.colors.textPrimary} /> : null}
      {swatch ? <View style={[styles.swatch, { backgroundColor: swatch }]} /> : null}
      <AppText variant="caption" style={{ fontWeight: selected ? '700' : '500' }}>
        {label}
      </AppText>
    </Pressable>
  );
}

/** An on/off row (accessible as a switch). */
export function Toggle({
  label,
  hint,
  value,
  onChange,
  testID,
}: {
  label: string;
  hint?: string;
  value: boolean;
  onChange: (v: boolean) => void;
  testID?: string;
}) {
  const theme = useTheme();
  return (
    <Pressable
      onPress={() => onChange(!value)}
      accessibilityRole="switch"
      accessibilityState={{ checked: value }}
      accessibilityLabel={label}
      accessibilityHint={hint}
      testID={testID}
      style={styles.toggle}
    >
      <View style={{ flex: 1 }}>
        <AppText variant="caption" style={{ fontWeight: '600' }}>
          {label}
        </AppText>
        {hint ? (
          <AppText variant="caption" tone="tertiary">
            {hint}
          </AppText>
        ) : null}
      </View>
      <View style={[styles.track, { backgroundColor: value ? theme.colors.accent : 'rgba(255,255,255,0.16)' }]}>
        <View style={[styles.knob, { alignSelf: value ? 'flex-end' : 'flex-start' }]} />
      </View>
    </Pressable>
  );
}

/** A row of tabs (accessible as a tab list). */
export function Tabs<T extends string>({
  tabs,
  value,
  onChange,
  testPrefix,
}: {
  tabs: readonly { value: T; label: string }[];
  value: T;
  onChange: (v: T) => void;
  testPrefix: string;
}) {
  const theme = useTheme();
  return (
    <View style={styles.tabs} accessibilityRole="tablist">
      {tabs.map(t => (
        <Pressable
          key={t.value}
          onPress={() => onChange(t.value)}
          accessibilityRole="tab"
          accessibilityState={{ selected: value === t.value }}
          testID={`${testPrefix}-${t.value}`}
          style={[styles.tab, { borderColor: value === t.value ? theme.colors.accent : 'rgba(255,255,255,0.14)' }]}
        >
          <AppText variant="caption" style={{ fontWeight: value === t.value ? '700' : '500' }}>
            {t.label}
          </AppText>
        </Pressable>
      ))}
    </View>
  );
}

export const pct = (v: number) => `${Math.round(v * 100)}`;
export const signed = (v: number) =>
  Math.abs(v) < 0.005 ? '0' : `${v > 0 ? '+' : '−'}${Math.round(Math.abs(v) * 100)}`;

const styles = StyleSheet.create({
  chip: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 5,
    minHeight: 32,
    paddingHorizontal: 11,
    borderRadius: 16,
    borderWidth: 1.5,
  },
  swatch: { width: 13, height: 13, borderRadius: 7, borderWidth: 1, borderColor: 'rgba(255,255,255,0.4)' },
  toggle: { flexDirection: 'row', alignItems: 'center', gap: 12, minHeight: 44 },
  track: { width: 42, height: 24, borderRadius: 12, padding: 3, justifyContent: 'center' },
  knob: { width: 18, height: 18, borderRadius: 9, backgroundColor: '#FFFFFF' },
  tabs: { flexDirection: 'row', flexWrap: 'wrap', gap: 6 },
  tab: {
    minHeight: 34,
    paddingHorizontal: 12,
    borderRadius: 17,
    borderWidth: 1.5,
    alignItems: 'center',
    justifyContent: 'center',
  },
});
