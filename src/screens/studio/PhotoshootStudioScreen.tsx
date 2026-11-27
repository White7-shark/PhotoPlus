import type { StudioKind } from '../../services/database/repositories/StudioProjectsRepository';
import React, { useState } from 'react';
import { Image, Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, GlassSurface, Icon, Screen } from '../../components';
import type { IconName } from '../../components/Icon';
import type { RootScreenProps } from '../../navigation/types';
import { PLATES, thumbAsset } from '../../services/studio/scene';
import { useTheme } from '../../theme';
import { t as tx } from '../../i18n';

export interface StudioTool {
  id: string;
  title: string;
  /** What the tool is, in a sentence (its tooltip). */
  about: string;
  icon: IconName;
  available: boolean;
  premium: boolean;
  route?: 'SceneSwap' | 'FilmCollage' | 'Relight' | 'Masters' | 'Motion' | 'ProductStudio' | 'Batch';
  /** Studios that work in projects open their project list first. */
  studio?: StudioKind;
}

export const STUDIO_TOOLS: StudioTool[] = [
  {
    id: 'scene-swap',
    title: 'Scene Swap',
    about: tx('photoshoot_studio_you_in_a_new_place'),
    icon: 'layers',
    available: true,
    premium: true,
    route: 'SceneSwap',
    studio: 'photoshoot',
  },
  {
    id: 'film-collage',
    title: 'Film & Collage',
    about: tx('photoshoot_studio_turn_photos_into_a_film'),
    icon: 'film',
    available: true,
    premium: false,
    route: 'FilmCollage',
  },
  {
    id: 'relight',
    title: 'Relight',
    about: tx('photoshoot_studio_studio_lighting_on_your_own'),
    icon: 'sun',
    available: true,
    premium: true,
    route: 'Relight',
  },
  {
    id: 'masters',
    title: 'Photo Arts',
    about: tx('masters_tile_about'),
    icon: 'brush',
    available: true,
    premium: true,
    route: 'Masters',
    studio: 'arts',
  },
  {
    id: 'motion',
    title: 'Photo Motion',
    about: tx('motion_tile_about'),
    icon: 'motion',
    available: true,
    premium: true,
    route: 'Motion',
    studio: 'motion',
  },
  {
    id: 'product',
    title: 'Product Studio',
    about: tx('photoshoot_studio_catalogue_shots_of_what_you'),
    icon: 'bag',
    available: true,
    premium: false,
    route: 'ProductStudio',
  },
  {
    id: 'batch',
    title: tx('photoshoot_studio_batch_edit'),
    about: tx('photoshoot_studio_one_setup_or_look_on'),
    icon: 'wand',
    available: true,
    premium: false,
    route: 'Batch',
  },
  {
    id: 'recreate',
    title: tx('photoshoot_studio_recreate_a_shoot'),
    about: tx('photoshoot_studio_follow_a_pack_s_scenes'),
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
          {tx('photoshoot_studio_recreate_a_professional_shoot_from')}
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
                onPress={() =>
                  t.available && t.studio
                    ? navigation.navigate('StudioProjects', { studio: t.studio })
                    : t.available && t.route
                      ? navigation.navigate(t.route)
                      : setTip(t.id)
                }
                onLongPress={() => setTip(t.id)}
                accessibilityRole="button"
                accessibilityLabel={`${t.title}${t.premium ? tx('photoshoot_studio_premium') : ''}${t.available ? '' : tx('photoshoot_studio_coming_soon')}`}
                accessibilityHint={tx('photoshoot_studio_long_press_to_read_what')}
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
                    {tx('photoshoot_studio_soon')}
                  </AppText>
                ) : null}
              </Pressable>
              <Pressable
                onPress={() => setTip(cur => (cur === t.id ? null : t.id))}
                accessibilityRole="button"
                accessibilityLabel={tx('photoshoot_studio_what_is', { title: t.title })}
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
                {tipTool.premium ? 'Premium' : tx('photoshoot_studio_free')}
                {tipTool.available ? '' : tx('photoshoot_studio_coming_soon_2')}
              </AppText>
              <Pressable
                onPress={() => setTip(null)}
                accessibilityRole="button"
                accessibilityLabel={tx('photoshoot_studio_close')}
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
            {tx('photoshoot_studio_tap_a_tool_to_open')}
          </AppText>
        )}

        <GlassSurface radius={18} style={{ padding: 14, gap: 6 }}>
          <AppText variant="bodyStrong">{tx('photoshoot_studio_real_places_nobody_in_them')}</AppText>
          <AppText variant="caption" tone="secondary">
            {tx('photoshoot_studio_every_scene_is_a_real')}
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
