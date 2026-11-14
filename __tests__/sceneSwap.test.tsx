import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { SceneSwapScreen } from '../src/screens/studio/SceneSwapScreen';
import { PhotoshootStudioScreen } from '../src/screens/studio/PhotoshootStudioScreen';
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { PLATES, autoFrame } from '../src/services/studio/scene';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';

const report = { coverage: 0.2, edge: 0.01, hairEdge: 0.3, box: [0.25, 0.1, 0.75, 0.95], cutLeft: false, cutTop: false, cutRight: false, cutBottom: false, found: true };
let cutN = 0;
const mockStudio = {
  startSession: jest.fn(async (_id: string, _req: object) => ({ sessionId: 's1', photoPath: '/cache/photo.jpg', cutoutPath: '/cache/cut_0.png', width: 900, height: 1200, report, parts: true })),
  refineCutout: jest.fn(async (_id: string, _req: object) => ({ cutoutPath: `/cache/cut_${++cutN}.png`, width: 900, height: 1200, report })),
  renderScene: jest.fn(async (_id: string, req: { quality: string; hidePerson?: boolean }) => ({
    outputPath: req.quality === 'full' ? '/files/studio/Studio_1.jpg' : req.hidePerson ? '/cache/empty.jpg' : '/cache/scene.jpg',
    width: 1024,
    height: 1280,
    sizeBytes: 1000,
    format: 'jpeg',
    box: [0.3, 0.3, 0.7, 0.9],
    exposure: 1,
    grounded: true,
  })),
  saveToGallery: jest.fn(async () => 'content://gallery/1'),
  endSession: jest.fn(),
  cancel: jest.fn(),
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
    },
  };
});

const navigation = { navigate: jest.fn(), goBack: jest.fn() };
const images = { add: jest.fn(async (r: object) => ({ id: 'img1', ...r })) };
const projects = { ensureInbox: jest.fn(async () => ({ id: 'inbox' })) };

async function open() {
  await render(
    <ThemeProvider preference="dark">
      <SceneSwapScreen navigation={navigation as never} route={{ key: 's', name: 'SceneSwap' } as never} />
    </ThemeProvider>,
  );
}

beforeEach(() => {
  jest.clearAllMocks();
  cutN = 0;
  useAppStore.setState({
    repos: { images, projects } as never,
    settings: { ...DEFAULT_SETTINGS },
    loadProjects: jest.fn(async () => undefined),
  } as never);
});

describe('Scene Swap', () => {
  it('cuts out the chosen photo, takes corrections, places the person and saves the photo', async () => {
    await open();
    // Every step runs inside act, and lets the device's (mocked) replies arrive there.
    const tick = () => new Promise<void>(r => setTimeout(() => r(), 0));
    const step = async (fn: () => unknown) =>
      act(async () => {
        await fn();
        for (let i = 0; i < 6; i++) await tick();
      });
    await step(() => fireEvent.press(screen.getByTestId('scene-pick')));
    expect(mockStudio.startSession.mock.calls[0][1]).toEqual({ inputPath: '/files/imports/me.jpg', points: [] });

    // A "remove" stroke across the canvas (each event on the canvas as it is now).
    const on = (name: string, ev: object) => step(() => fireEvent(screen.getByTestId('brush-canvas'), name, ev));
    await on('layout', { nativeEvent: { layout: { width: 340, height: 400 } } });
    await step(() => fireEvent.press(screen.getByText('Remove')));
    const ev = (x: number, y: number) => ({ nativeEvent: { locationX: x, locationY: y, touches: [{ locationX: x, locationY: y }] } });
    await on('responderGrant', ev(100, 100));
    await on('responderMove', ev(140, 120));
    await on('responderMove', ev(180, 140));
    await on('responderRelease', ev(180, 140));
    expect(mockStudio.refineCutout).toHaveBeenCalledTimes(1);
    const req = mockStudio.refineCutout.mock.calls[0][1] as unknown as { strokes: { brush: number; points: number[] }[]; hairDetail: number };
    expect(req.strokes).toHaveLength(1);
    expect(req.strokes[0].brush).toBe(1);
    expect(req.strokes[0].points.length).toBeGreaterThanOrEqual(4);
    expect(req.hairDetail).toBe(0.5);
    // Undo takes it back.
    await step(() => fireEvent.press(screen.getByLabelText('Undo')));
    expect(mockStudio.refineCutout).toHaveBeenCalledTimes(2);
    expect((mockStudio.refineCutout.mock.calls[1][1] as unknown as { strokes: unknown[] }).strokes).toHaveLength(0);

    // The scene: the first plate, the person and the empty scene drawn.
    await step(() => fireEvent.press(screen.getByTestId('scene-next')));
    expect(mockStudio.renderScene).toHaveBeenCalledTimes(2);
    const scene = mockStudio.renderScene.mock.calls[0][1] as unknown as { background: { kind: string; id: string }; placement: { y: number; height: number }; quality: string };
    expect(scene.background).toEqual({ kind: 'plate', id: PLATES[0].id });
    expect(scene.quality).toBe('preview');
    // Framed true to the scene: the size the floor gives where they stand, the crop auto framing chose.
    const auto = autoFrame(
      { box: report.box as [number, number, number, number], cutBottom: false, cutTop: false, cutSides: false, aspect: 900 / 1200, face: null },
      { kind: 'plate', plate: PLATES[0] },
      'portrait',
      PLATES[0].width,
      PLATES[0].height,
    );
    expect(scene.placement.y).toBeCloseTo(auto.placement.y, 5);
    expect(scene.placement.height).toBeCloseTo(auto.placement.height, 5);
    expect((scene as unknown as { crop: object }).crop).toEqual(auto.crop);

    // The Size slider: true to the scene at first, larger when pushed up, and Auto size brings it back.
    await step(() => fireEvent.press(screen.getByText('Adjust')));
    expect(screen.getByTestId('scene-size-note').props.children).toMatch(/True to the scene/);
    const calls = mockStudio.renderScene.mock.calls.length;
    for (let i = 0; i < 5; i++) await step(() => fireEvent(screen.getByTestId('scene-size'), 'accessibilityAction', { nativeEvent: { actionName: 'increment' } }));
    expect(screen.getByTestId('scene-size-note').props.children).toMatch(/Larger than the scene/);
    const bigger = mockStudio.renderScene.mock.calls[mockStudio.renderScene.mock.calls.length - 1][1] as unknown as { placement: { height: number } };
    expect(mockStudio.renderScene.mock.calls.length).toBeGreaterThan(calls);
    expect(bigger.placement.height).toBeGreaterThan(auto.placement.height * 1.05);
    await step(() => fireEvent.press(screen.getByTestId('scene-auto-size')));
    expect(screen.getByTestId('scene-size-note').props.children).toMatch(/True to the scene/);
    await step(() => fireEvent.press(screen.getByText('Backgrounds')));
    const bgOf = (i: number) => (mockStudio.renderScene.mock.calls[i][1] as unknown as { background: { kind: string; id?: string } }).background;

    // Another scene, then a plain colour: each re-draws the picture.
    await step(() => fireEvent.press(screen.getByTestId('bg-rose-chair')));
    expect(mockStudio.renderScene.mock.calls.some((_, i) => bgOf(i).id === 'rose-chair')).toBe(true);
    await step(() => fireEvent.press(screen.getByTestId('bg-tab-plain')));
    await step(() => fireEvent.press(screen.getByTestId('bg-plain-charcoal')));
    expect(mockStudio.renderScene.mock.calls.some((_, i) => bgOf(i).kind === 'plain')).toBe(true);

    // Save: full quality, into the gallery and the library, then opened.
    await step(() => fireEvent.press(screen.getByTestId('scene-save')));
    expect(mockStudio.renderScene.mock.calls.some(c => (c[1] as unknown as { quality: string }).quality === 'full')).toBe(true);
    expect(mockStudio.saveToGallery).toHaveBeenCalledWith('/files/studio/Studio_1.jpg');
    expect(images.add).toHaveBeenCalledWith(expect.objectContaining({ path: '/files/studio/Studio_1.jpg', source: 'export', projectId: 'inbox' }));
    expect(navigation.navigate).toHaveBeenCalledWith('PhotoPreview', { imageId: 'img1' });
  });

  it('says so when there is nobody in the photo', async () => {
    mockStudio.startSession.mockRejectedValueOnce(Object.assign(new Error('Nobody'), { code: 'NO_PERSON' }));
    await open();
    await act(async () => {
      await fireEvent.press(screen.getByTestId('scene-pick'));
      for (let i = 0; i < 4; i++) await new Promise<void>(r => setTimeout(() => r(), 0));
    });
    expect(screen.getByText(/Nobody was found in this photo/)).toBeTruthy();
  });

  it('is reached from the photoshoot studio', async () => {
    await render(
      <ThemeProvider preference="dark">
        <PhotoshootStudioScreen navigation={navigation as never} route={{ key: 'p', name: 'PhotoshootStudio' } as never} />
      </ThemeProvider>,
    );
    fireEvent.press(await screen.findByTestId('studio-scene-swap'));
    expect(navigation.navigate).toHaveBeenCalledWith('SceneSwap');
  });
});
