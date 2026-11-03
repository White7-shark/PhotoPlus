import type { BottomTabBarProps } from '@react-navigation/bottom-tabs';
import React from 'react';
import { Pressable, StyleSheet, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { AppText } from '../components/AppText';
import { GlassSurface } from '../components/GlassSurface';
import { Icon, type IconName } from '../components/Icon';
import { useTheme } from '../theme';

const META: Record<string, { icon: IconName; label: string }> = {
  Studio: { icon: 'aperture', label: 'Studio' },
  Projects: { icon: 'folder', label: 'Projects' },
  Camera: { icon: 'camera', label: 'Camera' },
  Packs: { icon: 'bag', label: 'Packs' },
  Settings: { icon: 'gear', label: 'Settings' },
};

/**
 * Floating glass dock with a raised shutter-style Camera button in the
 * centre — the app's primary action is always one tap away.
 */
export function GlassTabBar({ state, navigation }: BottomTabBarProps) {
  const theme = useTheme();
  const insets = useSafeAreaInsets();
  const activeRoute = state.routes[state.index]?.name;
  if (activeRoute === 'Camera') {
    return null; // Camera is immersive; it renders its own controls.
  }
  return (
    <View pointerEvents="box-none" style={[styles.wrap, { paddingBottom: Math.max(insets.bottom, 12) }]}>
      {/* Content scrolls underneath and Android RN has no backdrop blur, so the
          dock uses dense "smoked" glass to keep tab labels legible. */}
      <GlassSurface
        intensity="strong"
        radius={theme.radii.xl}
        style={[styles.bar, { backgroundColor: theme.mode === 'dark' ? 'rgba(16,14,25,0.86)' : 'rgba(251,250,253,0.9)' }]}
        accessibilityRole="tablist"
      >
        {state.routes.map((route, index) => {
          const focused = state.index === index;
          const meta = META[route.name] ?? { icon: 'aperture', label: route.name };
          const onPress = () => {
            const event = navigation.emit({ type: 'tabPress', target: route.key, canPreventDefault: true });
            if (!focused && !event.defaultPrevented) {
              navigation.navigate(route.name);
            }
          };
          if (route.name === 'Camera') {
            return (
              <Pressable
                key={route.key}
                onPress={onPress}
                accessibilityRole="tab"
                accessibilityLabel="Open camera"
                accessibilityState={{ selected: focused }}
                style={styles.shutterSlot}
              >
                <View style={[styles.shutter, { backgroundImage: theme.gradients.spectrum, boxShadow: `0 8px 24px -6px ${theme.colors.accentAlt}` }]}>
                  <View style={styles.shutterInner}>
                    <Icon name="camera" size={24} color="#FFFFFF" strokeWidth={2} />
                  </View>
                </View>
              </Pressable>
            );
          }
          const color = focused ? theme.colors.textPrimary : theme.colors.textTertiary;
          return (
            <Pressable
              key={route.key}
              onPress={onPress}
              accessibilityRole="tab"
              accessibilityLabel={meta.label}
              accessibilityState={{ selected: focused }}
              style={styles.tab}
            >
              <Icon name={meta.icon} size={22} color={color} strokeWidth={focused ? 2.1 : 1.75} />
              <AppText variant="caption" style={{ color, fontWeight: focused ? '700' : '500', fontSize: 11 }} maxFontSizeMultiplier={1.2}>
                {meta.label}
              </AppText>
              <View style={[styles.dot, { backgroundColor: focused ? theme.colors.accent : 'transparent' }]} />
            </Pressable>
          );
        })}
      </GlassSurface>
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: { position: 'absolute', left: 0, right: 0, bottom: 0, alignItems: 'center', paddingHorizontal: 16 },
  bar: { flexDirection: 'row', alignItems: 'center', width: '100%', maxWidth: 520, height: 68, paddingHorizontal: 6 },
  tab: { flex: 1, alignItems: 'center', justifyContent: 'center', gap: 2, minHeight: 56 },
  dot: { width: 4, height: 4, borderRadius: 2, marginTop: 1 },
  shutterSlot: { flex: 1, alignItems: 'center', justifyContent: 'center', minHeight: 56 },
  shutter: { width: 58, height: 58, borderRadius: 29, padding: 3, marginTop: -26 },
  shutterInner: {
    flex: 1,
    borderRadius: 26,
    alignItems: 'center',
    justifyContent: 'center',
    borderWidth: 2,
    borderColor: 'rgba(255,255,255,0.55)',
  },
});
