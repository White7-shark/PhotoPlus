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
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { FEATURES, FREE_DAILY_ALLOWANCE } from '../src/services/entitlements/features';
import { ARTISTS } from '../src/services/masters/catalog';
import { MastersService } from '../src/services/masters/MastersService';
import {
  LIVE_MS,
  MEDIA,
  PAPERS,
  SIGNATURE_DEFAULTS,
  clampPencil,
  isSignature,
  mediumName,
  paperName,
  signatureRequest,
} from '../src/services/masters/pencil';
import { ArtLiveScreen } from '../src/screens/studio/ArtLiveScreen';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';
import { SqlJsExecutor } from './helpers/sqljsExecutor';

let mockFull: 'ok' | 'slow' | 'fail' = 'ok';
let resolveFull: (() => void) | null = null;
const mockStudio = {
  renderMasters: jest.fn(async (_id: string, req: Record<string, unknown>) => {
    const out = (q: string) => ({
      outputPath: `/studio/${q}_${String(req.artist)}.jpg`,
      width: q === 'full' ? 2048 : 760,
      height: q === 'full' ? 2560 : 950,
      sizeBytes: 9,
      remaining: 1,
      inner: [0, 0, 1, 1],
      ...(req.live ? { blankPath: '/cache/blank.jpg' } : {}),
    });
    if (req.quality === 'full') {
      if (mockFull === 'fail') throw Object.assign(new Error('boom'), { code: 'INTERNAL' });
      if (mockFull === 'slow') await new Promise<void>(r => (resolveFull = r));
      return out('full');
    }
    return out(String(req.quality));
  }),
  pencilAllowance: jest.fn(async () => ({ remaining: 2, limit: 2, credits: 0, needsAd: true })),
  mastersAllowance: jest.fn(async () => ({ remaining: 1, limit: 1 })),
  cancel: jest.fn(),
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
async function freshRepos(): Promise<Repositories> {
  const db = await SqlJsExecutor.create();
  return openDatabase(async () => db);
}

beforeEach(() => {
  jest.clearAllMocks();
  mockFull = 'ok';
});

describe('Pencil Art', () => {
  it('has mediums, papers and options in the engine’s ranges, and a signature only when there is one', () => {
    expect(MEDIA.map(mediumName)).toEqual(['Graphite pencil', 'Charcoal', 'Red chalk']);
    expect(PAPERS.map(paperName)).toEqual(['White drawing paper', 'Cream', 'Grey toned', 'Tan toned']);
    expect(clampPencil({ medium: 7, paper: -1, intensity: 3, lines: 0.1, shading: NaN })).toEqual({
      medium: 2,
      paper: 0,
      intensity: 1,
      lines: 0.5,
      shading: 1,
    });
    expect(signatureRequest(SIGNATURE_DEFAULTS)).toBeUndefined();
    expect(signatureRequest({ ...SIGNATURE_DEFAULTS, enabled: true, text: '   ' })).toBeUndefined();
    expect(
      signatureRequest({ ...SIGNATURE_DEFAULTS, enabled: true, text: '  Ama   Owusu ', line2: 'Studio Ama' }),
    ).toEqual({
      text: 'Ama Owusu',
      line2: 'Studio Ama',
      style: 'script',
      position: 'right',
      size: 0.5,
      year: true,
    });
    expect(isSignature(DEFAULT_SETTINGS.artSignature)).toBe(true);
    expect(isSignature({ ...SIGNATURE_DEFAULTS, style: 'gothic' })).toBe(false);
    // Fast enough not to keep anyone waiting, long enough to enjoy.
    expect(LIVE_MS.pencil).toBeGreaterThanOrEqual(5000);
    expect(LIVE_MS.paint).toBeLessThanOrEqual(10000);
    expect(FEATURES.pencil.plan).toBe('premium');
    expect(FREE_DAILY_ALLOWANCE.pencil).toBe(2);
  });

  it('sends the drawing’s choices, and reads its picture area and blank sheet', async () => {
    const s = new MastersService(mockStudio as never);
    const p = await s.paint('/a.jpg', 3, 2, 'preview', 7, 'j1', undefined, 1, {
      mode: 'pencil',
      pencil: { medium: 2, paper: 3, intensity: 0.8, lines: 1.2, shading: 0.9 },
      signature: { text: 'Ama' },
      live: true,
    });
    expect(mockStudio.renderMasters.mock.calls[0][1]).toEqual({
      inputPath: '/a.jpg',
      artist: 3,
      level: 2,
      quality: 'preview',
      seed: 7,
      frame: 1,
      mode: 'pencil',
      pencil: { medium: 2, paper: 3, intensity: 0.8, lines: 1.2, shading: 0.9 },
      signature: { text: 'Ama' },
      live: true,
    });
    expect(p).toMatchObject({ blankPath: '/cache/blank.jpg', inner: [0, 0, 1, 1] });
    expect(await s.pencilAllowance()).toEqual({ remaining: 2, limit: 2, credits: 0, needsAd: true });
    mockStudio.pencilAllowance.mockRejectedValueOnce(new Error('x'));
    expect((await s.pencilAllowance()).needsAd).toBe(true);
  });

  it('is written in all eight other languages, every word, with each master’s drawing manner', () => {
    const keys = Object.keys(en).filter(
      k =>
        /^(pencil_|sig_|live_|sp_)/.test(k) ||
        [
          'feature_pencil',
          'feature_art_signature',
          'native_pencil_ad_required',
          'native_pencil_daily_used',
          'masters_mode_paint',
        ].includes(k),
    );
    expect(keys.length).toBeGreaterThanOrEqual(115);
    for (const a of ARTISTS) expect(keys).toContain(`pencil_way_${a.key}`);
    for (const q of ['What is Pencil Art?', 'How do projects work in the studios?']) {
      const k = Object.keys(en).find(x => (en as Record<string, string>)[x] === q)!;
      keys.push(k, k.replace(/_q$/, '_a'));
    }
    for (const table of [es, ptBR, fr, ar, hi, id, ru, zhCN] as Record<string, string>[]) {
      for (const k of keys) {
        expect(typeof table[k]).toBe('string');
        expect(table[k].length).toBeGreaterThan(0);
        for (const ph of (en as Record<string, string>)[k].match(/\{\w+\}/g) ?? []) expect(table[k]).toContain(ph);
      }
    }
    expect((fr as Record<string, string>).pencil_medium_red_chalk).toBe('Sanguine');
    expect((en as Record<string, string>).feature_pencil).toBe(FEATURES.pencil.title);
  });
});

describe('the live studio', () => {
  const job = {
    artist: 3,
    level: 1,
    seed: 7,
    mode: 'pencil',
    pencil: { medium: 0, paper: 0, intensity: 0.6, lines: 1, shading: 1 },
    frame: 0,
  };
  const nav = () => ({ navigate: jest.fn(), goBack: jest.fn(), replace: jest.fn() });

  it('draws the work live on its blank sheet while the HD work is made, then keeps it and opens its preview', async () => {
    const repos = await freshRepos();
    useAppStore.setState({ repos });
    const project = await repos.studioProjects.create('arts', 'Portraits');
    mockFull = 'slow';
    const n = nav();
    await wrap(
      <ArtLiveScreen
        navigation={n as never}
        route={
          {
            key: 'l',
            name: 'ArtLive',
            params: { job, photo: '/me.jpg', projectId: project.id, title: 'Albrecht Dürer · Pencil Art' },
          } as never
        }
      />,
    );
    await step(() => undefined);
    // Both at once: the quick picture to draw live (with its blank) and the HD work.
    const qualities = mockStudio.renderMasters.mock.calls.map(c => [c[1].quality, c[1].live === true]);
    expect(qualities).toEqual([
      ['preview', true],
      ['full', false],
    ]);
    const view = screen.getByTestId('art-reveal');
    expect(view.props).toMatchObject({
      work: '/studio/preview_3.jpg',
      blank: '/cache/blank.jpg',
      tool: 0,
      artist: 3,
      durationMs: 7000,
    });
    expect(screen.getByTestId('art-live-caption').props.children).toBe('Albrecht Dürer is drawing your portrait…');

    // Skip: straight to the finished drawing.
    await step(() => fireEvent.press(screen.getByTestId('art-live-skip')));
    expect(screen.getByTestId('art-reveal').props.finish).toBe(true);
    // Drawn, but the HD work isn't ready yet: finishing touches.
    await step(() => fireEvent(screen.getByTestId('art-reveal'), 'revealDone', { nativeEvent: { done: true } }));
    expect(screen.getByTestId('art-live-caption').props.children).toBe('Finishing touches…');
    expect(n.replace).not.toHaveBeenCalled();
    await step(() => resolveFull?.());
    const [work] = await repos.studioProjects.items(project.id, 'work');
    expect(work).toMatchObject({
      path: '/studio/full_3.jpg',
      sourcePath: '/me.jpg',
      title: 'Albrecht Dürer · Pencil Art',
      width: 2048,
      height: 2560,
      settings: { ...job, inner: [0, 0, 1, 1] },
    });
    expect(n.replace).toHaveBeenCalledWith('StudioItem', { itemId: work.id, fresh: true });
  });

  it('paints with a brush for a painting, and says so when the work can’t be made', async () => {
    const repos = await freshRepos();
    useAppStore.setState({ repos });
    const project = await repos.studioProjects.create('arts', 'P');
    mockFull = 'fail';
    const n = nav();
    await wrap(
      <ArtLiveScreen
        navigation={n as never}
        route={
          {
            key: 'l',
            name: 'ArtLive',
            params: {
              job: { ...job, mode: 'paint', pencil: undefined },
              photo: '/me.jpg',
              projectId: project.id,
              title: 'x',
            },
          } as never
        }
      />,
    );
    await step(() => undefined);
    expect(screen.getByTestId('art-reveal').props).toMatchObject({ tool: 1, durationMs: 8000 });
    expect(screen.getByText('The painting couldn’t be finished')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('art-live-back')));
    expect(n.goBack).toHaveBeenCalled();
    expect(await repos.studioProjects.items(project.id)).toEqual([]);
  });
});
