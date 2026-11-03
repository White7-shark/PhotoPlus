import { useWindowDimensions } from 'react-native';

import { layout } from '../theme/tokens';

export type Breakpoint = 'compact' | 'medium' | 'expanded';

export interface ResponsiveInfo {
  width: number;
  height: number;
  breakpoint: Breakpoint;
  isLandscape: boolean;
  /** Columns for card grids. */
  columns: number;
  /** Content width after the max-width clamp. */
  contentWidth: number;
  /** Horizontal page padding. */
  gutter: number;
  fontScale: number;
}

/** Material 3 window size classes: <600 compact, <840 medium, else expanded. */
export function computeResponsive(width: number, height: number, fontScale = 1): ResponsiveInfo {
  const breakpoint: Breakpoint = width < 600 ? 'compact' : width < 840 ? 'medium' : 'expanded';
  const gutter = breakpoint === 'compact' ? 16 : breakpoint === 'medium' ? 24 : 32;
  const contentWidth = Math.min(width - gutter * 2, layout.maxContentWidth);
  // Large accessibility fonts get fewer, wider columns so labels never truncate.
  const baseColumns = breakpoint === 'compact' ? 2 : breakpoint === 'medium' ? 3 : 4;
  const columns = fontScale >= 1.6 ? Math.max(1, baseColumns - 1) : baseColumns;
  return { width, height, breakpoint, isLandscape: width > height, columns, contentWidth, gutter, fontScale };
}

export function useResponsive(): ResponsiveInfo {
  const { width, height, fontScale } = useWindowDimensions();
  return computeResponsive(width, height, fontScale);
}
