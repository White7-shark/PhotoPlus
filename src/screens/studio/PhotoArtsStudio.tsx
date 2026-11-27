import React from 'react';
import { Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, GlassSurface, Icon } from '../../components';
import { useT, type StringKey } from '../../i18n';
import { BACKDROPS, FRAMES, SURFACES } from '../../services/masters/controls';
import { useTheme } from '../../theme';

function ChipRow<T extends string>({
  title,
  items,
  value,
  word,
  onChange,
  testID,
}: {
  title: string;
  items: readonly T[];
  value: number;
  word: (item: T) => string;
  onChange: (index: number) => void;
  testID: string;
}) {
  const theme = useTheme();
  return (
    <View style={{ gap: 8 }}>
      <AppText variant="caption" tone="secondary" style={{ fontWeight: '700' }}>
        {title}
      </AppText>
      <ScrollView
        horizontal
        showsHorizontalScrollIndicator={false}
        contentContainerStyle={{ gap: 8 }}
        accessibilityRole="radiogroup"
      >
        {items.map((item, i) => {
          const on = value === i;
          return (
            <Pressable
              key={item}
              onPress={() => onChange(i)}
              accessibilityRole="radio"
              accessibilityState={{ selected: on }}
              testID={`${testID}-${item}`}
              style={[
                styles.chip,
                { borderColor: on ? theme.colors.accent : 'rgba(255,255,255,0.14)' },
                on ? { backgroundColor: 'rgba(245,198,107,0.12)' } : null,
              ]}
            >
              <AppText variant="caption" style={{ fontWeight: on ? '700' : '500' }}>
                {word(item)}
              </AppText>
            </Pressable>
          );
        })}
      </ScrollView>
    </View>
  );
}

/**
 * Canvas, background and frame: what the painting is painted on, what is
 * painted behind the person, and a museum frame around it.
 */
export function PhotoArtsStudio({
  surface,
  backdrop,
  frame,
  open,
  onToggle,
  onSurface,
  onBackdrop,
  onFrame,
}: {
  surface: number;
  backdrop: number;
  frame: number;
  open: boolean;
  onToggle: () => void;
  onSurface: (i: number) => void;
  onBackdrop: (i: number) => void;
  onFrame: (i: number) => void;
}) {
  const theme = useTheme();
  const t = useT();
  return (
    <GlassSurface radius={18} style={styles.box} testID="masters-studio">
      <Pressable
        onPress={onToggle}
        accessibilityRole="button"
        accessibilityState={{ expanded: open }}
        testID="masters-studio-toggle"
        style={styles.head}
      >
        <Icon name="image" size={18} color={theme.colors.accent} />
        <AppText variant="bodyStrong" style={{ flex: 1 }}>
          {t('masters_studio_title')}
        </AppText>
        <Icon name={open ? 'minus' : 'plus'} size={18} color={theme.colors.textSecondary} />
      </Pressable>
      {open ? (
        <View style={{ gap: 14 }}>
          <ChipRow
            title={t('masters_surface')}
            items={SURFACES}
            value={surface}
            word={s => t(`masters_surface_${s}` as StringKey)}
            onChange={onSurface}
            testID="masters-surface"
          />
          <ChipRow
            title={t('masters_backdrop')}
            items={BACKDROPS}
            value={backdrop}
            word={b => t(`masters_backdrop_${b}` as StringKey)}
            onChange={onBackdrop}
            testID="masters-backdrop"
          />
          <ChipRow
            title={t('masters_frame')}
            items={FRAMES}
            value={frame}
            word={f => t(`masters_frame_${f}` as StringKey)}
            onChange={onFrame}
            testID="masters-frame"
          />
        </View>
      ) : null}
    </GlassSurface>
  );
}

const styles = StyleSheet.create({
  box: { padding: 14, gap: 12 },
  head: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  chip: { paddingHorizontal: 12, paddingVertical: 8, borderRadius: 999, borderWidth: 1.5 },
});
