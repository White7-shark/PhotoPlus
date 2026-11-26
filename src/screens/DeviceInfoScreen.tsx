import React, { useState } from 'react';
import { View } from 'react-native';

import { AppText, Badge, EmptyState, GlassSurface, ListRow, Screen, SectionHeader, StatTile } from '../components';
import type { RootScreenProps } from '../navigation/types';
import {
  cameraFeatureSupport,
  detectCapabilities,
  modelCompatibility,
  primaryCamera,
  storageHealth,
  tierLabel,
  classifyTier,
} from '../services/capabilities';
import { formatBytes } from '../services/storage/StorageService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';
import { t as tx } from '../i18n';

const yesNo = (v: boolean) => (v ? 'Supported' : 'Not available');

export function DeviceInfoScreen({ navigation }: RootScreenProps<'DeviceInfo'>) {
  const theme = useTheme();
  const caps = useAppStore(s => s.capabilities);
  const tier = useAppStore(s => s.tier);
  const stale = useAppStore(s => s.capabilitiesStale);
  const engine = useAppStore(s => s.engine);
  const [refreshing, setRefreshing] = useState(false);

  const refresh = async () => {
    setRefreshing(true);
    try {
      const fresh = await detectCapabilities();
      useAppStore.setState({ capabilities: fresh, tier: classifyTier(fresh), capabilitiesStale: false });
    } catch {
      // keep previous snapshot
    } finally {
      setRefreshing(false);
    }
  };

  if (!caps) {
    return (
      <Screen title={tx('device_info_device')} onBack={navigation.goBack} bottomInset={24}>
        <EmptyState
          icon="cpu"
          title={tx('device_info_device_profile_unavailable')}
          message={tx('device_info_capabilities_could_not_be_detected')}
          actionLabel={tx('device_info_try_again')}
          onAction={refresh}
        />
      </Screen>
    );
  }

  const support = cameraFeatureSupport(caps);
  const back = primaryCamera(caps, 'back');
  const models = modelCompatibility(caps);
  const storage = storageHealth(caps.storage.availableBytes);
  const tierTone =
    tier === 'performance' ? 'success' : tier === 'standard' ? 'accent' : tier === 'entry' ? 'warning' : 'danger';

  return (
    <Screen
      title={tx('device_info_device')}
      overline={`${caps.os.manufacturer} ${caps.os.model}`}
      onBack={navigation.goBack}
      bottomInset={24}
      onRefresh={refresh}
      refreshing={refreshing}
    >
      <View style={{ flexDirection: 'row', gap: 8, marginBottom: 16, flexWrap: 'wrap' }}>
        <Badge label={`${tierLabel[tier]} tier`} tone={tierTone} icon="shield" />
        <Badge label={tx('device_info_android_api', { release: caps.os.release, sdkInt: caps.os.sdkInt })} />
        {stale ? <Badge label={tx('device_info_cached_profile')} tone="warning" /> : null}
      </View>

      <View style={{ flexDirection: 'row', gap: 10, marginBottom: 10 }}>
        <StatTile icon="memory" label={tx('device_info_ram')} value={formatBytes(caps.memory.totalBytes, 1)} />
        <StatTile
          icon="storage"
          label={tx('device_info_free_storage')}
          value={formatBytes(caps.storage.availableBytes, 1)}
          tint={storage.level === 'ok' ? theme.colors.accentCool : theme.colors.warning}
        />
      </View>
      <View style={{ flexDirection: 'row', gap: 10, marginBottom: 24 }}>
        <StatTile
          icon="cpu"
          label={tx('device_info_cpu_cores')}
          value={String(caps.cpu.cores)}
          tint={theme.colors.info}
        />
        <StatTile
          icon="camera"
          label={tx('device_info_cameras')}
          value={String(caps.cameras.length)}
          tint={theme.colors.premium}
        />
      </View>

      <SectionHeader
        title={tx('device_info_camera_controls')}
        overline={
          back ? tx('device_info_main_sensor', { hardwareLevel: back.hardwareLevel }) : tx('device_info_no_back_camera')
        }
      />
      <GlassSurface style={{ marginBottom: 24 }}>
        <ListRow title={tx('device_info_manual_shutter_speed')} value={yesNo(support.manualExposure)} />
        <ListRow
          title={tx('device_info_manual_iso')}
          value={back?.ranges.iso ? `${back.ranges.iso[0]}–${back.ranges.iso[1]}` : yesNo(false)}
        />
        <ListRow title={tx('device_info_manual_focus')} value={yesNo(support.manualFocus)} />
        <ListRow title={tx('device_info_white_balance_control')} value={yesNo(support.manualWhiteBalance)} />
        <ListRow title={tx('device_info_raw_capture')} value={yesNo(support.rawCapture)} />
        <ListRow title={tx('device_info_exposure_compensation')} value={yesNo(support.exposureCompensation)} />
        <ListRow
          title={tx('device_info_zoom')}
          value={
            back?.ranges.zoomRatio ? `${back.ranges.zoomRatio[0]}× – ${back.ranges.zoomRatio[1]}×` : yesNo(support.zoom)
          }
        />
        <ListRow title={tx('device_info_flash')} value={yesNo(support.flash)} />
        <ListRow title={tx('device_info_front_camera')} value={yesNo(support.frontCamera)} last />
      </GlassSurface>

      <SectionHeader title={tx('device_info_formats_acceleration')} />
      <GlassSurface style={{ marginBottom: 24 }}>
        <ListRow title={tx('device_info_decode')} value={caps.imageFormats.decode.join(', ').toUpperCase()} />
        <ListRow title={tx('device_info_export')} value={caps.imageFormats.encode.join(', ').toUpperCase()} />
        <ListRow title={tx('device_info_opengl_es')} value={caps.acceleration.glEsVersion} />
        <ListRow
          title={tx('device_info_vulkan')}
          value={
            caps.acceleration.vulkan ? (caps.acceleration.vulkanVersion ?? tx('device_info_yes')) : tx('device_info_no')
          }
        />
        <ListRow
          title={tx('device_info_processing_engine')}
          value={
            engine
              ? tx('device_info_v_worker', {
                  version: engine.version,
                  workers: engine.workers,
                  value: engine.workers > 1 ? 's' : '',
                })
              : tx('device_info_unavailable')
          }
          last
        />
      </GlassSurface>

      <SectionHeader
        title={tx('device_info_on_device_ai_models')}
        overline={tx('device_info_planned_for_later_phases')}
      />
      <GlassSurface>
        {models.map((m, i) => (
          <ListRow
            key={m.model.id}
            title={m.model.name}
            subtitle={m.compatible ? `${m.model.purpose} · ${m.delegate?.toUpperCase()}` : m.reason}
            right={
              <Badge
                label={m.compatible ? tx('device_info_ready') : tx('device_info_limited')}
                tone={m.compatible ? 'success' : 'warning'}
              />
            }
            last={i === models.length - 1}
          />
        ))}
      </GlassSurface>
      <AppText variant="caption" tone="tertiary" style={{ marginTop: 12 }}>
        {tx('device_info_profiles_are_measured_on_this')}
      </AppText>
    </Screen>
  );
}
