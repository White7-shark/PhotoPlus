import React from 'react';
import { Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button, Icon } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import {
  FILM_GRADES,
  LIGHT_PRESETS,
  NO_FINISH,
  TEMPLATES,
  lightPreset,
  type FinishChoice,
  type LightChoice,
} from '../../services/studio/light';
import { useTheme } from '../../theme';

const pct = (v: number) => `${Math.round(v * 100)}`;
const signed = (v: number) => (Math.abs(v) < 0.005 ? '0' : `${v > 0 ? '+' : '−'}${Math.round(Math.abs(v) * 100)}`);
/** A light's direction in words (0 front, negative from the left). */
const side = (a: number) => (Math.abs(a) < 8 ? 'Front' : `${Math.round(Math.abs(a))}° ${a < 0 ? 'left' : 'right'}`);

function Chip({ id, label, selected, locked, onPress }: { id: string; label: string; selected: boolean; locked: boolean; onPress: () => void }) {
  const theme = useTheme();
  return (
    <Pressable
      onPress={onPress}
      accessibilityRole="radio"
      accessibilityLabel={locked ? `${label}, Premium` : label}
      accessibilityState={{ selected }}
      testID={id}
      style={[
        styles.chip,
        { borderColor: selected ? theme.colors.accent : 'rgba(255,255,255,0.14)', backgroundColor: selected ? 'rgba(255,255,255,0.08)' : 'transparent' },
      ]}
    >
      {locked ? <Icon name="crown" size={11} color={theme.colors.accent} /> : null}
      <AppText variant="caption" style={{ fontWeight: selected ? '700' : '500' }}>
        {label}
      </AppText>
    </Pressable>
  );
}

/**
 * Studio light: a look (soft, Rembrandt, split, rim, neon…), how strong, and
 * (Premium) its direction, softness and rim. A photo has no depth, so the
 * light is an estimate: strength eases it back, and turning or softening it
 * corrects a shadow that falls wrong. `premium`: the person's plan has it.
 */
export function LightPanel({
  value,
  onChoose,
  onChange,
  premium,
  onLocked,
}: {
  value: LightChoice | null;
  onChoose: (presetId: string) => void;
  onChange: (c: LightChoice | null) => void;
  premium: boolean;
  onLocked: () => void;
}) {
  const current = value ? lightPreset(value.preset) : lightPreset('as-shot');
  const base = current?.light;
  const tweak = (c: Partial<LightChoice>) => {
    if (!value) return;
    if (!premium) return onLocked();
    onChange({ ...value, ...c });
  };
  return (
    <View style={{ gap: 10 }} testID="light-panel">
      <View style={styles.wrap} accessibilityRole="radiogroup">
        {LIGHT_PRESETS.map(p => (
          <Chip
            key={p.id}
            id={`light-${p.id}`}
            label={p.name}
            selected={(value?.preset ?? 'as-shot') === p.id}
            locked={p.premium && !premium}
            onPress={() => (p.premium && !premium ? onLocked() : onChoose(p.id))}
          />
        ))}
      </View>
      <AppText variant="caption" tone="secondary" testID="light-about">
        {current?.about}
      </AppText>
      {value && base ? (
        <>
          <ValueSlider label="Strength" value={value.strength} onChange={v => onChange({ ...value, strength: v })} format={pct} testID="light-strength" />
          <ValueSlider
            label="Direction"
            value={value.angle ?? base.angle}
            min={-90}
            max={90}
            step={5}
            onChange={v => tweak({ angle: v })}
            format={side}
            disabled={!premium}
            testID="light-direction"
          />
          <ValueSlider
            label="Softness"
            value={value.softness ?? base.softness}
            onChange={v => tweak({ softness: v })}
            format={pct}
            disabled={!premium}
            testID="light-softness"
          />
          <ValueSlider
            label="Rim light"
            value={value.rim ?? Math.max(base.rimLeft ?? 0, base.rimRight ?? 0)}
            onChange={v => tweak({ rim: v })}
            format={pct}
            disabled={!premium}
            testID="light-rim"
          />
          <AppText variant="caption" tone="tertiary">
            {premium
              ? 'The light is drawn from your outline and face. If a shadow falls oddly, turn the light, soften it or lower the strength.'
              : 'Turning, softening and rim light are part of Premium.'}
          </AppText>
          <Button label="Reset light" variant="ghost" icon="refresh" onPress={() => onChange({ preset: value.preset, strength: 1 })} testID="light-reset" />
        </>
      ) : null}
    </View>
  );
}

/** The finish: a grade (film looks are Premium) and the person's own vignette, warmth and tint. */
export function FinishPanel({
  value,
  onChange,
  premium,
  onLocked,
}: {
  value: FinishChoice;
  onChange: (c: FinishChoice) => void;
  premium: boolean;
  onLocked: () => void;
}) {
  return (
    <View style={{ gap: 10 }} testID="finish-panel">
      <View style={styles.wrap} accessibilityRole="radiogroup">
        {FILM_GRADES.map(g => (
          <Chip
            key={g.id}
            id={`grade-${g.id}`}
            label={g.name}
            selected={value.grade === g.id}
            locked={g.premium && !premium}
            onPress={() => (g.premium && !premium ? onLocked() : onChange({ ...value, grade: g.id }))}
          />
        ))}
      </View>
      <ValueSlider label="Vignette" value={value.vignette} min={-1} max={1} onChange={v => onChange({ ...value, vignette: v })} format={signed} testID="finish-vignette" />
      <ValueSlider label="Warmth" value={value.warmth} min={-1} max={1} onChange={v => onChange({ ...value, warmth: v })} format={signed} testID="finish-warmth" />
      <ValueSlider label="Tint" value={value.tint} min={-1} max={1} onChange={v => onChange({ ...value, tint: v })} format={signed} testID="finish-tint" />
      <Button label="Reset finish" variant="ghost" icon="refresh" onPress={() => onChange(NO_FINISH)} testID="finish-reset" />
    </View>
  );
}

/** Composition templates: where you stand in the frame. */
export function TemplateRow({ onApply, premium, onLocked }: { onApply: (id: string) => void; premium: boolean; onLocked: () => void }) {
  return (
    <View style={{ gap: 8 }} testID="templates">
      <AppText variant="caption" tone="secondary">
        Composition
      </AppText>
      <View style={styles.wrap}>
        {TEMPLATES.map(t => (
          <Chip
            key={t.id}
            id={`template-${t.id}`}
            label={t.name}
            selected={false}
            locked={t.premium && !premium}
            onPress={() => (t.premium && !premium ? onLocked() : onApply(t.id))}
          />
        ))}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  chip: { flexDirection: 'row', alignItems: 'center', gap: 5, paddingHorizontal: 10, paddingVertical: 6, borderRadius: 999, borderWidth: 1.5 },
});
