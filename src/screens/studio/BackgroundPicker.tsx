import React, { useState } from 'react';
import { Image, Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, Icon } from '../../components';
import {
  CATEGORY_LABELS,
  PLAINS,
  platesByCategory,
  thumbAsset,
  type Background,
  type PlateDef,
  type SceneCategory,
} from '../../services/studio/scene';
import { useTheme } from '../../theme';

type Tab = SceneCategory;

/**
 * The background library: built-in clean plates by kind of place, plain
 * colours, the person's own backgrounds (and, later, those packs bring),
 * and "no background" for a transparent cut-out.
 */
export function BackgroundPicker({
  value,
  mine,
  onChoose,
  onAdd,
  onRemoveMine,
  transparent = true,
}: {
  value: Background;
  mine: readonly PlateDef[];
  onChoose: (b: Background) => void;
  /** Adds a photo of your own (no add tile without it). */
  onAdd?: () => void;
  onRemoveMine?: (p: PlateDef) => void;
  /** Offer "No background" (photos only; a video needs one). */
  transparent?: boolean;
}) {
  const theme = useTheme();
  const groups = platesByCategory(mine);
  const tabs: Tab[] = ['plain', ...groups.map(g => g.category).filter(c => c !== 'mine'), 'mine'];
  const [tab, setTab] = useState<Tab>(value.kind === 'plate' ? value.plate.category : value.kind === 'plain' ? 'plain' : 'studio');
  const selected = (id: string) =>
    (value.kind === 'plate' && value.plate.id === id) || (value.kind === 'plain' && value.plain.id === id) || (value.kind === 'transparent' && id === 'transparent');
  const tile = (key: string, label: string, chosen: boolean, onPress: () => void, content: React.ReactNode, onLongPress?: () => void) => (
    <Pressable
      key={key}
      onPress={onPress}
      onLongPress={onLongPress}
      accessibilityRole="button"
      accessibilityLabel={label}
      accessibilityState={{ selected: chosen }}
      testID={`bg-${key}`}
      style={{ width: 96, gap: 4 }}
    >
      <View style={[styles.thumb, { borderColor: chosen ? theme.colors.accent : 'transparent' }]}>{content}</View>
      <AppText variant="caption" numberOfLines={1} tone={chosen ? 'primary' : 'secondary'}>
        {label}
      </AppText>
    </Pressable>
  );
  const plates = tab === 'plain' ? [] : (groups.find(g => g.category === tab)?.plates ?? []);
  return (
    <View style={{ gap: 10 }}>
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
        {tabs.map(t => (
          <Pressable
            key={t}
            onPress={() => setTab(t)}
            accessibilityRole="tab"
            accessibilityState={{ selected: tab === t }}
            testID={`bg-tab-${t}`}
            style={[styles.chip, { backgroundColor: tab === t ? theme.colors.accent : 'rgba(255,255,255,0.08)' }]}
          >
            <AppText variant="caption" style={{ color: tab === t ? '#FFFFFF' : theme.colors.textSecondary, fontWeight: '700' }}>
              {CATEGORY_LABELS[t]}
            </AppText>
          </Pressable>
        ))}
      </ScrollView>
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 10 }}>
        {tab === 'plain' ? (
          <>
            {transparent ? tile('transparent', 'No background', selected('transparent'), () => onChoose({ kind: 'transparent' }), <Checks />) : null}
            {PLAINS.map(p =>
              tile(p.id, p.name, selected(p.id), () => onChoose({ kind: 'plain', plain: p }), <View style={[StyleSheet.absoluteFill, { backgroundColor: p.color }]} />),
            )}
          </>
        ) : null}
        {tab === 'mine' && onAdd
          ? tile(
              'add',
              'Add your own',
              false,
              onAdd,
              <View style={[StyleSheet.absoluteFill, styles.add]}>
                <Icon name="plus" size={26} color={theme.colors.textPrimary} />
              </View>,
            )
          : null}
        {plates.map(p =>
          tile(
            p.id,
            p.name,
            selected(p.id),
            () => onChoose({ kind: 'plate', plate: p }),
            <Image source={{ uri: p.source === 'builtin' ? `asset:/${thumbAsset(p.id)}` : `file://${p.path}` }} style={StyleSheet.absoluteFill} resizeMode="cover" />,
            p.source === 'mine' && onRemoveMine ? () => onRemoveMine(p) : undefined,
          ),
        )}
      </ScrollView>
      {tab === 'mine' ? (
        <AppText variant="caption" tone="secondary">
          Use a photo of an empty place, taken at about eye level. Hold a background to remove it.
        </AppText>
      ) : null}
    </View>
  );
}

function Checks() {
  const cells = [];
  for (let y = 0; y < 6; y++) for (let x = 0; x < 6; x++) if ((x + y) % 2 === 0) cells.push(<View key={`${x}${y}`} style={{ position: 'absolute', left: x * 16, top: y * 16, width: 16, height: 16, backgroundColor: '#CFCFD4' }} />);
  return <View style={[StyleSheet.absoluteFill, { backgroundColor: '#F2F2F5' }]}>{cells}</View>;
}

const styles = StyleSheet.create({
  thumb: { width: 96, height: 96, borderRadius: 14, overflow: 'hidden', borderWidth: 2, backgroundColor: '#1A1A20' },
  chip: { paddingHorizontal: 12, paddingVertical: 7, borderRadius: 999 },
  add: { alignItems: 'center', justifyContent: 'center', backgroundColor: 'rgba(255,255,255,0.08)' },
});
