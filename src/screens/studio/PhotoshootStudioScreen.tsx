import React, { useState } from 'react';
import { Image, Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, GlassSurface, Icon, Screen } from '../../components';
import type { IconName } from '../../components/Icon';
import type { RootScreenProps } from '../../navigation/types';
import { PLATES, thumbAsset } from '../../services/studio/scene';
import { useTheme } from '../../theme';

export interface StudioTool {
  id: string;
  title: string;
  /** What the tool is, in a sentence (its tooltip). */
  about: string;
  icon: IconName;
  available: boolean;
  premium: boolean;
  route?: 'SceneSwap' | 'FilmCollage' | 'Relight' | 'ProductStudio' | 'Batch';
}

export const STUDIO_TOOLS: StudioTool[] = [
  {
    id: 'scene-swap',
    title: 'Scene Swap',
    about:
      'You, in a new place: cut out of your photo and set in a studio, a park at golden hour, a café or a background of your own.',
    icon: 'layers',
    available: true,
    premium: true,
    route: 'SceneSwap',
  },
  {
    id: 'film-collage',
    title: 'Film & Collage',
    about:
      'Turn photos into a film strip or a collage. Every frame gets its own look, crop, colour and light; Mix it up does it for you.',
    icon: 'film',
    available: true,
    premium: false,
    route: 'FilmCollage',
  },
  {
    id: 'relight',
    title: 'Relight',
    about:
      'Studio lighting on your own photo, right where you stand: soft, Rembrandt, rim or neon light, with the background darkened or softened.',
    icon: 'sun',
    available: true,
    premium: true,
    route: 'Relight',
  },
  {
    id: 'product',
    title: 'Product Studio',
    about:
      'Catalogue shots of what you sell: the product cut out onto white, paper or a tabletop, with a natural shadow, true colour and the right size for every shop.',
    icon: 'bag',
    available: true,
    premium: false,
    route: 'ProductStudio',
  },
  {
    id: 'batch',
    title: 'Batch edit',
    about: 'One setup or look on many photos at once, every result saved beside its untouched original.',
    icon: 'wand',
    available: true,
    premium: false,
    route: 'Batch',
  },
  {
    id: 'recreate',
    title: 'Recreate a shoot',
    about: 'Follow a pack’s scenes, poses and light, shot by shot, to recreate a professional photoshoot. Coming soon.',
    icon: 'sparkles',
    available: false,
    premium: true,
  },
];

/**
 * The photoshoot studio: its tools as a grid of small tiles. Tapping a tile
 * opens the tool; the "i" on each tile (or a long press) shows what it is.
 */
export function PhotoshootStudioScreen({ navigation }: RootScreenProps<'PhotoshootStudio'>) {
  const theme = useTheme();
  const [tip, setTip] = useState<string | null>(null);
  const tipTool = STUDIO_TOOLS.find(t => t.id === tip);
  return (
    <Screen title="Photoshoot Studio" onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 16 }}>
        <AppText variant="body" tone="secondary">
          Recreate a professional shoot from your own photos, right on this phone.
        </AppText>
        <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
          {PLATES.slice(0, 8).map(p => (
            <Image
              key={p.id}
              source={{ uri: `asset:/${thumbAsset(p.id)}` }}
              style={styles.strip}
              accessibilityLabel={p.name}
            />
          ))}
        </ScrollView>

        <View style={styles.grid} testID="studio-tools">
          {STUDIO_TOOLS.map(t => (
            <View key={t.id} style={styles.cell}>
              <Pressable
                onPress={() => (t.available && t.route ? navigation.navigate(t.route) : setTip(t.id))}
                onLongPress={() => setTip(t.id)}
                accessibilityRole="button"
                accessibilityLabel={`${t.title}${t.premium ? ', Premium' : ''}${t.available ? '' : ', coming soon'}`}
                accessibilityHint="Long press to read what it is"
                testID={`studio-${t.id}`}
                style={({ pressed }) => [
                  styles.tile,
                  {
                    borderColor: tip === t.id ? theme.colors.accent : 'rgba(255,255,255,0.12)',
                    opacity: t.available ? (pressed ? 0.85 : 1) : 0.55,
                  },
                ]}
              >
                <View style={[styles.icon, { backgroundImage: theme.gradients.premium }]}>
                  <Icon name={t.icon} size={22} color="#1A1206" />
                </View>
                <AppText variant="caption" align="center" style={{ fontWeight: '700' }} numberOfLines={2}>
                  {t.title}
                </AppText>
                {t.premium ? (
                  <View style={styles.crown}>
                    <Icon name="crown" size={10} color={theme.colors.accent} />
                  </View>
                ) : null}
                {!t.available ? (
                  <AppText variant="caption" tone="tertiary" style={{ fontSize: 10 }}>
                    Soon
                  </AppText>
                ) : null}
              </Pressable>
              <Pressable
                onPress={() => setTip(cur => (cur === t.id ? null : t.id))}
                accessibilityRole="button"
                accessibilityLabel={`What is ${t.title}?`}
                hitSlop={8}
                testID={`studio-info-${t.id}`}
                style={styles.info}
              >
                <Icon name="info" size={12} color={theme.colors.textSecondary} />
              </Pressable>
            </View>
          ))}
        </View>

        {tipTool ? (
          <GlassSurface radius={16} style={styles.tip} accessibilityLiveRegion="polite" testID="studio-tip">
            <View style={styles.tipHead}>
              <Icon name={tipTool.icon} size={16} color={theme.colors.accent} />
              <AppText variant="bodyStrong" style={{ flex: 1 }}>
                {tipTool.title}
              </AppText>
              <AppText variant="caption" tone="tertiary">
                {tipTool.premium ? 'Premium' : 'Free'}
                {tipTool.available ? '' : ' · Coming soon'}
              </AppText>
              <Pressable
                onPress={() => setTip(null)}
                accessibilityRole="button"
                accessibilityLabel="Close"
                hitSlop={8}
                testID="studio-tip-close"
              >
                <Icon name="close" size={14} color={theme.colors.textSecondary} />
              </Pressable>
            </View>
            <AppText variant="caption" tone="secondary">
              {tipTool.about}
            </AppText>
          </GlassSurface>
        ) : (
          <AppText variant="caption" tone="tertiary">
            Tap a tool to open it, or the i to read what it does.
          </AppText>
        )}

        <GlassSurface radius={18} style={{ padding: 14, gap: 6 }}>
          <AppText variant="bodyStrong">Real places, nobody in them</AppText>
          <AppText variant="caption" tone="secondary">
            Every scene is a real photograph of an empty place, so nothing behind you has to be invented. Packs will
            bring more; you can add your own too.
          </AppText>
        </GlassSurface>
      </View>
    </Screen>
  );
}

const styles = StyleSheet.create({
  strip: { width: 88, height: 110, borderRadius: 14 },
  grid: { flexDirection: 'row', flexWrap: 'wrap', marginHorizontal: -5 },
  cell: { width: '33.333%', padding: 5 },
  tile: {
    aspectRatio: 1,
    borderRadius: 18,
    borderWidth: 1,
    backgroundColor: 'rgba(255,255,255,0.06)',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 6,
    padding: 8,
  },
  icon: { width: 40, height: 40, borderRadius: 13, alignItems: 'center', justifyContent: 'center' },
  crown: { position: 'absolute', top: 8, left: 8 },
  info: {
    position: 'absolute',
    top: 11,
    right: 11,
    width: 22,
    height: 22,
    borderRadius: 11,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(255,255,255,0.1)',
  },
  tip: { padding: 12, gap: 6 },
  tipHead: { flexDirection: 'row', alignItems: 'center', gap: 8 },
});
