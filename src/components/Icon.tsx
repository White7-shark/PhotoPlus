import React from 'react';
import Svg, { Circle, Path, Rect } from 'react-native-svg';

import { useTheme } from '../theme';

/**
 * Photo Plus icon set: 24-unit grid, 1.75 stroke, rounded joins. Drawn
 * in-house so the line weight matches the brand mark and no icon font needs
 * to be linked natively.
 */
export const ICONS = {
  camera: (
    <>
      <Path d="M4 8.5A2.5 2.5 0 0 1 6.5 6h1.6l1.4-2h5l1.4 2h1.6A2.5 2.5 0 0 1 20 8.5v8a2.5 2.5 0 0 1-2.5 2.5h-11A2.5 2.5 0 0 1 4 16.5z" />
      <Circle cx={12} cy={12.5} r={3.5} />
    </>
  ),
  aperture: (
    <>
      <Circle cx={12} cy={12} r={8.5} />
      <Path d="M12 3.5 15 12M20.4 10.5 12 15M15.5 19.8 9 12M5 17.5 12 9M3.6 10.5H12M9 4.2 12 9" />
    </>
  ),
  sparkles: (
    <>
      <Path d="M12 3.5 13.6 8.4 18.5 10 13.6 11.6 12 16.5 10.4 11.6 5.5 10 10.4 8.4z" />
      <Path d="M18.5 15.5 19.2 17.3 21 18 19.2 18.7 18.5 20.5 17.8 18.7 16 18 17.8 17.3z" />
    </>
  ),
  layers: (
    <>
      <Path d="M12 4 20 8.5 12 13 4 8.5z" />
      <Path d="M4 12.5 12 17 20 12.5" />
      <Path d="M4 16.5 12 21 20 16.5" />
    </>
  ),
  sliders: (
    <>
      <Path d="M5 6h9M18 6h1M5 12h3M12 12h7M5 18h11M20 18h-1" />
      <Circle cx={16} cy={6} r={2} />
      <Circle cx={10} cy={12} r={2} />
      <Circle cx={18} cy={18} r={2} transform="translate(-0.5 0)" />
    </>
  ),
  folder: <Path d="M3.5 7.5A2 2 0 0 1 5.5 5.5h3.8l2 2h7.2a2 2 0 0 1 2 2v7.5a2 2 0 0 1-2 2h-13a2 2 0 0 1-2-2z" />,
  bag: (
    <>
      <Path d="M5 8h14l-1 11.5a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 6 19.5z" />
      <Path d="M9 10V7a3 3 0 0 1 6 0v3" />
    </>
  ),
  gear: (
    <>
      <Circle cx={12} cy={12} r={3} />
      <Path d="M12 3.5v2M12 18.5v2M20.5 12h-2M5.5 12h-2M18 6l-1.4 1.4M7.4 16.6 6 18M18 18l-1.4-1.4M7.4 7.4 6 6" />
      <Circle cx={12} cy={12} r={6.5} />
    </>
  ),
  chevronRight: <Path d="M9.5 6 15.5 12 9.5 18" />,
  chevronLeft: <Path d="M14.5 6 8.5 12 14.5 18" />,
  close: <Path d="M6.5 6.5 17.5 17.5M17.5 6.5 6.5 17.5" />,
  plus: <Path d="M12 5v14M5 12h14" />,
  check: <Path d="M5 12.5 10 17.5 19 7" />,
  alert: (
    <>
      <Path d="M12 4 21 19.5H3z" />
      <Path d="M12 10v4.5M12 17.2v.3" />
    </>
  ),
  info: (
    <>
      <Circle cx={12} cy={12} r={8.5} />
      <Path d="M12 11v5.5M12 7.8v.3" />
    </>
  ),
  flash: <Path d="M13 3 6 13.5h5.5L10.5 21 18 10h-5.5z" />,
  flashOff: (
    <>
      <Path d="M13 3 9.8 7.8M8 10.5 6 13.5h5.5L10.5 21l4.2-6.2M16.4 12.4 18 10h-4.4" />
      <Path d="M4 4 20 20" />
    </>
  ),
  flip: (
    <>
      <Path d="M4 9.5A8 8 0 0 1 18.5 6.5L20 8" />
      <Path d="M20 4v4h-4" />
      <Path d="M20 14.5A8 8 0 0 1 5.5 17.5L4 16" />
      <Path d="M4 20v-4h4" />
    </>
  ),
  grid: (
    <>
      <Rect x={4} y={4} width={16} height={16} rx={2} />
      <Path d="M9.33 4v16M14.67 4v16M4 9.33h16M4 14.67h16" />
    </>
  ),
  cpu: (
    <>
      <Rect x={6.5} y={6.5} width={11} height={11} rx={2} />
      <Rect x={9.5} y={9.5} width={5} height={5} rx={1} />
      <Path d="M9.5 3.5v3M14.5 3.5v3M9.5 17.5v3M14.5 17.5v3M3.5 9.5h3M3.5 14.5h3M17.5 9.5h3M17.5 14.5h3" />
    </>
  ),
  memory: (
    <>
      <Rect x={3.5} y={7} width={17} height={10} rx={1.5} />
      <Path d="M7 7v10M11 7v10M15 7v10M6 17v2.5M18 17v2.5" />
    </>
  ),
  storage: (
    <>
      <Path d="M4 7c0-1.7 3.6-3 8-3s8 1.3 8 3-3.6 3-8 3-8-1.3-8-3z" />
      <Path d="M4 7v10c0 1.7 3.6 3 8 3s8-1.3 8-3V7M4 12c0 1.7 3.6 3 8 3s8-1.3 8-3" />
    </>
  ),
  lock: (
    <>
      <Rect x={5} y={10.5} width={14} height={10} rx={2.5} />
      <Path d="M8 10.5V8a4 4 0 0 1 8 0v2.5M12 14.5v2.5" />
    </>
  ),
  crown: <Path d="M4 8.5 8 12l4-6 4 6 4-3.5-1.5 10h-13z" />,
  image: (
    <>
      <Rect x={3.5} y={4.5} width={17} height={15} rx={2.5} />
      <Circle cx={9} cy={10} r={1.8} />
      <Path d="M20.5 15.5 15.5 10.5 6 19.5" />
    </>
  ),
  refresh: (
    <>
      <Path d="M19.5 12A7.5 7.5 0 1 1 17.3 6.7" />
      <Path d="M19.5 4v4.5H15" />
    </>
  ),
  play: <Path d="M8 5.5v13l11-6.5z" />,
  stop: <Rect x={6.5} y={6.5} width={11} height={11} rx={2} />,
  moon: <Path d="M19.5 14.5A7.5 7.5 0 1 1 9.5 4.5a6 6 0 0 0 10 10z" />,
  sun: (
    <>
      <Circle cx={12} cy={12} r={4} />
      <Path d="M12 2.5v2M12 19.5v2M21.5 12h-2M4.5 12h-2M18.7 5.3l-1.4 1.4M6.7 17.3l-1.4 1.4M18.7 18.7l-1.4-1.4M6.7 6.7 5.3 5.3" />
    </>
  ),
  trash: (
    <>
      <Path d="M4.5 7h15M9.5 7V4.5h5V7M6.5 7l1 13h9l1-13" />
      <Path d="M10 11v5.5M14 11v5.5" />
    </>
  ),
  shield: <Path d="M12 3.5 19 6v5.5c0 4.4-3 8-7 9.5-4-1.5-7-5.1-7-9.5V6z" />,
  wand: (
    <>
      <Path d="M5 19 15.5 8.5M14 6.5l3.5 3.5" />
      <Path d="M18 3.5v3M16.5 5h3M20 11v2M19 12h2M10 3.5v2M9 4.5h2" />
    </>
  ),
} as const;

export type IconName = keyof typeof ICONS;

interface IconProps {
  name: IconName;
  size?: number;
  color?: string;
  strokeWidth?: number;
  filled?: boolean;
}

export function Icon({ name, size = 22, color, strokeWidth = 1.75, filled = false }: IconProps) {
  const theme = useTheme();
  const stroke = color ?? theme.colors.textPrimary;
  return (
    <Svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill={filled ? stroke : 'none'}
      stroke={stroke}
      strokeWidth={strokeWidth}
      strokeLinecap="round"
      strokeLinejoin="round"
      accessible={false}
      importantForAccessibility="no"
    >
      {ICONS[name]}
    </Svg>
  );
}
