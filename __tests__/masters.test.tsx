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
import { MastersScreen } from '../src/screens/studio/MastersScreen';
import { PhotoshootStudioScreen, STUDIO_TOOLS } from '../src/screens/studio/PhotoshootStudioScreen';
import {
  ARTISTS,
  MOVEMENTS,
  artistAbout,
  artistName,
  artistTechnique,
  artistsOf,
} from '../src/services/masters/catalog';
import { MastersError, MastersService, isPainting } from '../src/services/masters/MastersService';
import { FEATURES, FREE_DAILY_ALLOWANCE, type FeatureKey } from '../src/services/entitlements/features';
import { featureTitleKey } from '../src/components/PremiumSheet';
import { ThemeProvider } from '../src/theme';

let mockLeft = 1;
// Pencil Art's day on the free plan (two, each after an ad), as the device counts it.
const mockPencil = { premium: false, remaining: 2, credits: 0, adFinishes: true };
const pencilState = () =>
  mockPencil.premium
    ? { remaining: -1, limit: 2, credits: 0, needsAd: false }
    : { remaining: mockPencil.remaining, limit: 2, credits: mockPencil.credits, needsAd: mockPencil.credits === 0 };
const mockStudio = {
  renderMasters: jest.fn(async (_id: string, req: { quality: string; artist: number; mode?: string }) => {
    if (req.quality === 'full') {
      if (mockLeft === 0) throw Object.assign(new Error('used'), { code: 'PREMIUM_REQUIRED' });
      mockLeft -= 1;
    }
    return {
      outputPath: `/cache/${req.mode === 'pencil' ? 'pencil' : 'masters'}_${req.quality}_${req.artist}.jpg`,
      width: 760,
      height: 950,
      sizeBytes: 1000,
      format: 'jpeg',
      remaining: mockLeft,
    };
  }),
  mastersAllowance: jest.fn(async () => ({ remaining: mockLeft, limit: 1 })),
  pencilAllowance: jest.fn(async () => pencilState()),
  saveToGallery: jest.fn(async () => 'content://gallery/1'),
  cancel: jest.fn(),
};
const mockAds = {
  initialize: jest.fn(async () => ({ canRequestAds: true })),
  showRewardedAdForTool: jest.fn(async (_tool: string) => {
    if (!mockPencil.adFinishes) return { rewarded: false, allowance: pencilState() };
    mockPencil.credits += 1;
    return { rewarded: true, allowance: pencilState() };
  }),
};
const mockStorage = { pickImage: jest.fn(async () => '/files/imports/me.jpg'), shareImages: jest.fn(async () => true) };
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
    for (let i = 0; i < 12; i++) await tick();
  });
const navigation = { navigate: jest.fn(), goBack: jest.fn(), setParams: jest.fn(), replace: jest.fn() };
const lastPreview = () =>
  mockStudio.renderMasters.mock.calls.filter(c => c[1].quality === 'preview').at(-1)![1] as Record<string, unknown>;

beforeEach(() => {
  jest.clearAllMocks();
  mockLeft = 1;
  Object.assign(mockPencil, { premium: false, remaining: 2, credits: 0, adFinishes: true });
});

describe('the twenty masters', () => {
  it('has twenty artists in the engine order, each in a movement, with name, technique and story', () => {
    expect(ARTISTS.map(a => a.id)).toEqual([...Array(20).keys()]);
    expect(new Set(ARTISTS.map(a => a.key)).size).toBe(20);
    expect(MOVEMENTS.flatMap(m => artistsOf(m)).length).toBe(20);
    for (const m of MOVEMENTS) expect(artistsOf(m).length).toBeGreaterThanOrEqual(1);
    expect(MOVEMENTS).toHaveLength(7);
    expect(artistsOf('drama').map(a => a.key)).toEqual(['caravaggio']);
    for (const a of ARTISTS) {
      expect(a.years).toMatch(/^\d{4}–\d{4}$/);
      expect(artistName(a)).not.toMatch(/^masters_/);
      expect(artistTechnique(a)).not.toMatch(/^masters_/);
      expect(artistAbout(a).length).toBeGreaterThan(40);
    }
    expect(artistName(ARTISTS[0])).toBe('Leonardo da Vinci');
    expect(FEATURES.masters.plan).toBe('premium');
    expect(FREE_DAILY_ALLOWANCE.masters).toBe(1);
    // Every feature's title is a string resource (so Google Play translates it).
    for (const k of Object.keys(FEATURES) as FeatureKey[])
      expect((en as Record<string, string>)[featureTitleKey(k)]).toBe(FEATURES[k].title);
    expect(featureTitleKey('exportHd')).toBe('feature_export_hd');
  });

  it('is written in all eight other languages, every word', () => {
    const keys = Object.keys(en).filter(
      k => k.startsWith('masters_') || k.startsWith('native_masters_') || k === 'feature_masters',
    );
    expect(keys.length).toBeGreaterThanOrEqual(99);
    for (const table of [es, ptBR, fr, ar, hi, id, ru, zhCN] as Record<string, string>[]) {
      for (const k of keys) {
        expect(typeof table[k]).toBe('string');
        expect(table[k].length).toBeGreaterThan(0);
        // Placeholders survive translation.
        for (const p of (en as Record<string, string>)[k].match(/\{\w+\}/g) ?? []) expect(table[k]).toContain(p);
      }
    }
    expect((fr as Record<string, string>).masters_artist_leonardo).toBe('Léonard de Vinci');
    expect((zhCN as Record<string, string>).masters_level_masterpiece).toBe('杰作');
  });
});

describe('the Masters service', () => {
  it('checks the result and maps errors', async () => {
    const native = { ...mockStudio } as never;
    const s = new MastersService(native);
    const p = await s.paint('/a.jpg', 6, 2, 'preview');
    expect(p.outputPath).toBe('/cache/masters_preview_6.jpg');
    expect(mockStudio.renderMasters.mock.calls[0][1]).toEqual({
      inputPath: '/a.jpg',
      artist: 6,
      level: 2,
      quality: 'preview',
      seed: 7,
    });
    await expect(s.paint('/a.jpg', 20, 1, 'preview')).rejects.toBeInstanceOf(MastersError);
    mockLeft = 0;
    await expect(s.paint('/a.jpg', 1, 1, 'full')).rejects.toMatchObject({ code: 'PREMIUM_REQUIRED' });
    expect(isPainting({ outputPath: '', width: 1, height: 1 })).toBe(false);
    expect(isPainting({ outputPath: '/x', width: 0, height: 1 })).toBe(false);
    expect(await s.allowance()).toEqual({ remaining: 0, limit: 1 });
  });
});

describe('the Masters screen', () => {
  it('is a Premium tile in the studio that opens its projects', async () => {
    const tile = STUDIO_TOOLS.find(t => t.id === 'masters');
    expect(tile).toMatchObject({ route: 'Masters', studio: 'arts', premium: true, available: true });
    await wrap(
      <PhotoshootStudioScreen
        navigation={navigation as never}
        route={{ key: 'p', name: 'PhotoshootStudio' } as never}
      />,
    );
    await fireEvent.press(screen.getByTestId('studio-masters'));
    expect(navigation.navigate).toHaveBeenCalledWith('StudioProjects', { studio: 'arts' });
  });

  it('starts from a project: without one it goes to choose one first', async () => {
    await wrap(
      <MastersScreen
        navigation={navigation as never}
        route={{ key: 'm', name: 'Masters', params: { photo: '/x.jpg' } } as never}
      />,
    );
    expect(navigation.replace).toHaveBeenCalledWith('StudioProjects', { studio: 'arts', photo: '/x.jpg' });
  });

  it('paints a preview, fills the cards, and makes the finished painting live in the studio, free once a day', async () => {
    await wrap(
      <MastersScreen
        navigation={navigation as never}
        route={{ key: 'm', name: 'Masters', params: { projectId: 'sp1' } } as never}
      />,
    );
    expect(screen.getByText('Your photo, painted by the great masters')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('masters-pick')));
    // The preview: Leonardo, a finished Painting.
    expect(lastPreview()).toMatchObject({ artist: 0, level: 1, inputPath: '/files/imports/me.jpg' });
    expect(lastPreview()).not.toHaveProperty('mode');
    expect(screen.getByTestId('masters-painting').props.source.uri).toBe('file:///cache/masters_preview_0.jpg');
    // The cards of the movement on show, each the photo in that artist's hand.
    const thumbs = mockStudio.renderMasters.mock.calls.filter(c => c[1].quality === 'thumb').map(c => c[1].artist);
    expect(thumbs).toEqual(artistsOf('renaissance').map(a => a.id));
    expect(screen.getByText('1 free painting today')).toBeTruthy();

    // Another movement and artist, a Masterpiece.
    await step(() => fireEvent.press(screen.getByTestId('masters-movement-classical')));
    await step(() => fireEvent.press(screen.getByTestId('masters-artist-rembrandt')));
    await step(() => fireEvent.press(screen.getByTestId('masters-level-2')));
    expect(screen.getByText('Chiaroscuro and impasto')).toBeTruthy();
    expect(lastPreview()).toMatchObject({ artist: 6, level: 2 });

    // Drag to compare with the photo (here a screen reader's steps): the photo shows from the left.
    expect(screen.queryByTestId('masters-before')).toBeNull();
    await step(() =>
      fireEvent(screen.getByTestId('masters-canvas-area'), 'layout', { nativeEvent: { layout: { width: 400 } } }),
    );
    await step(() =>
      fireEvent(screen.getByTestId('masters-canvas'), 'accessibilityAction', {
        nativeEvent: { actionName: 'increment' },
      }),
    );
    expect(screen.getByTestId('masters-before').props.source.uri).toBe('file:///files/imports/me.jpg');

    // The finished painting: made live in the studio, for this project.
    await step(() => fireEvent.press(screen.getByTestId('masters-paint')));
    expect(navigation.navigate).toHaveBeenLastCalledWith('ArtLive', {
      job: expect.objectContaining({ artist: 6, level: 2, mode: 'paint', seed: 7 }),
      photo: '/files/imports/me.jpg',
      projectId: 'sp1',
      title: 'Rembrandt · Painting',
    });
    // No signature on the free plan.
    expect((navigation.navigate.mock.calls.at(-1)![1] as { job: object }).job).not.toHaveProperty('signature');
  });

  it('offers Premium when the day’s free painting is used', async () => {
    mockLeft = 0;
    await wrap(
      <MastersScreen
        navigation={navigation as never}
        route={{ key: 'm', name: 'Masters', params: { photo: '/p.jpg', projectId: 'sp1' } } as never}
      />,
    );
    await step(() => undefined);
    expect(screen.getAllByText(/today’s free painting is used/i).length).toBeGreaterThan(0);
    await step(() => fireEvent.press(screen.getByTestId('masters-paint')));
    expect(navigation.navigate).not.toHaveBeenCalledWith('ArtLive', expect.anything());
    expect(screen.getByTestId('premium-sheet-plans')).toBeTruthy();
    expect(screen.getByText(FEATURES.masters.title)).toBeTruthy();
  });

  it('draws in pencil: the master’s drawing manner, medium and paper, two free a day each after an ad', async () => {
    await wrap(
      <MastersScreen
        navigation={navigation as never}
        route={{ key: 'm', name: 'Masters', params: { photo: '/p.jpg', projectId: 'sp1' } } as never}
      />,
    );
    await step(() => undefined);
    await step(() => fireEvent.press(screen.getByTestId('masters-mode-pencil')));
    expect(lastPreview()).toMatchObject({
      mode: 'pencil',
      pencil: { medium: 0, paper: 0, intensity: 0.6, lines: 1, shading: 1 },
    });
    // The cards redraw in pencil.
    expect(mockStudio.renderMasters.mock.calls.some(c => c[1].quality === 'thumb' && c[1].mode === 'pencil')).toBe(
      true,
    );
    expect(screen.getByTestId('masters-technique').props.children).toBe(
      'Left-handed parallel hatching under a smoky sfumato',
    );
    expect(screen.getByText('2 of 2 free drawings left today')).toBeTruthy();

    await step(() => fireEvent.press(screen.getByTestId('pencil-medium-red_chalk')));
    await step(() => fireEvent.press(screen.getByTestId('pencil-paper-tan')));
    expect(lastPreview()).toMatchObject({ mode: 'pencil', pencil: { medium: 2, paper: 3 } });
    // Natural keeps closer to the photo (the drawing's intensity).
    await step(() => fireEvent.press(screen.getByTestId('masters-intensity-natural')));
    expect((lastPreview().pencil as { intensity: number }).intensity).toBeLessThan(0.4);

    // A free drawing: an ad first; one not watched to the end unlocks nothing.
    mockPencil.adFinishes = false;
    await step(() => fireEvent.press(screen.getByTestId('masters-paint')));
    expect(screen.getByTestId('pencil-ad-sheet')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('pencil-ad-sheet-watch')));
    expect(mockAds.showRewardedAdForTool).toHaveBeenCalledWith('pencil');
    expect(screen.getByText(/The ad didn’t finish/)).toBeTruthy();
    expect(navigation.navigate).not.toHaveBeenCalledWith('ArtLive', expect.anything());
    mockPencil.adFinishes = true;
    await step(() => fireEvent.press(screen.getByTestId('pencil-ad-sheet-watch')));
    expect(navigation.navigate).toHaveBeenLastCalledWith(
      'ArtLive',
      expect.objectContaining({
        job: expect.objectContaining({ mode: 'pencil', pencil: expect.objectContaining({ medium: 2, paper: 3 }) }),
        title: 'Leonardo da Vinci · Pencil Art',
      }),
    );

    // Today's two are used: Premium is offered, with no ad.
    mockPencil.remaining = 0;
    mockPencil.credits = 0;
    await step(() => navigation.navigate.mockClear());
    await wrap(
      <MastersScreen
        navigation={navigation as never}
        route={{ key: 'm2', name: 'Masters', params: { photo: '/p.jpg', projectId: 'sp1' } } as never}
      />,
    );
    await step(() => undefined);
    await step(() => fireEvent.press(screen.getByTestId('masters-mode-pencil')));
    await step(() => fireEvent.press(screen.getByTestId('masters-paint')));
    expect(screen.queryByTestId('pencil-ad-sheet')).toBeNull();
    expect(screen.getByText(FEATURES.pencil.title)).toBeTruthy();
  });

  it('signs the work on Premium (and offers Premium for it on the free plan)', async () => {
    await wrap(
      <MastersScreen
        navigation={navigation as never}
        route={{ key: 'm', name: 'Masters', params: { photo: '/p.jpg', projectId: 'sp1' } } as never}
      />,
    );
    await step(() => undefined);
    await step(() => fireEvent.press(screen.getByTestId('signature-toggle')));
    expect(screen.getByText(FEATURES.artSignature.title)).toBeTruthy();
  });

  it('paints without limits on Premium, signed with the artist’s name', async () => {
    mockLeft = -1;
    mockPencil.premium = true;
    mockStudio.mastersAllowance.mockResolvedValue({ remaining: -1, limit: 1 });
    await wrap(
      <MastersScreen
        navigation={navigation as never}
        route={{ key: 'm', name: 'Masters', params: { photo: '/p.jpg', projectId: 'sp1' } } as never}
      />,
    );
    await step(() => undefined);
    expect(screen.getByText('Premium: paint without limits')).toBeTruthy();
    expect(navigation.setParams).toHaveBeenCalledWith({ photo: undefined, edit: undefined });
    await step(() => fireEvent.press(screen.getByTestId('signature-toggle')));
    await step(() => fireEvent.changeText(screen.getByTestId('signature-name'), 'Ama Owusu'));
    await step(() => fireEvent(screen.getByTestId('signature-name'), 'endEditing'));
    await step(() => fireEvent.press(screen.getByTestId('signature-style-serif')));
    expect(lastPreview().signature).toMatchObject({ text: 'Ama Owusu', style: 'serif', position: 'right', year: true });
    await step(() => fireEvent.press(screen.getByTestId('masters-paint')));
    expect(navigation.navigate).toHaveBeenLastCalledWith(
      'ArtLive',
      expect.objectContaining({
        job: expect.objectContaining({ signature: expect.objectContaining({ text: 'Ama Owusu' }) }),
      }),
    );
    // Pencil on Premium: no ads.
    await step(() => fireEvent.press(screen.getByTestId('masters-mode-pencil')));
    expect(screen.getByText('Premium: unlimited drawings, no ads')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('masters-paint')));
    expect(screen.queryByTestId('pencil-ad-sheet')).toBeNull();
    expect(mockAds.showRewardedAdForTool).not.toHaveBeenCalled();
    mockStudio.mastersAllowance.mockReset();
    mockStudio.mastersAllowance.mockImplementation(async () => ({ remaining: mockLeft, limit: 1 }));
  });

  it('opens a work again with what it was made with', async () => {
    await wrap(
      <MastersScreen
        navigation={navigation as never}
        route={
          {
            key: 'm',
            name: 'Masters',
            params: {
              photo: '/p.jpg',
              projectId: 'sp1',
              edit: {
                artist: 3,
                level: 2,
                seed: 9,
                mode: 'pencil',
                pencil: { medium: 1, paper: 2, intensity: 1, lines: 1.2, shading: 0.8 },
                frame: 1,
              },
            },
          } as never
        }
      />,
    );
    await step(() => undefined);
    expect(lastPreview()).toMatchObject({
      inputPath: '/p.jpg',
      artist: 3,
      level: 2,
      seed: 9,
      frame: 1,
      mode: 'pencil',
      pencil: { medium: 1, paper: 2, lines: 1.2, shading: 0.8 },
    });
    expect(screen.getByTestId('masters-mode-pencil').props.accessibilityState.selected).toBe(true);
  });
});
