import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import ar from '../src/i18n/strings/ar.json';
import en from '../src/i18n/strings/en.json';
import es from '../src/i18n/strings/es.json';
import fr from '../src/i18n/strings/fr.json';
import hi from '../src/i18n/strings/hi.json';
import id from '../src/i18n/strings/id.json';
import ptBR from '../src/i18n/strings/pt-BR.json';
import ru from '../src/i18n/strings/ru.json';
import zhCN from '../src/i18n/strings/zh-CN.json';
import { OnboardingScreen, PAGES } from '../src/screens/OnboardingScreen';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';

// The trial as the device holds it: simulated in testing builds (available), Play Billing in the store build.
let mockTrial = { available: true, active: false, used: false };
const mockEngine = {
  getPlan: jest.fn(async () => (mockTrial.active ? 'premium' : 'free')),
  trialState: jest.fn(async () => ({ ...mockTrial, endsAt: 0, days: 7 })),
  startTrial: jest.fn(async () => {
    if (!mockTrial.available || mockTrial.used) return false;
    mockTrial = { ...mockTrial, active: true, used: true };
    return true;
  }),
};
jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get engine() {
        return mockEngine;
      },
    },
  };
});
const mockEnsure = jest.fn(async (_p: string) => 'granted');
jest.mock('../src/services/permissions/PermissionService', () => ({
  getPermissionService: () => ({ ensure: (p: string) => mockEnsure(p) }),
}));

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const tick = () => new Promise<void>(r => setTimeout(() => r(), 0));
const step = async (fn: () => unknown) =>
  act(async () => {
    await fn();
    for (let i = 0; i < 10; i++) await tick();
  });
const nav = () => ({ reset: jest.fn(), navigate: jest.fn() });
const updateSetting = jest.fn(async () => undefined);

beforeEach(() => {
  jest.clearAllMocks();
  mockTrial = { available: true, active: false, used: false };
  useAppStore.setState({ updateSetting } as never);
});

async function toLastPage(from = 0) {
  for (let i = from; i < PAGES.length - 1; i++)
    await step(() => fireEvent.press(screen.getByTestId('onboarding-next')));
}

describe('onboarding', () => {
  it('shows what the app is about, a page for each part, and ends on the free trial', () => {
    expect(PAGES.map(p => p.art)).toEqual([
      'welcome',
      'camera',
      'assist',
      'studio',
      'arts',
      'motion',
      'toon',
      'private',
      'trial',
    ]);
    expect((en as Record<string, string>).onb_trial_cta).toBe('Get {days} days free trial');
  });

  it('pages through every feature, then starts the simulated 7-day trial and opens the app', async () => {
    const n = nav();
    await wrap(<OnboardingScreen navigation={n as never} route={{ key: 'o', name: 'Onboarding' } as never} />);
    await step(() => undefined);
    expect(screen.getByText('Page 1 of 9')).toBeTruthy();
    expect(screen.getByText('Welcome to Photo Plus')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('onboarding-next')));
    expect(screen.getByText('Page 2 of 9')).toBeTruthy();
    await toLastPage(1);
    expect(screen.getByText('Page 9 of 9')).toBeTruthy();
    expect(screen.getAllByText('Get 7 days free trial').length).toBeGreaterThan(0);
    expect(screen.queryByTestId('onboarding-next')).toBeNull();
    expect(screen.getByTestId('onboarding-test-note')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('onboarding-trial')));
    expect(mockEngine.startTrial).toHaveBeenCalled();
    expect(useAppStore.getState().entitlements.plan).toBe('premium');
    expect(mockEnsure).toHaveBeenCalledWith('camera');
    expect(updateSetting).toHaveBeenCalledWith('onboardingCompleted', true);
    expect(n.reset).toHaveBeenCalledWith({ index: 0, routes: [{ name: 'Main' }] });
    expect(n.navigate).not.toHaveBeenCalled();
  });

  it('skips to the trial page; a trial already used says so; the free plan opens the app', async () => {
    mockTrial = { available: true, active: false, used: true };
    const n = nav();
    await wrap(<OnboardingScreen navigation={n as never} route={{ key: 'o', name: 'Onboarding' } as never} />);
    await step(() => undefined);
    await step(() => fireEvent.press(screen.getByTestId('onboarding-skip')));
    expect(screen.getByText('Page 9 of 9')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('onboarding-trial')));
    expect(screen.getByTestId('onboarding-note').props.children).toBe(
      'The free trial has already been used on this phone.',
    );
    expect(n.reset).not.toHaveBeenCalled();
    await step(() => fireEvent.press(screen.getByTestId('onboarding-free')));
    expect(n.reset).toHaveBeenCalledWith({ index: 0, routes: [{ name: 'Main' }] });
    expect(updateSetting).toHaveBeenCalledWith('onboardingCompleted', true);
  });

  it('in the store build, the trial goes to the Premium page (Google Play starts it)', async () => {
    mockTrial = { available: false, active: false, used: false };
    const n = nav();
    await wrap(<OnboardingScreen navigation={n as never} route={{ key: 'o', name: 'Onboarding' } as never} />);
    await step(() => undefined);
    await step(() => fireEvent.press(screen.getByTestId('onboarding-skip')));
    expect(screen.queryByTestId('onboarding-test-note')).toBeNull();
    await step(() => fireEvent.press(screen.getByTestId('onboarding-trial')));
    expect(mockEngine.startTrial).not.toHaveBeenCalled();
    expect(n.reset).toHaveBeenCalledWith({ index: 0, routes: [{ name: 'Main' }] });
    expect(n.navigate).toHaveBeenCalledWith('Premium');
  });

  it('is written in all eight other languages, every word (and the new limits and owed-ads words too)', () => {
    const keys = Object.keys(en).filter(
      k =>
        /^(onb_|ads_owed|ads_offline|ads_watch_owed|arts_|toon_(more|outline|backdrop|brightness|warmth|texture|face_detail|hair_detail|ink))/.test(
          k,
        ) || ['native_arts_ad_required', 'native_arts_daily_used', 'native_ads_owed_frozen'].includes(k),
    );
    expect(keys.length).toBeGreaterThanOrEqual(55);
    for (const table of [es, ptBR, fr, ar, hi, id, ru, zhCN] as Record<string, string>[]) {
      for (const k of keys) {
        expect(typeof table[k]).toBe('string');
        expect(table[k].length).toBeGreaterThan(0);
        for (const ph of (en as Record<string, string>)[k].match(/\{\w+\}/g) ?? []) expect(table[k]).toContain(ph);
      }
    }
    // The FAQ tells the new limits.
    for (const table of [en, es, ptBR, fr, ar, hi, id, ru, zhCN] as Record<string, string>[]) {
      expect(table.faq_studio_16_a).toContain('5');
      expect(table.faq_studio_10_a).toContain('5');
    }
  });
});
