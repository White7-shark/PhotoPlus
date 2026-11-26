import React, { useState } from 'react';
import { Alert, Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, Screen } from '../../components';
import type { IconName } from '../../components/Icon';
import type { RootScreenProps } from '../../navigation/types';
import { money, perMonth, periodLabel, PLANS, savings, TRIAL_DAYS, type PlanId } from '../../services/billing/plans';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';
import { t as tx } from '../../i18n';

const BENEFITS: readonly [IconName, string, string][] = [
  ['layers', 'Photoshoot Studio, unlimited', 'Scene Swap, Relight, studio light, hair and every scene'],
  ['bag', 'Pro product photography', 'Tabletop, spotlit and transparent backdrops, cast shadows, batch editing'],
  ['download', 'HD and 4K downloads', 'Every size, with no Photo Plus mark'],
  ['sparkles', 'Every beauty look and filter', 'The full Beauty, Backdrops and Skin Tone collections'],
  ['camera', 'Advanced camera', 'Manual controls, Moonlight and Ultra Detail, Vibe Match, 20-second Glide'],
  ['crown', 'Your brand on every photo', 'Your own logo and watermark, packs and saved setups'],
];

/**
 * Premium plans: monthly, 3 months and yearly, each starting with a 7-day
 * free trial. Purchases go through Google Play Billing once it is connected;
 * until then the plans are shown and choosing one says so. Free-plan users
 * are pointed to Rewards to earn Premium time.
 */
export function PremiumScreen({ navigation }: RootScreenProps<'Premium'>) {
  const theme = useTheme();
  const plan = useAppStore(s => s.entitlements.plan);
  const [chosen, setChosen] = useState<PlanId>('yearly');
  const selected = PLANS.find(p => p.id === chosen) ?? PLANS[0];

  const start = () =>
    Alert.alert(
      tx('premium_premium_is_almost_here'),
      tx('premium_subscriptions_open_in_a_coming', { TRIALDAYS: TRIAL_DAYS }),
      [
        { text: tx('premium_not_now'), style: 'cancel' },
        { text: tx('premium_open_rewards'), onPress: () => navigation.navigate('Rewards') },
      ],
    );

  return (
    <Screen title="Premium" onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 16 }}>
        <GlassSurface radius={24} style={styles.hero} glow="rgba(245,198,107,0.28)">
          <View style={[styles.badge, { backgroundImage: theme.gradients.premium }]}>
            <Icon name="crown" size={28} color="#1A1206" strokeWidth={2} />
          </View>
          <AppText variant="title2" align="center" accessibilityRole="header">
            Photo Plus Premium
          </AppText>
          <AppText variant="body" tone="secondary" align="center">
            {tx('premium_every_studio_tool_every_look')}
          </AppText>
          <View style={[styles.trial, { backgroundImage: theme.gradients.premium }]}>
            <AppText variant="caption" style={styles.trialText}>
              {TRIAL_DAYS} {tx('premium_days_free_trial')}
            </AppText>
          </View>
        </GlassSurface>

        {plan === 'premium' ? (
          <GlassSurface radius={18} style={styles.section} testID="premium-active">
            <AppText variant="bodyStrong">{tx('premium_you_have_premium')}</AppText>
            <AppText variant="caption" tone="secondary">
              {tx('premium_every_tool_is_yours_thank')}
            </AppText>
          </GlassSurface>
        ) : null}

        <View style={{ gap: 10 }} accessibilityRole="radiogroup" testID="premium-plans">
          {PLANS.map(p => {
            const on = p.id === chosen;
            const save = savings(p);
            return (
              <Pressable
                key={p.id}
                onPress={() => setChosen(p.id)}
                accessibilityRole="radio"
                accessibilityState={{ selected: on }}
                accessibilityLabel={tx('premium_a', {
                  name: p.name,
                  money: money(p.price),
                  periodLabel: periodLabel(p),
                  value: save > 0 ? tx('premium_save', { save }) : '',
                })}
                testID={`premium-plan-${p.id}`}
                style={[
                  styles.plan,
                  {
                    borderColor: on ? theme.colors.accent : 'rgba(255,255,255,0.14)',
                    backgroundColor: on ? 'rgba(255,122,69,0.08)' : 'rgba(255,255,255,0.05)',
                  },
                ]}
              >
                <View style={[styles.radio, { borderColor: on ? theme.colors.accent : 'rgba(255,255,255,0.4)' }]}>
                  {on ? <View style={[styles.dot, { backgroundColor: theme.colors.accent }]} /> : null}
                </View>
                <View style={{ flex: 1, gap: 2 }}>
                  <View style={styles.row}>
                    <AppText variant="bodyStrong">{p.name}</AppText>
                    {p.badge ? (
                      <View style={[styles.pill, { backgroundImage: theme.gradients.premium }]}>
                        <AppText variant="caption" style={styles.pillText}>
                          {p.badge}
                        </AppText>
                      </View>
                    ) : null}
                  </View>
                  <AppText variant="caption" tone="secondary">
                    {p.months > 1
                      ? tx('premium_a_month_save', { money: money(perMonth(p)), save })
                      : tx('premium_cancel_any_time')}
                  </AppText>
                </View>
                <View style={{ alignItems: 'flex-end' }}>
                  <AppText variant="bodyStrong">{money(p.price)}</AppText>
                  <AppText variant="caption" tone="tertiary">
                    per {periodLabel(p)}
                  </AppText>
                </View>
              </Pressable>
            );
          })}
        </View>

        <Button
          label={tx('premium_start_day_free_trial', { TRIALDAYS: TRIAL_DAYS })}
          icon="crown"
          variant="premium"
          size="lg"
          onPress={start}
          testID="premium-start"
        />
        <AppText variant="caption" tone="tertiary" align="center">
          {TRIAL_DAYS} {tx('premium_days_free_then')} {money(selected.price)} per {periodLabel(selected)}
          {tx('premium_renews_automatically_cancel_any_time')}
        </AppText>

        <GlassSurface radius={18} style={styles.section}>
          <AppText variant="bodyStrong">{tx('premium_everything_in_premium')}</AppText>
          {BENEFITS.map(([ic, title, text]) => (
            <View key={title} style={styles.benefit}>
              <View style={[styles.benefitIcon, { backgroundImage: theme.gradients.premium }]}>
                <Icon name={ic} size={16} color="#1A1206" />
              </View>
              <View style={{ flex: 1 }}>
                <AppText variant="caption" style={{ fontWeight: '700' }}>
                  {title}
                </AppText>
                <AppText variant="caption" tone="secondary">
                  {text}
                </AppText>
              </View>
            </View>
          ))}
        </GlassSurface>

        {plan !== 'premium' ? (
          <Pressable onPress={() => navigation.navigate('Rewards')} accessibilityRole="button" testID="premium-rewards">
            <GlassSurface radius={18} style={[styles.section, styles.row]}>
              <Icon name="target" size={22} color={theme.colors.accent} />
              <View style={{ flex: 1 }}>
                <AppText variant="bodyStrong">{tx('premium_not_ready_yet_earn_premium')}</AppText>
                <AppText variant="caption" tone="secondary">
                  {tx('premium_check_in_complete_tasks_watch')}
                </AppText>
              </View>
              <Icon name="chevronRight" size={18} color={theme.colors.textTertiary} />
            </GlassSurface>
          </Pressable>
        ) : null}
      </View>
    </Screen>
  );
}

const styles = StyleSheet.create({
  hero: { padding: 22, gap: 10, alignItems: 'center' },
  badge: { width: 60, height: 60, borderRadius: 30, alignItems: 'center', justifyContent: 'center' },
  trial: { paddingHorizontal: 16, paddingVertical: 7, borderRadius: 999, marginTop: 4 },
  trialText: { color: '#1A1206', fontWeight: '800', letterSpacing: 0.8 },
  section: { padding: 16, gap: 10 },
  row: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  plan: { flexDirection: 'row', alignItems: 'center', gap: 12, padding: 14, borderRadius: 18, borderWidth: 1.5 },
  radio: { width: 22, height: 22, borderRadius: 11, borderWidth: 2, alignItems: 'center', justifyContent: 'center' },
  dot: { width: 11, height: 11, borderRadius: 6 },
  pill: { paddingHorizontal: 8, paddingVertical: 2, borderRadius: 8 },
  pillText: { color: '#1A1206', fontWeight: '800', fontSize: 10 },
  benefit: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  benefitIcon: { width: 32, height: 32, borderRadius: 10, alignItems: 'center', justifyContent: 'center' },
});
