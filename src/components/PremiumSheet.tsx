import React from 'react';
import { Modal, Pressable, StyleSheet, View } from 'react-native';

import { TRIAL_DAYS } from '../services/billing/plans';
import { FEATURES, PREMIUM_PRICE, type FeatureKey } from '../services/entitlements/features';
import { openScreen } from '../navigation/ref';
import { useT } from '../i18n';
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
  const t = useT();
  if (!feature) {
    return null;
  }
  return (
    <Modal transparent animationType="fade" visible onRequestClose={onClose} statusBarTranslucent>
      <Pressable
        style={styles.scrim}
        onPress={onClose}
        accessibilityLabel={t('common_close')}
        accessibilityRole="button"
      >
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
              {t('premium_sheet_body', {
                price: `${PREMIUM_PRICE.currency} ${PREMIUM_PRICE.amount}`,
                days: TRIAL_DAYS,
              })}
            </AppText>
            <Button
              label={t('premium_sheet_plans')}
              variant="premium"
              icon="crown"
              onPress={() => {
                onClose();
                openScreen('Premium');
              }}
              testID="premium-sheet-plans"
            />
            <Button
              label={t('premium_sheet_rewards')}
              variant="glass"
              icon="target"
              onPress={() => {
                onClose();
                openScreen('Rewards');
              }}
              testID="premium-sheet-rewards"
            />
            <Button label={t('common_not_now')} variant="ghost" onPress={onClose} />
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
