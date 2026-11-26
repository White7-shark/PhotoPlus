import React, { useState } from 'react';
import { Image, Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, Icon } from '../../components';
import { hexToRgb, rgbToHex, suggestForOutfit, type OutfitColour } from '../../services/studio/colors';
import { GRADIENTS } from '../../services/studio/light';
import {
  CATEGORY_LABELS,
  PLAINS,
  STUDIO_LIGHT,
  customPlain,
  withLight,
  platesByCategory,
  thumbAsset,
  type Background,
  type PlateDef,
  type SceneCategory,
} from '../../services/studio/scene';
import { useTheme } from '../../theme';
import { ColorPicker } from './ColorPicker';
import { t as tx } from '../../i18n';

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
  outfit = [],
}: {
  value: Background;
  mine: readonly PlateDef[];
  onChoose: (b: Background) => void;
  /** Adds a photo of your own (no add tile without it). */
  onAdd?: () => void;
  onRemoveMine?: (p: PlateDef) => void;
  /** Offer "No background" (photos only; a video needs one). */
  transparent?: boolean;
  /** The outfit's main colours, for paper colours that suit it. */
  outfit?: readonly OutfitColour[];
}) {
  const theme = useTheme();
  const groups = platesByCategory(mine);
  const tabs: Tab[] = ['plain', ...groups.map(g => g.category).filter(c => c !== 'mine'), 'mine'];
  const [tab, setTab] = useState<Tab>(value.kind === 'plate' ? value.plate.category : value.kind === 'plain' || value.kind === 'gradient' ? 'plain' : 'studio');
  const selected = (id: string) =>
    (value.kind === 'plate' && value.plate.id === id) ||
    (value.kind === 'plain' && value.plain.id === id) ||
    (value.kind === 'gradient' && value.gradient.id === id) ||
    (value.kind === 'transparent' && id === 'transparent');
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
  const plain = value.kind === 'plain' ? value.plain : null;
  const lit = (plain?.light ?? 0) > 0;
  const custom = plain?.id === 'plain-custom' ? plain : null;
  const [picking, setPicking] = useState(custom !== null);
  const suits = suggestForOutfit(outfit);
  const pickTile = tile(
    'pick',
    'Pick a colour',
    custom !== null,
    () => {
      setPicking(true);
      if (!custom) onChoose({ kind: 'plain', plain: customPlain(plain?.color ?? '#C4962F', plain ? plain.light ?? 0 : STUDIO_LIGHT) });
    },
    <View style={[StyleSheet.absoluteFill, styles.add, custom ? { backgroundColor: custom.color } : null]}>
      {custom ? null : <Rainbow />}
      <Icon name="plus" size={22} color="#FFFFFF" />
    </View>,
  );
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
            {pickTile}
            {transparent ? tile('transparent', 'No background', selected('transparent'), () => onChoose({ kind: 'transparent' }), <Checks />) : null}
            {GRADIENTS.map(g =>
              tile(
                g.id,
                g.name,
                selected(g.id),
                () => {
                  setPicking(false);
                  onChoose({ kind: 'gradient', gradient: g });
                },
                <GradientSwatch from={g.from} to={g.to} />,
              ),
            )}
            {PLAINS.map(p =>
              tile(
                p.id,
                p.name,
                selected(p.id),
                () => {
                  setPicking(false);
                  onChoose({ kind: 'plain', plain: plain ? withLight(p, lit) : p });
                },
                <View style={[StyleSheet.absoluteFill, { backgroundColor: p.color }]} />,
              ),
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
      {tab === 'plain' && suits.length > 0 ? (
        <View style={{ gap: 6 }} testID="outfit-suggestions">
          <AppText variant="caption" tone="secondary">
            {tx('background_picker_suits_your_outfit')}
          </AppText>
          <View style={styles.suits}>
            {suits.map(c => {
              const chosen = plain?.color.toUpperCase() === c.color.toUpperCase();
              return (
                <Pressable
                  key={c.id}
                  onPress={() => {
                    setPicking(false);
                    onChoose({ kind: 'plain', plain: { id: `plain-${c.id}`, name: c.name, color: c.color, vignette: true, light: plain && !lit ? 0 : STUDIO_LIGHT } });
                  }}
                  accessibilityRole="button"
                  accessibilityLabel={tx('background_picker_suits_your_outfit_2', { name: c.name })}
                  accessibilityState={{ selected: chosen }}
                  testID={`suit-${c.id}`}
                  style={[styles.suit, { borderColor: chosen ? theme.colors.accent : 'rgba(255,255,255,0.15)' }]}
                >
                  <View style={[styles.dot, { backgroundColor: c.color }]} />
                  <AppText variant="caption">{c.name}</AppText>
                </Pressable>
              );
            })}
          </View>
        </View>
      ) : null}
      {tab === 'plain' && custom && picking ? (
        <ColorPicker key={custom.color} value={custom.color} onChange={hex => onChoose({ kind: 'plain', plain: customPlain(hex, custom.light ?? 0) })} />
      ) : null}
      {tab === 'plain' && plain ? (
        <Pressable
          onPress={() => onChoose({ kind: 'plain', plain: withLight(plain, !lit) })}
          accessibilityRole="switch"
          accessibilityLabel={tx('background_picker_studio_light')}
          accessibilityState={{ checked: lit }}
          testID="studio-light"
          style={[styles.toggle, { backgroundColor: lit ? theme.colors.accent : 'rgba(255,255,255,0.08)' }]}
        >
          <Icon name="sun" size={16} color={lit ? '#FFFFFF' : theme.colors.textSecondary} />
          <View style={{ flex: 1 }}>
            <AppText variant="caption" style={{ fontWeight: '700', color: lit ? '#FFFFFF' : theme.colors.textPrimary }}>
              {tx('background_picker_studio_light_2', { value: lit ? 'on' : 'off' })}
            </AppText>
            <AppText variant="caption" style={{ color: lit ? 'rgba(255,255,255,0.85)' : theme.colors.textSecondary }}>
              {lit ? tx('background_picker_lit_like_studio_paper_its') : tx('background_picker_a_flat_colour')}
            </AppText>
          </View>
        </Pressable>
      ) : null}
      {tab === 'mine' ? (
        <AppText variant="caption" tone="secondary">
          {tx('background_picker_use_a_photo_of_an')}
        </AppText>
      ) : null}
    </View>
  );
}

function Rainbow() {
  const hues = ['#FF5A5A', '#FFB84D', '#F5E65A', '#5AD17A', '#4DB5FF', '#8A6BFF', '#FF6BD0'];
  return (
    <View style={[StyleSheet.absoluteFill, { flexDirection: 'row' }]}>
      {hues.map(h => (
        <View key={h} style={{ flex: 1, backgroundColor: h }} />
      ))}
    </View>
  );
}

/** A gradient's swatch: bands blended from one colour to the other. */
function GradientSwatch({ from, to }: { from: string; to: string }) {
  const a = hexToRgb(from),
    b = hexToRgb(to);
  const bands = Array.from({ length: 12 }, (_, i) => {
    const t = i / 11;
    return rgbToHex({ r: a.r + (b.r - a.r) * t, g: a.g + (b.g - a.g) * t, b: a.b + (b.b - a.b) * t });
  });
  return (
    <View style={StyleSheet.absoluteFill}>
      {bands.map((c, i) => (
        <View key={`${c}${i}`} style={{ flex: 1, backgroundColor: c }} />
      ))}
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
  suits: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  suit: { flexDirection: 'row', alignItems: 'center', gap: 6, paddingHorizontal: 10, paddingVertical: 6, borderRadius: 999, borderWidth: 1.5 },
  dot: { width: 16, height: 16, borderRadius: 8 },
  toggle: { flexDirection: 'row', alignItems: 'center', gap: 10, paddingHorizontal: 12, paddingVertical: 10, borderRadius: 14 },
});
