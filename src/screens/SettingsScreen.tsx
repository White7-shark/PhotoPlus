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
import type { TabScreenProps } from '../navigation/types';
import { errorMessage } from '../native';
import type { AppSettings } from '../services/database/repositories/SettingsRepository';
import { formatBytes, getStorageService, type StorageUsage } from '../services/storage/StorageService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';

const APP_VERSION = '0.2.0';

export function SettingsScreen({ navigation }: TabScreenProps<'Settings'>) {
  const theme = useTheme();
  const settings = useAppStore(s => s.settings);
  const updateSetting = useAppStore(s => s.updateSetting);
  const [usage, setUsage] = useState<StorageUsage | null>(null);
  const [usageError, setUsageError] = useState<string | null>(null);
  const [clearing, setClearing] = useState(false);

  const save = <K extends keyof AppSettings>(key: K, value: AppSettings[K]) =>
    updateSetting(key, value).catch(e => Alert.alert('Setting not saved', errorMessage(e)));

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
      Alert.alert('Cache cleared', `${formatBytes(freed)} freed.`);
      void loadUsage();
    } catch (e) {
      Alert.alert('Could not clear cache', errorMessage(e));
    } finally {
      setClearing(false);
    }
  };

  const usedFraction =
    usage && usage.deviceTotalBytes > 0 ? 1 - usage.deviceAvailableBytes / usage.deviceTotalBytes : 0;

  return (
    <Screen title="Settings" overline="Preferences">
      <SectionHeader title="Appearance" />
      <GlassSurface style={{ padding: 16, gap: 14, marginBottom: 24 }}>
        <SegmentedControl
          label="Theme"
          value={settings.themePreference}
          onChange={v => save('themePreference', v)}
          options={[
            { value: 'dark', label: 'Dark', icon: 'moon' },
            { value: 'light', label: 'Light', icon: 'sun' },
            { value: 'system', label: 'Auto', icon: 'refresh' },
          ]}
        />
        <AppText variant="caption" tone="tertiary">
          Dark is recommended for judging exposure and colour accurately.
        </AppText>
      </GlassSurface>

      <SectionHeader title="Camera & motion" />
      <GlassSurface style={{ marginBottom: 24 }}>
        <ToggleRow
          icon="grid"
          title="Composition grid"
          subtitle="Rule-of-thirds overlay in the camera"
          value={settings.cameraGrid}
          onChange={v => save('cameraGrid', v)}
        />
        <ToggleRow
          icon="flip"
          title="Mirror selfies"
          subtitle="Save front-camera photos as the preview shows them"
          value={settings.mirrorFrontCamera}
          onChange={v => save('mirrorFrontCamera', v)}
        />
        <ToggleRow
          icon="moon"
          title="Auto Light"
          subtitle="Clear photos in dark rooms and balanced ones in harsh light, with the flash or your screen when needed"
          value={settings.autoLight}
          onChange={v => save('autoLight', v)}
        />
        <View style={{ padding: 16, gap: 10 }}>
          <AppText variant="bodyStrong">Capture resolution</AppText>
          <SegmentedControl
            label="Capture resolution"
            value={settings.cameraResolution}
            onChange={v => save('cameraResolution', v)}
            options={[
              { value: 'max', label: 'Max' },
              { value: '12mp', label: '12 MP' },
              { value: '8mp', label: '8 MP' },
              { value: '5mp', label: '5 MP' },
            ]}
          />
          <AppText variant="caption" tone="tertiary">
            The camera uses the closest size its sensor offers. Smaller photos save space.
          </AppText>
        </View>
        <View style={{ padding: 16, gap: 10 }}>
          <AppText variant="bodyStrong">Aspect ratio</AppText>
          <SegmentedControl
            label="Aspect ratio"
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
          title="Haptic feedback"
          value={settings.haptics}
          onChange={v => save('haptics', v)}
        />
        <View style={{ padding: 16, gap: 10 }}>
          <AppText variant="bodyStrong">Reduce motion</AppText>
          <SegmentedControl
            label="Reduce motion"
            value={settings.reduceMotion}
            onChange={v => save('reduceMotion', v)}
            options={[
              { value: 'system', label: 'System' },
              { value: 'on', label: 'On' },
              { value: 'off', label: 'Off' },
            ]}
          />
        </View>
      </GlassSurface>

      <SectionHeader title="Export" />
      <GlassSurface style={{ padding: 16, gap: 14, marginBottom: 24 }}>
        <AppText variant="bodyStrong">Default format</AppText>
        <SegmentedControl
          label="Default export format"
          value={settings.defaultExportFormat}
          onChange={v => save('defaultExportFormat', v)}
          options={[
            { value: 'jpeg', label: 'JPEG' },
            { value: 'png', label: 'PNG' },
            { value: 'webp', label: 'WebP' },
          ]}
        />
        <AppText variant="bodyStrong">Quality</AppText>
        <SegmentedControl
          label="Default export quality"
          value={String(settings.defaultExportQuality) as '80' | '92' | '100'}
          onChange={v => save('defaultExportQuality', Number(v))}
          options={[
            { value: '80', label: 'Compact' },
            { value: '92', label: 'High' },
            { value: '100', label: 'Max' },
          ]}
        />
      </GlassSurface>

      <SectionHeader title="Storage" />
      <GlassSurface style={{ marginBottom: 24 }}>
        <View style={{ padding: 16, gap: 10 }}>
          {usage ? (
            <>
              <View style={{ flexDirection: 'row', justifyContent: 'space-between' }}>
                <AppText variant="bodyStrong">Photo Plus uses {formatBytes(usage.appTotalBytes)}</AppText>
                <AppText variant="callout" tone="secondary">
                  {formatBytes(usage.deviceAvailableBytes)} free
                </AppText>
              </View>
              <ProgressBar
                value={usedFraction}
                label="Device storage used"
                tint={usedFraction > 0.92 ? theme.colors.danger : undefined}
              />
            </>
          ) : usageError ? (
            <AppText variant="callout" tone="danger">
              Storage info unavailable: {usageError}
            </AppText>
          ) : (
            <>
              <Skeleton height={18} width="60%" />
              <Skeleton height={8} />
            </>
          )}
        </View>
        <ListRow icon="image" title="Captures" value={usage ? formatBytes(usage.areas.captures ?? 0) : '—'} />
        <ListRow
          icon="folder"
          title="Projects & exports"
          value={usage ? formatBytes((usage.areas.projects ?? 0) + (usage.areas.exports ?? 0)) : '—'}
        />
        <ListRow
          icon="layers"
          title="Packs & models"
          value={usage ? formatBytes((usage.areas.packs ?? 0) + (usage.areas.models ?? 0)) : '—'}
        />
        <ListRow
          icon="trash"
          iconTint={theme.colors.danger}
          title={clearing ? 'Clearing…' : 'Clear temporary cache'}
          value={usage ? formatBytes(usage.cacheBytes) : undefined}
          onPress={clearing ? undefined : clearCache}
          accessibilityHint="Removes temporary files. Your photos are not affected."
          last
        />
      </GlassSurface>

      <SectionHeader title="About" />
      <GlassSurface>
        {DEVELOPER_TOOLS ? (
          <>
            <ListRow
              icon="cpu"
              title="Device report"
              subtitle="Developer tools"
              onPress={() => navigation.navigate('DeviceInfo')}
            />
            <ListRow
              icon="wand"
              title="Self-test"
              subtitle="Developer tools"
              onPress={() => navigation.navigate('Diagnostics')}
            />
          </>
        ) : null}
        <ListRow icon="info" title="Version" value={APP_VERSION} last />
      </GlassSurface>
    </Screen>
  );
}
