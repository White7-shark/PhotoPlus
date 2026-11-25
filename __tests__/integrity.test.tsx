import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';
import { BackHandler, Text } from 'react-native';
import { SafeAreaProvider } from 'react-native-safe-area-context';

import { IntegrityGate } from '../src/app/IntegrityGate';
import { OK_STATE, skewWords, type IntegrityState } from '../src/services/integrity/IntegrityService';
import { ThemeProvider } from '../src/theme';

const NOW = 1_800_000_000_000;
const DAY = 86_400_000;
const ok = (): IntegrityState => ({ ...OK_STATE, now: NOW, lastOnline: NOW });
let mockNow: IntegrityState = ok();
let mockAfterCheck: IntegrityState = ok();
const mockIntegrity = {
  getState: jest.fn(async () => mockNow),
  check: jest.fn(async () => mockAfterCheck),
  later: jest.fn(async () => ({ ...mockNow, update: { ...mockNow.update, banner: false } })),
  startUpdate: jest.fn(async () => 'play'),
  openDateSettings: jest.fn(),
  addListener: jest.fn(),
  removeListeners: jest.fn(),
};
jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get integrity() {
        return mockIntegrity;
      },
    },
  };
});

const wrap = () =>
  render(
    <SafeAreaProvider
      initialMetrics={{
        frame: { x: 0, y: 0, width: 390, height: 844 },
        insets: { top: 0, left: 0, right: 0, bottom: 0 },
      }}
    >
      <ThemeProvider preference="dark">
        <IntegrityGate>
          <Text>The app</Text>
        </IntegrityGate>
      </ThemeProvider>
    </SafeAreaProvider>,
  );
const tick = () => new Promise<void>(r => setTimeout(() => r(), 0));
const step = async (fn: () => unknown) =>
  act(async () => {
    await fn();
    for (let i = 0; i < 8; i++) await tick();
  });

const blocked = (reason: IntegrityState['reason'], patch: Partial<IntegrityState> = {}): IntegrityState => ({
  ...ok(),
  blocked: true,
  reason,
  ...patch,
});

beforeEach(() => {
  jest.clearAllMocks();
  mockNow = ok();
  mockAfterCheck = ok();
});

describe('the integrity gate', () => {
  it('lets the app through when all is well, and checks online on opening', async () => {
    await step(() => wrap());
    expect(screen.getByText('The app')).toBeTruthy();
    expect(screen.queryByTestId('integrity-block')).toBeNull();
    expect(mockIntegrity.check).toHaveBeenCalledWith(false);
  });

  it('blocks on a wrong clock until Check time confirms it online; back does nothing', async () => {
    jest.spyOn(BackHandler, 'addEventListener');
    mockNow = blocked('time', { skewMinutes: 3 * 24 * 60 });
    mockAfterCheck = mockNow;
    await step(() => wrap());
    expect(screen.getByTestId('integrity-title').props.children).toMatch(/date and time are wrong/);
    expect(screen.getByTestId('integrity-reason').props.children).toMatch(/3 days ahead/);
    // Back is swallowed while blocked: the gate's handler says it was handled.
    const handlers = (BackHandler.addEventListener as unknown as jest.SpyInstance).mock.calls
      .filter(c => c[0] === 'hardwareBackPress')
      .map(c => c[1] as () => boolean);
    expect(handlers.length).toBeGreaterThan(0);
    expect(handlers.at(-1)!()).toBe(true);
    // Settings opens the phone's date & time.
    await step(() => fireEvent.press(screen.getByTestId('integrity-settings')));
    expect(mockIntegrity.openDateSettings).toHaveBeenCalled();
    // Still wrong after the check: still blocked, and told why.
    await step(() => fireEvent.press(screen.getByTestId('integrity-check')));
    expect(mockIntegrity.check).toHaveBeenLastCalledWith(true);
    expect(screen.getByTestId('integrity-result').props.children).toMatch(/still wrong/);
    expect(screen.getByTestId('integrity-block')).toBeTruthy();
    // Offline: the check can't confirm, still blocked.
    mockAfterCheck = { ...mockNow, online: false };
    await step(() => fireEvent.press(screen.getByTestId('integrity-check')));
    expect(screen.getByTestId('integrity-result').props.children).toMatch(/No internet/);
    expect(screen.getByTestId('integrity-block')).toBeTruthy();
    // Fixed and confirmed online: the app opens.
    mockAfterCheck = ok();
    await step(() => fireEvent.press(screen.getByTestId('integrity-check')));
    expect(screen.queryByTestId('integrity-block')).toBeNull();
  });

  it('a week offline: connect and check the app’s integrity', async () => {
    mockNow = blocked('offline', { offlineDays: 8.2, online: false });
    mockAfterCheck = mockNow;
    await step(() => wrap());
    expect(screen.getByTestId('integrity-title').props.children).toBe('Connect to the internet');
    expect(screen.getByTestId('integrity-reason').props.children).toMatch(/once every 7 days.*8 days/);
    mockAfterCheck = ok();
    await step(() => fireEvent.press(screen.getByTestId('integrity-check')));
    expect(screen.queryByTestId('integrity-block')).toBeNull();
  });

  it('an update: a banner with its deadline (Update, Later)', async () => {
    mockNow = { ...ok(), update: { pending: true, required: false, banner: true, deadline: NOW + 2 * DAY } };
    mockAfterCheck = mockNow;
    await step(() => wrap());
    expect(screen.getByTestId('update-banner')).toBeTruthy();
    expect(screen.getByText(/2 days left/)).toBeTruthy();
    expect(screen.getByText('The app')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('update-now')));
    expect(mockIntegrity.startUpdate).toHaveBeenCalledTimes(1);
    await step(() => fireEvent.press(screen.getByTestId('update-later')));
    expect(mockIntegrity.later).toHaveBeenCalled();
    expect(screen.queryByTestId('update-banner')).toBeNull();
  });

  it('an overdue update blocks the app until it is installed', async () => {
    mockNow = blocked('update', { update: { pending: true, required: true, banner: false, deadline: NOW - DAY } });
    mockAfterCheck = mockNow;
    await step(() => wrap());
    expect(screen.getByTestId('integrity-title').props.children).toBe('Update Photo Plus');
    expect(screen.queryByTestId('update-banner')).toBeNull();
    await step(() => fireEvent.press(screen.getByTestId('integrity-update')));
    expect(mockIntegrity.startUpdate).toHaveBeenCalledTimes(1);
    // Updated: the check clears it.
    mockAfterCheck = ok();
    await step(() => fireEvent.press(screen.getByTestId('integrity-check')));
    expect(screen.queryByTestId('integrity-block')).toBeNull();
  });

  it('says how far the clock is off', () => {
    expect(skewWords(12)).toBe('12 minutes ahead');
    expect(skewWords(-180)).toBe('3 hours behind');
    expect(skewWords(-3 * 24 * 60)).toBe('3 days behind');
    expect(skewWords(null)).toMatch(/different date/);
  });
});
