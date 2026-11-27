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
import { openDatabase, type Repositories } from '../src/services/database/Database';
import { FEATURES, FREE_DAILY_ALLOWANCE } from '../src/services/entitlements/features';
import { STUDIOS } from '../src/services/projects/studios';
import {
  BACKGROUNDS,
  DEFAULT_TOON,
  STYLES,
  backgroundName,
  clampToon,
  styleAbout,
  styleName,
  withStyle,
} from '../src/services/toon/catalog';
import { ToonService } from '../src/services/toon/ToonService';
import { ArtLiveScreen } from '../src/screens/studio/ArtLiveScreen';
import { STUDIO_TOOLS } from '../src/screens/studio/PhotoshootStudioScreen';
import { ToonScreen } from '../src/screens/studio/ToonScreen';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';
import { SqlJsExecutor } from './helpers/sqljsExecutor';

const mockToon = { premium: false, remaining: 2, credits: 0, adFinishes: true, fullFails: '' };
const toonState = () =>
  mockToon.premium
    ? { remaining: -1, limit: 2, credits: 0, needsAd: false }
    : { remaining: mockToon.remaining, limit: 2, credits: mockToon.credits, needsAd: mockToon.credits === 0 };
const mockStudio = {
  renderToon: jest.fn(async (_id: string, req: Record<string, unknown>) => {
    if (req.quality === 'full') {
      if (mockToon.fullFails) throw Object.assign(new Error('no'), { code: mockToon.fullFails });
      if (!mockToon.premium) {
        mockToon.credits -= 1;
        mockToon.remaining -= 1;
      }
    }
    return {
      outputPath: `/studio/toon_${String(req.quality)}_${String(req.style)}.jpg`,
      width: req.quality === 'full' ? 1600 : 720,
      height: req.quality === 'full' ? 2048 : 920,
      remaining: mockToon.remaining,
    };
  }),
  toonAllowance: jest.fn(async () => toonState()),
  renderMasters: jest.fn(async (_id: string, req: Record<string, unknown>) => ({
    outputPath: `/studio/${String(req.quality)}.jpg`,
    width: 760,
    height: 950,
    sizeBytes: 9,
    remaining: 1,
    inner: [0, 0, 1, 1],
    ...(req.live ? { blankPath: '/cache/blank.jpg' } : {}),
  })),
  cancel: jest.fn(),
};
const mockAds = {
  initialize: jest.fn(async () => ({ canRequestAds: true })),
  showRewardedAdForTool: jest.fn(async (_tool: string) => {
    if (!mockToon.adFinishes) return { rewarded: false, allowance: toonState() };
    mockToon.credits += 1;
    return { rewarded: true, allowance: toonState() };
  }),
};
const mockStorage = { pickImage: jest.fn(async () => '/files/imports/me.jpg') };
jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get studio() {
        return mockStudio;
      },
      get storage() {
        return mockStorage;
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
    for (let i = 0; i < 15; i++) await tick();
  });
const nav = () => ({ navigate: jest.fn(), goBack: jest.fn(), setParams: jest.fn(), replace: jest.fn() });
const lastPreview = () => mockStudio.renderToon.mock.calls.filter(c => c[1].quality === 'preview').at(-1)![1];
const slide = (testID: string, times: number, action: 'increment' | 'decrement' = 'increment') => {
  for (let i = 0; i < times; i++)
    fireEvent(screen.getByTestId(testID), 'accessibilityAction', { nativeEvent: { actionName: action } });
};
async function freshRepos(): Promise<Repositories> {
  const db = await SqlJsExecutor.create();
  return openDatabase(async () => db);
}

beforeEach(() => {
  jest.clearAllMocks();
  Object.assign(mockToon, { premium: false, remaining: 2, credits: 0, adFinishes: true, fullFails: '' });
});

describe('the Toon catalogue', () => {
  it('has six styles and six backgrounds in the engine’s order, each named and explained', () => {
    expect(STYLES.map(s => s.id)).toEqual([0, 1, 2, 3, 4, 5]);
    expect(STYLES.map(styleName)).toEqual(['Anime', 'Manga', 'Comic', 'Cartoon', 'Webtoon', 'Retro 90s']);
    for (const s of STYLES) expect(styleAbout(s).length).toBeGreaterThan(20);
    expect(BACKGROUNDS.map(backgroundName)).toEqual([
      'Your scene',
      'Soft focus',
      'Anime sky',
      'Sunset',
      'Starry night',
      'Pastel',
    ]);
    // Manga is ink on paper: no colour boost, no glow.
    expect(STYLES[1].start).toMatchObject({ vivid: 0, glow: 0 });
    expect(FEATURES.toon.plan).toBe('premium');
    expect(FREE_DAILY_ALLOWANCE.toon).toBe(2);
    expect(STUDIOS.toon).toMatchObject({ kind: 'toon', route: 'Toon' });
    expect(STUDIO_TOOLS.find(t => t.id === 'toon')).toMatchObject({
      route: 'Toon',
      studio: 'toon',
      premium: true,
      available: true,
    });
  });

  it('keeps every control within the engine’s ranges, and a style brings its own look but keeps the background', () => {
    expect(clampToon({ style: 9, background: -3, lines: 4, shades: 11, vivid: -7, detail: NaN, eyes: 2 })).toEqual({
      ...DEFAULT_TOON,
      style: 5,
      background: 0,
      lines: 1,
      shades: 6,
      vivid: -1,
      eyes: 1,
    });
    expect(clampToon({ shades: 3.6 }).shades).toBe(4);
    const s = withStyle({ ...DEFAULT_TOON, background: 2, lines: 0.1, strength: 0.6 }, 3);
    expect(s).toEqual({ style: 3, background: 2, strength: 0.6, ...STYLES[3].start });
    expect(clampToon({ strength: 1.4 }).strength).toBe(1);
    expect(clampToon({ strength: -1 }).strength).toBe(0);
    expect(DEFAULT_TOON.strength).toBe(1);
  });
});

describe('the Toon service', () => {
  it('sends the settings, checks what comes back, and reads the allowance', async () => {
    const s = new ToonService(mockStudio as never, mockAds as never, async () => true);
    const p = await s.render('/a.jpg', { ...DEFAULT_TOON, style: 2, lines: 3 }, 'preview', 9, 'job1');
    expect(mockStudio.renderToon).toHaveBeenCalledWith('job1', {
      inputPath: '/a.jpg',
      quality: 'preview',
      seed: 9,
      ...DEFAULT_TOON,
      style: 2,
      lines: 1,
    });
    expect(p).toEqual({ outputPath: '/studio/toon_preview_2.jpg', width: 720, height: 920, remaining: 2 });
    mockStudio.renderToon.mockResolvedValueOnce({ outputPath: '', width: 1, height: 1 } as never);
    await expect(s.render('/a.jpg', DEFAULT_TOON, 'thumb')).rejects.toMatchObject({ code: 'INTERNAL' });
    mockStudio.renderToon.mockRejectedValueOnce(Object.assign(new Error('ad'), { code: 'AD_REQUIRED' }));
    await expect(s.render('/a.jpg', DEFAULT_TOON, 'full')).rejects.toMatchObject({ code: 'AD_REQUIRED' });

    expect(await s.allowance()).toEqual({ remaining: 2, limit: 2, credits: 0, needsAd: true });
    mockStudio.toonAllowance.mockRejectedValueOnce(new Error('x'));
    expect(await s.allowance()).toEqual({ remaining: 2, limit: 2, credits: 0, needsAd: true });
    mockToon.premium = true;
    expect((await s.allowance()).needsAd).toBe(false);
    mockToon.premium = false;
    expect(await s.unlock()).toMatchObject({ rewarded: true, allowance: { credits: 1, needsAd: false } });
    expect(mockAds.showRewardedAdForTool).toHaveBeenCalledWith('toon');
    await expect(new ToonService(mockStudio as never, null).unlock()).rejects.toMatchObject({
      code: 'ADS_UNAVAILABLE',
    });
  });
});

describe('the Toon Studio screen', () => {
  it('starts from a project: without one it goes to choose one first', async () => {
    const n = nav();
    await wrap(
      <ToonScreen navigation={n as never} route={{ key: 't', name: 'Toon', params: { photo: '/x.jpg' } } as never} />,
    );
    expect(n.replace).toHaveBeenCalledWith('StudioProjects', { studio: 'toon', photo: '/x.jpg' });
  });

  it('previews each change, fills the style cards, and makes a free picture after an ad, kept in the project', async () => {
    const repos = await freshRepos();
    useAppStore.setState({ repos });
    const project = await repos.studioProjects.create('toon', 'Me as anime');
    const n = nav();
    await wrap(
      <ToonScreen
        navigation={n as never}
        route={{ key: 't', name: 'Toon', params: { projectId: project.id } } as never}
      />,
    );
    expect(screen.getByText('You, as anime or a cartoon')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('toon-pick')));
    // The preview: anime, the person's own scene.
    expect(lastPreview()).toMatchObject({ inputPath: '/files/imports/me.jpg', ...DEFAULT_TOON });
    expect(screen.getByTestId('toon-canvas')).toBeTruthy();
    // A card for every style, each the photo in that style.
    const thumbs = mockStudio.renderToon.mock.calls.filter(c => c[1].quality === 'thumb').map(c => c[1].style);
    expect(thumbs).toEqual([0, 1, 2, 3, 4, 5]);
    expect(screen.getByText('2 of 2 free pictures left today')).toBeTruthy();

    // A style, a background, and each control.
    await step(() => fireEvent.press(screen.getByTestId('toon-style-manga')));
    expect(lastPreview()).toMatchObject({ style: 1, ...STYLES[1].start });
    expect(screen.getByTestId('toon-style-about').props.children).toBe('Black ink and screentone on white paper');
    await step(() => fireEvent.press(screen.getByTestId('toon-bg-sky')));
    expect(lastPreview()).toMatchObject({ style: 1, background: 2 });
    await step(() => fireEvent.press(screen.getByTestId('toon-shades-5')));
    expect(lastPreview().shades).toBe(5);
    for (let i = 0; i < 2; i++) await step(() => slide('toon-lines', 1));
    for (let i = 0; i < 3; i++) await step(() => slide('toon-eyes', 1));
    for (let i = 0; i < 2; i++) await step(() => slide('toon-glow', 1));
    for (let i = 0; i < 1; i++) await step(() => slide('toon-skin', 1, 'decrement'));
    for (let i = 0; i < 1; i++) await step(() => slide('toon-detail', 1));
    for (let i = 0; i < 2; i++) await step(() => slide('toon-vivid', 1));
    const last = lastPreview();
    expect(last.lines as number).toBeGreaterThan(STYLES[1].start.lines);
    expect(last.eyes as number).toBeGreaterThan(STYLES[1].start.eyes);
    expect(last.glow as number).toBeGreaterThan(0);
    expect(last.skin as number).toBeLessThan(STYLES[1].start.skin);
    expect(last.vivid as number).toBeGreaterThan(0);
    // Back to the style's own look (the background stays).
    await step(() => fireEvent.press(screen.getByTestId('toon-reset')));
    expect(lastPreview()).toMatchObject({ style: 1, background: 2, ...STYLES[1].start });
    // Strength: the drawing blended with the photo.
    for (let i = 0; i < 3; i++) await step(() => slide('toon-strength', 1, 'decrement'));
    expect(lastPreview().strength as number).toBeCloseTo(0.85, 5);
    // Settings seen before come back from the cache: no new render.
    const renders = mockStudio.renderToon.mock.calls.filter(c => c[1].quality === 'preview').length;
    await step(() => fireEvent.press(screen.getByTestId('toon-style-anime')));
    await step(() => fireEvent.press(screen.getByTestId('toon-style-manga')));
    expect(mockStudio.renderToon.mock.calls.filter(c => c[1].quality === 'preview').length).toBe(renders + 1);
    for (let i = 0; i < 3; i++) await step(() => slide('toon-strength', 1));
    expect(screen.getByTestId('toon-canvas')).toBeTruthy();

    // A free picture: an ad first; one not watched to the end unlocks nothing.
    mockToon.adFinishes = false;
    await step(() => fireEvent.press(screen.getByTestId('toon-create')));
    expect(screen.getByTestId('toon-ad-sheet')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('toon-ad-sheet-watch')));
    expect(mockAds.showRewardedAdForTool).toHaveBeenCalledWith('toon');
    expect(screen.getByText(/The ad didn’t finish/)).toBeTruthy();
    expect(mockStudio.renderToon.mock.calls.some(c => c[1].quality === 'full')).toBe(false);
    mockToon.adFinishes = true;
    await step(() => fireEvent.press(screen.getByTestId('toon-ad-sheet-watch')));
    const full = mockStudio.renderToon.mock.calls.find(c => c[1].quality === 'full')![1];
    expect(full).toMatchObject({ style: 1, background: 2, quality: 'full' });
    const [work] = await repos.studioProjects.items(project.id, 'work');
    expect(work).toMatchObject({
      path: '/studio/toon_full_1.jpg',
      sourcePath: '/files/imports/me.jpg',
      title: 'Manga · Anime sky',
      width: 1600,
      height: 2048,
      settings: { style: 1, background: 2, strength: 1, ...STYLES[1].start },
    });
    expect(n.navigate).toHaveBeenCalledWith('StudioItem', { itemId: work.id, fresh: true });
    expect(screen.getByText('1 of 2 free pictures left today')).toBeTruthy();
  });

  it('offers Premium once today’s two are made, with no ad', async () => {
    mockToon.remaining = 0;
    await wrap(
      <ToonScreen
        navigation={nav() as never}
        route={{ key: 't', name: 'Toon', params: { photo: '/p.jpg', projectId: 'sp1' } } as never}
      />,
    );
    await step(() => undefined);
    expect(screen.getByText(/You’ve made today’s 2 free pictures/)).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('toon-create')));
    expect(screen.queryByTestId('toon-ad-sheet')).toBeNull();
    expect(screen.getByText(FEATURES.toon.title)).toBeTruthy();
    expect(mockAds.showRewardedAdForTool).not.toHaveBeenCalled();
  });

  it('makes unlimited pictures on Premium with no ads, and edits a picture again with its own settings', async () => {
    mockToon.premium = true;
    const repos = await freshRepos();
    useAppStore.setState({ repos });
    const project = await repos.studioProjects.create('toon', 'P');
    const n = nav();
    const edit = { ...DEFAULT_TOON, style: 4, background: 4, shades: 6, glow: 0.9 };
    await wrap(
      <ToonScreen
        navigation={n as never}
        route={{ key: 't', name: 'Toon', params: { photo: '/p.jpg', projectId: project.id, edit } } as never}
      />,
    );
    await step(() => undefined);
    expect(lastPreview()).toMatchObject({ inputPath: '/p.jpg', ...edit });
    expect(n.setParams).toHaveBeenCalledWith({ photo: undefined, edit: undefined });
    expect(screen.getByText('Premium: unlimited pictures, no ads')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('toon-create')));
    await step(() => fireEvent.press(screen.getByTestId('toon-create')));
    expect(mockAds.showRewardedAdForTool).not.toHaveBeenCalled();
    expect(await repos.studioProjects.items(project.id, 'work')).toHaveLength(2);
    expect((await repos.studioProjects.items(project.id, 'work'))[0].title).toBe('Webtoon · Starry night');
  });

  it('asks for an ad if the device says one is needed, and says so when a picture can’t be made', async () => {
    mockToon.premium = true;
    mockToon.fullFails = 'AD_REQUIRED';
    await wrap(
      <ToonScreen
        navigation={nav() as never}
        route={{ key: 't', name: 'Toon', params: { photo: '/p.jpg', projectId: 'sp1' } } as never}
      />,
    );
    await step(() => undefined);
    await step(() => fireEvent.press(screen.getByTestId('toon-create')));
    expect(screen.getByTestId('toon-ad-sheet')).toBeTruthy();
    mockToon.fullFails = 'INTERNAL';
    await step(() => fireEvent.press(screen.getByText('Not now')));
    await step(() => fireEvent.press(screen.getByTestId('toon-create')));
    expect(screen.getByText('The picture couldn’t be made')).toBeTruthy();
  });

  it('is written in all eight other languages, every word', () => {
    const keys = Object.keys(en).filter(k => /^(toon_|native_toon_|live_speed_)/.test(k) || k === 'feature_toon');
    expect(keys.length).toBeGreaterThanOrEqual(60);
    for (const s of STYLES)
      expect(keys).toEqual(expect.arrayContaining([`toon_style_${s.key}`, `toon_style_${s.key}_about`]));
    for (const b of BACKGROUNDS) expect(keys).toContain(`toon_bg_${b}`);
    const q = Object.keys(en).find(x => (en as Record<string, string>)[x] === 'What is Toon Studio?')!;
    keys.push(q, q.replace(/_q$/, '_a'));
    for (const table of [es, ptBR, fr, ar, hi, id, ru, zhCN] as Record<string, string>[]) {
      for (const k of keys) {
        expect(typeof table[k]).toBe('string');
        expect(table[k].length).toBeGreaterThan(0);
        for (const ph of (en as Record<string, string>)[k].match(/\{\w+\}/g) ?? []) expect(table[k]).toContain(ph);
      }
    }
    expect((en as Record<string, string>).feature_toon).toBe(FEATURES.toon.title);
  });
});

describe('the live studio’s speed', () => {
  const job = {
    artist: 3,
    level: 1,
    seed: 7,
    mode: 'pencil',
    pencil: { medium: 0, paper: 0, intensity: 0.6, lines: 1, shading: 1 },
    frame: 0,
  };

  it('draws faster or slower as the person chooses, and remembers it', async () => {
    const repos = await freshRepos();
    const updateSetting = jest.fn(async () => undefined);
    useAppStore.setState({ repos, updateSetting } as never);
    useAppStore.setState(s => ({ settings: { ...s.settings, artLiveSpeed: 1 } }));
    const project = await repos.studioProjects.create('arts', 'P');
    await wrap(
      <ArtLiveScreen
        navigation={nav() as never}
        route={
          {
            key: 'l',
            name: 'ArtLive',
            params: { job, photo: '/me.jpg', projectId: project.id, title: 'x' },
          } as never
        }
      />,
    );
    await step(() => undefined);
    expect(screen.getByTestId('art-reveal').props.durationMs).toBe(7000);
    expect(screen.getByTestId('art-live-speed').props.accessibilityValue).toEqual({ text: 'Normal' });
    // Twice as fast: half the time.
    for (let i = 0; i < 10; i++) for (let i = 0; i < 1; i++) await step(() => slide('art-live-speed', 1));
    expect(screen.getByTestId('art-reveal').props.durationMs).toBe(3500);
    expect(screen.getByTestId('art-live-speed').props.accessibilityValue).toEqual({ text: 'Fast · 2×' });
    expect(updateSetting).toHaveBeenLastCalledWith('artLiveSpeed', 2);
    // Never slower than half speed.
    for (let i = 0; i < 20; i++) for (let i = 0; i < 1; i++) await step(() => slide('art-live-speed', 1, 'decrement'));
    expect(screen.getByTestId('art-reveal').props.durationMs).toBe(14000);
    expect(screen.getByTestId('art-live-speed').props.accessibilityValue).toEqual({ text: 'Slow · 0.5×' });
    expect(updateSetting).toHaveBeenLastCalledWith('artLiveSpeed', 0.5);
  });
});
