import React, { useState } from 'react';
import { Pressable, StyleSheet, View } from 'react-native';

import { AppText, Icon } from '../../components';
import {
  HAIR_COLOURS,
  hairSummary,
  stylesFor,
  type HairBrush,
  type HairChoice,
  type HairGroup,
} from '../../services/studio/hair';
import { HairEditor } from './HairEditor';
import { useTheme } from '../../theme';
import { t as tx } from '../../i18n';

type Tab = 'colour' | HairGroup | 'edit';
const TABS: { value: Tab; label: string }[] = [
  { value: 'colour', label: tx('hair_picker_colour') },
  { value: 'women', label: tx('hair_picker_women') },
  { value: 'men', label: tx('hair_picker_men') },
  { value: 'edit', label: tx('hair_picker_edit') },
];

/** The brush state the screen keeps (it shows the canvas). */
export interface HairBrushState {
  brushing: boolean;
  onBrushing: (on: boolean) => void;
  brush: HairBrush;
  onBrush: (b: HairBrush) => void;
  radius: number;
  onRadius: (r: number) => void;
}

/**
 * Hair (Premium): a colour for the person's own hair, or a new style from
 * the women's or men's list (with or without a new colour). `locked`: the
 * free plan, where a choice opens Premium instead.
 */
export function HairPicker({
  value,
  onChange,
  locked,
  onLocked,
  brushes,
}: {
  value: HairChoice;
  onChange: (c: HairChoice) => void;
  locked: boolean;
  onLocked: () => void;
  /** Hand editing (style shape, fit, colour finish, brushes); none: no Edit tab. */
  brushes?: HairBrushState;
}) {
  const theme = useTheme();
  const [tab, setTab] = useState<Tab>('colour');
  const choose = (c: HairChoice) => (locked ? onLocked() : onChange(c));
  const chip = (key: string, label: string, selected: boolean, onPress: () => void, swatch?: string) => (
    <Pressable
      key={key}
      onPress={onPress}
      accessibilityRole="radio"
      accessibilityLabel={label}
      accessibilityState={{ selected }}
      testID={`hair-${key}`}
      style={[
        styles.chip,
        {
          borderColor: selected ? theme.colors.accent : 'rgba(255,255,255,0.14)',
          backgroundColor: selected ? 'rgba(255,255,255,0.08)' : 'transparent',
        },
      ]}
    >
      {swatch ? <View style={[styles.dot, { backgroundColor: swatch }]} /> : null}
      <AppText variant="caption" style={{ fontWeight: selected ? '700' : '500' }}>
        {label}
      </AppText>
    </Pressable>
  );
  return (
    <View style={{ gap: 10 }} testID="hair-picker">
      <View style={styles.head}>
        <AppText variant="caption" tone="secondary" style={{ flex: 1 }} testID="hair-summary">
          {hairSummary(value)}
        </AppText>
        {locked ? (
          <View style={styles.badge}>
            <Icon name="crown" size={12} color={theme.colors.accent} />
            <AppText variant="caption" tone="accent">
              Premium
            </AppText>
          </View>
        ) : null}
      </View>
      <View style={styles.row} accessibilityRole="tablist">
        {TABS.filter(t => t.value !== 'edit' || (brushes && (value.style || value.color))).map(t => (
          <Pressable
            key={t.value}
            onPress={() => (t.value === 'edit' && locked ? onLocked() : setTab(t.value))}
            accessibilityRole="tab"
            accessibilityState={{ selected: tab === t.value }}
            testID={`hair-tab-${t.value}`}
            style={[styles.tab, { backgroundColor: tab === t.value ? theme.colors.accent : 'rgba(255,255,255,0.08)' }]}
          >
            <AppText
              variant="caption"
              style={{ color: tab === t.value ? '#FFFFFF' : theme.colors.textSecondary, fontWeight: '700' }}
            >
              {t.label}
            </AppText>
          </Pressable>
        ))}
      </View>
      {tab === 'edit' && brushes && (value.style || value.color) ? (
        <HairEditor value={value} onChange={onChange} {...brushes} />
      ) : (
        <View style={styles.wrap} accessibilityRole="radiogroup">
          {tab === 'colour' ? (
            <>
              {chip('own-colour', 'Own colour', !value.color, () => choose({ ...value, color: null }))}
              {HAIR_COLOURS.map(c =>
                chip(
                  c.id,
                  c.name,
                  value.color?.toUpperCase() === c.color,
                  () => choose({ ...value, color: c.color }),
                  c.color,
                ),
              )}
            </>
          ) : (
            <>
              {chip('own-style', 'Own hair', !value.style, () => choose({ ...value, style: null }))}
              {stylesFor(tab === 'men' ? 'men' : 'women').map(st =>
                chip(st.id, st.name, value.style === st.id, () => choose({ ...value, style: st.id })),
              )}
            </>
          )}
        </View>
      )}
      <AppText variant="caption" tone="tertiary">
        {tab === 'colour'
          ? tx('hair_picker_colours_your_own_hair_or')
          : tab === 'edit'
            ? tx('hair_picker_make_it_right_by_hand')
            : tx('hair_picker_your_own_hair_is_taken')}
      </AppText>
    </View>
  );
}

const styles = StyleSheet.create({
  head: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  badge: { flexDirection: 'row', alignItems: 'center', gap: 4 },
  row: { flexDirection: 'row', gap: 8 },
  tab: { paddingHorizontal: 12, paddingVertical: 7, borderRadius: 999 },
  wrap: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  chip: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    paddingHorizontal: 10,
    paddingVertical: 6,
    borderRadius: 999,
    borderWidth: 1.5,
  },
  dot: { width: 14, height: 14, borderRadius: 7, borderWidth: 1, borderColor: 'rgba(255,255,255,0.4)' },
});
