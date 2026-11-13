import React, { useState } from 'react';
import { Modal, Pressable, ScrollView, StyleSheet, View } from 'react-native';
import Svg from 'react-native-svg';

import { AppText, Button, GlassSurface, Icon, SegmentedControl, ToggleRow } from '../../../components';
import { errorCode } from '../../../native';
import { friendlyMessage } from '../../../services/errors';
import { poseFromPhoto } from '../../../services/pose/fromPhoto';
import {
  CATEGORY_LABELS,
  canUsePose,
  posesByCategory,
  shown,
  type PoseCategory,
  type PoseDef,
} from '../../../services/pose/library';
import { MAX_MY_POSES, type GridStyle, type PoseAssistantSettings } from '../../../services/pose/settings';
import type { PosePhotoResult } from '../../../services/camera/types';
import { useTheme } from '../../../theme';
import { PoseSilhouette } from './PoseSilhouette';

const GRID_OPTIONS: { value: 'off' | GridStyle; label: string }[] = [
  { value: 'off', label: 'Off' },
  { value: 'thirds', label: 'Thirds' },
  { value: 'golden', label: 'Golden' },
  { value: 'cross', label: 'Centre' },
  { value: 'diagonals', label: 'Diagonal' },
];

function PoseTile({
  pose,
  selected,
  locked,
  onPress,
  onRemove,
}: {
  pose: PoseDef;
  selected: boolean;
  locked: boolean;
  onPress: () => void;
  onRemove?: () => void;
}) {
  const theme = useTheme();
  return (
    <Pressable
      onPress={onPress}
      accessibilityRole="button"
      accessibilityLabel={`${pose.name}${locked ? ', Premium' : ''}`}
      accessibilityState={{ selected }}
      style={[styles.tile, selected ? { borderColor: theme.colors.premium } : null]}
    >
      <Svg width={64} height={86}>
        <PoseSilhouette
          points={pose.landmarks.map(p => ({ x: p[0], y: p[1], ok: shown(p) }))}
          unit={pose.unit}
          width={64}
          height={86}
          color={selected ? theme.colors.premium : '#FFFFFF'}
          opacity={locked ? 0.45 : 1}
        />
      </Svg>
      <AppText variant="caption" numberOfLines={2} style={styles.tileName}>
        {pose.name}
      </AppText>
      {locked ? (
        <View style={styles.lock}>
          <Icon name="crown" size={11} color={theme.colors.premium} strokeWidth={2.2} />
        </View>
      ) : null}
      {onRemove ? (
        <Pressable
          onPress={onRemove}
          hitSlop={8}
          style={styles.remove}
          accessibilityRole="button"
          accessibilityLabel={`Remove ${pose.name}`}
        >
          <Icon name="close" size={12} color="#FFFFFF" strokeWidth={2.4} />
        </Pressable>
      ) : null}
    </Pressable>
  );
}

/**
 * The photo assistant: framing help (grids, level, face position) for
 * everyone; poses to copy (simple demonstrations free, the full library and
 * poses from your own photos with Premium); and, with Premium, live matching
 * with step-by-step guidance and an automatic photo when the pose is right.
 */
export function PoseSheet({
  visible,
  onClose,
  premium,
  grid,
  onGrid,
  settings,
  onSettings,
  myPoses,
  onMyPoses,
  pickPhoto,
  analyzePhoto,
  onPremium,
}: {
  visible: boolean;
  onClose: () => void;
  premium: boolean;
  grid: 'off' | GridStyle;
  onGrid: (g: 'off' | GridStyle) => void;
  settings: PoseAssistantSettings;
  onSettings: (s: PoseAssistantSettings) => void;
  myPoses: PoseDef[];
  onMyPoses: (p: PoseDef[]) => void;
  pickPhoto: () => Promise<string | null>;
  analyzePhoto: (uri: string) => Promise<PosePhotoResult>;
  onPremium: () => void;
}) {
  const theme = useTheme();
  const groups = posesByCategory(myPoses);
  const [category, setCategory] = useState<PoseCategory>(groups.find(g => g.category !== 'mine')?.category ?? 'portrait');
  const [busy, setBusy] = useState(false);
  const [note, setNote] = useState<string | null>(null);
  const shownGroup = groups.find(g => g.category === category) ?? groups[0];
  const set = (patch: Partial<PoseAssistantSettings>) => onSettings({ ...settings, ...patch });

  const choose = (pose: PoseDef) => {
    if (!canUsePose(pose, premium)) {
      onPremium();
      return;
    }
    set({ poseId: settings.poseId === pose.id ? null : pose.id });
  };

  const addFromPhoto = async () => {
    if (!premium) {
      onPremium();
      return;
    }
    if (myPoses.length >= MAX_MY_POSES) {
      setNote(`You can keep up to ${MAX_MY_POSES} poses. Remove one to add another.`);
      return;
    }
    setBusy(true);
    setNote(null);
    try {
      const out = await poseFromPhoto(pickPhoto, analyzePhoto, myPoses.length + 1);
      if (out.kind === 'pose') {
        onMyPoses([out.pose, ...myPoses]);
        set({ poseId: out.pose.id });
        setCategory('mine');
      } else if (out.kind === 'noPerson') {
        setNote('No one was found in that photo. Try one where the person is clearly visible.');
      }
    } catch (e) {
      setNote(friendlyMessage(errorCode(e), 'That photo couldn’t be read. Try another one.'));
    } finally {
      setBusy(false);
    }
  };

  return (
    <Modal transparent animationType="fade" visible={visible} onRequestClose={onClose} statusBarTranslucent>
      <Pressable style={styles.scrim} onPress={onClose} accessibilityRole="button" accessibilityLabel="Close the photo assistant">
        <Pressable onPress={() => undefined} style={styles.wrap}>
          <GlassSurface intensity="strong" radius={28} style={{ paddingVertical: 18 }}>
            <ScrollView style={{ maxHeight: 620 }} contentContainerStyle={{ gap: 14, paddingHorizontal: 18 }}>
              <View style={styles.header}>
                <Icon name="figure" size={20} color={theme.colors.premium} />
                <AppText variant="title3" accessibilityRole="header" style={{ flex: 1 }}>
                  Photo assistant
                </AppText>
              </View>

              <AppText variant="bodyStrong">Framing</AppText>
              <SegmentedControl label="Grid" options={GRID_OPTIONS} value={grid} onChange={onGrid} />
              <ToggleRow
                icon="exposure"
                title="Level"
                subtitle="A line that turns green when the phone is straight"
                value={settings.level}
                onChange={v => set({ level: v })}
              />
              <ToggleRow
                icon="target"
                title="Face position"
                subtitle="Helps you put the eyes on the upper third"
                value={settings.faceGuide}
                onChange={v => set({ faceGuide: v })}
              />

              <AppText variant="bodyStrong">Poses</AppText>
              <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
                {groups.map(g => {
                  const selected = g.category === shownGroup?.category;
                  return (
                    <Pressable
                      key={g.category}
                      onPress={() => setCategory(g.category)}
                      accessibilityRole="tab"
                      accessibilityState={{ selected }}
                      style={[
                        styles.pill,
                        selected ? { borderColor: theme.colors.premium, backgroundColor: 'rgba(245,198,107,0.16)' } : null,
                      ]}
                    >
                      <AppText variant="caption" style={{ color: selected ? theme.colors.premium : '#FFFFFF', fontWeight: '700' }}>
                        {CATEGORY_LABELS[g.category]}
                      </AppText>
                    </Pressable>
                  );
                })}
              </ScrollView>
              <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 10 }}>
                <Pressable
                  onPress={() => void addFromPhoto()}
                  accessibilityRole="button"
                  accessibilityLabel={premium ? 'Use a pose from my photo' : 'Use a pose from my photo, Premium'}
                  style={[styles.tile, styles.addTile]}
                >
                  <Icon name={busy ? 'refresh' : 'image'} size={22} color={theme.colors.premium} />
                  <AppText variant="caption" style={styles.tileName}>
                    {busy ? 'Reading…' : 'From my photo'}
                  </AppText>
                  {!premium ? (
                    <View style={styles.lock}>
                      <Icon name="crown" size={11} color={theme.colors.premium} strokeWidth={2.2} />
                    </View>
                  ) : null}
                </Pressable>
                {(shownGroup?.poses ?? []).map(p => (
                  <PoseTile
                    key={p.id}
                    pose={p}
                    selected={settings.poseId === p.id}
                    locked={!canUsePose(p, premium)}
                    onPress={() => choose(p)}
                    onRemove={
                      p.category === 'mine'
                        ? () => {
                            onMyPoses(myPoses.filter(m => m.id !== p.id));
                            if (settings.poseId === p.id) set({ poseId: null });
                          }
                        : undefined
                    }
                  />
                ))}
              </ScrollView>
              {note ? (
                <AppText variant="caption" tone="danger" accessibilityLiveRegion="polite">
                  {note}
                </AppText>
              ) : null}
              {settings.poseId ? (
                <ToggleRow
                  icon="flip"
                  title="Use the other hand"
                  subtitle="Flip the pose left to right"
                  value={settings.mirror}
                  onChange={v => set({ mirror: v })}
                />
              ) : null}

              <View style={styles.header}>
                <AppText variant="bodyStrong" style={{ flex: 1 }}>
                  Live matching
                </AppText>
                {!premium ? (
                  <View style={styles.crown}>
                    <Icon name="crown" size={12} color={theme.colors.premium} strokeWidth={2.2} />
                    <AppText variant="caption" style={{ color: theme.colors.premium, fontWeight: '700' }}>
                      Premium
                    </AppText>
                  </View>
                ) : null}
              </View>
              {premium ? (
                <>
                  <ToggleRow
                    icon="sliders"
                    title="Guided capture"
                    subtitle="One step at a time: position, pose, then hold still"
                    value={settings.guided}
                    onChange={v => set({ guided: v })}
                  />
                  <ToggleRow
                    icon="camera"
                    title="Take the photo for me"
                    subtitle="When the pose and framing match and you hold still"
                    value={settings.autoCapture}
                    onChange={v => set({ autoCapture: v })}
                  />
                  {settings.autoCapture ? (
                    <>
                      <SegmentedControl
                        label="How close the match must be"
                        options={[
                          { value: '70', label: 'Relaxed' },
                          { value: '80', label: 'Close' },
                          { value: '90', label: 'Exact' },
                        ]}
                        value={String(settings.threshold >= 88 ? 90 : settings.threshold >= 75 ? 80 : 70)}
                        onChange={v => set({ threshold: Number(v) })}
                      />
                      <SegmentedControl
                        label="Hold for"
                        options={[
                          { value: '700', label: 'A moment' },
                          { value: '1200', label: '1 second' },
                          { value: '2500', label: '2 seconds' },
                        ]}
                        value={String(settings.holdMs <= 900 ? 700 : settings.holdMs <= 1800 ? 1200 : 2500)}
                        onChange={v => set({ holdMs: Number(v) })}
                      />
                    </>
                  ) : null}
                </>
              ) : (
                <>
                  <AppText variant="caption" tone="secondary">
                    See your pose next to the outline, a score as you move, arrows to each part, how far to stand,
                    and a photo taken by itself when you’ve got it.
                  </AppText>
                  <Button label="Unlock live matching" icon="crown" variant="glass" onPress={onPremium} />
                </>
              )}
            </ScrollView>
          </GlassSurface>
        </Pressable>
      </Pressable>
    </Modal>
  );
}

const styles = StyleSheet.create({
  scrim: { flex: 1, backgroundColor: 'rgba(0,0,0,0.5)', justifyContent: 'flex-end', padding: 12 },
  wrap: { width: '100%', maxWidth: 520, alignSelf: 'center' },
  header: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  crown: { flexDirection: 'row', alignItems: 'center', gap: 4 },
  pill: {
    paddingHorizontal: 12,
    paddingVertical: 7,
    borderRadius: 16,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.25)',
  },
  tile: {
    width: 84,
    paddingVertical: 8,
    borderRadius: 14,
    borderWidth: 1.5,
    borderColor: 'rgba(255,255,255,0.18)',
    backgroundColor: 'rgba(255,255,255,0.06)',
    alignItems: 'center',
    gap: 4,
  },
  addTile: { justifyContent: 'center', minHeight: 124 },
  tileName: { color: '#FFFFFF', textAlign: 'center', paddingHorizontal: 4 },
  lock: { position: 'absolute', top: 6, right: 6 },
  remove: {
    position: 'absolute',
    top: 4,
    left: 4,
    width: 20,
    height: 20,
    borderRadius: 10,
    backgroundColor: 'rgba(0,0,0,0.55)',
    alignItems: 'center',
    justifyContent: 'center',
  },
});
