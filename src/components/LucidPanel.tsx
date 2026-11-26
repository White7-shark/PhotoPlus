import React, { useState } from 'react';
import { ActivityIndicator, Alert, Pressable, StyleSheet, View } from 'react-native';

import { useFeatureCheck } from '../hooks/useFeature';
import { t } from '../i18n';
import { errorMessage } from '../native';
import { chooseWallpaper, pickLucidBackground, useLucid, type LucidSource } from '../services/lucid/LucidService';
import { useTheme } from '../theme';
import { AppText } from './AppText';
import { GlassSurface } from './GlassSurface';
import { Icon, type IconName } from './Icon';
import { PremiumSheet } from './PremiumSheet';

const SOURCES: { id: LucidSource; label: () => string; icon: IconName; premium: boolean }[] = [
  { id: 'wallpaper', label: () => t('lucid_wallpaper'), icon: 'phone', premium: false },
  { id: 'photo', label: () => t('lucid_photo'), icon: 'image', premium: true },
  { id: 'video', label: () => t('lucid_video'), icon: 'film', premium: true },
];

/**
 * The Lucid theme's settings: what's behind the glass (the phone's
 * wallpaper — free — or your own photo or looping clip — Premium) and the
 * colours taken from it.
 */
export function LucidPanel() {
  const theme = useTheme();
  const can = useFeatureCheck();
  const { source, look, busy, seeThrough } = useLucid();
  const [premium, setPremium] = useState(false);

  const choose = async (id: LucidSource) => {
    try {
      if (id === 'wallpaper') {
        await chooseWallpaper();
        return;
      }
      if (!can('lucidCustom').allowed) {
        setPremium(true);
        return;
      }
      await pickLucidBackground(id);
    } catch (e) {
      Alert.alert(t('lucid_failed'), errorMessage(e));
    }
  };

  return (
    <GlassSurface style={styles.card} testID="lucid-panel">
      <AppText variant="bodyStrong">{t('lucid_title')}</AppText>
      <View style={styles.row}>
        {SOURCES.map(s => {
          const on = s.id === source;
          return (
            <Pressable
              key={s.id}
              onPress={() => void choose(s.id)}
              disabled={busy}
              accessibilityRole="radio"
              accessibilityState={{ selected: on, disabled: busy }}
              testID={`lucid-source-${s.id}`}
              style={[
                styles.chip,
                {
                  backgroundColor: on ? theme.colors.accent : theme.colors.glassFillStrong,
                  borderColor: on ? theme.colors.accent : theme.colors.glassStroke,
                },
              ]}
            >
              <Icon name={s.icon} size={15} color={on ? '#FFFFFF' : theme.colors.textPrimary} />
              <AppText
                variant="callout"
                style={{ color: on ? '#FFFFFF' : theme.colors.textPrimary, fontWeight: '700' }}
              >
                {s.label()}
              </AppText>
              {s.premium && !on ? <Icon name="crown" size={12} color={theme.colors.premium} /> : null}
            </Pressable>
          );
        })}
        {busy ? <ActivityIndicator color={theme.colors.accent} /> : null}
      </View>
      {look ? (
        <View style={styles.row}>
          {[look.accent, look.colors.accentAlt ?? look.accent, look.colors.background ?? look.veilColor].map((c, i) => (
            <View key={i} style={[styles.swatch, { backgroundColor: c, borderColor: theme.colors.glassStroke }]} />
          ))}
          <AppText variant="caption" tone="secondary" style={{ flex: 1 }} testID="lucid-colours">
            {t('lucid_colours', { text: look.scheme === 'dark' ? t('lucid_light_text') : t('lucid_dark_text') })}
          </AppText>
        </View>
      ) : null}
      <AppText variant="caption" tone="tertiary">
        {seeThrough ? t('lucid_note_see_through') : t('lucid_note')}
      </AppText>
      <PremiumSheet
        feature={premium ? 'lucidCustom' : null}
        message={t('lucid_premium')}
        onClose={() => setPremium(false)}
      />
    </GlassSurface>
  );
}

const styles = StyleSheet.create({
  card: { padding: 16, gap: 12 },
  row: { flexDirection: 'row', flexWrap: 'wrap', alignItems: 'center', gap: 8 },
  chip: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    paddingVertical: 9,
    paddingHorizontal: 14,
    borderRadius: 999,
    borderWidth: 1,
  },
  swatch: { width: 24, height: 24, borderRadius: 12, borderWidth: 1 },
});
