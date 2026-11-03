import React, { useState } from 'react';
import { View } from 'react-native';

import { AppText, Badge, EmptyState, GlassSurface, ListRow, Screen, SectionHeader, StatTile } from '../components';
import type { RootScreenProps } from '../navigation/types';
import { cameraFeatureSupport, detectCapabilities, modelCompatibility, primaryCamera, storageHealth, tierLabel, classifyTier } from '../services/capabilities';
import { formatBytes } from '../services/storage/StorageService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';

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
      <Screen title="Device" onBack={navigation.goBack} bottomInset={24}>
        <EmptyState icon="cpu" title="Device profile unavailable" message="Capabilities could not be detected on this launch." actionLabel="Try again" onAction={refresh} />
      </Screen>
    );
  }

  const support = cameraFeatureSupport(caps);
  const back = primaryCamera(caps, 'back');
  const models = modelCompatibility(caps);
  const storage = storageHealth(caps.storage.availableBytes);
  const tierTone = tier === 'performance' ? 'success' : tier === 'standard' ? 'accent' : tier === 'entry' ? 'warning' : 'danger';

  return (
    <Screen title="Device" overline={`${caps.os.manufacturer} ${caps.os.model}`} onBack={navigation.goBack} bottomInset={24} onRefresh={refresh} refreshing={refreshing}>
      <View style={{ flexDirection: 'row', gap: 8, marginBottom: 16, flexWrap: 'wrap' }}>
        <Badge label={`${tierLabel[tier]} tier`} tone={tierTone} icon="shield" />
        <Badge label={`Android ${caps.os.release} · API ${caps.os.sdkInt}`} />
        {stale ? <Badge label="Cached profile" tone="warning" /> : null}
      </View>

      <View style={{ flexDirection: 'row', gap: 10, marginBottom: 10 }}>
        <StatTile icon="memory" label="RAM" value={formatBytes(caps.memory.totalBytes, 1)} />
        <StatTile icon="storage" label="Free storage" value={formatBytes(caps.storage.availableBytes, 1)} tint={storage.level === 'ok' ? theme.colors.accentCool : theme.colors.warning} />
      </View>
      <View style={{ flexDirection: 'row', gap: 10, marginBottom: 24 }}>
        <StatTile icon="cpu" label="CPU cores" value={String(caps.cpu.cores)} tint={theme.colors.info} />
        <StatTile icon="camera" label="Cameras" value={String(caps.cameras.length)} tint={theme.colors.premium} />
      </View>

      <SectionHeader title="Camera controls" overline={back ? `Main sensor · ${back.hardwareLevel}` : 'No back camera'} />
      <GlassSurface style={{ marginBottom: 24 }}>
        <ListRow title="Manual shutter speed" value={yesNo(support.manualExposure)} />
        <ListRow title="Manual ISO" value={back?.ranges.iso ? `${back.ranges.iso[0]}–${back.ranges.iso[1]}` : yesNo(false)} />
        <ListRow title="Manual focus" value={yesNo(support.manualFocus)} />
        <ListRow title="White balance control" value={yesNo(support.manualWhiteBalance)} />
        <ListRow title="RAW capture" value={yesNo(support.rawCapture)} />
        <ListRow title="Exposure compensation" value={yesNo(support.exposureCompensation)} />
        <ListRow title="Zoom" value={back?.ranges.zoomRatio ? `${back.ranges.zoomRatio[0]}× – ${back.ranges.zoomRatio[1]}×` : yesNo(support.zoom)} />
        <ListRow title="Flash" value={yesNo(support.flash)} />
        <ListRow title="Front camera" value={yesNo(support.frontCamera)} last />
      </GlassSurface>

      <SectionHeader title="Formats & acceleration" />
      <GlassSurface style={{ marginBottom: 24 }}>
        <ListRow title="Decode" value={caps.imageFormats.decode.join(', ').toUpperCase()} />
        <ListRow title="Export" value={caps.imageFormats.encode.join(', ').toUpperCase()} />
        <ListRow title="OpenGL ES" value={caps.acceleration.glEsVersion} />
        <ListRow title="Vulkan" value={caps.acceleration.vulkan ? caps.acceleration.vulkanVersion ?? 'Yes' : 'No'} />
        <ListRow title="Processing engine" value={engine ? `v${engine.version} · ${engine.workers} worker${engine.workers > 1 ? 's' : ''}` : 'Unavailable'} last />
      </GlassSurface>

      <SectionHeader title="On-device AI models" overline="Planned for later phases" />
      <GlassSurface>
        {models.map((m, i) => (
          <ListRow
            key={m.model.id}
            title={m.model.name}
            subtitle={m.compatible ? `${m.model.purpose} · ${m.delegate?.toUpperCase()}` : m.reason}
            right={<Badge label={m.compatible ? 'Ready' : 'Limited'} tone={m.compatible ? 'success' : 'warning'} />}
            last={i === models.length - 1}
          />
        ))}
      </GlassSurface>
      <AppText variant="caption" tone="tertiary" style={{ marginTop: 12 }}>
        Profiles are measured on this device and stay on it.
      </AppText>
    </Screen>
  );
}
