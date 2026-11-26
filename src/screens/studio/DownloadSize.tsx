import React from 'react';
import { Pressable, StyleSheet, View } from 'react-native';

import { AppText, Icon } from '../../components';
import { EXPORT_SIZES, type ExportSizeId } from '../../services/studio/exportSize';
import { useTheme } from '../../theme';
import { t as tx } from '../../i18n';

/**
 * The download size of a studio photo: 360p and 480p on every plan; 720p,
 * 1080p, HD and 4K with Premium (a locked size opens Premium instead).
 */
export function DownloadSize({
  value,
  onChange,
  premium,
  onLocked,
}: {
  value: ExportSizeId;
  onChange: (id: ExportSizeId) => void;
  premium: boolean;
  onLocked: () => void;
}) {
  const theme = useTheme();
  return (
    <View style={{ gap: 6 }} testID="download-size">
      <AppText variant="caption" tone="secondary">
        {tx('download_size_download_size')}
      </AppText>
      <View style={styles.row} accessibilityRole="radiogroup">
        {EXPORT_SIZES.map(s => {
          const locked = s.premium && !premium;
          const selected = value === s.id;
          return (
            <Pressable
              key={s.id}
              onPress={() => (locked ? onLocked() : onChange(s.id))}
              accessibilityRole="radio"
              accessibilityLabel={locked ? tx('download_size_premium', { label: s.label }) : s.label}
              accessibilityState={{ selected }}
              testID={`size-${s.id}`}
              style={[styles.chip, { borderColor: selected ? theme.colors.accent : 'rgba(255,255,255,0.14)' }]}
            >
              {locked ? <Icon name="crown" size={11} color={theme.colors.accent} /> : null}
              <AppText variant="caption" style={{ fontWeight: selected ? '700' : '500' }}>
                {s.label}
              </AppText>
            </Pressable>
          );
        })}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  row: { flexDirection: 'row', flexWrap: 'wrap', gap: 6 },
  chip: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 4,
    paddingHorizontal: 10,
    paddingVertical: 6,
    borderRadius: 999,
    borderWidth: 1.5,
  },
});
