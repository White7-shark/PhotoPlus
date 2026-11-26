import React from 'react';
import { Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { DEFAULT_LIPSTICK_STRENGTH, DEFAULT_NATURAL, LIPSTICKS, type LipsChoice } from '../../services/studio/lips';
import { useTheme } from '../../theme';
import { t as tx } from '../../i18n';

const pct = (v: number) => `${Math.round(v * 100)}`;

/**
 * Pink lips: a natural soft pink that adapts to the person (lips that are
 * already pink and fair faces get very little; brown or greyish lips the
 * most). On or off, and how strong.
 */
export function PinkLipsPanel({
  value,
  onChange,
}: {
  value: LipsChoice | null;
  onChange: (c: LipsChoice | null) => void;
}) {
  const on = value?.kind === 'natural';
  return (
    <View style={{ gap: 10 }} testID="pink-lips-panel">
      <AppText variant="bodyStrong">{tx('lips_pink_lips')}</AppText>
      <AppText variant="caption" tone="secondary">
        {tx('lips_a_natural_soft_pink_made')}
        {value?.kind === 'lipstick' ? tx('lips_this_replaces_the_lipstick') : ''}
      </AppText>
      {on ? (
        <>
          <ValueSlider
            label={tx('lips_strength')}
            value={value.strength}
            min={0.1}
            max={1}
            step={0.05}
            onChange={v => onChange({ kind: 'natural', strength: v })}
            format={pct}
            testID="pink-lips-strength"
          />
          <Button
            label={tx('lips_natural_lips')}
            variant="ghost"
            icon="refresh"
            onPress={() => onChange(null)}
            testID="pink-lips-off"
          />
        </>
      ) : (
        <Button
          label={tx('lips_add_pink_lips')}
          icon="sparkles"
          variant="glass"
          onPress={() => onChange(DEFAULT_NATURAL)}
          testID="pink-lips-on"
        />
      )}
    </View>
  );
}

/** Lipstick: the shades people wear most, and how strong. */
export function LipstickPanel({
  value,
  onChange,
}: {
  value: LipsChoice | null;
  onChange: (c: LipsChoice | null) => void;
}) {
  const theme = useTheme();
  const current = value?.kind === 'lipstick' ? value : null;
  return (
    <View style={{ gap: 10 }} testID="lipstick-panel">
      <AppText variant="bodyStrong">{tx('lips_lipstick')}</AppText>
      <View style={styles.wrap} accessibilityRole="radiogroup">
        {LIPSTICKS.map(s => {
          const selected = current?.shade === s.id;
          return (
            <Pressable
              key={s.id}
              onPress={() =>
                onChange({ kind: 'lipstick', shade: s.id, strength: current?.strength ?? DEFAULT_LIPSTICK_STRENGTH })
              }
              accessibilityRole="radio"
              accessibilityLabel={s.name}
              accessibilityState={{ selected }}
              testID={`lipstick-${s.id}`}
              style={[styles.chip, { borderColor: selected ? theme.colors.accent : 'rgba(255,255,255,0.14)' }]}
            >
              <View style={[styles.dot, { backgroundColor: s.color }]} />
              <AppText variant="caption" style={{ fontWeight: selected ? '700' : '500' }}>
                {s.name}
              </AppText>
            </Pressable>
          );
        })}
      </View>
      {current ? (
        <>
          <ValueSlider
            label={tx('lips_strength')}
            value={current.strength}
            min={0.1}
            max={1}
            step={0.05}
            onChange={v => onChange({ ...current, strength: v })}
            format={pct}
            testID="lipstick-strength"
          />
          <Button
            label={tx('lips_no_lipstick')}
            variant="ghost"
            icon="refresh"
            onPress={() => onChange(null)}
            testID="lipstick-off"
          />
        </>
      ) : (
        <AppText variant="caption" tone="secondary">
          {tx('lips_choose_a_shade_it_keeps')}
        </AppText>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
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
  dot: { width: 14, height: 14, borderRadius: 7 },
});
