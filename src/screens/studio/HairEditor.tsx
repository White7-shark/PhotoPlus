import React from 'react';
import { Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button, IconButton } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { NO_EDIT, type HairBrush, type HairChoice, type HairEdit } from '../../services/studio/hair';
import { useTheme } from '../../theme';

const signed = (v: number) => (Math.abs(v) < 0.005 ? '0' : `${v > 0 ? '+' : '−'}${Math.round(Math.abs(v) * 100)}`);
const pct = (v: number) => `${Math.round(v * 100)}`;

/** The hair brushes: what each does, in plain words, and its paint colour. */
export const HAIR_BRUSHES: { value: HairBrush; label: string; hint: string; colour: string }[] = [
  {
    value: 'erase',
    label: 'Erase hair',
    hint: 'Paint where the new hair should not be.',
    colour: 'rgba(255,107,107,0.55)',
  },
  {
    value: 'restore',
    label: 'Restore',
    hint: 'Paint to bring back the photo as it was: their own hair, skin or clothes.',
    colour: 'rgba(91,228,155,0.55)',
  },
  {
    value: 'colourOn',
    label: 'Add colour',
    hint: 'Paint over hair the colour missed.',
    colour: 'rgba(245,198,107,0.6)',
  },
  {
    value: 'colourOff',
    label: 'Remove colour',
    hint: 'Paint over brows, skin or anything the colour should not touch.',
    colour: 'rgba(120,170,255,0.55)',
  },
];
export const HAIR_BRUSH_COLOURS = Object.fromEntries(HAIR_BRUSHES.map(b => [b.value, b.colour])) as Record<
  HairBrush,
  string
>;

/**
 * Hair, finished by hand (Premium): the style's shape and where it sits,
 * the colour's finish, and brushes to paint on the photo. For any head, any
 * hair: what the engine draws is a start; the photographer makes it right.
 */
export function HairEditor({
  value,
  onChange,
  brushing,
  onBrushing,
  brush,
  onBrush,
  radius,
  onRadius,
}: {
  value: HairChoice;
  onChange: (c: HairChoice) => void;
  brushing: boolean;
  onBrushing: (on: boolean) => void;
  brush: HairBrush;
  onBrush: (b: HairBrush) => void;
  radius: number;
  onRadius: (r: number) => void;
}) {
  const theme = useTheme();
  const e = value.edit ?? NO_EDIT;
  const set = (patch: Partial<HairEdit>) => onChange({ ...value, edit: { ...e, ...patch } });
  const strokes = value.strokes ?? [];
  const styled = Boolean(value.style);
  const coloured = Boolean(value.color);
  const slider = (label: string, key: keyof HairEdit, min = -1, max = 1, format = signed, step = 0.05) => (
    <ValueSlider
      key={key}
      label={label}
      value={e[key] ?? 0}
      min={min}
      max={max}
      step={step}
      onChange={v => set({ [key]: v })}
      format={format}
      testID={`hair-edit-${key}`}
    />
  );
  return (
    <View style={{ gap: 12 }} testID="hair-editor">
      {styled ? (
        <View style={styles.section}>
          <AppText variant="bodyStrong">Shape</AppText>
          {slider('Length', 'length')}
          {slider('Volume', 'volume')}
          {slider('Height on top', 'height')}
          {slider('Fringe', 'fringe')}
          <View style={styles.row}>
            <View style={{ flex: 1 }}>
              <ValueSlider
                label="Parting"
                value={e.part ?? 0}
                min={-1}
                max={1}
                step={0.05}
                onChange={v => set({ part: v })}
                format={v => (e.part == null ? 'As styled' : Math.abs(v) < 0.05 ? 'Centre' : v < 0 ? 'Left' : 'Right')}
                testID="hair-edit-part"
              />
            </View>
            {e.part != null ? (
              <Button
                label="As styled"
                variant="ghost"
                onPress={() => set({ part: null })}
                testID="hair-edit-part-reset"
              />
            ) : null}
          </View>
          {slider('Waves and curls', 'waves')}
          {slider('Shine', 'shine')}
        </View>
      ) : null}
      {styled ? (
        <View style={styles.section}>
          <AppText variant="bodyStrong">Fit to the head</AppText>
          {slider('Left or right', 'moveX', -0.5, 0.5, v => signed(v * 2), 0.01)}
          {slider('Up or down', 'moveY', -0.5, 0.5, v => signed(v * 2), 0.01)}
          {slider('Size', 'size', 0.7, 1.4, v => `${Math.round(v * 100)}%`, 0.01)}
          {slider('Turn', 'rotate', -30, 30, v => `${Math.round(v)}°`, 1)}
        </View>
      ) : null}
      {coloured || styled ? (
        <View style={styles.section}>
          <AppText variant="bodyStrong">Colour finish</AppText>
          {coloured ? slider('Depth', 'depth') : null}
          {!styled ? slider('Shine', 'shine') : null}
          {slider('Darker roots', 'roots', 0, 1, pct)}
          {slider('Ends', 'ends', -1, 1, v =>
            Math.abs(v) < 0.005 ? '0' : v > 0 ? `Lighter ${pct(v)}` : `Darker ${pct(-v)}`,
          )}
        </View>
      ) : null}
      <View style={styles.section}>
        <View style={styles.row}>
          <AppText variant="bodyStrong" style={{ flex: 1 }}>
            Brushes
          </AppText>
          <IconButton
            icon="undo"
            label="Undo the last stroke"
            size={36}
            disabled={strokes.length === 0}
            onPress={() => onChange({ ...value, strokes: strokes.slice(0, -1) })}
          />
        </View>
        <View style={styles.wrap} accessibilityRole="radiogroup">
          {HAIR_BRUSHES.map(b => (
            <Pressable
              key={b.value}
              onPress={() => onBrush(b.value)}
              accessibilityRole="radio"
              accessibilityState={{ selected: brush === b.value }}
              testID={`hair-brush-${b.value}`}
              style={[styles.chip, { borderColor: brush === b.value ? theme.colors.accent : 'rgba(255,255,255,0.14)' }]}
            >
              <View style={[styles.dot, { backgroundColor: b.colour }]} />
              <AppText variant="caption" style={{ fontWeight: brush === b.value ? '700' : '500' }}>
                {b.label}
              </AppText>
            </Pressable>
          ))}
        </View>
        <AppText variant="caption" tone="secondary">
          {HAIR_BRUSHES.find(b => b.value === brush)?.hint}
        </AppText>
        <ValueSlider
          label="Brush size"
          value={radius}
          min={0.01}
          max={0.1}
          step={0.005}
          onChange={onRadius}
          format={v => `${Math.round(v * 1000) / 10}`}
        />
        <Button
          label={brushing ? 'Done painting' : 'Paint on the photo'}
          icon={brushing ? 'check' : 'wand'}
          variant={brushing ? 'primary' : 'glass'}
          onPress={() => onBrushing(!brushing)}
          testID="hair-paint"
        />
      </View>
      <Button
        label="Reset my changes"
        variant="ghost"
        icon="refresh"
        disabled={!value.edit && strokes.length === 0}
        onPress={() => onChange({ style: value.style, color: value.color })}
        testID="hair-edit-reset"
      />
    </View>
  );
}

const styles = StyleSheet.create({
  section: { gap: 8, padding: 12, borderRadius: 14, backgroundColor: 'rgba(255,255,255,0.05)' },
  row: { flexDirection: 'row', alignItems: 'center', gap: 8 },
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
  dot: { width: 12, height: 12, borderRadius: 6 },
});
