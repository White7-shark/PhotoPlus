import React, { useState } from 'react';
import { View } from 'react-native';

import { AppText, AuroraBackground, Button, GlassSurface, Icon, LogoMark, type IconName } from '../components';
import { useResponsive } from '../hooks/useResponsive';
import type { RootScreenProps } from '../navigation/types';
import { getPermissionService } from '../services/permissions/PermissionService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

const PROMISES: { icon: IconName; title: string; body: string }[] = [
  {
    icon: 'aperture',
    title: 'A pro camera in your pocket',
    body: 'Manual controls and composition guides, tuned to your phone.',
  },
  {
    icon: 'sparkles',
    title: 'An assistant that directs',
    body: 'Pose, light and framing guidance — on-device, no cloud.',
  },
  {
    icon: 'shield',
    title: 'Private by design',
    body: 'Works offline. Your photos never leave this device unless you share them.',
  },
];

export function OnboardingScreen({ navigation }: RootScreenProps<'Onboarding'>) {
  const theme = useTheme();
  const insets = useSafeAreaInsets();
  const r = useResponsive();
  const updateSetting = useAppStore(s => s.updateSetting);
  const [busy, setBusy] = useState(false);

  const finish = async (askCamera: boolean) => {
    setBusy(true);
    try {
      if (askCamera) {
        await getPermissionService()
          .ensure('camera')
          .catch(() => undefined);
      }
      await updateSetting('onboardingCompleted', true).catch(() => undefined);
      navigation.reset({ index: 0, routes: [{ name: 'Main' }] });
    } finally {
      setBusy(false);
    }
  };

  return (
    <View style={{ flex: 1, paddingTop: insets.top + 32, paddingBottom: insets.bottom + 24 }}>
      <AuroraBackground />
      <View style={{ width: r.contentWidth, alignSelf: 'center', flex: 1 }}>
        <View style={{ alignItems: 'center', marginTop: 12 }}>
          <LogoMark size={112} />
          <AppText variant="display" align="center" style={{ marginTop: 22 }} accessibilityRole="header">
            Photo Plus
          </AppText>
          <AppText variant="body" tone="secondary" align="center" style={{ marginTop: 8, maxWidth: 320 }}>
            Your photoshoot studio — camera, director and darkroom in one.
          </AppText>
        </View>
        <View style={{ gap: 12, marginTop: 36 }}>
          {PROMISES.map(p => (
            <GlassSurface
              key={p.title}
              radius={theme.radii.lg}
              style={{ flexDirection: 'row', gap: 14, padding: 16, alignItems: 'center' }}
              accessible
              accessibilityLabel={`${p.title}. ${p.body}`}
            >
              <View
                style={{
                  width: 44,
                  height: 44,
                  borderRadius: 14,
                  alignItems: 'center',
                  justifyContent: 'center',
                  backgroundImage: theme.gradients.spectrum,
                }}
              >
                <Icon name={p.icon} size={22} color="#FFFFFF" />
              </View>
              <View style={{ flex: 1 }}>
                <AppText variant="bodyStrong">{p.title}</AppText>
                <AppText variant="caption" tone="secondary" style={{ marginTop: 2 }}>
                  {p.body}
                </AppText>
              </View>
            </GlassSurface>
          ))}
        </View>
        <View style={{ flex: 1, minHeight: 24 }} />
        <Button
          label="Get started"
          size="lg"
          icon="camera"
          fullWidth
          loading={busy}
          onPress={() => finish(true)}
          accessibilityHint="Asks for camera access, then opens the studio"
        />
        <Button
          label="Explore first"
          variant="ghost"
          fullWidth
          disabled={busy}
          onPress={() => finish(false)}
          style={{ marginTop: 6 }}
        />
      </View>
    </View>
  );
}
