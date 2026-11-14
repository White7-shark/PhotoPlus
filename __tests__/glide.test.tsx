import { act, fireEvent, render, screen, waitFor } from '@testing-library/react-native';
import React from 'react';

import { GlidePreviewScreen } from '../src/screens/GlidePreviewScreen';
import { CameraScreen } from '../src/screens/camera/CameraScreen';
import { CameraService } from '../src/services/camera/CameraService';
import {
  GLIDE_FREE_MAX_MS,
  GLIDE_PREMIUM_MAX_MS,
  glideBlur,
  glideMaxMs,
  glideFileName,
  glideRequest,
  glideStageLabel,
  glideSummary,
  isGlideStage,
  recordedFraction,
  secondsLeft,
  steadierPercent,
  type GlideClip,
} from '../src/services/camera/glide';
import { modeLabel } from '../src/services/camera/model';
import { cameraRecipe } from '../src/services/beauty/camera';
import { DEFAULT_ADJUST, DEFAULT_PORTRAIT, resolveRecipe } from '../src/services/beauty/recipe';
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { DEFAULT_WATERMARK } from '../src/services/edit/watermark';
import { friendlyMessage } from '../src/services/errors';
import { ImageProcessingService } from '../src/services/processing/ImageProcessingService';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';
import { rawCameraCaps } from './helpers/fixtures';

// ------------------------------------------------------------------ mocks

let finishRecording: ((clip: { path: string; durationMs: number }) => void) | null = null;

const mockCamera = {
  isSessionReady: jest.fn(async () => true),
  setLiveBeauty: jest.fn(),
  setBeautyOriginal: jest.fn(),
  getCapabilities: jest.fn(async () => rawCameraCaps()),
  configure: jest.fn(),
  capturePhoto: jest.fn(),
  cancelCapture: jest.fn(),
  startVideo: jest.fn(
    () =>
      new Promise<object>(resolve => {
        finishRecording = resolve;
      }),
  ),
  stopVideo: jest.fn(() => finishRecording?.({ path: '/captures/glide/GLIDE_1_raw.mp4', durationMs: 3200 })),
  setZoom: jest.fn(async (r: number) => r),
  setExposureCompensation: jest.fn(async (i: number) => i),
  focusAt: jest.fn(async () => true),
  cancelFocus: jest.fn(),
  setFlash: jest.fn(),
  setTorch: jest.fn(async () => true),
  setFocusPeaking: jest.fn(),
  setFaceTracking: jest.fn(),
  setManual: jest.fn(async (s: object) => s),
  resetManual: jest.fn(),
  addListener: jest.fn(),
  removeListeners: jest.fn(),
};

const glideResult = {
  width: 1080,
  height: 1920,
  frames: 96,
  fps: 30,
  durationMs: 3200,
  zoom: 1.12,
  shakeBefore: 4.6,
  shakeAfter: 1.7,
  processMs: 41000,
  watermarked: true,
};

const mockEngine = {
  glide: jest.fn(async (_jobId: string, request: { outputPath: string; scene?: object }) => ({
    ...glideResult,
    outputPath: request.outputPath,
    ...(request.scene ? { scene: true } : {}),
  })),
  glideSubject: jest.fn(async () => ({
    found: true,
    box: [0.3, 0.12, 0.7, 0.96],
    cutLeft: false,
    cutTop: false,
    cutRight: false,
    cutBottom: false,
    aspect: 9 / 16,
  })),
  cancel: jest.fn(),
};

const mockStorage = {
  getPaths: jest.fn(async () => ({ exports: '/exports', captures: '/captures' })),
  saveVideoToGallery: jest.fn(async () => 'content://media/video/1'),
  shareImages: jest.fn(async () => true),
  deleteFile: jest.fn(async () => true),
};

jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get camera() {
        return mockCamera;
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

const mockEnsure = jest.fn(async (_p: string) => 'granted');
jest.mock('../src/services/permissions/PermissionService', () => ({
  getPermissionService: () => ({
    ensure: (p: string) => mockEnsure(p),
    request: async () => 'granted',
    openSettings: async () => undefined,
  }),
}));

jest.mock('@react-navigation/native', () => ({
  ...jest.requireActual('@react-navigation/native'),
  useIsFocused: () => true,
}));

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);

beforeEach(() => {
  jest.clearAllMocks();
  finishRecording = null;
  useAppStore.setState({ repos: null, settings: { ...DEFAULT_SETTINGS }, isForeground: true });
});

// ------------------------------------------------------------- pure logic

describe('Glide logic', () => {
  it('builds the engine request from Spotlight and the beauty filter', () => {
    const plain = glideRequest({ inputPath: '/a.mp4', outputPath: '/b.mp4', spotlight: 'off', recipe: null });
    expect(plain).toEqual({
      inputPath: '/a.mp4',
      outputPath: '/b.mp4',
      steady: 1,
      maxZoom: 1.12,
      denoise: 1,
      sharpen: 1,
      blur: 0,
      pop: 0,
      beauty: null,
    });
    // Spotlight alone (no recipe): the clip's own blur.
    expect(glideRequest({ inputPath: '/a.mp4', outputPath: '/b.mp4', spotlight: 'strong', recipe: null }).blur).toBe(
      0.9,
    );
    // A beauty filter with Spotlight: one recipe carries both; the old fields stay neutral.
    const recipe = cameraRecipe({ on: true, preset: 'golden_goddess', adjust: DEFAULT_ADJUST }, true, {
      ...DEFAULT_PORTRAIT,
      depthBlur: glideBlur('strong'),
    });
    const full = glideRequest({
      inputPath: '/a.mp4',
      outputPath: '/b.mp4',
      spotlight: 'strong',
      recipe,
      watermark: DEFAULT_WATERMARK,
    });
    expect(full.blur).toBe(0);
    expect(full.beauty).toBeNull();
    expect(full.beautyRecipe?.portrait.blur).toBe(0.9);
    expect(full.beautyRecipe?.grade.bgGlow).toBe(0.5);
    expect(full.watermark).toBe(DEFAULT_WATERMARK);
    // A recipe that changes nothing is not sent.
    expect(
      glideRequest({
        inputPath: '/a',
        outputPath: '/b',
        spotlight: 'off',
        recipe: resolveRecipe(null, DEFAULT_ADJUST),
      }),
    ).not.toHaveProperty('beautyRecipe');
  });

  it('times the recording by plan: 10 s free, 30 s with Premium', () => {
    expect(GLIDE_FREE_MAX_MS).toBe(10_000);
    expect(GLIDE_PREMIUM_MAX_MS).toBe(30_000);
    expect(glideMaxMs('free')).toBe(10_000);
    expect(glideMaxMs('premium')).toBe(30_000);
    expect(secondsLeft(0)).toBe(10);
    expect(secondsLeft(100)).toBe(10);
    expect(secondsLeft(1000)).toBe(9);
    expect(secondsLeft(9900)).toBe(1);
    expect(secondsLeft(12000)).toBe(0);
    expect(secondsLeft(-50)).toBe(10);
    expect(secondsLeft(1000, 30_000)).toBe(29);
    expect(recordedFraction(5000)).toBe(0.5);
    expect(recordedFraction(15_000, 30_000)).toBe(0.5);
    expect(recordedFraction(40_000, 30_000)).toBe(1);
  });

  it('names clips by date and labels each step in plain words', () => {
    expect(glideFileName(new Date(2026, 10, 7, 9, 5, 3))).toBe('PhotoPlus_Glide_20261107_090503.mp4');
    expect(glideStageLabel('analysing', 0.1)).toBe('Finding the shake · 10%');
    expect(glideStageLabel('measuring', 0.27)).toBe('Reading the light…');
    expect(glideStageLabel('crafting', 0.648)).toBe('Crafting your Glide · 65%');
    expect(glideStageLabel('done', 1)).toBe('Saving…');
    expect(glideStageLabel(null, 0)).toBe('Finding the shake · 0%');
    expect(isGlideStage('crafting')).toBe(true);
    expect(isGlideStage('styling')).toBe(false);
    expect(modeLabel('glide')).toBe('Glide');
    expect(friendlyMessage('CLIP_TOO_SHORT')).toMatch(/at least a second/);
  });

  it('reports how much steadier the clip became, only when there was shake', () => {
    expect(steadierPercent(4.6, 1.7)).toBe(63);
    expect(steadierPercent(0.3, 0.1)).toBeNull(); // already steady
    expect(steadierPercent(4, 5)).toBeNull(); // never claims a gain that isn't there
    expect(steadierPercent(Number.NaN, 1)).toBeNull();
    expect(glideSummary({ shakeBefore: 4.6, shakeAfter: 1.7, durationMs: 3200 })).toBe('3.2 s · 63% steadier');
    expect(glideSummary({ shakeBefore: 0.2, shakeAfter: 0.2, durationMs: 7000 })).toBe('7.0 s');
  });
});

// --------------------------------------------------------------- services

describe('Glide services', () => {
  it('records through the camera module and reports failures with stable codes', async () => {
    const native = {
      startVideo: jest.fn(async () => ({ path: '/c/g.mp4', durationMs: 7000 })),
      stopVideo: jest.fn(),
    };
    const service = new CameraService(native as never, { addListener: jest.fn() });
    await expect(service.startVideo({ audio: true, maxMs: 7000 })).resolves.toEqual({
      path: '/c/g.mp4',
      durationMs: 7000,
    });
    native.startVideo.mockResolvedValueOnce({} as never);
    await expect(service.startVideo({ audio: false, maxMs: 7000 })).rejects.toMatchObject({
      code: 'RECORDING_FAILED',
    });
    native.startVideo.mockRejectedValueOnce(Object.assign(new Error('busy'), { code: 'NOT_READY' }));
    await expect(service.startVideo({ audio: false, maxMs: 7000 })).rejects.toMatchObject({ code: 'NOT_READY' });
    service.stopVideo();
    expect(native.stopVideo).toHaveBeenCalled();
  });

  it('runs the glide job with progress and each step reported once', async () => {
    const listeners: ((p: object) => void)[] = [];
    const events = {
      addListener: jest.fn((_e: string, l: (p: object) => void) => {
        listeners.push(l);
        return { remove: () => listeners.splice(listeners.indexOf(l), 1) };
      }),
    };
    const engine = {
      glide: jest.fn(async (jobId: string) => {
        for (const [stage, progress] of [
          ['analysing', 0.1],
          ['analysing', 0.2],
          ['crafting', 0.5],
        ] as const) {
          listeners.forEach(l => l({ jobId, progress, stage }));
        }
        listeners.forEach(l => l({ jobId: 'other', progress: 0.9, stage: 'done' }));
        return { ...glideResult, outputPath: '/exports/o.mp4' };
      }),
      cancel: jest.fn(),
    };
    const service = new ImageProcessingService(engine as never, events);
    const stages: string[] = [];
    const progress: number[] = [];
    const request = glideRequest({
      inputPath: '/c/r.mp4',
      outputPath: '/exports/o.mp4',
      spotlight: 'off',
      recipe: null,
    });
    const r = await service.glide(request, { onStage: s => stages.push(s), onProgress: p => progress.push(p) });
    expect(r.outputPath).toBe('/exports/o.mp4');
    expect(stages).toEqual(['analysing', 'crafting']);
    expect(progress).toEqual([0.1, 0.2, 0.5]);
    expect(listeners).toHaveLength(0); // every listener removed
    await expect(service.glide({ ...request, outputPath: request.inputPath })).rejects.toMatchObject({
      code: 'INVALID_ARGUMENT',
    });
  });
});

// ------------------------------------------------------------------ camera

async function renderGlideCamera() {
  const navigation = { navigate: jest.fn(), goBack: jest.fn() };
  await render(<CameraScreen navigation={navigation as never} route={{ key: 'c', name: 'Camera' } as never} />);
  const ready = async () => {
    const host = await screen.findByTestId('camera-preview');
    await act(async () => {
      host.props.onCameraReady({ nativeEvent: { lensFacing: 'back' } });
    });
  };
  await ready();
  await fireEvent.press(await screen.findByTestId('mode-glide'));
  // Glide rebinds the camera for video; the shutter waits for it.
  expect(screen.getByTestId('shutter').props.accessibilityState.disabled).toBe(true);
  await ready();
  return { navigation };
}

describe('Glide in the camera', () => {
  it('records 16:9 video with sound, crafts the clip, saves it and opens it', async () => {
    const { navigation } = await renderGlideCamera();
    expect(mockCamera.configure).toHaveBeenLastCalledWith({
      aspectRatio: '16:9',
      resolution: 'max',
      raw: false,
      video: true,
      beauty: false,
    });
    expect(mockEnsure).toHaveBeenCalledWith('microphone');
    expect(screen.getByTestId('glide-panel')).toBeTruthy();
    // Photo-only controls step aside.
    expect(screen.queryByTestId('props-chip')).toBeNull();
    expect(screen.queryByTestId('vibe-chip')).toBeNull();
    await fireEvent.press(screen.getByRole('radio', { name: 'Medium' }));

    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.startVideo).toHaveBeenCalledWith({ audio: true, maxMs: 10_000 }));
    expect(screen.getByTestId('glide-recording')).toBeTruthy();
    expect(screen.getByText('10s left')).toBeTruthy();
    expect(screen.getByLabelText('Stop recording')).toBeTruthy();
    expect(screen.queryByTestId('mode-photo')).toBeNull(); // no mode changes mid-clip

    // Second press stops early.
    await fireEvent.press(screen.getByTestId('shutter'));
    expect(mockCamera.stopVideo).toHaveBeenCalled();
    await waitFor(() => expect(mockEngine.glide).toHaveBeenCalled());
    const request = mockEngine.glide.mock.calls[0][1] as unknown as Record<string, unknown>;
    expect(request).toMatchObject({
      inputPath: '/captures/glide/GLIDE_1_raw.mp4',
      steady: 1,
      beauty: null,
      // Spotlight Medium with beauty off: Portrait mode's controls, no preset.
      beautyRecipe: { portrait: { blur: 0.6, smooth: 0 }, face: { blush: 0 } },
      watermark: DEFAULT_SETTINGS.watermark,
    });
    expect(request.outputPath).toMatch(/^\/exports\/PhotoPlus_Glide_\d{8}_\d{6}\.mp4$/);
    await waitFor(() => expect(mockStorage.saveVideoToGallery).toHaveBeenCalledWith(request.outputPath));
    await waitFor(() =>
      expect(navigation.navigate).toHaveBeenCalledWith('GlidePreview', {
        clip: expect.objectContaining({
          outputPath: request.outputPath,
          rawPath: '/captures/glide/GLIDE_1_raw.mp4',
          galleryUri: 'content://media/video/1',
        }),
      }),
    );
    expect(await screen.findByText('Glide saved · 3.2 s · 63% steadier')).toBeTruthy();
    expect(screen.getByTestId('watch-last-clip')).toBeTruthy();
  });

  it('keeps the beauty filter from the camera and records silently without the microphone', async () => {
    mockEnsure.mockImplementation(async (p: string) => (p === 'microphone' ? 'denied' : 'granted'));
    useAppStore.setState(st => ({
      settings: { ...st.settings, cameraBeauty: { on: true, preset: 'peach_perfection', adjust: DEFAULT_ADJUST } },
    }));
    await renderGlideCamera();
    expect(screen.getByText(/Peach Perfection · no sound/)).toBeTruthy();
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.startVideo).toHaveBeenCalledWith({ audio: false, maxMs: 10_000 }));
    await act(async () => finishRecording?.({ path: '/captures/glide/GLIDE_2_raw.mp4', durationMs: 7000 }));
    await waitFor(() => expect(mockEngine.glide).toHaveBeenCalled());
    expect(mockEngine.glide.mock.calls[0][1]).toMatchObject({
      blur: 0,
      beautyRecipe: { portrait: { blur: 0, smooth: 0.55 }, face: { blush: 0.4 }, grade: { skinSheen: 0.5 } },
    });
    mockEnsure.mockImplementation(async () => 'granted');
  });

  it('records up to 30 s with Premium', async () => {
    useAppStore.setState(st => ({ entitlements: { ...st.entitlements, plan: 'premium' } }));
    await renderGlideCamera();
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.startVideo).toHaveBeenCalledWith({ audio: true, maxMs: 30_000 }));
    expect(screen.getByText('30s left')).toBeTruthy();
    await act(async () => finishRecording?.({ path: '/captures/glide/GLIDE_3_raw.mp4', durationMs: 25_000 }));
    await waitFor(() => expect(mockEngine.glide).toHaveBeenCalled());
    useAppStore.setState(st => ({ entitlements: { ...st.entitlements, plan: 'free' } }));
  });

  it('discards a clip under a second and explains why', async () => {
    await renderGlideCamera();
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.startVideo).toHaveBeenCalled());
    await act(async () => finishRecording?.({ path: '/captures/glide/GLIDE_3_raw.mp4', durationMs: 400 }));
    expect(await screen.findByText(friendlyMessage('CLIP_TOO_SHORT'))).toBeTruthy();
    expect(mockEngine.glide).not.toHaveBeenCalled();
    expect(mockStorage.deleteFile).toHaveBeenCalledWith('/captures/glide/GLIDE_3_raw.mp4');
    // Ready for the next clip.
    expect(screen.getByLabelText('Record a Glide clip')).toBeTruthy();
  });

  it('shows a friendly message when crafting fails', async () => {
    mockEngine.glide.mockRejectedValueOnce(Object.assign(new Error('oom'), { code: 'OUT_OF_MEMORY' }));
    await renderGlideCamera();
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.startVideo).toHaveBeenCalled());
    await act(async () => finishRecording?.({ path: '/captures/glide/GLIDE_4_raw.mp4', durationMs: 5000 }));
    expect(await screen.findByText(friendlyMessage('OUT_OF_MEMORY'))).toBeTruthy();
    expect(mockStorage.saveVideoToGallery).not.toHaveBeenCalled();
  });

  it('switching back to photos rebinds without video', async () => {
    await renderGlideCamera();
    await fireEvent.press(screen.getByTestId('mode-photo'));
    await waitFor(() =>
      expect(mockCamera.configure).toHaveBeenLastCalledWith({
        aspectRatio: '4:3',
        resolution: 'max',
        raw: false,
        video: false,
        beauty: false,
      }),
    );
    expect(screen.queryByTestId('glide-panel')).toBeNull();
  });
});

// ----------------------------------------------------------------- preview

const clip: GlideClip = {
  ...glideResult,
  outputPath: '/exports/PhotoPlus_Glide_1.mp4',
  rawPath: '/captures/glide/GLIDE_1_raw.mp4',
  galleryUri: null,
};

describe('Glide preview', () => {
  it('plays the clip, compares with the recording, shares and saves', async () => {
    const navigation = { navigate: jest.fn(), goBack: jest.fn() };
    await wrap(
      <GlidePreviewScreen
        navigation={navigation as never}
        route={{ key: 'g', name: 'GlidePreview', params: { clip } } as never}
      />,
    );
    expect(screen.getByTestId('video-view').props.source).toBe(clip.outputPath);
    expect(screen.getByText('63% steadier')).toBeTruthy();
    expect(screen.getByText('Photo Plus mark')).toBeTruthy();

    await fireEvent.press(screen.getByText('As recorded'));
    expect(screen.getByTestId('video-view').props.source).toBe(clip.rawPath);

    await fireEvent.press(screen.getByTestId('mute'));
    expect(screen.getByTestId('video-view').props.muted).toBe(true);

    await fireEvent.press(screen.getByTestId('glide-share'));
    await waitFor(() => expect(mockStorage.shareImages).toHaveBeenCalledWith([clip.outputPath], 'Share video'));

    await fireEvent.press(screen.getByTestId('glide-save'));
    await waitFor(() => expect(mockStorage.saveVideoToGallery).toHaveBeenCalledWith(clip.outputPath));
    expect(await screen.findByText('Saved to your gallery')).toBeTruthy();
    expect(screen.getByText('Saved to gallery')).toBeTruthy();
  });

  it('moves the person into a new scene (Premium), true to its size, and shows the new clip', async () => {
    useAppStore.setState(st => ({ entitlements: { ...st.entitlements, plan: 'premium' } }));
    await wrap(
      <GlidePreviewScreen
        navigation={{ navigate: jest.fn(), goBack: jest.fn() } as never}
        route={{ key: 'g', name: 'GlidePreview', params: { clip } } as never}
      />,
    );
    await fireEvent.press(screen.getByTestId('glide-scene'));
    await fireEvent.press(screen.getByTestId('bg-tab-indoors'));
    await fireEvent.press(screen.getByTestId('bg-grand-hall'));
    expect(screen.getByText('Auto size: true to the scene.')).toBeTruthy();
    await fireEvent.press(screen.getByTestId('glide-scene-make'));
    await waitFor(() => expect(mockEngine.glide).toHaveBeenCalled());
    expect(mockEngine.glideSubject).toHaveBeenCalledWith(expect.any(String), { inputPath: clip.rawPath });
    const request = mockEngine.glide.mock.calls[0][1] as unknown as {
      inputPath: string;
      outputPath: string;
      scene: { background: object; box: number[]; placement: { height: number; y: number }; crop: { h: number } };
    };
    // From the recording, into the Grand Hall, at the person's usual box in the clip.
    expect(request.inputPath).toBe(clip.rawPath);
    expect(request.scene.background).toEqual({ kind: 'plate', id: 'grand-hall' });
    expect(request.scene.box).toEqual([0.3, 0.12, 0.7, 0.96]);
    expect(request.scene.placement.height).toBeGreaterThan(0.5);
    expect(await screen.findByText('You’re in the new scene. Saved to your gallery.')).toBeTruthy();
    expect(screen.getByText('In a new scene, steadied and cleaned')).toBeTruthy();
    expect(screen.getByTestId('video-view').props.source).toBe(request.outputPath);
    useAppStore.setState(st => ({ entitlements: { ...st.entitlements, plan: 'free' } }));
  });

  it('offers Premium for a new scene on the free plan', async () => {
    useAppStore.setState(st => ({ entitlements: { ...st.entitlements, plan: 'free' } }));
    await wrap(
      <GlidePreviewScreen
        navigation={{ navigate: jest.fn(), goBack: jest.fn() } as never}
        route={{ key: 'g', name: 'GlidePreview', params: { clip } } as never}
      />,
    );
    await fireEvent.press(screen.getByTestId('glide-scene'));
    expect(screen.queryByTestId('glide-scene-make')).toBeNull();
    expect(mockEngine.glideSubject).not.toHaveBeenCalled();
  });

  it('says so when a clip cannot be played', async () => {
    await wrap(
      <GlidePreviewScreen
        navigation={{ goBack: jest.fn() } as never}
        route={{ key: 'g', name: 'GlidePreview', params: { clip: { ...clip, galleryUri: 'content://x' } } } as never}
      />,
    );
    expect(screen.getByText('Saved to gallery')).toBeTruthy();
    await act(async () => {
      screen.getByTestId('video-view').props.onVideoError({ nativeEvent: { message: 'decoder' } });
    });
    expect(screen.getByText('This clip can’t be played.')).toBeTruthy();
  });
});
