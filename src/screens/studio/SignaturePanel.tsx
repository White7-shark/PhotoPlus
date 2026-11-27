import React from 'react';
import { Pressable, StyleSheet, Switch, TextInput, View } from 'react-native';

import { AppText, GlassSurface, Icon } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { useT, type StringKey } from '../../i18n';
import { SIG_POSITIONS, SIG_STYLES, type Signature } from '../../services/masters/pencil';
import { useTheme } from '../../theme';

/**
 * The artist's signature (Premium): the person's name — and their brand or
 * studio, and the year — signed into the corner of the drawing or painting.
 * On the free plan the panel shows what it does and offers Premium.
 */
export function SignaturePanel({
  value,
  premium,
  open,
  onToggle,
  onChange,
  onEnd,
  onLocked,
}: {
  value: Signature;
  premium: boolean;
  open: boolean;
  onToggle: () => void;
  /** Every keystroke / move. */
  onChange: (s: Signature) => void;
  /** When a change is done (a field left, a choice made): the preview follows. */
  onEnd: (s: Signature) => void;
  onLocked: () => void;
}) {
  const theme = useTheme();
  const t = useT();
  const accent = theme.colors.accent;
  const input = {
    minHeight: 44,
    borderRadius: theme.radii.sm,
    paddingHorizontal: 12,
    color: theme.colors.textPrimary,
    backgroundColor: theme.colors.glassFill,
    borderWidth: 1,
    borderColor: theme.colors.glassStroke,
    fontSize: 15,
  } as const;
  const chip = (on: boolean) => [
    styles.chip,
    { borderColor: on ? accent : 'rgba(255,255,255,0.14)' },
    on ? { backgroundColor: 'rgba(245,198,107,0.12)' } : null,
  ];
  return (
    <GlassSurface radius={18} style={styles.box} testID="signature-panel">
      <Pressable
        onPress={premium ? onToggle : onLocked}
        accessibilityRole="button"
        accessibilityState={{ expanded: open }}
        testID="signature-toggle"
        style={styles.head}
      >
        <Icon name="brush" size={18} color={accent} />
        <View style={{ flex: 1 }}>
          <AppText variant="bodyStrong">{t('sig_title')}</AppText>
          <AppText variant="caption" tone="tertiary">
            {premium ? t('sig_about') : t('sig_locked')}
          </AppText>
        </View>
        {premium ? (
          <Switch
            value={value.enabled}
            onValueChange={v => {
              const next = { ...value, enabled: v };
              onChange(next);
              onEnd(next);
              if (v && !open) onToggle();
            }}
            accessibilityLabel={t('sig_title')}
            trackColor={{ false: theme.colors.glassFillStrong, true: accent }}
            thumbColor="#FFFFFF"
            testID="signature-switch"
          />
        ) : (
          <View style={[styles.lock, { backgroundImage: theme.gradients.premium }]}>
            <Icon name="crown" size={14} color="#1A1206" />
          </View>
        )}
      </Pressable>
      {premium && open ? (
        <View style={{ gap: 12 }}>
          <TextInput
            value={value.text}
            onChangeText={text => onChange({ ...value, text, enabled: true })}
            onEndEditing={() => onEnd({ ...value, enabled: value.text.trim().length > 0 })}
            placeholder={t('sig_name_placeholder')}
            placeholderTextColor={theme.colors.textTertiary}
            maxLength={40}
            accessibilityLabel={t('sig_name')}
            testID="signature-name"
            style={input}
          />
          <TextInput
            value={value.line2}
            onChangeText={line2 => onChange({ ...value, line2 })}
            onEndEditing={() => onEnd(value)}
            placeholder={t('sig_line2_placeholder')}
            placeholderTextColor={theme.colors.textTertiary}
            maxLength={40}
            accessibilityLabel={t('sig_line2')}
            testID="signature-line2"
            style={input}
          />
          <AppText variant="caption" tone="secondary" style={{ fontWeight: '700' }}>
            {t('sig_style')}
          </AppText>
          <View style={styles.row} accessibilityRole="radiogroup">
            {SIG_STYLES.map(s => (
              <Pressable
                key={s}
                onPress={() => {
                  const next = { ...value, style: s };
                  onChange(next);
                  onEnd(next);
                }}
                accessibilityRole="radio"
                accessibilityState={{ selected: value.style === s }}
                testID={`signature-style-${s}`}
                style={[chip(value.style === s), { flex: 1, alignItems: 'center' }]}
              >
                <AppText variant="callout" style={{ fontWeight: value.style === s ? '700' : '500' }}>
                  {t(`sig_style_${s}` as StringKey)}
                </AppText>
              </Pressable>
            ))}
          </View>
          <AppText variant="caption" tone="secondary" style={{ fontWeight: '700' }}>
            {t('sig_position')}
          </AppText>
          <View style={styles.row} accessibilityRole="radiogroup">
            {SIG_POSITIONS.map(p => (
              <Pressable
                key={p}
                onPress={() => {
                  const next = { ...value, position: p };
                  onChange(next);
                  onEnd(next);
                }}
                accessibilityRole="radio"
                accessibilityState={{ selected: value.position === p }}
                testID={`signature-position-${p}`}
                style={[chip(value.position === p), { flex: 1, alignItems: 'center' }]}
              >
                <AppText variant="callout" style={{ fontWeight: value.position === p ? '700' : '500' }}>
                  {t(`sig_pos_${p}` as StringKey)}
                </AppText>
              </Pressable>
            ))}
          </View>
          <ValueSlider
            label={t('sig_size')}
            value={value.size}
            onChange={size => onChange({ ...value, size })}
            onEnd={() => onEnd(value)}
            format={v => `${Math.round(v * 100)}%`}
            testID="signature-size"
          />
          <View style={styles.yearRow}>
            <AppText variant="callout" style={{ flex: 1 }}>
              {t('sig_year')}
            </AppText>
            <Switch
              value={value.year}
              onValueChange={year => {
                const next = { ...value, year };
                onChange(next);
                onEnd(next);
              }}
              accessibilityLabel={t('sig_year')}
              trackColor={{ false: theme.colors.glassFillStrong, true: accent }}
              thumbColor="#FFFFFF"
              testID="signature-year"
            />
          </View>
        </View>
      ) : null}
    </GlassSurface>
  );
}

const styles = StyleSheet.create({
  box: { padding: 14, gap: 12 },
  head: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  lock: { width: 28, height: 28, borderRadius: 14, alignItems: 'center', justifyContent: 'center' },
  row: { flexDirection: 'row', gap: 8 },
  chip: { paddingHorizontal: 12, paddingVertical: 9, borderRadius: 999, borderWidth: 1.5 },
  yearRow: { flexDirection: 'row', alignItems: 'center', gap: 10 },
});
