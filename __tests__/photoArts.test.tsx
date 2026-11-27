import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { MastersScreen } from '../src/screens/studio/MastersScreen';
import { offset } from '../src/screens/studio/PhotoArtsTouch';
import { SettingsScreen } from '../src/screens/SettingsScreen';
import {
  ARTIST_WAY,
  CONTROL_RANGES,
  DEFAULT_PHOTO_ARTS,
  clampControls,
  isArtControls,
  isArtistWay,
  isPhotoArtsSettings,
} from '../src/services/masters/controls';
import { DEFAULT_SETTINGS, isValidSetting } from '../src/services/database/repositories/SettingsRepository';
import { getProcessingService } from '../src/services/processing/ImageProcessingService';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';

const mockStudio = {
  renderMasters: jest.fn(async (_id: string, req: { quality: string; artist: number }) => ({
    outputPath: `/cache/ms_${req.quality}_${req.artist}.jpg`,
    width: 760,
    height: 950,
    sizeBytes: 1000,
    remaining: 1,
  })),
  mastersAllowance: jest.fn(async () => ({ remaining: 1, limit: 1 })),
  saveToGallery: jest.fn(),
  cancel: jest.fn(),
};
let mockSim = { available: true, on: false };
const mockEngine = {
  getPlan: jest.fn(async () => (mockSim.on ? 'premium' : 'free')),
  setDebugPlan: jest.fn(async (plan: string) => {
    if (!mockSim.available) return false;
    mockSim = { ...mockSim, on: plan === 'premium' };
    return true;
  }),
  getPremiumSimulation: jest.fn(async () => mockSim),
};
const mockStorage = {
  pickImage: jest.fn(async () => '/files/imports/me.jpg'),
  usage: jest.fn(async () => {
    throw new Error('no storage');
  }),
};
jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get studio() {
        return mockStudio;
      },
      get engine() {
        return mockEngine;
      },
      get storage() {
        return mockStorage;
      },
    },
  };
});

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const tick = () => new Promise<void>(r => setTimeout(() => r(), 0));
const step = async (fn: () => unknown) =>
  act(async () => {
    await fn();
    for (let i = 0; i < 12; i++) await tick();
  });
const navigation = { navigate: jest.fn(), goBack: jest.fn(), setParams: jest.fn() };
const nudge = (id: string, way: 'increment' | 'decrement', times = 1) =>
  step(async () => {
    for (let i = 0; i < times; i++)
      await fireEvent(screen.getByTestId(id), 'accessibilityAction', { nativeEvent: { actionName: way } });
  });
// The last preview or painting asked for (cards are painted the artist's way, without controls).
const lastRequest = () =>
  mockStudio.renderMasters.mock.calls.filter(c => c[1].quality !== 'thumb').at(-1)![1] as unknown as {
    quality: string;
    controls?: Record<string, number>;
  };

beforeEach(() => {
  jest.clearAllMocks();
  mockSim = { available: true, on: false };
  useAppStore.setState({
    repos: null,
    settings: { ...DEFAULT_SETTINGS },
    premiumSimulation: { available: false, on: false },
  } as never);
});

describe('your touch: the controls', () => {
  it('starts at the artist’s own way, stays in range and is checked', () => {
    expect(isArtControls(ARTIST_WAY)).toBe(true);
    expect(isArtistWay(ARTIST_WAY)).toBe(true);
    expect(isArtistWay({ ...ARTIST_WAY, brush: 1.2 })).toBe(false);
    for (const r of CONTROL_RANGES) {
      expect(ARTIST_WAY[r.key]).toBeGreaterThanOrEqual(r.min);
      expect(ARTIST_WAY[r.key]).toBeLessThanOrEqual(r.max);
    }
    expect(clampControls({ brush: 9, vivid: -4, palette: Number.NaN })).toEqual({
      ...ARTIST_WAY,
      brush: 1.6,
      vivid: -1,
    });
    expect(isArtControls({ ...ARTIST_WAY, texture: 3 })).toBe(false);
    expect(isPhotoArtsSettings(DEFAULT_PHOTO_ARTS)).toBe(true);
    expect(isPhotoArtsSettings({ ...DEFAULT_PHOTO_ARTS, level: 3 })).toBe(false);
    expect(isValidSetting('photoArts', DEFAULT_PHOTO_ARTS)).toBe(true);
    expect(isValidSetting('photoArts', { level: 1 })).toBe(false);
    // Shown as the distance from the artist's way, in any language.
    expect(offset('brush', 1, 0.6, 1.6)).toBe('0');
    expect(offset('brush', 1.3, 0.6, 1.6)).toBe('+30');
    expect(offset('vivid', -0.5, -1, 1)).toBe('−25');
  });

  it('guides the painting from the screen, repaints when the finger lifts, remembers it and resets', async () => {
    await wrap(<MastersScreen navigation={navigation as never} route={{ key: 'm', name: 'Masters' } as never} />);
    expect(screen.getByText('Photo Arts')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('masters-pick')));
    expect(lastRequest().controls).toEqual(ARTIST_WAY);

    await step(() => fireEvent.press(screen.getByTestId('masters-touch-toggle')));
    expect(screen.getByText('Brush size')).toBeTruthy();
    expect(screen.getByText('Bolder')).toBeTruthy();
    await nudge('masters-ctl-brush', 'increment', 2);
    await nudge('masters-ctl-background', 'decrement', 1);
    const req = lastRequest();
    expect(req.quality).toBe('preview');
    expect(req.controls).toMatchObject({ brush: 1.1, background: 0.95 });
    // Remembered for next time.
    expect(useAppStore.getState().settings.photoArts.controls).toMatchObject({ brush: 1.1, background: 0.95 });

    // The finished painting uses the touch too.
    await step(() => fireEvent.press(screen.getByTestId('masters-paint')));
    expect(lastRequest()).toMatchObject({ quality: 'full', controls: { brush: 1.1 } });

    // Back to the artist's way.
    await step(() => fireEvent.press(screen.getByTestId('masters-touch-reset')));
    expect(lastRequest().controls).toEqual(ARTIST_WAY);
    expect(useAppStore.getState().settings.photoArts.controls).toEqual(ARTIST_WAY);
  });

  it('follows its settings: the finish to start with, not remembering, and cards without paintings', async () => {
    useAppStore.setState({
      settings: {
        ...DEFAULT_SETTINGS,
        photoArts: { level: 2, remember: false, cardPreviews: false, controls: { ...ARTIST_WAY, brush: 1.5 } },
      },
    } as never);
    await wrap(<MastersScreen navigation={navigation as never} route={{ key: 'm', name: 'Masters' } as never} />);
    await step(() => fireEvent.press(screen.getByTestId('masters-pick')));
    // A Masterpiece, the artist's way (the touch isn't remembered), and no card paintings.
    expect(lastRequest()).toMatchObject({ quality: 'preview', level: 2, controls: ARTIST_WAY });
    expect(mockStudio.renderMasters.mock.calls.filter(c => c[1].quality === 'thumb')).toHaveLength(0);
    expect(screen.getByText('L')).toBeTruthy();
  });
});

describe('Settings', () => {
  it('sets Photo Arts’s finish, memory, cards and resets the touch', async () => {
    useAppStore.setState({
      settings: { ...DEFAULT_SETTINGS, photoArts: { ...DEFAULT_PHOTO_ARTS, controls: { ...ARTIST_WAY, age: 0 } } },
    } as never);
    await wrap(<SettingsScreen navigation={{ navigate: jest.fn() } as never} route={{} as never} />);
    expect(screen.getByTestId('settings-photo-arts')).toBeTruthy();
    await fireEvent.press(screen.getByRole('radio', { name: 'Masterpiece' }));
    expect(useAppStore.getState().settings.photoArts.level).toBe(2);
    await fireEvent.press(screen.getByTestId('settings-ms-reset'));
    expect(useAppStore.getState().settings.photoArts.controls).toEqual(ARTIST_WAY);
    await fireEvent(screen.getByRole('switch', { name: /Remember my touch/ }), 'valueChange', false);
    expect(useAppStore.getState().settings.photoArts.remember).toBe(false);
    // No Testing section where the build doesn't allow the simulation.
    expect(screen.queryByTestId('settings-testing')).toBeNull();
  });

  it('lets testers simulate Premium, and stop', async () => {
    useAppStore.setState({ premiumSimulation: { available: true, on: false } } as never);
    await wrap(<SettingsScreen navigation={{ navigate: jest.fn() } as never} route={{} as never} />);
    expect(screen.getByText('Simulate Premium')).toBeTruthy();
    await step(() => fireEvent(screen.getByRole('switch', { name: /Simulate Premium/ }), 'valueChange', true));
    expect(mockEngine.setDebugPlan).toHaveBeenCalledWith('premium');
    expect(useAppStore.getState().entitlements.plan).toBe('premium');
    expect(useAppStore.getState().premiumSimulation.on).toBe(true);
    expect(screen.getByText('Premium is simulated on this phone')).toBeTruthy();
    await step(() => fireEvent(screen.getByRole('switch', { name: /Simulate Premium/ }), 'valueChange', false));
    expect(useAppStore.getState().entitlements.plan).toBe('free');
  });

  it('reads what the build allows', async () => {
    expect(await getProcessingService().premiumSimulation()).toEqual({ available: true, on: false });
    mockSim = { available: false, on: false };
    expect(await getProcessingService().simulatePremium(true)).toBe('free');
  });
});
