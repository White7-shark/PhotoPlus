import React from 'react';
import { View } from 'react-native';

import { AppText, Badge, GlassSurface, Icon, PremiumBadge, Screen, SectionHeader } from '../components';
import { useResponsive } from '../hooks/useResponsive';
import { PREMIUM_PRICE } from '../services/entitlements/features';
import { useTheme } from '../theme';
import { t as tx } from '../i18n';

interface PackPreview {
  id: string;
  title: string;
  mood: string;
  shots: number;
  gradient: string;
  included: boolean;
}

/**
 * Phase 1 shell. Browsing and previews are free (spec §2.1); unlocking and
 * applying packs arrive with the marketplace backend and Play Billing.
 * These cards are design placeholders, clearly labelled as coming soon.
 */
const SAMPLE_PACKS: PackPreview[] = [
  {
    id: 'golden-coast',
    title: tx('marketplace_golden_coast'),
    mood: 'Warm beach editorial',
    shots: 24,
    gradient: 'linear-gradient(160deg, #FFB36B 0%, #FF5E7E 55%, #5A2A6E 100%)',
    included: true,
  },
  {
    id: 'neon-noir',
    title: tx('marketplace_neon_noir'),
    mood: 'Cinematic city night',
    shots: 18,
    gradient: 'linear-gradient(160deg, #2DD4BF 0%, #5B5BF7 50%, #120A2A 100%)',
    included: false,
  },
  {
    id: 'studio-ivory',
    title: tx('marketplace_studio_ivory'),
    mood: 'Clean high-key portraits',
    shots: 30,
    gradient: 'linear-gradient(160deg, #FFF3E2 0%, #E8C9A8 50%, #8E6B55 100%)',
    included: true,
  },
  {
    id: 'kente-royale',
    title: tx('marketplace_kente_royale'),
    mood: 'Bold heritage colour',
    shots: 20,
    gradient: 'linear-gradient(160deg, #F5C66B 0%, #2F9E5B 45%, #B4232F 100%)',
    included: false,
  },
];

export function MarketplaceScreen() {
  const theme = useTheme();
  const r = useResponsive();
  const cols = r.breakpoint === 'compact' ? 2 : 3;
  const gap = 12;
  const width = (r.contentWidth - gap * (cols - 1)) / cols;

  return (
    <Screen title={tx('marketplace_photoshoot_packs')} overline={tx('marketplace_marketplace')}>
      <GlassSurface radius={theme.radii.xl} glow="rgba(245,198,107,0.25)" style={{ marginBottom: 24 }}>
        <View
          style={{
            padding: 20,
            backgroundImage: 'linear-gradient(130deg, rgba(245,198,107,0.28) 0%, rgba(245,198,107,0.04) 70%)',
            gap: 10,
          }}
        >
          <PremiumBadge />
          <AppText variant="title2">{tx('marketplace_recreate_iconic_shoots')}</AppText>
          <AppText variant="body" tone="secondary">
            {tx('marketplace_packs_bring_backdrops_lighting_recipes')} {PREMIUM_PRICE.currency} {PREMIUM_PRICE.amount}/
            {PREMIUM_PRICE.period}.
          </AppText>
          <View
            style={{ flexDirection: 'row', alignItems: 'center', gap: 8, marginTop: 4 }}
            accessible
            accessibilityLabel={tx('marketplace_premium_subscriptions_arrive_in_a')}
          >
            <Icon name="crown" size={16} color={theme.colors.premium} />
            <AppText variant="callout" tone="premium" style={{ fontWeight: '700' }}>
              {tx('marketplace_premium_subscriptions_arrive_in_a')}
            </AppText>
          </View>
        </View>
      </GlassSurface>

      <SectionHeader
        overline={tx('marketplace_preview')}
        title={tx('marketplace_featured_packs')}
        action={<Badge label="Coming soon" />}
      />
      <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap }}>
        {SAMPLE_PACKS.map(pack => (
          <View
            key={pack.id}
            style={{ width }}
            accessible
            accessibilityLabel={tx('marketplace_looks_coming_soon', {
              title: pack.title,
              mood: pack.mood,
              shots: pack.shots,
              value: pack.included ? tx('marketplace_included_with_premium') : tx('marketplace_separate_purchase'),
            })}
          >
            <GlassSurface radius={theme.radii.lg} style={{ padding: 0 }}>
              <View
                style={{
                  aspectRatio: 0.8,
                  backgroundImage: pack.gradient,
                  justifyContent: 'space-between',
                  padding: 12,
                }}
              >
                <View style={{ flexDirection: 'row', justifyContent: 'space-between' }}>
                  <Badge
                    label={pack.included ? tx('marketplace_included') : tx('marketplace_add_on')}
                    tone="media"
                    icon={pack.included ? 'crown' : 'plus'}
                  />
                  <View
                    style={{
                      width: 30,
                      height: 30,
                      borderRadius: 15,
                      alignItems: 'center',
                      justifyContent: 'center',
                      backgroundColor: 'rgba(0,0,0,0.35)',
                    }}
                  >
                    <Icon name="lock" size={15} color="#FFFFFF" />
                  </View>
                </View>
                <View style={{ backgroundImage: theme.gradients.scrim, margin: -12, padding: 12, paddingTop: 30 }}>
                  <AppText variant="bodyStrong" style={{ color: '#FFFFFF' }}>
                    {pack.title}
                  </AppText>
                  <AppText variant="caption" style={{ color: 'rgba(255,255,255,0.82)' }}>
                    {pack.mood} · {pack.shots} looks
                  </AppText>
                </View>
              </View>
            </GlassSurface>
          </View>
        ))}
      </View>
    </Screen>
  );
}
