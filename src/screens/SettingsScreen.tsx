import { DEVELOPER_TOOLS } from '../app/developer';
import React, { useCallback, useEffect, useState } from 'react';
import { Alert, View } from 'react-native';

import {
  AppText,
  GlassSurface,
  ListRow,
  ProgressBar,
  Screen,
  SectionHeader,
  SegmentedControl,
  Skeleton,
  ToggleRow,
} from '../components';
import { LucidPanel } from '../components/LucidPanel';
import type { TabScreenProps } from '../navigation/types';
import { errorMessage } from '../native';
import type { AppSettings } from '../services/database/repositories/SettingsRepository';
import { formatBytes, getStorageService, type StorageUsage } from '../services/storage/StorageService';
import { useAppStore } from '../state/store';
import { TRIAL_DAYS } from '../services/billing/plans';
import { levelName, type MastersLevel } from '../services/masters/catalog';
import { ARTIST_WAY } from '../services/masters/controls';
import { useI18n, useT } from '../i18n';
import { useTheme } from '../theme';
import { t as tx } from '../i18n';

const APP_VERSION = '0.2.0';

export function SettingsScreen({ navigation }: TabScreenProps<'Settings'>) {
  const theme = useTheme();
  const t = useT();
  const language = useI18n(s => s.language);
  const followsPhone = useI18n(s => s.choice === '');
  const settings = useAppStore(s => s.settings);
  const updateSetting = useAppStore(s => s.updateSetting);
  const [usage, setUsage] = useState<StorageUsage | null>(null);
  const [usageError, setUsageError] = useState<string | null>(null);
  const [clearing, setClearing] = useState(false);
  const simulation = useAppStore(s => s.premiumSimulation);
  const setPremiumSimulation = useAppStore(s => s.setPremiumSimulation);
  const ms = settings.photoArts;

  const save = <K extends keyof AppSettings>(key: K, value: AppSettings[K]) =>
    updateSetting(key, value).catch(e => Alert.alert(t('settings_not_saved'), errorMessage(e)));

  // Wrapped so a missing native module surfaces as a message, not a crash.
  const loadUsage = useCallback(
    () =>
      Promise.resolve()
        .then(() => getStorageService().usage())
        .then(u => {
          setUsage(u);
          setUsageError(null);
        })
        .catch(e => setUsageError(errorMessage(e))),
    [],
  );

  useEffect(() => {
    void loadUsage();
  }, [loadUsage]);

  const clearCache = async () => {
    setClearing(true);
    try {
      const freed = await getStorageService().clearCache();
      Alert.alert(t('settings_cache_cleared'), t('settings_cache_freed', { size: formatBytes(freed) }));
      void loadUsage();
    } catch (e) {
      Alert.alert(t('settings_cache_failed'), errorMessage(e));
    } finally {
      setClearing(false);
    }
  };

  const usedFraction =
    usage && usage.deviceTotalBytes > 0 ? 1 - usage.deviceAvailableBytes / usage.deviceTotalBytes : 0;

  return (
    <Screen title={t('settings_title')} overline={t('settings_overline')}>
      <SectionHeader title={t('settings_section_premium')} />
      <GlassSurface style={{ marginBottom: 24 }}>
        <ListRow
          icon="crown"
          title={t('settings_premium_title')}
          subtitle={t('settings_premium_detail', { days: TRIAL_DAYS })}
          onPress={() => navigation.navigate('Premium')}
        />
        <ListRow
          icon="target"
          title={t('settings_rewards_title')}
          subtitle={t('settings_rewards_detail')}
          onPress={() => navigation.navigate('Rewards')}
          last
        />
      </GlassSurface>
      {simulation.available ? (
        <>
          <SectionHeader title={t('settings_section_testing')} />
          <GlassSurface style={{ marginBottom: 24 }} testID="settings-testing">
            <ToggleRow
              icon="crown"
              title={t('settings_sim_title')}
              subtitle={simulation.on ? t('settings_sim_on') : t('settings_sim_detail')}
              value={simulation.on}
              onChange={on =>
                setPremiumSimulation(on).catch(e => Alert.alert(t('settings_sim_failed'), errorMessage(e)))
              }
              last
            />
          </GlassSurface>
        </>
      ) : null}
      <SectionHeader title={t('settings_section_language')} />
      <GlassSurface style={{ marginBottom: 24 }}>
        <ListRow
          icon="globe"
          title={t('language_title')}
          subtitle={followsPhone ? t('language_system_detail', { name: language.name }) : undefined}
          value={followsPhone ? undefined : language.name}
          onPress={() => navigation.navigate('Language')}
          last
        />
      </GlassSurface>
      <SectionHeader title={t('settings_section_appearance')} />
      <GlassSurface style={{ padding: 16, gap: 14, marginBottom: 24 }}>
        <SegmentedControl
          label={t('settings_theme')}
          value={settings.themePreference}
          onChange={v => save('themePreference', v)}
          options={[
            { value: 'dark', label: t('settings_theme_dark'), icon: 'moon' },
            { value: 'light', label: t('settings_theme_light'), icon: 'sun' },
            { value: 'system', label: t('settings_theme_auto'), icon: 'refresh' },
            { value: 'lucid', label: 'Lucid', icon: 'drop' },
          ]}
        />
        <AppText variant="caption" tone="tertiary">
          {settings.themePreference === 'lucid' ? t('settings_theme_note_lucid') : t('settings_theme_note')}
        </AppText>
        {settings.themePreference === 'lucid' ? <LucidPanel /> : null}
      </GlassSurface>

      <SectionHeader title="Photo Arts" overline={t('settings_ms_detail')} />
      <GlassSurface style={{ padding: 16, gap: 14, marginBottom: 24 }} testID="settings-photo-arts">
        <SegmentedControl
          label={t('settings_ms_level')}
          value={String(ms.level) as '0' | '1' | '2'}
          onChange={v => save('photoArts', { ...ms, level: Number(v) as MastersLevel })}
          options={([0, 1, 2] as const).map(l => ({ value: String(l) as '0' | '1' | '2', label: levelName(l) }))}
        />
      </GlassSurface>
      <GlassSurface style={{ marginBottom: 24 }}>
        <ToggleRow
          icon="sliders"
          title={t('settings_ms_remember')}
          subtitle={t('settings_ms_remember_detail')}
          value={ms.remember}
          onChange={v => save('photoArts', { ...ms, remember: v })}
        />
        <ToggleRow
          icon="image"
          title={t('settings_ms_cards')}
          subtitle={t('settings_ms_cards_detail')}
          value={ms.cardPreviews}
          onChange={v => save('photoArts', { ...ms, cardPreviews: v })}
        />
        <ListRow
          icon="refresh"
          title={t('settings_ms_reset')}
          subtitle={t('settings_ms_reset_detail')}
          onPress={() => save('photoArts', { ...ms, controls: { ...ARTIST_WAY } })}
          testID="settings-ms-reset"
          last
        />
      </GlassSurface>

      <SectionHeader title={t('settings_section_camera')} />
      <GlassSurface style={{ marginBottom: 24 }}>
        <ToggleRow
          icon="grid"
          title={t('settings_grid_title')}
          subtitle={t('settings_grid_detail')}
          value={settings.cameraGrid}
          onChange={v => save('cameraGrid', v)}
        />
        <ToggleRow
          icon="sparkles"
          title={t('settings_lively_title')}
          subtitle={t('settings_lively_detail')}
          value={settings.livelyColour}
          onChange={v => save('livelyColour', v)}
        />
        <ToggleRow
          icon="flip"
          title={t('settings_mirror_title')}
          subtitle={t('settings_mirror_detail')}
          value={settings.mirrorFrontCamera}
          onChange={v => save('mirrorFrontCamera', v)}
        />
        <ToggleRow
          icon="moon"
          title={t('settings_autolight_title')}
          subtitle={t('settings_autolight_detail')}
          value={settings.autoLight}
          onChange={v => save('autoLight', v)}
        />
        <View style={{ padding: 16, gap: 10 }}>
          <AppText variant="bodyStrong">{t('settings_resolution')}</AppText>
          <SegmentedControl
            label={t('settings_resolution')}
            value={settings.cameraResolution}
            onChange={v => save('cameraResolution', v)}
            options={[
              { value: 'max', label: t('settings_resolution_max') },
              { value: '12mp', label: tx('settings_12_mp') },
              { value: '8mp', label: tx('settings_8_mp') },
              { value: '5mp', label: tx('settings_5_mp') },
            ]}
          />
          <AppText variant="caption" tone="tertiary">
            {t('settings_resolution_note')}
          </AppText>
        </View>
        <View style={{ padding: 16, gap: 10 }}>
          <AppText variant="bodyStrong">{t('settings_aspect')}</AppText>
          <SegmentedControl
            label={t('settings_aspect')}
            value={settings.cameraAspectRatio}
            onChange={v => save('cameraAspectRatio', v)}
            options={[
              { value: '4:3', label: '4:3' },
              { value: '16:9', label: '16:9' },
              { value: '1:1', label: '1:1' },
            ]}
          />
        </View>
        <ToggleRow
          icon="sparkles"
          title={t('settings_haptics')}
          value={settings.haptics}
          onChange={v => save('haptics', v)}
        />
        <View style={{ padding: 16, gap: 10 }}>
          <AppText variant="bodyStrong">{t('settings_motion')}</AppText>
          <SegmentedControl
            label={t('settings_motion')}
            value={settings.reduceMotion}
            onChange={v => save('reduceMotion', v)}
            options={[
              { value: 'system', label: t('settings_motion_system') },
              { value: 'on', label: t('settings_motion_on') },
              { value: 'off', label: t('settings_motion_off') },
            ]}
          />
        </View>
      </GlassSurface>

      <SectionHeader title={t('settings_section_export')} />
      <GlassSurface style={{ padding: 16, gap: 14, marginBottom: 24 }}>
        <AppText variant="bodyStrong">{t('settings_format')}</AppText>
        <SegmentedControl
          label={t('settings_format')}
          value={settings.defaultExportFormat}
          onChange={v => save('defaultExportFormat', v)}
          options={[
            { value: 'jpeg', label: 'JPEG' },
            { value: 'png', label: 'PNG' },
            { value: 'webp', label: 'WebP' },
          ]}
        />
        <AppText variant="bodyStrong">{t('settings_quality')}</AppText>
        <SegmentedControl
          label={t('settings_quality')}
          value={String(settings.defaultExportQuality) as '80' | '92' | '100'}
          onChange={v => save('defaultExportQuality', Number(v))}
          options={[
            { value: '80', label: t('settings_quality_compact') },
            { value: '92', label: t('settings_quality_high') },
            { value: '100', label: t('settings_quality_max') },
          ]}
        />
      </GlassSurface>

      <SectionHeader title={t('settings_section_storage')} />
      <GlassSurface style={{ marginBottom: 24 }}>
        <View style={{ padding: 16, gap: 10 }}>
          {usage ? (
            <>
              <View style={{ flexDirection: 'row', justifyContent: 'space-between' }}>
                <AppText variant="bodyStrong">
                  {t('settings_storage_uses', { size: formatBytes(usage.appTotalBytes) })}
                </AppText>
                <AppText variant="callout" tone="secondary">
                  {t('settings_storage_free', { size: formatBytes(usage.deviceAvailableBytes) })}
                </AppText>
              </View>
              <ProgressBar
                value={usedFraction}
                label={t('settings_storage_used')}
                tint={usedFraction > 0.92 ? theme.colors.danger : undefined}
              />
            </>
          ) : usageError ? (
            <AppText variant="callout" tone="danger">
              {t('settings_storage_unavailable', { error: usageError })}
            </AppText>
          ) : (
            <>
              <Skeleton height={18} width="60%" />
              <Skeleton height={8} />
            </>
          )}
        </View>
        <ListRow
          icon="image"
          title={t('settings_captures')}
          value={usage ? formatBytes(usage.areas.captures ?? 0) : '—'}
        />
        <ListRow
          icon="folder"
          title={t('settings_projects_exports')}
          value={usage ? formatBytes((usage.areas.projects ?? 0) + (usage.areas.exports ?? 0)) : '—'}
        />
        <ListRow
          icon="layers"
          title={t('settings_packs_models')}
          value={usage ? formatBytes((usage.areas.packs ?? 0) + (usage.areas.models ?? 0)) : '—'}
        />
        <ListRow
          icon="trash"
          iconTint={theme.colors.danger}
          title={clearing ? t('settings_clearing') : t('settings_clear_cache')}
          value={usage ? formatBytes(usage.cacheBytes) : undefined}
          onPress={clearing ? undefined : clearCache}
          accessibilityHint={t('settings_clear_hint')}
          last
        />
      </GlassSurface>

      <SectionHeader title={t('settings_section_help')} />
      <GlassSurface style={{ marginBottom: 24 }}>
        <ListRow
          icon="help"
          title={t('settings_help_title')}
          subtitle={t('settings_help_detail')}
          onPress={() => navigation.navigate('Help')}
          testID="settings-help"
        />
        <ListRow
          icon="lock"
          title={t('settings_privacy_title')}
          subtitle={t('settings_privacy_detail')}
          onPress={() => navigation.navigate('Privacy')}
          last
        />
      </GlassSurface>
      <SectionHeader title={t('settings_section_about')} />
      <GlassSurface>
        {DEVELOPER_TOOLS ? (
          <>
            <ListRow
              icon="cpu"
              title={t('settings_device_report')}
              subtitle={t('settings_developer_tools')}
              onPress={() => navigation.navigate('DeviceInfo')}
            />
            <ListRow
              icon="wand"
              title={t('settings_self_test')}
              subtitle={t('settings_developer_tools')}
              onPress={() => navigation.navigate('Diagnostics')}
            />
          </>
        ) : null}
        <ListRow icon="info" title={t('settings_version')} value={APP_VERSION} last />
      </GlassSurface>
    </Screen>
  );
}
