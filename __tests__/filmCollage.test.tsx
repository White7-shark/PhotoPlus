import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { FilmCollageScreen } from '../src/screens/studio/FilmCollageScreen';
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import {
  COLLAGE_LAYOUTS,
  collageRequest,
  filmRequest,
  framesAfterRemoving,
  mixedFrames,
  plainFrame,
  type FilmChoice,
} from '../src/services/studio/film';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';

const result = (path: string) => ({ outputPath: path, width: 600, height: 1400, sizeBytes: 1000, format: 'jpeg' });
const mockStudio = {
  renderFilm: jest.fn(async (_id: string, req: { quality: string }) =>
    result(req.quality === 'full' ? '/files/studio/Film_1.jpg' : '/cache/film.jpg'),
  ),
  renderCollage: jest.fn(async (_id: string, req: { quality: string }) =>
    result(req.quality === 'full' ? '/files/studio/Collage_1.jpg' : '/cache/collage.jpg'),
  ),
  saveToGallery: jest.fn(async () => 'content://gallery/1'),
  startSession: jest.fn(),
  refineCutout: jest.fn(),
  renderScene: jest.fn(),
  endSession: jest.fn(),
  cancel: jest.fn(),
};
let picks = ['/files/imports/a.jpg', '/files/imports/b.jpg'];
const mockStorage = { pickImage: jest.fn(async () => picks.shift() ?? null) };

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

describe('film and collage requests', () => {
  it('mixes one photo into a strip of different frames, like a contact sheet', () => {
    const frames = mixedFrames(1, 4);
    expect(frames.map(f => f.look)).toEqual(['original', 'bw', 'vintage', 'noir']);
    expect(new Set(frames.map(f => `${f.zoom}-${f.flip}`)).size).toBeGreaterThan(2);
    expect(frames.every(f => f.photo === 0)).toBe(true);
    // Several photos: each first appears as shot, variations come after.
    const three = mixedFrames(3, 6);
    expect(three.map(f => f.photo)).toEqual([0, 1, 2, 0, 1, 2]);
    expect(three.slice(0, 3).every(f => f.look === 'original')).toBe(true);
    expect(three.slice(3).every(f => f.look === 'bw')).toBe(true);
    expect(mixedFrames(1, 99)).toHaveLength(12);
  });

  it('keeps frames valid when a photo is removed', () => {
    const frames = [plainFrame(0), plainFrame(1), plainFrame(2)];
    expect(framesAfterRemoving(frames, 1).map(f => f.photo)).toEqual([0, 0, 1]);
  });

  it('asks the device for looks by number, within range', () => {
    const film: FilmChoice = {
      style: 'booth',
      vertical: true,
      portrait: true,
      markings: false,
      frames: [{ photo: 0, look: 'noir', zoom: 9, panX: -4, panY: 0.5, flip: true }],
    };
    expect(filmRequest(['/a.jpg'], film, 'preview')).toEqual({
      photos: ['/a.jpg'],
      frames: [{ photo: 0, look: 9, zoom: 4, panX: -1, panY: 0.5, flip: true }],
      style: 'booth',
      vertical: true,
      portrait: true,
      markings: false,
      quality: 'preview',
    });
    const c = collageRequest(
      ['/a.jpg', '/b.jpg'],
      { layout: 'hero3', aspect: 'square', gap: 0.5, radius: 0.1, background: '#111114', frames: [] },
      'full',
    );
    expect(c.cells).toHaveLength(3);
    expect(c.cells.map(x => x.photo)).toEqual([0, 1, 0]);
    expect(c).toMatchObject({ aspect: 1, gap: 0.1, radius: 0.1, background: '#111114', quality: 'full' });
    for (const l of COLLAGE_LAYOUTS)
      for (const [x, y, w, h] of l.cells) expect(x >= 0 && y >= 0 && x + w <= 1.0001 && y + h <= 1.0001).toBe(true);
  });
});

const navigation = { navigate: jest.fn(), goBack: jest.fn(), setParams: jest.fn() };
const images = { add: jest.fn(async (r: object) => ({ id: 'img1', ...r })) };
const projects = { ensureInbox: jest.fn(async () => ({ id: 'inbox' })) };
const tick = () => new Promise<void>(r => setTimeout(() => r(), 0));
const step = async (fn: () => unknown) =>
  act(async () => {
    await fn();
    for (let i = 0; i < 6; i++) await tick();
  });

async function open(params?: { photo?: string }) {
  await render(
    <ThemeProvider preference="dark">
      <FilmCollageScreen navigation={navigation as never} route={{ key: 'f', name: 'FilmCollage', params } as never} />
    </ThemeProvider>,
  );
}

beforeEach(() => {
  jest.clearAllMocks();
  picks = ['/files/imports/a.jpg', '/files/imports/b.jpg'];
  useAppStore.setState({
    repos: { images, projects } as never,
    settings: { ...DEFAULT_SETTINGS },
    loadProjects: jest.fn(async () => undefined),
  } as never);
});

describe('Film & Collage screen', () => {
  it('turns photos into a film strip, transforms frames, makes a collage and saves', async () => {
    await open();
    await step(() => fireEvent.press(screen.getByTestId('film-add')));
    // One photo: a mixed strip of four frames, previewed.
    const first = mockStudio.renderFilm.mock.calls.at(-1)![1] as unknown as {
      frames: { look: number }[];
      style: string;
      quality: string;
    };
    expect(first).toMatchObject({ style: 'classic', quality: 'preview' });
    expect(first.frames.map(f => f.look)).toEqual([0, 1, 3, 9]);
    expect(screen.getByTestId('film-preview')).toBeTruthy();

    // The photo booth (portrait frames); a second photo; frame 2 in sepia, mirrored.
    await step(() => fireEvent.press(screen.getByTestId('film-style-booth')));
    await step(() => fireEvent.press(screen.getByTestId('film-add')));
    await step(() => fireEvent.press(screen.getByTestId('frame-1')));
    await step(() => fireEvent.press(screen.getByTestId('look-sepia')));
    await step(() => fireEvent.press(screen.getByTestId('frame-flip')));
    const film = mockStudio.renderFilm.mock.calls.at(-1)![1] as unknown as {
      photos: string[];
      style: string;
      portrait: boolean;
      frames: { photo: number; look: number; flip: boolean }[];
    };
    expect(film.photos).toEqual(['/files/imports/a.jpg', '/files/imports/b.jpg']);
    expect(film).toMatchObject({ style: 'booth', portrait: true });
    expect(film.frames[1]).toMatchObject({ look: 2, flip: true });

    // Collage: a layout of four, then Save.
    await step(() => fireEvent.press(screen.getByTestId('mode-collage')));
    await step(() => fireEvent.press(screen.getByTestId('layout-grid4')));
    const col = mockStudio.renderCollage.mock.calls.at(-1)![1] as unknown as {
      cells: { photo: number }[];
      quality: string;
    };
    expect(col.cells.map(c => c.photo)).toEqual([0, 1, 0, 1]);
    await step(() => fireEvent.press(screen.getByTestId('film-save')));
    expect(mockStudio.renderCollage.mock.calls.at(-1)![1]).toMatchObject({ quality: 'full' });
    expect(mockStudio.saveToGallery).toHaveBeenCalledWith('/files/studio/Collage_1.jpg');
    expect(images.add).toHaveBeenCalledWith(
      expect.objectContaining({ path: '/files/studio/Collage_1.jpg', source: 'export' }),
    );
    expect(navigation.navigate).toHaveBeenCalledWith('PhotoPreview', { imageId: 'img1' });
  });

  it('takes photos with the camera and adds the one just taken', async () => {
    await open();
    await step(() => fireEvent.press(screen.getByTestId('film-shoot')));
    expect(navigation.navigate).toHaveBeenCalledWith('Main', { screen: 'Camera', params: { returnTo: 'FilmCollage' } });
    jest.clearAllMocks();
    await open({ photo: '/files/captures/shot.jpg' });
    await step(async () => undefined);
    expect(navigation.setParams).toHaveBeenCalledWith({ photo: undefined });
    expect((mockStudio.renderFilm.mock.calls.at(-1)![1] as unknown as { photos: string[] }).photos).toEqual([
      '/files/captures/shot.jpg',
    ]);
  });
});
