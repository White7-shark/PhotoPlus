import React from 'react';
import { Image, View, type ImageStyle, type StyleProp, type ViewStyle } from 'react-native';

import { useTheme } from '../theme';
import { AppText } from './AppText';

// Rendered from the master artwork by scripts/generate-icons.py.
const EMBLEM = require('../assets/brand/logo-emblem.png');
const FULL_LOGO = require('../assets/brand/logo-full.png');

interface LogoMarkProps {
  size?: number;
  style?: StyleProp<ViewStyle>;
}

/** Camera emblem from the Photo Plus logo, as a rounded app-icon tile. */
export function LogoMark({ size = 48, style }: LogoMarkProps) {
  const imageStyle: ImageStyle = { width: size, height: size, borderRadius: size * 0.225 };
  return (
    <View style={style} accessibilityRole="image" accessibilityLabel="Photo Plus logo">
      <Image source={EMBLEM} style={imageStyle} resizeMode="cover" />
    </View>
  );
}

/** The complete Photo Plus logo artwork (emblem, wordmark and tagline). */
export function LogoFull({ size = 220, style }: { size?: number; style?: StyleProp<ViewStyle> }) {
  return (
    <View style={style} accessibilityRole="image" accessibilityLabel="Photo Plus. Shoot, edit, create more.">
      <Image source={FULL_LOGO} style={{ width: size, height: size }} resizeMode="contain" />
    </View>
  );
}

/** Emblem + "Photo Plus" wordmark for headers, styled after the logo lettering. */
export function Logo({ size = 36 }: { size?: number }) {
  const theme = useTheme();
  const text = { fontSize: size * 0.64, fontWeight: '900' as const, letterSpacing: -0.6 };
  return (
    <View
      style={{ flexDirection: 'row', alignItems: 'center', gap: size * 0.28 }}
      accessible
      accessibilityRole="header"
      accessibilityLabel="Photo Plus"
    >
      <LogoMark size={size} />
      <View style={{ flexDirection: 'row', alignItems: 'baseline' }} importantForAccessibility="no-hide-descendants">
        <AppText style={[text, { color: theme.colors.textPrimary }]}>Photo</AppText>
        <AppText style={[text, { color: theme.colors.brandGold }]}>Plus</AppText>
      </View>
    </View>
  );
}
