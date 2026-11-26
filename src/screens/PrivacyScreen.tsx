import React, { useEffect, useMemo, useState } from 'react';
import { StyleSheet, View } from 'react-native';

import { AppText, GlassSurface, ListRow, Screen } from '../components';
import { t, useI18n } from '../i18n';
import type { RootScreenProps } from '../navigation/types';
import { getAdsService } from '../services/ads/AdsService';
import { privacySections } from '../services/help/faq';
import { useTheme } from '../theme';

/**
 * The privacy policy (its words are string resources, so it reads in the
 * app's language), with the ad privacy choices Google's consent form gives
 * where the law asks for them.
 */
export function PrivacyScreen({ navigation }: RootScreenProps<'Privacy'>) {
  const theme = useTheme();
  const strings = useI18n(s => s.strings);
  const sections = useMemo(() => privacySections(), [strings]); // eslint-disable-line react-hooks/exhaustive-deps
  const [adChoices, setAdChoices] = useState<boolean | null>(null);
  const [note, setNote] = useState<string | null>(null);

  useEffect(() => {
    let live = true;
    void getAdsService()
      .privacyOptionsRequired()
      .then(r => live && setAdChoices(r))
      .catch(() => live && setAdChoices(false));
    return () => {
      live = false;
    };
  }, []);

  const openAdChoices = async () => {
    setNote(null);
    if (!adChoices) {
      setNote(t('privacy_ad_choices_none'));
      return;
    }
    const shown = await getAdsService()
      .showPrivacyOptions()
      .catch(() => false);
    if (!shown) setNote(t('privacy_ad_choices_failed'));
  };

  return (
    <Screen
      title={t('privacy_title')}
      overline={t('privacy_overline')}
      onBack={() => navigation.goBack()}
      bottomInset={32}
    >
      <AppText variant="caption" tone="tertiary" style={{ marginBottom: 14 }} testID="privacy-updated">
        {t('privacy_updated')}
      </AppText>
      <GlassSurface style={{ marginBottom: 8 }}>
        <ListRow
          icon="shield"
          title={t('privacy_ad_choices')}
          subtitle={t('privacy_ad_choices_detail')}
          onPress={() => void openAdChoices()}
          last
        />
      </GlassSurface>
      {note ? (
        <AppText variant="callout" tone="secondary" style={{ marginBottom: 8 }} testID="privacy-note">
          {note}
        </AppText>
      ) : null}
      <GlassSurface style={styles.body}>
        {sections.map((s, i) => (
          <View
            key={s.title}
            style={[
              styles.section,
              i < sections.length - 1 && {
                borderBottomWidth: StyleSheet.hairlineWidth,
                borderBottomColor: theme.colors.divider,
              },
            ]}
            testID={`privacy-section-${i + 1}`}
          >
            <View style={styles.head}>
              <View style={[styles.num, { backgroundColor: theme.colors.glassFillStrong }]}>
                <AppText variant="caption" tone="accent">
                  {String(i + 1)}
                </AppText>
              </View>
              <AppText variant="title3" accessibilityRole="header" style={{ flex: 1 }}>
                {s.title}
              </AppText>
            </View>
            <AppText variant="body" tone="secondary">
              {s.body}
            </AppText>
          </View>
        ))}
      </GlassSurface>
      <GlassSurface style={{ marginTop: 16 }}>
        <ListRow
          icon="help"
          title={t('settings_help_title')}
          subtitle={t('settings_help_detail')}
          onPress={() => navigation.navigate('Help')}
          last
        />
      </GlassSurface>
    </Screen>
  );
}

const styles = StyleSheet.create({
  body: { marginTop: 12 },
  section: { padding: 16, gap: 8 },
  head: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  num: { width: 26, height: 26, borderRadius: 13, alignItems: 'center', justifyContent: 'center' },
});
