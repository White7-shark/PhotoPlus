import React from 'react';
import { Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { useT, type StringKey } from '../../i18n';
import {
  ARTIST_WAY,
  CONTROL_RANGES,
  isArtistWay,
  type ArtControls,
  type ControlKey,
} from '../../services/masters/controls';
import { useTheme } from '../../theme';

/** How far from the artist's own way: 0 there, +30 or −20 (per cent of the range) either side. */
export function offset(key: ControlKey, v: number, min: number, max: number) {
  const d = Math.round(((v - ARTIST_WAY[key]) / (max - min)) * 100);
  return d === 0 ? '0' : `${d > 0 ? '+' : '−'}${Math.abs(d)}`;
}

/**
 * Your touch: the person guides the artist's hand — brush size, colours,
 * vividness, likeness, texture, age and background. The preview repaints
 * when a finger lifts; "Paint it the artist's way" puts everything back.
 */
export function PhotoArtsTouch({
  value,
  open,
  onToggle,
  onChange,
  onEnd,
  onReset,
  disabled,
}: {
  value: ArtControls;
  open: boolean;
  onToggle: () => void;
  onChange: (next: ArtControls) => void;
  onEnd: () => void;
  onReset: () => void;
  disabled?: boolean;
}) {
  const theme = useTheme();
  const t = useT();
  const changed = !isArtistWay(value);
  return (
    <GlassSurface radius={18} style={styles.box} testID="masters-touch">
      <Pressable
        onPress={onToggle}
        accessibilityRole="button"
        accessibilityState={{ expanded: open }}
        testID="masters-touch-toggle"
        style={styles.head}
      >
        <Icon name="sliders" size={18} color={theme.colors.accent} />
        <View style={{ flex: 1 }}>
          <AppText variant="bodyStrong">{t('masters_touch_title')}</AppText>
          {changed && !open ? (
            <AppText variant="caption" style={{ color: theme.colors.accent }}>
              {t('masters_touch_changed')}
            </AppText>
          ) : null}
        </View>
        <Icon name={open ? 'minus' : 'plus'} size={18} color={theme.colors.textSecondary} />
      </Pressable>
      {open ? (
        <View style={{ gap: 12 }}>
          <AppText variant="caption" tone="tertiary">
            {t('masters_touch_about')}
          </AppText>
          {CONTROL_RANGES.map(r => (
            <View key={r.key} style={{ gap: 2 }}>
              <ValueSlider
                label={t(`masters_ctl_${r.key}` as StringKey)}
                value={value[r.key]}
                min={r.min}
                max={r.max}
                step={r.step}
                disabled={disabled}
                onChange={v => onChange({ ...value, [r.key]: v })}
                onEnd={onEnd}
                format={v => offset(r.key, v, r.min, r.max)}
                testID={`masters-ctl-${r.key}`}
              />
              <View style={styles.ends}>
                <AppText variant="caption" tone="tertiary" style={styles.end}>
                  {t(`masters_ctl_${r.key}_lo` as StringKey)}
                </AppText>
                <AppText variant="caption" tone="tertiary" style={styles.end}>
                  {t(`masters_ctl_${r.key}_hi` as StringKey)}
                </AppText>
              </View>
            </View>
          ))}
          <Button
            label={t('masters_reset')}
            icon="refresh"
            variant="ghost"
            disabled={!changed || disabled}
            onPress={onReset}
            testID="masters-touch-reset"
          />
        </View>
      ) : null}
    </GlassSurface>
  );
}

const styles = StyleSheet.create({
  box: { padding: 14, gap: 12 },
  head: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  ends: { flexDirection: 'row', justifyContent: 'space-between' },
  end: { fontSize: 10 },
});
