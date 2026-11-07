import React, { useState } from 'react';
import { Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, Icon, PreviewTile } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { BEAUTY_PRESETS, presetCover } from '../../services/beauty/catalog';
import type { CameraBeauty } from '../../services/beauty/camera';
import { GROUP_MAX, type BeautyAdjust, type PortraitControls } from '../../services/beauty/recipe';
import { useTheme } from '../../theme';

const pct = (v: number) => `${Math.round(v * 100)}%`;
const signed = (v: number) => (Math.round(v * 100) === 0 ? '0' : `${v > 0 ? '+' : ''}${Math.round(v * 100)}`);

/**
 * Beauty in the camera: the 13 presets (each with its cover), Intensity, and
 * the Skin / Face / Lighting / Background sliders. Changes show live.
 */
export function BeautyTray({
  beauty,
  premiumAllowed,
  liveNote,
  onChange,
  onLocked,
}: {
  beauty: CameraBeauty;
  premiumAllowed: boolean;
  /** Shown when this phone cannot show the live preview (photos still get the filter). */
  liveNote: string | null;
  onChange: (next: CameraBeauty) => void;
  onLocked: () => void;
}) {
  const [adjusting, setAdjusting] = useState(false);
  const setAdjust = (patch: Partial<BeautyAdjust>) => onChange({ ...beauty, adjust: { ...beauty.adjust, ...patch } });
  return (
    <View style={styles.panel} testID="beauty-tray">
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 10 }}>
        <PreviewTile
          label="Off"
          placeholderIcon="close"
          width={64}
          height={64}
          selected={!beauty.on}
          onPress={() => onChange({ ...beauty, on: false })}
          testID="beauty-off"
        />
        {BEAUTY_PRESETS.map(p => {
          const locked = p.tier === 'premium' && !premiumAllowed;
          return (
            <PreviewTile
              key={p.id}
              label={p.name}
              source={presetCover(p.id)}
              width={64}
              height={64}
              locked={locked}
              selected={beauty.on && beauty.preset === p.id}
              onPress={() => (locked ? onLocked() : onChange({ ...beauty, on: true, preset: p.id }))}
              testID={`beauty-${p.id}`}
            />
          );
        })}
      </ScrollView>
      {beauty.on ? (
        <>
          <ValueSlider
            label="Intensity"
            value={beauty.adjust.intensity}
            onChange={v => setAdjust({ intensity: v })}
            format={pct}
            testID="beauty-intensity"
          />
          <Pressable
            onPress={() => setAdjusting(a => !a)}
            accessibilityRole="button"
            accessibilityLabel={adjusting ? 'Hide fine adjustments' : 'Adjust skin, face, lighting and background'}
            accessibilityState={{ expanded: adjusting }}
            style={styles.adjustRow}
            hitSlop={8}
            testID="beauty-adjust"
          >
            <Icon name="sliders" size={14} color="#FFFFFF" strokeWidth={2} />
            <AppText variant="caption" style={styles.adjustLabel}>
              {adjusting ? 'Less' : 'Adjust'}
            </AppText>
          </Pressable>
          {adjusting ? (
            <View style={{ gap: 8 }}>
              {(
                [
                  ['skin', 'Skin'],
                  ['face', 'Face'],
                  ['lighting', 'Lighting'],
                  ['background', 'Background'],
                ] as const
              ).map(([key, label]) => (
                <ValueSlider
                  key={key}
                  label={label}
                  value={beauty.adjust[key]}
                  max={GROUP_MAX}
                  onChange={v => setAdjust({ [key]: v })}
                  format={pct}
                  testID={`beauty-${key}`}
                />
              ))}
            </View>
          ) : null}
        </>
      ) : null}
      {liveNote ? (
        <AppText variant="caption" style={styles.note} accessibilityLiveRegion="polite">
          {liveNote}
        </AppText>
      ) : null}
    </View>
  );
}

/** Hold to see the camera without beauty. */
export function OriginalButton({ onHold }: { onHold: (held: boolean) => void }) {
  const theme = useTheme();
  const [held, setHeld] = useState(false);
  const set = (v: boolean) => {
    setHeld(v);
    onHold(v);
  };
  return (
    <Pressable
      onPressIn={() => set(true)}
      onPressOut={() => set(false)}
      accessibilityRole="button"
      accessibilityLabel="Hold to see the original"
      accessibilityState={{ selected: held }}
      hitSlop={8}
      style={[
        styles.original,
        held ? { backgroundColor: 'rgba(255,255,255,0.22)', borderColor: theme.colors.premium } : null,
      ]}
      testID="beauty-original"
    >
      <Icon name="compare" size={14} color="#FFFFFF" strokeWidth={2} />
      <AppText variant="caption" style={styles.adjustLabel}>
        {held ? 'Original' : 'Hold: original'}
      </AppText>
    </Pressable>
  );
}

/**
 * Portrait (Spotlight) controls, independent of the beauty preset: depth
 * blur, beauty intensity, subject and background light, background tone and
 * edge refinement.
 */
export function PortraitControlsPanel({
  controls,
  intensity,
  points,
  onControls,
  onIntensity,
  onClear,
}: {
  controls: PortraitControls;
  /** The beauty preset's intensity (null when beauty is off). */
  intensity: number | null;
  points: number;
  onControls: (next: PortraitControls) => void;
  onIntensity: (v: number) => void;
  onClear: () => void;
}) {
  const [more, setMore] = useState(false);
  const set = (patch: Partial<PortraitControls>) => onControls({ ...controls, ...patch });
  return (
    <View style={styles.panel} testID="spotlight-panel">
      <View style={styles.hintRow}>
        <Icon name="target" size={14} color="#FFFFFF" strokeWidth={2} />
        <AppText
          variant="caption"
          style={{ color: 'rgba(255,255,255,0.86)', flex: 1 }}
          accessibilityLiveRegion="polite"
        >
          {points === 0 ? 'Tap what should shine — or let us find it' : `${points} chosen · tap again to remove`}
        </AppText>
        {points > 0 ? (
          <Pressable onPress={onClear} accessibilityRole="button" accessibilityLabel="Clear choices" hitSlop={10}>
            <AppText variant="caption" style={{ color: '#FFFFFF', fontWeight: '800' }}>
              Clear
            </AppText>
          </Pressable>
        ) : null}
      </View>
      <ValueSlider
        label="Depth blur"
        value={controls.depthBlur}
        onChange={v => set({ depthBlur: v })}
        format={pct}
        testID="portrait-blur"
      />
      {intensity !== null ? (
        <ValueSlider label="Beauty" value={intensity} onChange={onIntensity} format={pct} testID="portrait-beauty" />
      ) : null}
      <Pressable
        onPress={() => setMore(m => !m)}
        accessibilityRole="button"
        accessibilityLabel={more ? 'Fewer portrait controls' : 'More portrait controls: light, tone and edges'}
        accessibilityState={{ expanded: more }}
        style={styles.adjustRow}
        hitSlop={8}
        testID="portrait-more"
      >
        <Icon name="sliders" size={14} color="#FFFFFF" strokeWidth={2} />
        <AppText variant="caption" style={styles.adjustLabel}>
          {more ? 'Less' : 'Light, tone & edges'}
        </AppText>
      </Pressable>
      {more ? (
        <View style={{ gap: 8 }}>
          <ValueSlider
            label="Subject light"
            value={controls.subjectLight}
            min={-1}
            onChange={v => set({ subjectLight: v })}
            format={signed}
            testID="portrait-subject-light"
          />
          <ValueSlider
            label="Background light"
            value={controls.backgroundLight}
            min={-1}
            onChange={v => set({ backgroundLight: v })}
            format={signed}
            testID="portrait-bg-light"
          />
          <ValueSlider
            label="Background tone"
            value={controls.backgroundTone}
            min={-1}
            onChange={v => set({ backgroundTone: v })}
            format={v =>
              Math.abs(v) < 0.005 ? 'Neutral' : `${v > 0 ? 'Warm' : 'Cool'} ${Math.round(Math.abs(v) * 100)}`
            }
            testID="portrait-bg-tone"
          />
          <ValueSlider
            label="Edge refinement"
            value={controls.edge}
            onChange={v => set({ edge: v })}
            format={pct}
            testID="portrait-edge"
          />
        </View>
      ) : null}
    </View>
  );
}

const styles = StyleSheet.create({
  panel: {
    marginHorizontal: 12,
    borderRadius: 22,
    padding: 12,
    gap: 10,
    backgroundColor: 'rgba(10,9,17,0.72)',
    borderWidth: StyleSheet.hairlineWidth * 2,
    borderColor: 'rgba(255,255,255,0.14)',
  },
  hintRow: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  adjustRow: { flexDirection: 'row', alignItems: 'center', gap: 6, alignSelf: 'flex-start' },
  adjustLabel: { color: '#FFFFFF', fontWeight: '700' },
  note: { color: 'rgba(255,255,255,0.7)' },
  original: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    height: 32,
    paddingHorizontal: 12,
    borderRadius: 16,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.22)',
    backgroundColor: 'rgba(10,9,17,0.55)',
    alignSelf: 'center',
  },
});
