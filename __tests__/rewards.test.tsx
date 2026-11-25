import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';
import { Alert } from 'react-native';

import { PremiumSheet } from '../src/components/PremiumSheet';
import { PremiumScreen } from '../src/screens/premium/PremiumScreen';
import { RewardsScreen } from '../src/screens/premium/RewardsScreen';
import { perMonth, PLANS, PRICES_CONFIRMED, savings, SUBSCRIPTION_ID, TRIAL_DAYS } from '../src/services/billing/plans';
import { HASHTAGS, PLAY_URL, SHARE_APPS, SHARE_STYLES, shareText } from '../src/services/rewards/campaign';
import { isRewardsState, recordActivity, type RewardsState } from '../src/services/rewards/RewardsService';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';

const mockOpen = jest.fn();
jest.mock('../src/navigation/ref', () => ({ openScreen: (n: string) => mockOpen(n) }));

const NOW = 1_800_000_000_000;
const base = (): RewardsState => ({
  subscribed: false,
  points: 640,
  earned: 900,
  now: NOW,
  checkIn: { day: 3, done: false, schedule: [10, 15, 20, 25, 30, 40, 60] },
  ads: { today: 2, perDay: 8, points: 20, wait: 0 },
  tasks: [
    { id: 'first-photo', points: 30, daily: false, goal: 1, progress: 1, claimed: false },
    { id: 'daily-photos', points: 15, daily: true, goal: 3, progress: 1, claimed: false },
    { id: 'first-edit', points: 30, daily: false, goal: 1, progress: 1, claimed: true },
  ],
  shares: {
    count: 12,
    goal: 30,
    days: 2,
    lockHours: 8,
    ready: false,
    opensAt: 0,
    locks: { 'com.whatsapp': NOW + 5 * 3_600_000 },
    counts: { 'com.whatsapp': 4 },
  },
  premiumUntil: 0,
  tools: {},
  daysUsed: 0,
  capDays: 10,
  store: [
    { id: 'tool', cost: 250, days: 0, hours: 24 },
    { id: 'premium-1', cost: 600, days: 1, hours: 0 },
    { id: 'premium-3', cost: 1500, days: 3, hours: 0 },
    { id: 'premium-7', cost: 3200, days: 7, hours: 0 },
  ],
});
let mockState = base();
const withState = (patch: Partial<RewardsState>) => (mockState = { ...mockState, ...patch });
const mockRewards = {
  getState: jest.fn(async () => mockState),
  checkIn: jest.fn(async () => ({
    points: 20,
    state: withState({ points: 660, checkIn: { ...mockState.checkIn, done: true } }),
  })),
  claimTask: jest.fn(async (id: string) => ({
    points: 30,
    state: withState({ points: 670, tasks: mockState.tasks.map(t => (t.id === id ? { ...t, claimed: true } : t)) }),
  })),
  redeem: jest.fn(async (item: string, tool: string) => ({
    until: NOW + 86_400_000,
    state: withState(
      item === 'tool'
        ? { points: 390, tools: { [tool]: NOW + 86_400_000 } }
        : { points: 40, premiumUntil: NOW + 86_400_000 },
    ),
  })),
  claimSharePass: jest.fn(async () => ({
    until: NOW + 2 * 86_400_000,
    state: withState({ premiumUntil: NOW + 2 * 86_400_000 }),
  })),
  shareTargets: jest.fn(async () => [
    { pkg: 'com.zzz.notes', activity: 'Share', label: 'Notes', app: 'Notes', icon: '' },
    {
      pkg: 'com.whatsapp',
      activity: 'ContactPicker',
      label: 'WhatsApp',
      app: 'WhatsApp',
      icon: '/cache/share-icons/wa.png',
    },
    {
      pkg: 'com.whatsapp',
      activity: 'StatusShare',
      label: 'My status',
      app: 'WhatsApp',
      icon: '/cache/share-icons/wa2.png',
    },
    { pkg: 'com.instagram.android', activity: 'Feed', label: 'Feed', app: 'Instagram', icon: '' },
    { bad: true },
  ]),
  share: jest.fn(async (target: string, _activity: string) => ({
    opened: true,
    counted: true,
    target,
    state: withState({
      shares: {
        ...mockState.shares,
        count: mockState.shares.count + 1,
        locks: { ...mockState.shares.locks, [target]: NOW + 8 * 3_600_000 },
        counts: { ...mockState.shares.counts, [target]: (mockState.shares.counts[target] ?? 0) + 1 },
      },
    }),
  })),
  recordActivity: jest.fn(),
  addListener: jest.fn(),
  removeListeners: jest.fn(),
};
const mockAds = {
  initialize: jest.fn(async () => ({ canRequestAds: true })),
  showRewardedAdForPoints: jest.fn(async () => ({
    rewarded: true,
    points: 20,
    state: withState({ points: 660, ads: { ...mockState.ads, today: 3, wait: 60 } }),
  })),
};
jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get rewards() {
        return mockRewards;
      },
      get ads() {
        return mockAds;
      },
    },
  };
});

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const tick = () => new Promise<void>(r => setTimeout(() => r(), 0));
const step = async (fn: () => unknown) =>
  act(async () => {
    await fn();
    for (let i = 0; i < 8; i++) await tick();
  });
const nav = () => ({ navigate: jest.fn(), goBack: jest.fn() });

beforeEach(() => {
  jest.clearAllMocks();
  mockState = base();
  useAppStore.setState({ entitlements: { plan: 'free', ownedPackIds: [], includedPackIds: [] } } as never);
});

describe('Premium plans', () => {
  it('monthly is GHS 50; longer plans save, each with a 7-day free trial', () => {
    expect(PLANS.map(p => [p.id, p.months])).toEqual([
      ['monthly', 1],
      ['quarterly', 3],
      ['yearly', 12],
    ]);
    expect(PLANS[0].price).toBe(50);
    expect(PLANS[0].currency).toBe('GHS');
    expect(savings(PLANS[0])).toBe(0);
    expect(savings(PLANS[1])).toBeGreaterThan(0);
    expect(savings(PLANS[2])).toBeGreaterThan(savings(PLANS[1]));
    expect(perMonth(PLANS[2])).toBeLessThan(perMonth(PLANS[1]));
    expect(TRIAL_DAYS).toBe(7);
    expect(SUBSCRIPTION_ID).toBe('photoplus_premium');
    // To be confirmed before release.
    expect(PRICES_CONFIRMED).toBe(false);
  });

  it('shows the plans, the trial and the way to Rewards', async () => {
    const n = nav();
    const alert = jest.spyOn(Alert, 'alert').mockImplementation(() => undefined);
    await step(() => wrap(<PremiumScreen navigation={n as never} route={{ key: 'p', name: 'Premium' } as never} />));
    expect(screen.getByTestId('premium-plan-yearly').props.accessibilityState.selected).toBe(true);
    expect(screen.getByText('GHS 50')).toBeTruthy();
    expect(screen.getAllByText(/7 DAYS FREE TRIAL|7-day free trial/).length).toBeGreaterThan(0);
    await step(() => fireEvent.press(screen.getByTestId('premium-plan-monthly')));
    expect(screen.getByText(/then GHS 50 per month/)).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('premium-start')));
    expect(alert).toHaveBeenCalled();
    await step(() => fireEvent.press(screen.getByTestId('premium-rewards')));
    expect(n.navigate).toHaveBeenCalledWith('Rewards');
  });

  it('the Premium sheet leads to the plans and to Rewards', async () => {
    const close = jest.fn();
    await step(() => wrap(<PremiumSheet feature="relight" onClose={close} />));
    expect(screen.getByText(/7-day free trial/)).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('premium-sheet-plans')));
    await step(() => fireEvent.press(screen.getByTestId('premium-sheet-rewards')));
    expect(mockOpen.mock.calls.map(c => c[0])).toEqual(['Premium', 'Rewards']);
    expect(close).toHaveBeenCalledTimes(2);
  });
});

describe('the share campaign', () => {
  it('post and friend words carry the link, the trial and (for posts) hashtags, and vary', () => {
    for (const form of ['post', 'status', 'friends'] as const) {
      const texts = [0, 1, 2].map(i => shareText(form, i));
      expect(new Set(texts).size).toBe(3);
      for (const t of texts) {
        expect(t).toContain(PLAY_URL);
        expect(t).toMatch(/7[- ]days? free|FREE for 7 days|7 days FREE/i);
        // Posts carry the hashtags; a note to friends does not.
        expect(t.includes(HASHTAGS.join(' '))).toBe(form !== 'friends');
      }
    }
    expect(shareText('post', 3)).toBe(shareText('post', 0));
    expect(SHARE_STYLES.map(s => s.asset)).toEqual(['post.jpg', 'status.jpg', 'friends.jpg']);
    expect(SHARE_APPS.some(a => a.pkg === 'com.whatsapp')).toBe(true);
  });

  it('records activity without ever throwing', () => {
    mockRewards.recordActivity.mockImplementationOnce(() => {
      throw new Error('nope');
    });
    expect(() => recordActivity('photo', 'edit')).not.toThrow();
    expect(mockRewards.recordActivity).toHaveBeenCalledTimes(2);
    expect(isRewardsState(base())).toBe(true);
    expect(isRewardsState({ points: 1 })).toBe(false);
  });
});

describe('Rewards', () => {
  it('checks in, watches an ad, collects a task and redeems a tool for a day', async () => {
    await step(() =>
      wrap(<RewardsScreen navigation={nav() as never} route={{ key: 'r', name: 'Rewards' } as never} />),
    );
    expect(screen.getByText('640')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('rewards-checkin-button')));
    expect(screen.getByText(/\+20 points. See you tomorrow/)).toBeTruthy();
    expect(screen.getByTestId('rewards-checkin-button').props.accessibilityState.disabled).toBe(true);

    await step(() => fireEvent.press(screen.getByTestId('rewards-ad')));
    expect(mockAds.showRewardedAdForPoints).toHaveBeenCalled();
    expect(screen.getByText(/Next ad in 60 s/)).toBeTruthy();

    await step(() => fireEvent.press(screen.getByTestId('rewards-task-first-photo')));
    expect(mockRewards.claimTask).toHaveBeenCalledWith('first-photo');
    // Not done yet: nothing is claimed.
    await step(() => fireEvent.press(screen.getByTestId('rewards-task-daily-photos')));
    expect(mockRewards.claimTask).toHaveBeenCalledTimes(1);

    await step(() => fireEvent.press(screen.getByTestId('rewards-tool-relight')));
    await step(() => fireEvent.press(screen.getByTestId('rewards-redeem-tool')));
    expect(mockRewards.redeem).toHaveBeenCalledWith('tool', 'relight');
    expect(screen.getByText(/Relight unlocked · 24 h left/)).toBeTruthy();
    // Not enough points for a week: told how many more.
    await step(() => fireEvent.press(screen.getByTestId('rewards-redeem-premium-7')));
    expect(mockRewards.redeem).toHaveBeenCalledTimes(1);
    expect(screen.getByText(/more points/)).toBeTruthy();
  });

  it('shares through the app’s own share sheet: every app, counts and 8-hour locks', async () => {
    await step(() =>
      wrap(<RewardsScreen navigation={nav() as never} route={{ key: 'r', name: 'Rewards' } as never} />),
    );
    expect(screen.getByTestId('rewards-share-count').props.children).toEqual([12, ' of ', 30, ' shares']);
    await step(() => fireEvent.press(screen.getByTestId('rewards-share-open')));
    // Status: the tall picture with the post words.
    await step(() => fireEvent.press(screen.getByTestId('rewards-form-status')));
    expect(screen.getByTestId('rewards-share-picture').props.source.uri).toBe('asset:/promo/status.jpg');
    await step(() => fireEvent.press(screen.getByTestId('rewards-share-choose')));
    expect(screen.getByTestId('share-sheet')).toBeTruthy();
    // Every app that takes a picture, popular ones first; an app with two share screens shows both.
    const ids = screen.getAllByTestId(/^share-target-/).map(e => e.props.testID as string);
    expect(ids).toEqual([
      'share-target-com.whatsapp-ContactPicker',
      'share-target-com.whatsapp-StatusShare',
      'share-target-com.instagram.android-Feed',
      'share-target-com.zzz.notes-Share',
    ]);
    // WhatsApp: shared 4 times, locked for 5 more hours (both its screens).
    expect(screen.getAllByText('4')).toHaveLength(2);
    expect(screen.getByLabelText('WhatsApp, opens again in 5 hours')).toBeTruthy();
    expect(screen.getByLabelText('WhatsApp My status, opens again in 5 hours')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('share-target-com.whatsapp-StatusShare')));
    expect(mockRewards.share).not.toHaveBeenCalled();
    // Instagram: shared straight to its screen, with the picture and the post words.
    await step(() => fireEvent.press(screen.getByTestId('share-target-com.instagram.android-Feed')));
    const [target, activity, asset, text] = mockRewards.share.mock.calls[0] as unknown as string[];
    expect([target, activity, asset]).toEqual(['com.instagram.android', 'Feed', 'status.jpg']);
    expect(text).toContain('#PhotoPlus');
    expect(screen.getByText(/Shared to Instagram — it counts!/)).toBeTruthy();
    expect(screen.getByTestId('rewards-share-count').props.children).toEqual([13, ' of ', 30, ' shares']);
    // Now Instagram is locked too, and counted once.
    await step(() => fireEvent.press(screen.getByTestId('rewards-form-friends')));
    await step(() => fireEvent.press(screen.getByTestId('rewards-share-choose')));
    expect(screen.getByLabelText('Instagram, opens again in 8 hours')).toBeTruthy();
    // Friends: the square picture and a personal note.
    await step(() => fireEvent.press(screen.getByTestId('share-target-com.zzz.notes-Share')));
    const [t2, , a2, words] = mockRewards.share.mock.calls[1] as unknown as string[];
    expect([t2, a2]).toEqual(['com.zzz.notes', 'friends.jpg']);
    expect(words).not.toContain('#PhotoPlus');
  });

  it('30 shares: collect two days of Premium; subscribers are told Rewards are for the free plan', async () => {
    mockState = { ...base(), shares: { ...base().shares, count: 0, ready: true } };
    await step(() =>
      wrap(<RewardsScreen navigation={nav() as never} route={{ key: 'r', name: 'Rewards' } as never} />),
    );
    await step(() => fireEvent.press(screen.getByTestId('rewards-share-claim')));
    expect(mockRewards.claimSharePass).toHaveBeenCalled();
    expect(screen.getAllByText(/Premium until/).length).toBeGreaterThan(0);
    screen.unmount();
    mockState = { ...base(), subscribed: true };
    await step(() =>
      wrap(<RewardsScreen navigation={nav() as never} route={{ key: 'r', name: 'Rewards' } as never} />),
    );
    expect(screen.getByTestId('rewards-subscribed')).toBeTruthy();
  });
});
