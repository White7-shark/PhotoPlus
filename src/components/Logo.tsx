import React, { useMemo } from 'react';
import { View, type ViewStyle } from 'react-native';
import Svg, { Circle, Defs, LinearGradient, Path, RadialGradient, Stop } from 'react-native-svg';

import { apertureGeometry, BLADE_COLORS } from '../brand/logoGeometry';
import { useTheme } from '../theme';
import { AppText } from './AppText';

interface LogoMarkProps {
  size?: number;
  style?: ViewStyle;
}

/** The "Aperture Plus" mark, rendered from the shared brand geometry. */
export function LogoMark({ size = 48, style }: LogoMarkProps) {
  const g = useMemo(() => apertureGeometry(100), []);
  const R = g.outerRadius;
  const c = g.center;
  return (
    <View style={style} accessibilityRole="image" accessibilityLabel="Photo Plus logo">
      <Svg width={size} height={size} viewBox="0 0 100 100">
        <Defs>
          {BLADE_COLORS.map((color, i) => (
            <LinearGradient key={color + i} id={`blade${i}`} x1="0" y1="0" x2="1" y2="1">
              <Stop offset="0" stopColor={color} />
              <Stop offset="1" stopColor={BLADE_COLORS[(i + 1) % BLADE_COLORS.length]} />
            </LinearGradient>
          ))}
          <RadialGradient id="lens" cx="0.4" cy="0.35" r="0.7">
            <Stop offset="0" stopColor="#2A2340" />
            <Stop offset="1" stopColor="#07060C" />
          </RadialGradient>
          <LinearGradient id="sheen" x1="0" y1="0" x2="0.6" y2="1">
            <Stop offset="0" stopColor="#FFFFFF" stopOpacity={0.55} />
            <Stop offset="1" stopColor="#FFFFFF" stopOpacity={0.05} />
          </LinearGradient>
        </Defs>
        <Circle cx={c} cy={c} r={R + g.bladeGap} fill="#0A0911" />
        {g.blades.map((d, i) => (
          <Path
            key={i}
            d={d}
            fill={`url(#blade${i})`}
            stroke="#0A0911"
            strokeWidth={g.bladeGap}
            strokeLinejoin="round"
          />
        ))}
        <Circle cx={c} cy={c} r={g.innerRadius * 0.98} fill="url(#lens)" />
        <Path
          d={`M ${c - R * 0.86} ${c - R * 0.12} A ${R * 0.9} ${R * 0.9} 0 0 1 ${c + R * 0.62} ${c - R * 0.7} A ${R * 1.3} ${R * 1.3} 0 0 0 ${c - R * 0.86} ${c - R * 0.12} Z`}
          fill="url(#sheen)"
        />
        <Path d={g.plus} stroke="#FFFFFF" strokeWidth={g.plusStroke} strokeLinecap="round" />
        <Circle cx={c + g.innerRadius * 0.62} cy={c - g.innerRadius * 0.62} r={g.plusStroke * 0.42} fill="#F5C66B" />
      </Svg>
    </View>
  );
}

interface WordmarkProps {
  size?: number;
}

/** Mark + "Photo Plus" wordmark. */
export function Logo({ size = 36 }: WordmarkProps) {
  const theme = useTheme();
  return (
    <View
      style={{ flexDirection: 'row', alignItems: 'center', gap: size * 0.28 }}
      accessible
      accessibilityRole="header"
      accessibilityLabel="Photo Plus"
    >
      <LogoMark size={size} />
      <View style={{ flexDirection: 'row', alignItems: 'baseline' }} importantForAccessibility="no-hide-descendants">
        <AppText
          style={{ fontSize: size * 0.62, fontWeight: '800', letterSpacing: -0.6, color: theme.colors.textPrimary }}
        >
          Photo
        </AppText>
        <AppText
          style={{
            fontSize: size * 0.62,
            fontWeight: '300',
            letterSpacing: -0.4,
            marginLeft: size * 0.12,
            color: theme.colors.accent,
          }}
        >
          Plus
        </AppText>
      </View>
    </View>
  );
}
