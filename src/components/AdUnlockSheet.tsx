import React from 'react';
import { Modal, Pressable, StyleSheet, View } from 'react-native';

import { useT } from '../i18n';
import { useTheme } from '../theme';
import { AppText } from './AppText';
import { Button } from './Button';
import { GlassSurface } from './GlassSurface';
import { Icon } from './Icon';

/**
 * Unlock one free use with a short rewarded ad (the free plan's Pencil Art
 * drawings and Photo Motion videos), or go Premium for no ads at all.
 */
export function AdUnlockSheet({
  open,
  title,
  body,
  left,
  notice,
  busy,
  watchLabel,
  premiumLabel,
  laterLabel,
  onWatch,
  onPremium,
  onClose,
  testID = 'ad-sheet',
}: {
  open: boolean;
  title: string;
  body: string;
  /** "1 of 2 free … left today" */
  left?: string | null;
  notice?: string | null;
  busy: boolean;
  watchLabel: string;
  premiumLabel: string;
  laterLabel: string;
  onWatch: () => void;
  onPremium: () => void;
  onClose: () => void;
  testID?: string;
}) {
  const theme = useTheme();
  const t = useT();
  if (!open) return null;
  return (
    <Modal
      transparent
      animationType="fade"
      visible
      onRequestClose={onClose}
      statusBarTranslucent
      navigationBarTranslucent
    >
      <Pressable
        style={styles.scrim}
        onPress={() => !busy && onClose()}
        accessibilityRole="button"
        accessibilityLabel={t('common_close')}
      >
        <Pressable onPress={() => undefined} style={styles.wrap}>
          <GlassSurface intensity="strong" radius={28} style={styles.sheet} testID={testID}>
            <View style={[styles.badge, { backgroundImage: theme.gradients.premium }]}>
              <Icon name="play" size={24} color="#1A1206" strokeWidth={2} />
            </View>
            <AppText variant="title2" align="center" accessibilityRole="header">
              {title}
            </AppText>
            <AppText variant="body" tone="secondary" align="center">
              {body}
            </AppText>
            {left ? (
              <AppText variant="caption" align="center" style={{ fontWeight: '700' }}>
                {left}
              </AppText>
            ) : null}
            {notice ? (
              <AppText
                variant="callout"
                align="center"
                style={{ color: theme.colors.warning }}
                accessibilityLiveRegion="polite"
              >
                {notice}
              </AppText>
            ) : null}
            <Button
              label={watchLabel}
              icon="play"
              size="lg"
              loading={busy}
              disabled={busy}
              onPress={onWatch}
              testID={`${testID}-watch`}
            />
            <Button
              label={premiumLabel}
              icon="crown"
              variant="premium"
              disabled={busy}
              onPress={onPremium}
              testID={`${testID}-premium`}
            />
            <Button label={laterLabel} variant="ghost" disabled={busy} onPress={onClose} />
          </GlassSurface>
        </Pressable>
      </Pressable>
    </Modal>
  );
}

const styles = StyleSheet.create({
  scrim: { flex: 1, backgroundColor: 'rgba(5,5,10,0.6)', justifyContent: 'center', padding: 24 },
  wrap: { width: '100%', maxWidth: 440, alignSelf: 'center' },
  sheet: { padding: 22, gap: 12 },
  badge: {
    width: 56,
    height: 56,
    borderRadius: 28,
    alignItems: 'center',
    justifyContent: 'center',
    alignSelf: 'center',
  },
});
