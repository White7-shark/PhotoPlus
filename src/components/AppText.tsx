import React from 'react';
import { Text, type TextProps, type TextStyle } from 'react-native';

import { useTheme, type TypographyVariant } from '../theme';

type Tone = 'primary' | 'secondary' | 'tertiary' | 'accent' | 'premium' | 'danger' | 'success' | 'inverse';

export interface AppTextProps extends TextProps {
  variant?: TypographyVariant;
  tone?: Tone;
  align?: TextStyle['textAlign'];
}

/**
 * All text goes through here so typography and colour stay on-system.
 * Font scaling is allowed (accessibility) but capped so dense controls
 * never overflow their glass containers.
 */
export function AppText({ variant = 'body', tone = 'primary', align, style, maxFontSizeMultiplier, ...rest }: AppTextProps) {
  const theme = useTheme();
  const colors: Record<Tone, string> = {
    primary: theme.colors.textPrimary,
    secondary: theme.colors.textSecondary,
    tertiary: theme.colors.textTertiary,
    accent: theme.colors.accent,
    premium: theme.colors.premium,
    danger: theme.colors.danger,
    success: theme.colors.success,
    inverse: theme.colors.textInverse,
  };
  const isDisplay = variant === 'display' || variant === 'title1';
  return (
    <Text
      maxFontSizeMultiplier={maxFontSizeMultiplier ?? (isDisplay ? 1.3 : 1.8)}
      style={[theme.typography[variant], { color: colors[tone] }, align ? { textAlign: align } : null, style]}
      {...rest}
    />
  );
}
