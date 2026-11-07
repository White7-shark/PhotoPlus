import React from 'react';
import { Pressable, StyleSheet, View } from 'react-native';

import { AppText, Icon } from '../../components';
import { GLIDE_SPOTLIGHT_LEVELS, type GlideSpotlight } from '../../services/camera/glide';
import { BLUR_LEVELS, GLOW_LEVELS, type BlurLevel, type GlowLevel } from '../../services/camera/model';
import { useTheme } from '../../theme';

/** Numbered markers on what the user chose to make stand out. */
export function SpotlightMarkers({
  points,
  frame,
}: {
  points: readonly { x: number; y: number }[];
  frame: { width: number; height: number };
}) {
  const theme = useTheme();
  return (
    <View pointerEvents="none" style={StyleSheet.absoluteFill} accessibilityLabel={`${points.length} subjects chosen`}>
      {points.map((p, i) => (
        <View
          key={`${p.x.toFixed(3)}-${p.y.toFixed(3)}`}
          style={[
            styles.marker,
            { left: p.x * frame.width - 17, top: p.y * frame.height - 17, borderColor: theme.colors.premium },
          ]}
        >
          <View style={[styles.markerCore, { backgroundColor: theme.colors.premium }]}>
            <AppText variant="caption" style={{ color: '#1A1206', fontWeight: '800', fontSize: 11 }}>
              {i + 1}
            </AppText>
          </View>
        </View>
      ))}
    </View>
  );
}

function Pill({ label, selected, onPress }: { label: string; selected: boolean; onPress: () => void }) {
  const theme = useTheme();
  return (
    <Pressable
      onPress={onPress}
      accessibilityRole="radio"
      accessibilityLabel={label}
      accessibilityState={{ selected }}
      hitSlop={6}
      style={[
        styles.pill,
        selected ? { borderColor: theme.colors.premium, backgroundColor: 'rgba(245,198,107,0.16)' } : null,
      ]}
    >
      <AppText variant="caption" style={{ color: selected ? theme.colors.premium : '#FFFFFF', fontWeight: '700' }}>
        {label}
      </AppText>
    </Pressable>
  );
}

/** Spotlight options: background blur and Glow (skin), plus the subject hint. */
export function SpotlightPanel({
  blur,
  glow,
  points,
  onBlur,
  onGlow,
  onClear,
}: {
  blur: BlurLevel;
  glow: GlowLevel;
  points: number;
  onBlur: (b: BlurLevel) => void;
  onGlow: (g: GlowLevel) => void;
  onClear: () => void;
}) {
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
      <View style={styles.row} accessibilityRole="radiogroup" accessibilityLabel="Background blur">
        <AppText variant="caption" style={styles.rowLabel}>
          Blur
        </AppText>
        {BLUR_LEVELS.map(l => (
          <Pill key={l.id} label={l.label} selected={blur === l.id} onPress={() => onBlur(l.id)} />
        ))}
      </View>
      <View style={styles.row} accessibilityRole="radiogroup" accessibilityLabel="Glow skin">
        <AppText variant="caption" style={styles.rowLabel}>
          Glow
        </AppText>
        {GLOW_LEVELS.map(l => (
          <Pill key={l.id} label={l.label} selected={glow === l.id} onPress={() => onGlow(l.id)} />
        ))}
      </View>
    </View>
  );
}

/** Glide options: Spotlight for the clip (beauty follows the camera's Beauty filter). */
export function GlidePanel({
  spotlight,
  look,
  audio,
  onSpotlight,
}: {
  spotlight: GlideSpotlight;
  /** The beauty filter's name, when one is on. */
  look: string | undefined;
  audio: boolean;
  onSpotlight: (s: GlideSpotlight) => void;
}) {
  return (
    <View style={styles.panel} testID="glide-panel">
      <View style={styles.hintRow}>
        <Icon name="play" size={14} color="#FFFFFF" strokeWidth={2} />
        <AppText variant="caption" style={{ color: 'rgba(255,255,255,0.86)', flex: 1 }}>
          {`Up to 7 seconds · steadied, cleaned and sharpened${look ? ` · ${look}` : ''}${audio ? '' : ' · no sound'}`}
        </AppText>
      </View>
      <AppText variant="caption" style={[styles.rowLabel, { width: undefined }]}>
        Spotlight · background blur
      </AppText>
      <View style={styles.row} accessibilityRole="radiogroup" accessibilityLabel="Spotlight background blur">
        {GLIDE_SPOTLIGHT_LEVELS.map(l => (
          <Pill key={l.id} label={l.label} selected={spotlight === l.id} onPress={() => onSpotlight(l.id)} />
        ))}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  marker: {
    position: 'absolute',
    width: 34,
    height: 34,
    borderRadius: 17,
    borderWidth: 2,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(10,9,17,0.25)',
  },
  markerCore: { width: 18, height: 18, borderRadius: 9, alignItems: 'center', justifyContent: 'center' },
  panel: {
    marginHorizontal: 12,
    borderRadius: 22,
    padding: 12,
    gap: 10,
    backgroundColor: 'rgba(10,9,17,0.66)',
    borderWidth: StyleSheet.hairlineWidth * 2,
    borderColor: 'rgba(255,255,255,0.14)',
    backgroundImage:
      'linear-gradient(160deg, rgba(255,255,255,0.12) 0%, rgba(255,255,255,0.02) 40%, rgba(255,255,255,0) 100%)',
  },
  hintRow: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  row: { flexDirection: 'row', alignItems: 'center', gap: 6, flexWrap: 'wrap' },
  rowLabel: { color: 'rgba(255,255,255,0.7)', width: 38, fontWeight: '700' },
  pill: {
    height: 32,
    paddingHorizontal: 12,
    borderRadius: 16,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.22)',
    alignItems: 'center',
    justifyContent: 'center',
  },
});
