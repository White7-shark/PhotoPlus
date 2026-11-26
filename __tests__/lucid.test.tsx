import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { AuroraBackground } from '../src/components/AuroraBackground';
import { LucidPanel } from '../src/components/LucidPanel';
import {
  chooseWallpaper,
  ensureLucid,
  hydrateLucid,
  LUCID_KEY,
  pickLucidBackground,
  useLucid,
} from '../src/services/lucid/LucidService';
import { useAppStore } from '../src/state/store';
import { ThemeProvider, createTheme } from '../src/theme';
import { contrast, hexToRgb, lucidLook, luminance, statsFromColors, type LookStats } from '../src/theme/lucid';

const mockLucid = {
  wallpaper: jest.fn(),
  imageStats: jest.fn(),
  showWallpaper: jest.fn(async () => true),
  pickBackground: jest.fn(),
};
jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get lucid() {
        return mockLucid;
      },
    },
  };
});

const BRIGHT: LookStats = {
  mean: 0.7,
  p10: 0.35,
  p50: 0.72,
  p90: 0.95,
  colors: ['#F2E6D0', '#7FB3E0'],
  saturation: 0.2,
};
const DARK: LookStats = {
  mean: 0.06,
  p10: 0.01,
  p50: 0.04,
  p90: 0.42,
  colors: ['#101830', '#E0408A'],
  saturation: 0.5,
};

/** The luminance of an rgba() colour laid over a background luminance. */
const over = (rgba: string, below: number) => {
  const m = /rgba\((\d+),(\d+),(\d+),([\d.]+)\)/.exec(rgba)!;
  const a = Number(m[4]);
  return luminance([Number(m[1]), Number(m[2]), Number(m[3])]) * a + below * (1 - a);
};

beforeEach(() => {
  jest.clearAllMocks();
  useLucid.setState({ source: 'wallpaper', media: null, seeThrough: false, look: null, busy: false });
});

describe('the Lucid look', () => {
  it('a bright picture gets dark text; a dark one white text', () => {
    expect(lucidLook(BRIGHT).scheme).toBe('light');
    expect(lucidLook(DARK).scheme).toBe('dark');
    // The wallpaper's hint tips a middling picture to dark text.
    const mid = { ...BRIGHT, p50: 0.3 };
    expect(lucidLook(mid).scheme).toBe('dark');
    expect(lucidLook(mid, true).scheme).toBe('light');
  });

  it('keeps every word readable on any picture (WCAG: cards 4.5:1, titles 3:1, accent 3:1)', () => {
    // Pictures from black to white, flat to very contrasty.
    for (let p10 = 0; p10 <= 0.9; p10 += 0.15)
      for (let spread = 0; spread <= 1 - p10; spread += 0.2) {
        const stats: LookStats = {
          mean: p10 + spread / 2,
          p10,
          p50: p10 + spread / 2,
          p90: Math.min(1, p10 + spread),
          colors: ['#3366CC', '#CC8844'],
          saturation: 0.4,
        };
        const look = lucidLook(stats);
        const dark = look.scheme === 'dark';
        const text = luminance(hexToRgb(look.colors.textPrimary!));
        const veil = luminance(hexToRgb(look.veilColor));
        const worst = dark ? stats.p90 : stats.p10;
        const veiled = worst * (1 - look.veil) + veil * look.veil;
        const card = over(look.colors.glassFill!, veiled);
        expect(contrast(text, card)).toBeGreaterThanOrEqual(4.4);
        expect(contrast(text, veiled)).toBeGreaterThanOrEqual(2.9);
        expect(contrast(luminance(hexToRgb(look.accent)), card)).toBeGreaterThanOrEqual(3);
      }
  });

  it('takes its accent from the picture’s most colourful colour', () => {
    const look = lucidLook({ ...DARK, colors: ['#101010', '#202020', '#E0408A'] });
    const [r, g, b] = hexToRgb(look.accent);
    expect(r).toBeGreaterThan(g);
    expect(r).toBeGreaterThan(b - 10);
  });

  it('guesses safely from the wallpaper’s colours alone', () => {
    const s = statsFromColors(['#203040', '#405060'], false);
    expect(s.p10).toBeLessThanOrEqual(s.p50);
    expect(s.p90).toBeGreaterThanOrEqual(s.p50);
    expect(statsFromColors([], true).p50).toBeGreaterThanOrEqual(0.45);
  });

  it('becomes the theme: its colours over the light or dark base', () => {
    const look = lucidLook(BRIGHT);
    const theme = createTheme('dark', look);
    expect(theme.mode).toBe('light');
    expect(theme.lucid).toBe(look);
    expect(theme.colors.glassFill).toBe(look.colors.glassFill);
    expect(theme.colors.premium).toBe(createTheme('light').colors.premium);
    expect(createTheme('dark').lucid).toBeNull();
  });
});

describe('the Lucid background source', () => {
  it('a wallpaper Android keeps private: the window turns see-through, the look from its colours', async () => {
    mockLucid.wallpaper.mockResolvedValue({
      path: null,
      stats: null,
      live: false,
      colors: ['#203050'],
      darkText: false,
    });
    await chooseWallpaper();
    expect(mockLucid.showWallpaper).toHaveBeenLastCalledWith(true);
    const s = useLucid.getState();
    expect(s.seeThrough).toBe(true);
    expect(s.media).toBeNull();
    expect(s.look?.scheme).toBe('dark');
  });

  it('a readable wallpaper is kept as a picture (no see-through window)', async () => {
    mockLucid.wallpaper.mockResolvedValue({
      path: '/files/lucid/wallpaper-1.jpg',
      stats: BRIGHT,
      live: false,
      colors: [],
      darkText: true,
    });
    await chooseWallpaper();
    expect(mockLucid.showWallpaper).toHaveBeenLastCalledWith(false);
    expect(useLucid.getState()).toMatchObject({
      media: '/files/lucid/wallpaper-1.jpg',
      seeThrough: false,
      source: 'wallpaper',
    });
  });

  it('a photo or clip: copied in, measured, saved; cancelled changes nothing', async () => {
    const saved: Record<string, unknown> = {};
    await hydrateLucid({
      getInternal: async <T,>(k: string) => (saved[k] as T) ?? null,
      setInternal: async (k: string, v: unknown) => {
        saved[k] = v;
      },
    });
    mockLucid.pickBackground.mockResolvedValueOnce(null);
    expect(await pickLucidBackground('photo')).toBe(false);
    expect(useLucid.getState().look).toBeNull();
    mockLucid.pickBackground.mockResolvedValueOnce({ path: '/files/lucid/background-2.mp4', stats: DARK });
    expect(await pickLucidBackground('video')).toBe(true);
    expect(useLucid.getState()).toMatchObject({ source: 'video', media: '/files/lucid/background-2.mp4' });
    expect(saved[LUCID_KEY]).toMatchObject({ source: 'video', media: '/files/lucid/background-2.mp4' });
    // Saved state comes back on the next start.
    useLucid.setState({ look: null, media: null, source: 'wallpaper' });
    await hydrateLucid({
      getInternal: async <T,>(k: string) => (saved[k] as T) ?? null,
      setInternal: async () => undefined,
    });
    expect(useLucid.getState().source).toBe('video');
  });

  it('a background file that has gone falls back to the wallpaper', async () => {
    useLucid.setState({ look: lucidLook(DARK), media: '/files/lucid/gone.jpg', source: 'photo' });
    mockLucid.wallpaper.mockResolvedValue({ path: null, stats: null, live: true, colors: [], darkText: false });
    await ensureLucid(async () => false);
    expect(useLucid.getState()).toMatchObject({ source: 'wallpaper', seeThrough: true });
    mockLucid.wallpaper.mockClear();
    await ensureLucid(async () => true);
    expect(mockLucid.wallpaper).not.toHaveBeenCalled();
  });
});

describe('Lucid on screen', () => {
  it('the background is the picture under its veil (or only the veil over the wallpaper)', async () => {
    const look = lucidLook(DARK);
    useLucid.setState({ look, media: '/files/lucid/background.jpg', source: 'photo' });
    await act(async () => {
      await render(
        <ThemeProvider preference="lucid" lucid={look}>
          <AuroraBackground />
        </ThemeProvider>,
      );
    });
    expect(screen.getByTestId('lucid-picture', { includeHiddenElements: true }).props.source).toEqual({
      uri: 'file:///files/lucid/background.jpg',
    });
    expect(screen.getByTestId('lucid-veil', { includeHiddenElements: true }).props.style[1].opacity).toBeCloseTo(
      look.veil,
    );
  });

  it('the panel: the wallpaper is free, your own photo or clip is Premium', async () => {
    mockLucid.wallpaper.mockResolvedValue({ path: null, stats: null, live: false, colors: [], darkText: false });
    useLucid.setState({ look: lucidLook(DARK), seeThrough: true });
    await act(async () => {
      await render(
        <ThemeProvider preference="dark">
          <LucidPanel />
        </ThemeProvider>,
      );
    });
    expect(screen.getByTestId('lucid-colours').props.children).toMatch(/light text/);
    await act(async () => fireEvent.press(screen.getByTestId('lucid-source-wallpaper')));
    expect(mockLucid.wallpaper).toHaveBeenCalled();
    // In tests every feature is allowed (enforcement is off), so the photo picker opens.
    mockLucid.pickBackground.mockResolvedValueOnce(null);
    await act(async () => fireEvent.press(screen.getByTestId('lucid-source-photo')));
    expect(mockLucid.pickBackground).toHaveBeenCalledWith('photo');
    expect(useAppStore.getState().entitlements).toBeDefined();
  });
});
