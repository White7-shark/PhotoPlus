import React from 'react';
import { Image, Pressable, View, type ImageSourcePropType } from 'react-native';

import { useTheme } from '../theme';
import { AppText } from './AppText';
import { Icon, type IconName } from './Icon';

/**
 * A picture-first choice (props, frames): a preview of exactly what will be
 * applied, its name underneath, a crown when it is Premium and locked.
 */
export function PreviewTile({
  label,
  uri,
  source,
  selected,
  locked,
  onPress,
  width = 76,
  height = 76,
  placeholderIcon = 'image',
  dark = false,
  testID,
}: {
  label: string;
  uri?: string | null;
  /** A bundled picture filling the tile (beauty presets). */
  source?: ImageSourcePropType | null;
  selected?: boolean;
  /** Premium and not available on this plan. */
  locked?: boolean;
  onPress: () => void;
  width?: number;
  height?: number;
  placeholderIcon?: IconName;
  /** Props read better on a darker tile (most artwork is light on transparent). */
  dark?: boolean;
  testID?: string;
}) {
  const theme = useTheme();
  return (
    <Pressable
      onPress={onPress}
      accessibilityRole="button"
      accessibilityLabel={`${label}${locked ? ', premium' : ''}`}
      accessibilityState={{ selected: !!selected }}
      testID={testID}
      style={{ width, alignItems: 'center', gap: 6 }}
    >
      <View
        style={{
          width,
          height,
          borderRadius: 14,
          overflow: 'hidden',
          alignItems: 'center',
          justifyContent: 'center',
          borderWidth: selected ? 2 : 1,
          borderColor: selected ? theme.colors.accent : theme.colors.glassStroke,
          backgroundColor: dark ? 'rgba(10,9,17,0.55)' : theme.colors.glassFill,
        }}
      >
        {source ? (
          <Image source={source} style={{ width, height, opacity: locked ? 0.7 : 1 }} resizeMode="cover" />
        ) : uri ? (
          <Image
            source={{ uri }}
            style={{ width: width - 10, height: height - 10, opacity: locked ? 0.85 : 1 }}
            resizeMode="contain"
          />
        ) : (
          <Icon name={placeholderIcon} size={22} color={theme.colors.textTertiary} />
        )}
        {locked ? (
          <View
            style={{
              position: 'absolute',
              right: 5,
              top: 5,
              width: 20,
              height: 20,
              borderRadius: 10,
              alignItems: 'center',
              justifyContent: 'center',
              backgroundColor: 'rgba(10,9,17,0.7)',
            }}
          >
            <Icon name="crown" size={11} color={theme.colors.premium} strokeWidth={2.2} />
          </View>
        ) : null}
      </View>
      <AppText variant="caption" numberOfLines={1} style={{ maxWidth: width + 8 }}>
        {label}
      </AppText>
    </Pressable>
  );
}
