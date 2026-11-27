import React from 'react';
import { Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, GlassSurface, Icon } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { useT } from '../../i18n';
import { MEDIA, PAPERS, mediumName, paperName, type PencilOptions } from '../../services/masters/pencil';
import { useTheme } from '../../theme';

const SWATCH: Record<(typeof PAPERS)[number], string> = {
  white: '#F8F7F3',
  cream: '#F3EAD6',
  grey: '#B3B4B3',
  tan: '#C9B08A',
};
const LEAD: Record<(typeof MEDIA)[number], string> = { graphite: '#2E2E33', charcoal: '#111111', red_chalk: '#8E3B22' };

/** The drawing: what it is drawn with and on, how bold the lines and how deep the shading. */
export function PencilPanel({
  value,
  onChange,
  onEnd,
}: {
  value: PencilOptions;
  /** A choice (the preview follows at once). */
  onChange: (p: PencilOptions) => void;
  /** A slider lifted (the preview follows). */
  onEnd: (p: PencilOptions) => void;
}) {
  const theme = useTheme();
  const t = useT();
  const accent = theme.colors.accent;
  const chip = (on: boolean) => [
    styles.chip,
    { borderColor: on ? accent : 'rgba(255,255,255,0.14)' },
    on ? { backgroundColor: 'rgba(245,198,107,0.12)' } : null,
  ];
  return (
    <GlassSurface radius={18} style={styles.box} testID="pencil-panel">
      <View style={styles.head}>
        <Icon name="brush" size={18} color={accent} />
        <AppText variant="bodyStrong" style={{ flex: 1 }}>
          {t('pencil_panel_title')}
        </AppText>
        <AppText variant="caption" tone="tertiary">
          HD
        </AppText>
      </View>
      <AppText variant="caption" tone="secondary" style={{ fontWeight: '700' }}>
        {t('pencil_medium')}
      </AppText>
      <ScrollView
        horizontal
        showsHorizontalScrollIndicator={false}
        contentContainerStyle={{ gap: 8 }}
        accessibilityRole="radiogroup"
      >
        {MEDIA.map((m, i) => (
          <Pressable
            key={m}
            onPress={() => onChange({ ...value, medium: i })}
            accessibilityRole="radio"
            accessibilityState={{ selected: value.medium === i }}
            testID={`pencil-medium-${m}`}
            style={[chip(value.medium === i), styles.withDot]}
          >
            <View style={[styles.dot, { backgroundColor: LEAD[m] }]} />
            <AppText variant="caption" style={{ fontWeight: value.medium === i ? '700' : '500' }}>
              {mediumName(m)}
            </AppText>
          </Pressable>
        ))}
      </ScrollView>
      <AppText variant="caption" tone="secondary" style={{ fontWeight: '700' }}>
        {t('pencil_paper')}
      </AppText>
      <ScrollView
        horizontal
        showsHorizontalScrollIndicator={false}
        contentContainerStyle={{ gap: 8 }}
        accessibilityRole="radiogroup"
      >
        {PAPERS.map((p, i) => (
          <Pressable
            key={p}
            onPress={() => onChange({ ...value, paper: i })}
            accessibilityRole="radio"
            accessibilityState={{ selected: value.paper === i }}
            testID={`pencil-paper-${p}`}
            style={[chip(value.paper === i), styles.withDot]}
          >
            <View
              style={[styles.dot, { backgroundColor: SWATCH[p], borderWidth: 1, borderColor: 'rgba(0,0,0,0.2)' }]}
            />
            <AppText variant="caption" style={{ fontWeight: value.paper === i ? '700' : '500' }}>
              {paperName(p)}
            </AppText>
          </Pressable>
        ))}
      </ScrollView>
      <ValueSlider
        label={t('pencil_lines')}
        value={value.lines}
        min={0.5}
        max={1.5}
        onChange={lines => onChange({ ...value, lines })}
        onEnd={() => onEnd(value)}
        format={v => `${Math.round(v * 100)}%`}
        testID="pencil-lines"
      />
      <ValueSlider
        label={t('pencil_shading')}
        value={value.shading}
        min={0.5}
        max={1.5}
        onChange={shading => onChange({ ...value, shading })}
        onEnd={() => onEnd(value)}
        format={v => `${Math.round(v * 100)}%`}
        testID="pencil-shading"
      />
      <AppText variant="caption" tone="tertiary">
        {t('pencil_hd_note')}
      </AppText>
    </GlassSurface>
  );
}

const styles = StyleSheet.create({
  box: { padding: 14, gap: 10 },
  head: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  chip: { paddingHorizontal: 12, paddingVertical: 8, borderRadius: 999, borderWidth: 1.5 },
  withDot: { flexDirection: 'row', alignItems: 'center', gap: 7 },
  dot: { width: 14, height: 14, borderRadius: 7 },
});
