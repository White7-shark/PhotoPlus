import React from 'react';
import { Modal, Pressable, StyleSheet, View } from 'react-native';

import { FEATURES, PREMIUM_PRICE, type FeatureKey } from '../services/entitlements/features';
import { useTheme } from '../theme';
import { AppText } from './AppText';
import { Button } from './Button';
import { GlassSurface } from './GlassSurface';
import { Icon } from './Icon';

/** Explains a Premium feature when the free plan can't use it. */
export function PremiumSheet({
  feature,
  onClose,
  message,
}: {
  feature: FeatureKey | null;
  onClose: () => void;
  /** Extra context shown above the plan line (e.g. a used-up daily allowance). */
  message?: string | null;
}) {
  const theme = useTheme();
  if (!feature) {
    return null;
  }
  return (
    <Modal transparent animationType="fade" visible onRequestClose={onClose} statusBarTranslucent>
      <Pressable style={styles.scrim} onPress={onClose} accessibilityLabel="Close" accessibilityRole="button">
        <Pressable onPress={() => undefined} style={styles.wrap}>
          <GlassSurface intensity="strong" radius={28} style={{ padding: 22, gap: 12 }} glow="rgba(245,198,107,0.3)">
            <View style={[styles.badge, { backgroundImage: theme.gradients.premium }]}>
              <Icon name="crown" size={26} color="#1A1206" strokeWidth={2} />
            </View>
            <AppText variant="title2" align="center" accessibilityRole="header">
              {FEATURES[feature].title}
            </AppText>
            {message ? (
              <AppText variant="bodyStrong" align="center">
                {message}
              </AppText>
            ) : null}
            <AppText variant="body" tone="secondary" align="center">
              Part of Photo Plus Premium — {PREMIUM_PRICE.currency} {PREMIUM_PRICE.amount} a {PREMIUM_PRICE.period}.
              Premium arrives in a coming update.
            </AppText>
            <Button label="Got it" variant="premium" onPress={onClose} />
          </GlassSurface>
        </Pressable>
      </Pressable>
    </Modal>
  );
}

const styles = StyleSheet.create({
  scrim: { flex: 1, backgroundColor: 'rgba(5,5,10,0.6)', justifyContent: 'center', padding: 24 },
  wrap: { maxWidth: 420, width: '100%', alignSelf: 'center' },
  badge: {
    width: 60,
    height: 60,
    borderRadius: 30,
    alignItems: 'center',
    justifyContent: 'center',
    alignSelf: 'center',
  },
});
