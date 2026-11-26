import React, { useState } from 'react';
import { View } from 'react-native';

import { AppText, AuroraBackground, Button, GlassSurface, Icon, LogoFull, type IconName } from '../components';
import { useResponsive } from '../hooks/useResponsive';
import type { RootScreenProps } from '../navigation/types';
import { getPermissionService } from '../services/permissions/PermissionService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { t as tx } from '../i18n';

const PROMISES: { icon: IconName; title: string; body: string }[] = [
  {
    icon: 'aperture',
    title: tx('onboarding_a_pro_camera_in_your'),
    body: tx('onboarding_manual_controls_and_composition_guides'),
  },
  {
    icon: 'sparkles',
    title: tx('onboarding_an_assistant_that_directs'),
    body: tx('onboarding_pose_light_and_framing_guidance'),
  },
  {
    icon: 'shield',
    title: tx('onboarding_private_by_design'),
    body: tx('onboarding_works_offline_your_photos_never'),
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
          <LogoFull size={Math.min(240, r.contentWidth * 0.7)} />
          <AppText variant="body" tone="secondary" align="center" style={{ marginTop: 14, maxWidth: 320 }}>
            {tx('onboarding_your_photoshoot_studio_camera_director')}
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
          label={tx('onboarding_get_started')}
          size="lg"
          icon="camera"
          fullWidth
          loading={busy}
          onPress={() => finish(true)}
          accessibilityHint={tx('onboarding_asks_for_camera_access_then')}
        />
        <Button
          label={tx('onboarding_explore_first')}
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
