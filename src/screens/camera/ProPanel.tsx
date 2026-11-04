import React, { useMemo, useState } from 'react';
import { Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, Icon, type IconName } from '../../components';
import {
  availablePresets,
  compensatedIso,
  focusStops,
  formatFocusDistance,
  formatIso,
  formatShutter,
  KELVIN_STOPS,
  WHITE_BALANCE_LABELS,
  type CapturePreset,
} from '../../services/camera/model';
import type { CameraCapabilities, ManualSettings, WhiteBalanceId } from '../../services/camera/types';
import { useTheme } from '../../theme';
import { StopDial } from './StopDial';
import type { Metadata } from './useCameraSession';

type Tab = 'iso' | 'shutter' | 'focus' | 'wb' | 'assist' | 'presets';

export interface ProPanelProps {
  caps: CameraCapabilities;
  manual: ManualSettings;
  metadata: Metadata | null;
  sharpness: number | null;
  raw: boolean;
  peaking: boolean;
  onManual: (next: ManualSettings) => void;
  onRaw: (enabled: boolean) => void;
  onPeaking: (enabled: boolean) => void;
  onPreset: (preset: CapturePreset) => void;
}

/**
 * Premium manual controls. Only controls the active camera really supports
 * are listed (capability detection), and every dial clamps to the sensor.
 */
export function ProPanel({
  caps,
  manual,
  metadata,
  sharpness,
  raw,
  peaking,
  onManual,
  onRaw,
  onPeaking,
  onPreset,
}: ProPanelProps) {
  const theme = useTheme();
  const m = caps.manual;
  const presets = useMemo(() => availablePresets(caps), [caps]);
  const tabs = useMemo(() => {
    const t: { id: Tab; label: string; icon: IconName }[] = [];
    if (m.exposure && m.isoStops.length) {
      t.push({ id: 'iso', label: 'ISO', icon: 'aperture' });
    }
    if (m.exposure && m.shutterStops.length) {
      t.push({ id: 'shutter', label: 'Shutter', icon: 'timer' });
    }
    if (m.focus) {
      t.push({ id: 'focus', label: 'Focus', icon: 'target' });
    }
    if (m.kelvin || m.whiteBalanceModes.length > 1) {
      t.push({ id: 'wb', label: 'WB', icon: 'thermometer' });
    }
    t.push({ id: 'assist', label: 'Assist', icon: 'sliders' });
    if (presets.length) {
      t.push({ id: 'presets', label: 'Presets', icon: 'wand' });
    }
    return t;
  }, [m, presets.length]);
  const [tab, setTab] = useState<Tab>(tabs[0]?.id ?? 'assist');
  const current = tabs.some(t => t.id === tab) ? tab : (tabs[0]?.id ?? 'assist');

  const liveIso = metadata?.iso ?? null;
  const liveShutter = metadata?.exposureTimeNs ?? null;

  const setShutter = (ns: number) => {
    // Keep brightness when only the shutter is chosen: ISO follows.
    const iso = manual.iso ?? compensatedIso(liveIso, liveShutter, ns, m.iso);
    onManual({ ...manual, exposureTimeNs: ns, iso: iso ?? manual.iso });
  };

  return (
    <View style={styles.panel} testID="pro-panel">
      <ScrollView
        horizontal
        showsHorizontalScrollIndicator={false}
        contentContainerStyle={styles.tabs}
        accessibilityRole="tablist"
      >
        {tabs.map(t => {
          const selected = t.id === current;
          const isManual =
            (t.id === 'iso' && manual.iso != null) ||
            (t.id === 'shutter' && manual.exposureTimeNs != null) ||
            (t.id === 'focus' && manual.focusDiopters != null) ||
            (t.id === 'wb' && (manual.whiteBalance ?? 'auto') !== 'auto');
          return (
            <Pressable
              key={t.id}
              onPress={() => setTab(t.id)}
              accessibilityRole="tab"
              accessibilityLabel={`${t.label}${isManual ? ', manual' : ''}`}
              accessibilityState={{ selected }}
              style={[styles.tab, selected ? styles.tabSelected : null]}
            >
              <Icon name={t.icon} size={14} color={selected ? theme.colors.premium : '#FFFFFF'} strokeWidth={2} />
              <AppText
                variant="caption"
                style={{ color: selected ? theme.colors.premium : '#FFFFFF', fontWeight: '700' }}
              >
                {t.label}
              </AppText>
              {isManual ? <View style={[styles.dot, { backgroundColor: theme.colors.premium }]} /> : null}
            </Pressable>
          );
        })}
      </ScrollView>

      <View style={styles.body}>
        {current === 'iso' ? (
          <Row onAuto={manual.iso != null ? () => onManual({ ...manual, iso: null }) : undefined}>
            <StopDial
              label="ISO"
              values={m.isoStops}
              value={manual.iso ?? null}
              format={v => `${v}`}
              autoLabel={liveIso ? `Auto · ${formatIso(liveIso)}` : 'Auto'}
              onChange={iso => onManual({ ...manual, iso })}
            />
          </Row>
        ) : null}

        {current === 'shutter' ? (
          <Row
            onAuto={
              manual.exposureTimeNs != null ? () => onManual({ ...manual, exposureTimeNs: null, iso: null }) : undefined
            }
          >
            <StopDial
              label="Shutter speed"
              values={m.shutterStops}
              value={manual.exposureTimeNs ?? null}
              format={formatShutter}
              autoLabel={liveShutter ? `Auto · ${formatShutter(liveShutter)}` : 'Auto'}
              onChange={setShutter}
            />
          </Row>
        ) : null}

        {current === 'focus' ? (
          <Row onAuto={manual.focusDiopters != null ? () => onManual({ ...manual, focusDiopters: null }) : undefined}>
            <StopDial
              label="Focus distance"
              values={focusStops(m.minFocusDiopters)}
              value={manual.focusDiopters ?? null}
              format={formatFocusDistance}
              autoLabel={
                metadata?.focusDiopters != null ? `AF · ${formatFocusDistance(metadata.focusDiopters)}` : 'Autofocus'
              }
              onChange={focusDiopters => onManual({ ...manual, focusDiopters })}
            />
          </Row>
        ) : null}

        {current === 'wb' ? (
          <View style={{ gap: 8 }}>
            <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={styles.wbRow}>
              {[...m.whiteBalanceModes, ...(m.kelvin ? (['kelvin'] as WhiteBalanceId[]) : [])].map(id => (
                <Toggle
                  key={id}
                  label={WHITE_BALANCE_LABELS[id]}
                  selected={(manual.whiteBalance ?? 'auto') === id}
                  onPress={() =>
                    onManual({ ...manual, whiteBalance: id, kelvin: id === 'kelvin' ? (manual.kelvin ?? 5500) : null })
                  }
                />
              ))}
            </ScrollView>
            {manual.whiteBalance === 'kelvin' ? (
              <StopDial
                label="Colour temperature"
                values={KELVIN_STOPS}
                value={manual.kelvin ?? 5500}
                format={k => `${k}K`}
                onChange={kelvin => onManual({ ...manual, whiteBalance: 'kelvin', kelvin })}
              />
            ) : null}
          </View>
        ) : null}

        {current === 'assist' ? (
          <View style={styles.assist}>
            {caps.focusPeaking ? (
              <Toggle
                label={peaking && sharpness !== null ? `Peaking · ${Math.round(sharpness * 100)}` : 'Focus peaking'}
                icon="target"
                selected={peaking}
                onPress={() => onPeaking(!peaking)}
              />
            ) : null}
            {caps.raw ? <Toggle label="RAW + JPEG" icon="layers" selected={raw} onPress={() => onRaw(!raw)} /> : null}
            {m.aeLock ? (
              <Toggle
                label="AE lock"
                icon="lock"
                selected={!!manual.aeLock}
                onPress={() => onManual({ ...manual, aeLock: !manual.aeLock })}
              />
            ) : null}
            {m.awbLock ? (
              <Toggle
                label="AWB lock"
                icon="lock"
                selected={!!manual.awbLock}
                onPress={() => onManual({ ...manual, awbLock: !manual.awbLock })}
              />
            ) : null}
          </View>
        ) : null}

        {current === 'presets' ? (
          <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={styles.presets}>
            {presets.map(p => (
              <Pressable
                key={p.id}
                onPress={() => onPreset(p)}
                accessibilityRole="button"
                accessibilityLabel={`${p.title} preset`}
                accessibilityHint={p.description}
                style={styles.preset}
              >
                <AppText variant="bodyStrong" style={{ color: '#FFFFFF' }}>
                  {p.title}
                </AppText>
                <AppText variant="caption" style={{ color: 'rgba(255,255,255,0.7)' }} numberOfLines={2}>
                  {p.description}
                </AppText>
              </Pressable>
            ))}
          </ScrollView>
        ) : null}
      </View>
    </View>
  );
}

function Row({ children, onAuto }: { children: React.ReactNode; onAuto?: () => void }) {
  return (
    <View style={styles.row}>
      <View style={{ flex: 1 }}>{children}</View>
      <Pressable
        onPress={onAuto}
        disabled={!onAuto}
        accessibilityRole="button"
        accessibilityLabel="Back to automatic"
        accessibilityState={{ disabled: !onAuto }}
        style={[styles.auto, { opacity: onAuto ? 1 : 0.4 }]}
        hitSlop={8}
      >
        <AppText variant="caption" style={{ color: '#FFFFFF', fontWeight: '800' }}>
          A
        </AppText>
      </Pressable>
    </View>
  );
}

function Toggle({
  label,
  icon,
  selected,
  onPress,
}: {
  label: string;
  icon?: IconName;
  selected: boolean;
  onPress: () => void;
}) {
  const theme = useTheme();
  return (
    <Pressable
      onPress={onPress}
      accessibilityRole="switch"
      accessibilityLabel={label}
      accessibilityState={{ checked: selected }}
      style={[
        styles.toggle,
        selected ? { borderColor: theme.colors.premium, backgroundColor: 'rgba(245,198,107,0.16)' } : null,
      ]}
    >
      {icon ? <Icon name={icon} size={14} color={selected ? theme.colors.premium : '#FFFFFF'} strokeWidth={2} /> : null}
      <AppText variant="caption" style={{ color: selected ? theme.colors.premium : '#FFFFFF', fontWeight: '700' }}>
        {label}
      </AppText>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  panel: {
    marginHorizontal: 12,
    borderRadius: 24,
    backgroundColor: 'rgba(10,9,17,0.66)',
    borderWidth: StyleSheet.hairlineWidth * 2,
    borderColor: 'rgba(255,255,255,0.14)',
    paddingVertical: 10,
    gap: 6,
    backgroundImage:
      'linear-gradient(160deg, rgba(255,255,255,0.12) 0%, rgba(255,255,255,0.02) 40%, rgba(255,255,255,0) 100%)',
  },
  tabs: { paddingHorizontal: 10, gap: 6 },
  tab: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 5,
    paddingHorizontal: 12,
    height: 34,
    borderRadius: 17,
  },
  tabSelected: { backgroundColor: 'rgba(255,255,255,0.12)' },
  dot: { width: 5, height: 5, borderRadius: 3 },
  body: { paddingHorizontal: 12, minHeight: 84, justifyContent: 'center' },
  row: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  auto: {
    width: 36,
    height: 36,
    borderRadius: 18,
    alignItems: 'center',
    justifyContent: 'center',
    borderWidth: 1.5,
    borderColor: 'rgba(255,255,255,0.6)',
  },
  wbRow: { gap: 6 },
  assist: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  toggle: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    height: 36,
    paddingHorizontal: 12,
    borderRadius: 18,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.22)',
  },
  presets: { gap: 8 },
  preset: {
    width: 150,
    padding: 12,
    borderRadius: 16,
    gap: 4,
    backgroundColor: 'rgba(255,255,255,0.08)',
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.14)',
  },
});
