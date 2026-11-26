import React from 'react';
import { Alert } from 'react-native';

import { GlassSurface, Icon, ListRow, Screen } from '../components';
import { AppText } from '../components/AppText';
import { LANGUAGES, setLanguage, systemLanguage, useI18n, useT } from '../i18n';
import type { RootScreenProps } from '../navigation/types';
import { errorMessage } from '../native';
import { useTheme } from '../theme';

/**
 * The app's language: the phone's own, or English and 8 more. Each is
 * named in its own words so anyone can find theirs.
 */
export function LanguageScreen({ navigation }: RootScreenProps<'Language'>) {
  const theme = useTheme();
  const t = useT();
  const choice = useI18n(s => s.choice);
  const phone = systemLanguage();

  const pick = (tag: string) => {
    if (tag === choice) return;
    try {
      setLanguage(tag);
    } catch (e) {
      Alert.alert(t('language_failed'), errorMessage(e));
    }
  };
  const tick = (on: boolean) =>
    on ? <Icon name="check" size={20} color={theme.colors.accent} strokeWidth={2.4} /> : undefined;

  return (
    <Screen
      title={t('language_title')}
      overline={t('language_overline')}
      onBack={() => navigation.goBack()}
      bottomInset={32}
    >
      <GlassSurface style={{ marginBottom: 16 }}>
        <ListRow
          icon="phone"
          title={t('language_system')}
          subtitle={t('language_system_detail', { name: phone.name })}
          onPress={() => pick('')}
          right={tick(choice === '')}
          last
        />
      </GlassSurface>
      <GlassSurface style={{ marginBottom: 16 }}>
        {LANGUAGES.map((l, i) => (
          <ListRow
            key={l.tag}
            icon="globe"
            title={l.name}
            subtitle={l.name === l.english ? undefined : l.english}
            onPress={() => pick(l.tag)}
            right={tick(choice === l.tag)}
            last={i === LANGUAGES.length - 1}
          />
        ))}
      </GlassSurface>
      <AppText variant="caption" tone="tertiary">
        {t('language_note')}
      </AppText>
    </Screen>
  );
}
