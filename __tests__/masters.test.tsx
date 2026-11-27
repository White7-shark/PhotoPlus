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
const mockStudio = {
  renderMasters: jest.fn(async (_id: string, req: { quality: string; artist: number }) => {
    if (req.quality === 'full') {
      if (mockLeft === 0) throw Object.assign(new Error('used'), { code: 'PREMIUM_REQUIRED' });
      mockLeft -= 1;
    }
    return {
      outputPath: `/cache/masters_${req.quality}_${req.artist}.jpg`,
      width: 760,
      height: 950,
      sizeBytes: 1000,
      format: 'jpeg',
      remaining: mockLeft,
    };
  }),
  mastersAllowance: jest.fn(async () => ({ remaining: mockLeft, limit: 1 })),
  saveToGallery: jest.fn(async () => 'content://gallery/1'),
  cancel: jest.fn(),
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

beforeEach(() => {
  jest.clearAllMocks();
  mockLeft = 1;
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
  it('is a Premium tile in the studio', async () => {
    const tile = STUDIO_TOOLS.find(t => t.id === 'masters');
    expect(tile).toMatchObject({ route: 'Masters', premium: true, available: true });
    await wrap(
      <PhotoshootStudioScreen
        navigation={navigation as never}
        route={{ key: 'p', name: 'PhotoshootStudio' } as never}
      />,
    );
    await fireEvent.press(screen.getByTestId('studio-masters'));
    expect(navigation.navigate).toHaveBeenCalledWith('Masters');
  });

  it('paints a preview, fills the cards, paints the finished one free once a day, then offers Premium', async () => {
    await wrap(<MastersScreen navigation={navigation as never} route={{ key: 'm', name: 'Masters' } as never} />);
    expect(screen.getByText('Your photo, painted by the great masters')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('masters-pick')));
    // The preview: Leonardo, a finished Painting.
    const previews = mockStudio.renderMasters.mock.calls.filter(c => c[1].quality === 'preview');
    expect(previews.at(-1)![1]).toMatchObject({ artist: 0, level: 1, inputPath: '/files/imports/me.jpg' });
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
    expect(mockStudio.renderMasters.mock.calls.at(-1)![1]).toMatchObject({ artist: 6, level: 2, quality: 'preview' });

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
    expect(screen.getByTestId('masters-painting').props.source.uri).toBe('file:///cache/masters_preview_6.jpg');

    // The finished painting: today's free one.
    await step(() => fireEvent.press(screen.getByTestId('masters-paint')));
    expect(mockStudio.renderMasters.mock.calls.at(-1)![1]).toMatchObject({ artist: 6, level: 2, quality: 'full' });
    expect(screen.getByTestId('masters-painting').props.source.uri).toBe('file:///cache/masters_full_6.jpg');
    await step(() => fireEvent.press(screen.getByTestId('masters-save')));
    expect(mockStudio.saveToGallery).toHaveBeenCalledWith('/cache/masters_full_6.jpg');
    // Saved, here and on its card in the session's gallery.
    expect(screen.getAllByText('Saved to your gallery')).toHaveLength(2);
    expect(screen.getByTestId('masters-gallery')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('masters-share')));
    expect(mockStorage.shareImages).toHaveBeenCalledWith(['/cache/masters_full_6.jpg'], 'Rembrandt');

    // Another try: a new preview; the day's free painting is used, so Premium is offered.
    await step(() => fireEvent.press(screen.getByTestId('masters-again')));
    expect(mockStudio.renderMasters.mock.calls.at(-1)![1]).toMatchObject({ seed: 8, quality: 'preview' });
    expect(screen.getAllByText(/today’s free painting is used/i).length).toBeGreaterThan(0);
    const fulls = mockStudio.renderMasters.mock.calls.filter(c => c[1].quality === 'full').length;
    await step(() => fireEvent.press(screen.getByTestId('masters-paint')));
    expect(mockStudio.renderMasters.mock.calls.filter(c => c[1].quality === 'full').length).toBe(fulls);
    expect(screen.getByTestId('premium-sheet-plans')).toBeTruthy();
    expect(screen.getByText(FEATURES.masters.title)).toBeTruthy();
  });

  it('paints without limits on Premium', async () => {
    mockLeft = -1;
    mockStudio.mastersAllowance.mockResolvedValueOnce({ remaining: -1, limit: 1 });
    await wrap(
      <MastersScreen
        navigation={navigation as never}
        route={{ key: 'm', name: 'Masters', params: { photo: '/p.jpg' } } as never}
      />,
    );
    await step(() => undefined);
    expect(screen.getByText('Premium: paint without limits')).toBeTruthy();
    expect(navigation.setParams).toHaveBeenCalledWith({ photo: undefined });
  });
});
