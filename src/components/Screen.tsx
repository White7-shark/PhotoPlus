import React from 'react';
import { RefreshControl, ScrollView, View, type ViewStyle } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { useResponsive } from '../hooks/useResponsive';
import { useTheme } from '../theme';
import { AppText } from './AppText';
import { AuroraBackground } from './AuroraBackground';
import { IconButton } from './Button';

interface ScreenProps {
  title?: string;
  overline?: string;
  headerRight?: React.ReactNode;
  onBack?: () => void;
  children: React.ReactNode;
  scroll?: boolean;
  aurora?: 'default' | 'calm' | 'none';
  refreshing?: boolean;
  onRefresh?: () => void;
  /** Extra bottom space (e.g. above the floating tab bar). */
  bottomInset?: number;
  contentStyle?: ViewStyle;
}

/**
 * Page scaffold: aurora backdrop, safe areas, centred max-width column and
 * responsive gutters. Every screen uses it so spacing is consistent.
 */
export function Screen({
  title,
  overline,
  headerRight,
  onBack,
  children,
  scroll = true,
  aurora = 'default',
  refreshing,
  onRefresh,
  bottomInset,
  contentStyle,
}: ScreenProps) {
  const theme = useTheme();
  const insets = useSafeAreaInsets();
  const r = useResponsive();
  const bottom = (bottomInset ?? theme.layout.tabBarHeight + 24) + insets.bottom;

  const header =
    title || onBack || headerRight ? (
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 12, marginBottom: 20 }}>
        {onBack ? <IconButton icon="chevronLeft" label="Go back" onPress={onBack} size={44} /> : null}
        <View style={{ flex: 1 }}>
          {overline ? (
            <AppText variant="overline" tone="accent">
              {overline.toUpperCase()}
            </AppText>
          ) : null}
          {title ? (
            <AppText variant={onBack ? 'title2' : 'title1'} accessibilityRole="header">
              {title}
            </AppText>
          ) : null}
        </View>
        {headerRight}
      </View>
    ) : null;

  const inner = (
    <View style={[{ width: r.contentWidth, alignSelf: 'center' }, contentStyle]}>
      {header}
      {children}
    </View>
  );

  return (
    <View style={{ flex: 1, backgroundColor: theme.colors.background }}>
      {aurora !== 'none' ? <AuroraBackground variant={aurora === 'calm' ? 'calm' : 'default'} /> : null}
      {scroll ? (
        <ScrollView
          contentContainerStyle={{ paddingTop: insets.top + 16, paddingBottom: bottom }}
          showsVerticalScrollIndicator={false}
          refreshControl={
            onRefresh ? (
              <RefreshControl
                refreshing={!!refreshing}
                onRefresh={onRefresh}
                tintColor={theme.colors.accent}
                colors={[theme.colors.accent]}
                progressBackgroundColor={theme.colors.surface}
              />
            ) : undefined
          }
        >
          {inner}
        </ScrollView>
      ) : (
        <View style={{ flex: 1, paddingTop: insets.top + 16, paddingBottom: bottom }}>{inner}</View>
      )}
    </View>
  );
}
