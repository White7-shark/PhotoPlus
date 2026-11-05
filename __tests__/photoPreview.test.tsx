import { act, fireEvent, render, screen, waitFor } from '@testing-library/react-native';
import React from 'react';

import { PILLARS } from '../src/screens/pillars';
import { HomeScreen } from '../src/screens/HomeScreen';
import { PhotoPreviewScreen } from '../src/screens/PhotoPreviewScreen';
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { saveVibeLook } from '../src/services/looks/LookService';
import { getProcessingService } from '../src/services/processing/ImageProcessingService';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';

const mockEngine = {
  readImageMetadata: jest.fn(async () => ({ width: 3000, height: 4000, sizeBytes: 2_000_000, mimeType: 'image/jpeg' })),
  analyzeQuality: jest.fn(async () => ({
    noise: 0.004,
    blurPx: 0.5,
    sharpness: 0.4,
    meanLuma: 0.5,
    highlightsClipped: 0,
    shadowsClipped: 0,
  })),
  portrait: jest.fn(async (_id: string, r: { outputPath: string }) => ({
    outputPath: r.outputPath,
    width: 3000,
    height: 4000,
    sizeBytes: 1_500_000,
    portrait: { people: 1, spots: 3, skin: 0.04, subject: 0.35 },
  })),
  exportImage: jest.fn(async () => ({
    outputPath: '/data/exports/PhotoPlus_1.jpg',
    width: 3000,
    height: 4000,
    sizeBytes: 1_000_000,
    galleryUri: null,
    watermark: { applied: true, enforced: false, plan: 'free' },
  })),
  enhance: jest.fn(),
  applyLook: jest.fn(),
  analyzeLook: jest.fn(),
  cancel: jest.fn(),
};
const mockStorage = {
  fileExists: jest.fn(async () => false),
  deleteFile: jest.fn(async () => true),
  shareImages: jest.fn(async () => true),
  pickImage: jest.fn(async () => '/data/imports/PP_IMPORT_1.jpg'),
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
      get storage() {
        return mockStorage;
      },
    },
  };
});

const photo = {
  id: 'img_1',
  projectId: 'prj_1',
  path: '/data/captures/PP_1.jpg',
  source: 'camera' as const,
  width: 3000,
  height: 4000,
  sizeBytes: 2_000_000,
  mimeType: 'image/jpeg',
  capturedAt: 1,
  metadata: { captureMode: 'enhance' },
  createdAt: 1,
};

function fakeRepos(record: typeof photo & { metadata: Record<string, unknown> } = photo) {
  const added: unknown[] = [];
  return {
    added,
    repos: {
      images: {
        get: jest.fn(async () => record),
        add: jest.fn(async (img: object) => {
          added.push(img);
          return { ...img, id: 'img_2', createdAt: 2, metadata: {} };
        }),
        remove: jest.fn(),
      },
      projects: { ensureInbox: jest.fn(async () => ({ id: 'prj_1' })) },
    } as never,
  };
}

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);

beforeEach(() => {
  jest.clearAllMocks();
});

describe('photo preview', () => {
  it('shares the photo to other apps through export (with the watermark the plan requires)', async () => {
    const { repos } = fakeRepos();
    useAppStore.setState({ repos, settings: { ...DEFAULT_SETTINGS } });
    const navigation = { goBack: jest.fn(), replace: jest.fn() };
    await wrap(
      <PhotoPreviewScreen navigation={navigation as never} route={{ params: { imageId: 'img_1' } } as never} />,
    );
    await screen.findByText('Make it shine');
    await fireEvent.press(screen.getByTestId('share'));
    await waitFor(() => expect(mockStorage.shareImages).toHaveBeenCalled());
    expect(mockEngine.exportImage).toHaveBeenCalledWith(
      expect.any(String),
      expect.objectContaining({
        inputPath: '/data/captures/PP_1.jpg',
        destination: 'share',
        watermark: expect.objectContaining({ style: 'signature', position: 'br' }),
      }),
    );
    // Only the exported file leaves the app, never the library photo itself.
    expect(mockStorage.shareImages).toHaveBeenCalledWith(['/data/exports/PhotoPlus_1.jpg'], 'Share your photo');
  });

  it('Spotlight: tap what should shine, apply, and keep it', async () => {
    const { repos, added } = fakeRepos();
    useAppStore.setState({ repos, settings: { ...DEFAULT_SETTINGS } });
    const navigation = { goBack: jest.fn(), replace: jest.fn() };
    await wrap(
      <PhotoPreviewScreen navigation={navigation as never} route={{ params: { imageId: 'img_1' } } as never} />,
    );
    await fireEvent.press(await screen.findByTestId('tool-spotlight'));
    const photoView = screen.getByLabelText('Photo. Tap what should shine');
    await act(async () => {
      fireEvent(photoView, 'layout', { nativeEvent: { layout: { width: 300, height: 400 } } });
    });
    await fireEvent(photoView, 'press', { nativeEvent: { locationX: 90, locationY: 200 } });
    expect(screen.getByText('1 chosen · tap again to remove')).toBeTruthy();
    await fireEvent.press(screen.getByTestId('apply-tool'));
    await waitFor(() => expect(mockEngine.portrait).toHaveBeenCalled());
    const request = mockEngine.portrait.mock.calls[0][1] as { points: number[]; retouch: number; outputPath: string };
    expect(request.points).toEqual([0.3, 0.5]);
    expect(request.retouch).toBe(0.75); // Glow "Smooth" by default
    expect(request.outputPath).toBe('/data/captures/PP_1_spotlight.jpg');
    expect(await screen.findByText('Glow · 3 blemishes cleared')).toBeTruthy();
    await fireEvent.press(screen.getByRole('button', { name: 'Keep' }));
    await waitFor(() => expect(navigation.replace).toHaveBeenCalledWith('PhotoPreview', { imageId: 'img_2' }));
    expect(added[0]).toMatchObject({
      path: '/data/captures/PP_1_spotlight.jpg',
      metadata: { captureMode: 'spotlight', originalPath: '/data/captures/PP_1.jpg', originalOwned: false },
    });
  });

  it('offers every tool under its app name, premium ones marked', async () => {
    const { repos } = fakeRepos();
    useAppStore.setState({ repos, settings: { ...DEFAULT_SETTINGS } });
    await wrap(
      <PhotoPreviewScreen
        navigation={{ goBack: jest.fn() } as never}
        route={{ params: { imageId: 'img_1' } } as never}
      />,
    );
    await screen.findByText('Make it shine');
    expect(screen.getByRole('tab', { name: 'Clarity' })).toBeTruthy();
    expect(screen.getByRole('tab', { name: 'Spotlight' })).toBeTruthy();
    expect(screen.getByRole('tab', { name: 'Vibe Match, premium' })).toBeTruthy();
    expect(screen.getByRole('tab', { name: 'Ultra Detail, premium' })).toBeTruthy();
  });
});

describe('home', () => {
  it('Edit a photo brings one in from the gallery and opens it', async () => {
    const { repos, added } = fakeRepos();
    useAppStore.setState({ repos, settings: { ...DEFAULT_SETTINGS }, projects: [], projectsStatus: 'ready' });
    const navigation = { navigate: jest.fn() };
    await wrap(<HomeScreen navigation={navigation as never} route={{} as never} />);
    await fireEvent.press(screen.getByRole('button', { name: 'Edit a photo' }));
    await waitFor(() => expect(navigation.navigate).toHaveBeenCalledWith('PhotoPreview', { imageId: 'img_2' }));
    expect(mockStorage.pickImage).toHaveBeenCalledWith('imports');
    expect(added[0]).toMatchObject({
      path: '/data/imports/PP_IMPORT_1.jpg',
      source: 'import',
      width: 3000,
      height: 4000,
    });
  });

  it('never shows internal milestones', () => {
    for (const p of PILLARS) {
      expect(['Available now', 'Coming soon']).toContain(p.availability);
      expect(JSON.stringify(p)).not.toMatch(/phase/i);
    }
  });
});

describe('review regressions', () => {
  it('Spotlight taps work on a photo that opens in before/after', async () => {
    const { repos } = fakeRepos({
      ...photo,
      metadata: { captureMode: 'enhance', originalPath: '/data/captures/PP_1_original.jpg' },
    });
    useAppStore.setState({ repos, settings: { ...DEFAULT_SETTINGS } });
    await wrap(
      <PhotoPreviewScreen
        navigation={{ goBack: jest.fn() } as never}
        route={{ params: { imageId: 'img_1' } } as never}
      />,
    );
    expect(await screen.findByRole('adjustable', { name: 'Before and after comparison' })).toBeTruthy();
    await fireEvent.press(screen.getByTestId('tool-spotlight'));
    const photoView = screen.getByLabelText('Photo. Tap what should shine');
    await act(async () => {
      fireEvent(photoView, 'layout', { nativeEvent: { layout: { width: 200, height: 200 } } });
    });
    await fireEvent(photoView, 'press', { nativeEvent: { locationX: 100, locationY: 50 } });
    expect(screen.getByText('1 chosen · tap again to remove')).toBeTruthy();
  });

  it('an edit can never overwrite the original photo', async () => {
    await expect(
      getProcessingService().spotlight({ inputPath: '/data/a.jpg', outputPath: '/data/a.jpg' }),
    ).rejects.toMatchObject({ code: 'INVALID_ARGUMENT' });
    expect(mockEngine.portrait).not.toHaveBeenCalled();
  });

  it('changing or turning off Vibe Match removes the old style photo', async () => {
    const look = Array.from({ length: 20 }, () => 0.1);
    const settings = { ...DEFAULT_SETTINGS };
    useAppStore.setState({ repos: null, settings });
    const first = { referencePath: '/data/looks/a.jpg', look, strength: 0.8 };
    const second = { referencePath: '/data/looks/b.jpg', look, strength: 0.8 };
    await saveVibeLook(second, first);
    expect(mockStorage.deleteFile).toHaveBeenCalledWith('/data/looks/a.jpg');
    expect(useAppStore.getState().settings.vibeLook).toEqual(second);
    await saveVibeLook({ ...second, strength: 1 }, second); // same photo, new strength: kept
    expect(mockStorage.deleteFile).toHaveBeenCalledTimes(1);
    await saveVibeLook(null, second);
    expect(mockStorage.deleteFile).toHaveBeenLastCalledWith('/data/looks/b.jpg');
  });
});
