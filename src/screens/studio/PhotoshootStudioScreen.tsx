import React from 'react';
import { Image, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, Badge, GlassSurface, Icon, PremiumBadge, PressableCard, Screen } from '../../components';
import type { IconName } from '../../components/Icon';
import type { RootScreenProps } from '../../navigation/types';
import { PLATES, thumbAsset } from '../../services/studio/scene';
import { useTheme } from '../../theme';

interface Tool {
  id: string;
  title: string;
  tagline: string;
  icon: IconName;
  available: boolean;
}

const TOOLS: Tool[] = [
  { id: 'scene-swap', title: 'Scene Swap', tagline: 'You, in a new place: studios, parks, golden hour, your own backgrounds', icon: 'layers', available: true },
  { id: 'recreate', title: 'Recreate a shoot', tagline: 'Follow a pack’s scenes, poses and light, shot by shot', icon: 'sparkles', available: false },
  { id: 'relight', title: 'Relight', tagline: 'Studio lighting on any photo', icon: 'sun', available: false },
];

/**
 * The photoshoot studio: tools that recreate a professional shoot from the
 * person's own photos. Scene Swap is the first; the others come with packs.
 */
export function PhotoshootStudioScreen({ navigation }: RootScreenProps<'PhotoshootStudio'>) {
  const theme = useTheme();
  return (
    <Screen title="Photoshoot Studio" onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 16 }}>
        <AppText variant="body" tone="secondary">
          Recreate a professional shoot from your own photos, right on this phone.
        </AppText>
        <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
          {PLATES.slice(0, 8).map(p => (
            <Image key={p.id} source={{ uri: `asset:/${thumbAsset(p.id)}` }} style={styles.strip} accessibilityLabel={p.name} />
          ))}
        </ScrollView>
        {TOOLS.map(t => (
          <PressableCard
            key={t.id}
            label={`${t.title}. ${t.tagline}. Premium. ${t.available ? 'Available now' : 'Coming soon'}.`}
            onPress={() => (t.available ? navigation.navigate('SceneSwap') : undefined)}
            radius={theme.radii.lg}
            testID={`studio-${t.id}`}
          >
            <View style={{ flexDirection: 'row', alignItems: 'center', gap: 14, padding: 16, opacity: t.available ? 1 : 0.6 }}>
              <View style={[styles.icon, { backgroundImage: theme.gradients.premium }]}>
                <Icon name={t.icon} size={22} color="#1A1206" />
              </View>
              <View style={{ flex: 1, gap: 2 }}>
                <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
                  <AppText variant="bodyStrong">{t.title}</AppText>
                  <PremiumBadge />
                </View>
                <AppText variant="caption" tone="secondary">
                  {t.tagline}
                </AppText>
              </View>
              {t.available ? <Icon name="chevronRight" size={18} color={theme.colors.textTertiary} /> : <Badge label="Soon" tone="neutral" />}
            </View>
          </PressableCard>
        ))}
        <GlassSurface radius={18} style={{ padding: 14, gap: 6 }}>
          <AppText variant="bodyStrong">Real places, nobody in them</AppText>
          <AppText variant="caption" tone="secondary">
            Every scene is a real photograph of an empty place, so nothing behind you has to be invented. Packs will bring more; you can add
            your own too.
          </AppText>
        </GlassSurface>
      </View>
    </Screen>
  );
}

const styles = StyleSheet.create({
  strip: { width: 88, height: 110, borderRadius: 14 },
  icon: { width: 44, height: 44, borderRadius: 14, alignItems: 'center', justifyContent: 'center' },
});
