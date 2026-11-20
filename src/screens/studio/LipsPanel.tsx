import React from 'react';
import { Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { DEFAULT_LIPS, LIP_SHADES, type LipsChoice } from '../../services/studio/lips';
import { useTheme } from '../../theme';

const pct = (v: number) => `${Math.round(v * 100)}`;

/**
 * Pink lips: a pink shade for the person's lips and how strong it is. The
 * lips are found from the face, keep their own light and gloss, and dark
 * lips are lifted so the pink shows on every skin tone.
 */
export function LipsPanel({ value, onChange }: { value: LipsChoice | null; onChange: (c: LipsChoice | null) => void }) {
  const theme = useTheme();
  return (
    <View style={{ gap: 10 }} testID="lips-panel">
      <AppText variant="bodyStrong">Pink lips</AppText>
      <View style={styles.wrap} accessibilityRole="radiogroup">
        {LIP_SHADES.map(s => {
          const selected = value?.shade === s.id;
          return (
            <Pressable
              key={s.id}
              onPress={() => onChange({ ...(value ?? DEFAULT_LIPS), shade: s.id })}
              accessibilityRole="radio"
              accessibilityLabel={s.name}
              accessibilityState={{ selected }}
              testID={`lips-${s.id}`}
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
      {value ? (
        <>
          <ValueSlider
            label="Strength"
            value={value.strength}
            min={0.1}
            max={1}
            step={0.05}
            onChange={v => onChange({ ...value, strength: v })}
            format={pct}
            testID="lips-strength"
          />
          <Button
            label="Natural lips"
            variant="ghost"
            icon="refresh"
            onPress={() => onChange(null)}
            testID="lips-off"
          />
        </>
      ) : (
        <AppText variant="caption" tone="secondary">
          Choose a shade. The lips keep their own shine and lines, and the pink shows on every skin tone.
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
