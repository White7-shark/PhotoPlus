import React, { useCallback, useEffect, useState } from 'react';
import { ActivityIndicator, Image, Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, Screen } from '../../components';
import type { RootScreenProps } from '../../navigation/types';
import { promoAsset, SHARE_STYLES, shareStyle, shareText, type ShareForm } from '../../services/rewards/campaign';
import {
  getRewardsService,
  RewardsError,
  TASK_TEXT,
  TOOL_TEXT,
  type RewardItem,
  type RewardsState,
  type RewardTool,
  type ShareTarget,
} from '../../services/rewards/RewardsService';
import { useI18n, useT } from '../../i18n';
import { useTheme } from '../../theme';
import { Choice } from '../product/controls';

import { ShareSheet, targetName } from './ShareSheet';
import { t as tx } from '../../i18n';

const ITEM_TEXT: Record<RewardItem, string> = {
  tool: 'One Premium tool for 24 hours',
  'premium-1': 'Premium for 1 day',
  'premium-3': 'Premium for 3 days',
  'premium-7': 'Premium for 7 days',
};

const hoursLeft = (until: number, now: number) => Math.max(1, Math.ceil((until - now) / 3_600_000));
const when = (ms: number) => new Date(ms).toLocaleDateString(undefined, { day: 'numeric', month: 'short' });

/**
 * Rewards (free plan): points from a daily check-in streak, short rewarded
 * ads and tasks, spent on Premium days or a Premium tool for a day; and the
 * share campaign — share Photo Plus 30 times (each app once every 8 hours)
 * for two days of Premium. The device keeps the ledger; this shows it.
 */
export function RewardsScreen({ navigation }: RootScreenProps<'Rewards'>) {
  const theme = useTheme();
  const tr = useT();
  const language = useI18n(st => st.language.tag);
  const rewards = getRewardsService();
  const [state, setState] = useState<RewardsState | null>(null);
  const [busy, setBusy] = useState<string | null>(null);
  const [note, setNote] = useState<{ text: string; good: boolean } | null>(null);
  const [tool, setTool] = useState<RewardTool>('sceneSwap');
  const [sharing, setSharing] = useState(false);
  const [form, setForm] = useState<ShareForm>('post');
  const [sheet, setSheet] = useState(false);
  // Which version of the words is shared next (they change from share to share).
  const [turn, setTurn] = useState(0);

  useEffect(() => {
    let live = true;
    rewards
      .state()
      .then(s => live && setState(s))
      .catch(() => live && setNote({ text: tx('rewards_rewards_couldn_t_be_loaded'), good: false }));
    return () => {
      live = false;
    };
  }, [rewards]);

  const act = useCallback(
    async (
      id: string,
      run: () => Promise<{ state: RewardsState } & Record<string, unknown>>,
      good: (r: Record<string, unknown>) => string,
    ) => {
      setBusy(id);
      setNote(null);
      try {
        const r = await run();
        setState(r.state);
        setNote({ text: good(r), good: true });
      } catch (e) {
        setNote({
          text: e instanceof RewardsError ? e.message : tx('rewards_something_went_wrong_please_try'),
          good: false,
        });
      } finally {
        setBusy(null);
      }
    },
    [],
  );

  if (!state) {
    return (
      <Screen title="Rewards" onBack={() => navigation.goBack()}>
        {note ? (
          <AppText variant="body" tone="secondary">
            {note.text}
          </AppText>
        ) : (
          <ActivityIndicator color={theme.colors.accent} />
        )}
      </Screen>
    );
  }

  if (state.subscribed) {
    return (
      <Screen title="Rewards" onBack={() => navigation.goBack()}>
        <GlassSurface radius={18} style={styles.section} testID="rewards-subscribed">
          <AppText variant="bodyStrong">{tx('rewards_you_have_premium')}</AppText>
          <AppText variant="caption" tone="secondary">
            {tx('rewards_rewards_are_for_the_free')}
          </AppText>
        </GlassSurface>
      </Screen>
    );
  }

  const now = state.now;
  const premiumOn = state.premiumUntil > now;
  const tools = Object.entries(state.tools) as [RewardTool, number][];
  const s = state.shares;
  const campaignClosed = s.opensAt > now;
  const style = shareStyle(form);
  const text = shareText(form, turn, language);
  const asset = promoAsset(form, language);

  const share = (target: ShareTarget) => {
    setSheet(false);
    const { name } = targetName(target, [target]);
    void act(
      `share-${target.pkg}`,
      async () => {
        const r = await rewards.share(target, asset, text);
        setTurn(t => t + 1);
        return r;
      },
      r =>
        r.counted ? `Shared to ${name} — it counts!` : `Shared to ${name}. It counted recently, so this one doesn’t.`,
    );
  };

  return (
    <Screen title="Rewards" overline={tx('rewards_free_plan')} onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 14 }}>
        <GlassSurface radius={22} style={styles.balance} glow="rgba(245,198,107,0.25)" testID="rewards-balance">
          <AppText variant="caption" tone="secondary">
            {tx('rewards_your_points')}
          </AppText>
          <AppText variant="title1" style={{ fontSize: 40, lineHeight: 46 }}>
            {state.points.toLocaleString()}
          </AppText>
          {premiumOn ? (
            <View style={[styles.pill, { backgroundImage: theme.gradients.premium }]}>
              <AppText variant="caption" style={styles.pillText}>
                {tx('rewards_premium_until')} {when(state.premiumUntil)}
              </AppText>
            </View>
          ) : null}
          {tools.map(([t, until]) => (
            <AppText key={t} variant="caption" tone="secondary">
              {TOOL_TEXT[t]} {tx('rewards_unlocked')} {hoursLeft(until, now)} {tx('rewards_h_left')}
            </AppText>
          ))}
        </GlassSurface>

        {note ? (
          <AppText
            variant="callout"
            style={{ color: note.good ? theme.colors.success : theme.colors.warning }}
            accessibilityLiveRegion="polite"
            testID="rewards-note"
          >
            {note.text}
          </AppText>
        ) : null}

        {/* ---- Daily check-in */}
        <GlassSurface radius={18} style={styles.section} testID="rewards-checkin">
          <AppText variant="bodyStrong">{tx('rewards_daily_check_in')}</AppText>
          <AppText variant="caption" tone="secondary">
            {tx('rewards_check_in_every_day_the')}
          </AppText>
          <View style={styles.week}>
            {state.checkIn.schedule.map((pts, i) => {
              const day = i + 1;
              const past = day < state.checkIn.day || (state.checkIn.done && day === state.checkIn.day);
              const today = day === state.checkIn.day;
              return (
                <View
                  key={day}
                  style={[
                    styles.day,
                    {
                      borderColor: today ? theme.colors.accent : 'rgba(255,255,255,0.12)',
                      backgroundColor: past ? 'rgba(255,122,69,0.18)' : 'rgba(255,255,255,0.05)',
                    },
                  ]}
                >
                  <AppText variant="caption" tone="tertiary" style={{ fontSize: 10 }}>
                    {tx('rewards_day')} {day}
                  </AppText>
                  {past ? (
                    <Icon name="check" size={14} color={theme.colors.accent} />
                  ) : (
                    <AppText variant="caption" style={{ fontWeight: '700' }}>
                      +{pts}
                    </AppText>
                  )}
                </View>
              );
            })}
          </View>
          <Button
            label={
              state.checkIn.done
                ? tx('rewards_checked_in_come_back_tomorrow')
                : tx('rewards_check_in', { value: state.checkIn.schedule[state.checkIn.day - 1] })
            }
            icon="check"
            onPress={() =>
              act(
                'checkin',
                () => rewards.checkIn(),
                r => `+${String(r.points)} points. See you tomorrow!`,
              )
            }
            loading={busy === 'checkin'}
            disabled={state.checkIn.done || busy !== null}
            testID="rewards-checkin-button"
          />
        </GlassSurface>

        {/* ---- Ads */}
        <GlassSurface radius={18} style={styles.section} testID="rewards-ads">
          <View style={styles.row}>
            <Icon name="play" size={20} color={theme.colors.accent} />
            <AppText variant="bodyStrong" style={{ flex: 1 }}>
              {tx('rewards_watch_a_short_ad')}
            </AppText>
            <AppText variant="caption" tone="secondary">
              {state.ads.today} of {state.ads.perDay} today
            </AppText>
          </View>
          <Button
            label={
              state.ads.wait < 0
                ? tx('rewards_today_s_ads_are_done')
                : state.ads.wait > 0
                  ? tx('rewards_next_ad_in_s', { value: Math.ceil(state.ads.wait) })
                  : tx('rewards_watch_points', { points: state.ads.points })
            }
            icon="play"
            variant="glass"
            onPress={() =>
              act(
                'ad',
                () => rewards.watchAd(),
                r =>
                  r.rewarded ? `+${String(r.points)} points. Thanks for watching!` : 'Watch to the end to earn points.',
              )
            }
            loading={busy === 'ad'}
            disabled={state.ads.wait !== 0 || busy !== null}
            testID="rewards-ad"
          />
        </GlassSurface>

        {/* ---- Tasks */}
        <GlassSurface radius={18} style={styles.section} testID="rewards-tasks">
          <AppText variant="bodyStrong">{tx('rewards_tasks')}</AppText>
          {state.tasks.map(t => {
            const done = t.progress >= t.goal;
            const copy = TASK_TEXT[t.id] ?? { title: t.id, hint: '' };
            return (
              <View key={t.id} style={styles.task}>
                <View style={{ flex: 1 }}>
                  <AppText variant="caption" style={{ fontWeight: '700' }}>
                    {copy.title}
                    {t.daily ? tx('rewards_daily') : ''}
                  </AppText>
                  <AppText variant="caption" tone="tertiary">
                    {copy.hint}
                    {t.goal > 1 ? ` · ${t.progress}/${t.goal}` : ''}
                  </AppText>
                </View>
                {t.claimed ? (
                  <View style={styles.row}>
                    <Icon name="check" size={14} color={theme.colors.accent} />
                    <AppText variant="caption" tone="secondary">
                      {tx('rewards_done')}
                    </AppText>
                  </View>
                ) : (
                  <Choice
                    label={done ? tx('rewards_collect', { points: t.points }) : `+${t.points}`}
                    selected={done}
                    onPress={() =>
                      done
                        ? act(
                            `task-${t.id}`,
                            () => rewards.claimTask(t.id),
                            r => `+${String(r.points)} points!`,
                          )
                        : undefined
                    }
                    testID={`rewards-task-${t.id}`}
                  />
                )}
              </View>
            );
          })}
        </GlassSurface>

        {/* ---- Share campaign */}
        <GlassSurface radius={18} style={styles.section} testID="rewards-share">
          <View style={styles.row}>
            <Icon name="share" size={20} color={theme.colors.accent} />
            <AppText variant="bodyStrong" style={{ flex: 1 }}>
              {tx('rewards_share_photo_plus')} {s.days} {tx('rewards_days_of_premium')}
            </AppText>
          </View>
          <AppText variant="caption" tone="secondary">
            {tx('rewards_share')} {s.goal} {tx('rewards_times_to_your_apps_each')} {s.lockHours} hours.
          </AppText>
          <View style={styles.track}>
            <View
              style={[
                styles.fill,
                {
                  width: `${Math.round(((s.ready ? s.goal : s.count) / s.goal) * 100)}%`,
                  backgroundImage: theme.gradients.premium,
                },
              ]}
            />
          </View>
          <AppText variant="caption" tone="tertiary" testID="rewards-share-count">
            {s.ready ? s.goal : s.count} of {s.goal} shares
          </AppText>
          {s.ready ? (
            <Button
              label={tx('rewards_collect_days_of_premium', { days: s.days })}
              icon="crown"
              variant="premium"
              onPress={() =>
                act(
                  'pass',
                  () => rewards.claimSharePass(),
                  r => `Premium until ${when(Number(r.until))}. Enjoy!`,
                )
              }
              loading={busy === 'pass'}
              testID="rewards-share-claim"
            />
          ) : campaignClosed ? (
            <AppText variant="caption" tone="secondary">
              {tx('rewards_you_ve_done_this_one')} {when(s.opensAt)}.
            </AppText>
          ) : (
            <Button
              label={sharing ? tx('rewards_hide') : tx('rewards_share_now')}
              icon="share"
              variant="glass"
              onPress={() => setSharing(v => !v)}
              testID="rewards-share-open"
            />
          )}
          {sharing && !s.ready && !campaignClosed ? (
            <View style={{ gap: 10 }} testID="rewards-share-panel">
              <View style={styles.wrap}>
                {SHARE_STYLES.map(st => (
                  <Choice
                    key={st.id}
                    label={tr(st.name)}
                    selected={form === st.id}
                    onPress={() => setForm(st.id)}
                    testID={`rewards-form-${st.id}`}
                  />
                ))}
              </View>
              <AppText variant="caption" tone="tertiary">
                {tr(style.about)}
              </AppText>
              <Image
                source={{ uri: `asset:/promo/${asset}` }}
                style={[styles.promo, { aspectRatio: style.aspect }]}
                resizeMode="cover"
                accessibilityLabel={tx('rewards_the_picture_you_share')}
                testID="rewards-share-picture"
              />
              <View style={styles.textBox}>
                <AppText variant="caption" tone="secondary" numberOfLines={6} testID="rewards-share-text">
                  {text}
                </AppText>
              </View>
              <AppText variant="caption" tone="tertiary" testID="rewards-share-caption-note">
                {tx('rewards_the_link_and_hashtags_go')}
              </AppText>
              <Button
                label={tx('rewards_choose_an_app_to_share')}
                icon="share"
                onPress={() => setSheet(true)}
                disabled={busy !== null}
                testID="rewards-share-choose"
              />
            </View>
          ) : null}
        </GlassSurface>
        <ShareSheet
          visible={sheet}
          onClose={() => setSheet(false)}
          onPick={share}
          locks={s.locks}
          counts={s.counts ?? {}}
          now={now}
          busy={busy !== null}
        />

        {/* ---- Spend */}
        <GlassSurface radius={18} style={styles.section} testID="rewards-store">
          <AppText variant="bodyStrong">{tx('rewards_spend_your_points')}</AppText>
          {state.store.map(item => {
            const short = state.points < item.cost;
            return (
              <View key={item.id} style={{ gap: 8 }}>
                <View style={styles.task}>
                  <View style={{ flex: 1 }}>
                    <AppText variant="caption" style={{ fontWeight: '700' }}>
                      {ITEM_TEXT[item.id]}
                    </AppText>
                    <AppText variant="caption" tone="tertiary">
                      {item.cost.toLocaleString()} points
                      {short ? tx('rewards_to_go', { value: (item.cost - state.points).toLocaleString() }) : ''}
                    </AppText>
                  </View>
                  <Choice
                    label={tx('rewards_redeem')}
                    icon="crown"
                    selected={!short}
                    onPress={() =>
                      short
                        ? setNote({
                            text: tx('rewards_you_need_more_points', {
                              value: (item.cost - state.points).toLocaleString(),
                            }),
                            good: false,
                          })
                        : act(
                            `redeem-${item.id}`,
                            () => rewards.redeem(item.id, item.id === 'tool' ? tool : undefined),
                            () =>
                              item.id === 'tool'
                                ? `${TOOL_TEXT[tool]} is yours for 24 hours!`
                                : 'Premium is on. Enjoy!',
                          )
                    }
                    testID={`rewards-redeem-${item.id}`}
                  />
                </View>
                {item.id === 'tool' ? (
                  <View style={styles.wrap}>
                    {(Object.keys(TOOL_TEXT) as RewardTool[]).map(t => (
                      <Choice
                        key={t}
                        label={TOOL_TEXT[t]}
                        selected={tool === t}
                        onPress={() => setTool(t)}
                        testID={`rewards-tool-${t}`}
                      />
                    ))}
                  </View>
                ) : null}
              </View>
            );
          })}
          <AppText variant="caption" tone="tertiary">
            {tx('rewards_rewards_give_up_to')} {state.capDays} {tx('rewards_days_of_premium_in_any')}
            {state.daysUsed} {tx('rewards_used_premium_itself_has_no')}
          </AppText>
        </GlassSurface>

        <Pressable onPress={() => navigation.navigate('Premium')} accessibilityRole="button" testID="rewards-premium">
          <GlassSurface radius={18} style={[styles.section, styles.row]}>
            <Icon name="crown" size={22} color={theme.colors.accent} />
            <View style={{ flex: 1 }}>
              <AppText variant="bodyStrong">{tx('rewards_want_everything_always')}</AppText>
              <AppText variant="caption" tone="secondary">
                {tx('rewards_premium_unlocks_every_tool_with')}
              </AppText>
            </View>
            <Icon name="chevronRight" size={18} color={theme.colors.textTertiary} />
          </GlassSurface>
        </Pressable>
      </View>
    </Screen>
  );
}

const styles = StyleSheet.create({
  balance: { padding: 18, gap: 4, alignItems: 'center' },
  section: { padding: 14, gap: 10 },
  row: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  wrap: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  pill: { paddingHorizontal: 12, paddingVertical: 4, borderRadius: 999, marginTop: 6 },
  pillText: { color: '#1A1206', fontWeight: '800' },
  week: { flexDirection: 'row', gap: 5 },
  day: { flex: 1, alignItems: 'center', gap: 4, paddingVertical: 8, borderRadius: 12, borderWidth: 1.5 },
  task: { flexDirection: 'row', alignItems: 'center', gap: 10, minHeight: 44 },
  track: { height: 8, borderRadius: 4, backgroundColor: 'rgba(255,255,255,0.1)', overflow: 'hidden' },
  fill: { height: 8, borderRadius: 4 },
  promo: { width: '62%', alignSelf: 'center', borderRadius: 14 },
  textBox: { padding: 10, borderRadius: 12, backgroundColor: 'rgba(255,255,255,0.05)' },
  app: {
    minWidth: 92,
    minHeight: 48,
    paddingHorizontal: 10,
    borderRadius: 14,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.16)',
    alignItems: 'center',
    justifyContent: 'center',
  },
});
