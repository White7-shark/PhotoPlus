import React from 'react';
import { StyleSheet, View, type ViewProps, type ViewStyle } from 'react-native';

import { useTheme } from '../theme';

export interface GlassSurfaceProps extends ViewProps {
  /** Higher intensity = more opaque frost; use for controls over busy imagery. */
  intensity?: 'subtle' | 'regular' | 'strong';
  radius?: number;
  padding?: number;
  glow?: string;
  style?: ViewStyle | ViewStyle[];
}

/**
 * iOS-style glass: translucent fill, a diagonal specular sheen, a hairline
 * light edge on top and a soft drop shadow. Content behind (the aurora or a
 * photo) shows through, which is what makes the material read as glass.
 *
 * Real backdrop blur is not available to React Native views on Android, so
 * the effect is built from layered translucency; the aurora behind it is
 * itself pre-blurred (see AuroraBackground), giving the frosted look.
 */
export function GlassSurface({
  intensity = 'regular',
  radius,
  padding,
  glow,
  style,
  children,
  ...rest
}: GlassSurfaceProps) {
  const theme = useTheme();
  const isDark = theme.mode === 'dark';
  const fill =
    intensity === 'strong'
      ? theme.colors.glassFillStrong
      : intensity === 'subtle'
        ? 'rgba(255,255,255,0.035)'
        : theme.colors.glassFill;
  const r = radius ?? theme.radii.lg;
  return (
    <View
      style={[
        {
          borderRadius: r,
          backgroundColor: isDark ? fill : theme.colors.glassFill,
          borderWidth: StyleSheet.hairlineWidth * 2,
          borderColor: theme.colors.glassStroke,
          backgroundImage: isDark ? theme.gradients.glassSheen : theme.gradients.glassSheenLight,
          boxShadow: [
            { offsetX: 0, offsetY: 18, blurRadius: 40, spreadDistance: -12, color: theme.colors.glassShadow },
            ...(glow ? [{ offsetX: 0, offsetY: 0, blurRadius: 32, spreadDistance: -6, color: glow }] : []),
          ],
          padding,
          overflow: 'hidden',
        },
        style,
      ]}
      {...rest}
    >
      {/* Specular top edge */}
      <View
        pointerEvents="none"
        style={{
          position: 'absolute',
          top: 0,
          left: r * 0.6,
          right: r * 0.6,
          height: 1,
          backgroundImage: `linear-gradient(90deg, rgba(255,255,255,0) 0%, ${theme.colors.glassHighlight} 50%, rgba(255,255,255,0) 100%)`,
        }}
      />
      {children}
    </View>
  );
}
