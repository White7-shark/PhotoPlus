import { act, fireEvent, render, screen, waitFor } from '@testing-library/react-native';
import React from 'react';

import { CameraScreen } from '../src/screens/camera/CameraScreen';
import { ProPanel } from '../src/screens/camera/ProPanel';
import { StopDial } from '../src/screens/camera/StopDial';
import { clampSplit, CompareView } from '../src/screens/CompareView';
import { parseCameraCapabilities } from '../src/services/camera/parse';
import type { CameraCapabilities } from '../src/services/camera/types';
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';
import { rawCameraCaps } from './helpers/fixtures';

const mockCamera = {
  isSessionReady: jest.fn(async () => true),
  getCapabilities: jest.fn(async () => rawCameraCaps()),
  configure: jest.fn(),
  capturePhoto: jest.fn(async (o: { mode: string; captureId: string }) => ({
    path: `/captures/${o.captureId}.jpg`,
    width: 4032,
    height: 3024,
    sizeBytes: 1000,
    capturedAt: 1,
    mode: o.mode,
  })),
  cancelCapture: jest.fn(),
  setZoom: jest.fn(async (r: number) => r),
  setExposureCompensation: jest.fn(async (i: number) => i),
  focusAt: jest.fn(async () => true),
  cancelFocus: jest.fn(),
  setFlash: jest.fn(),
  setTorch: jest.fn(async () => true),
  setFocusPeaking: jest.fn(),
  setManual: jest.fn(async (s: object) => s),
  resetManual: jest.fn(),
  addListener: jest.fn(),
  removeListeners: jest.fn(),
};

jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  // A getter: the module is evaluated before mockCamera is initialised.
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get camera() {
        return mockCamera;
      },
    },
  };
});

jest.mock('../src/services/permissions/PermissionService', () => ({
  getPermissionService: () => ({
    ensure: async () => 'granted',
    request: async () => 'granted',
    openSettings: async () => undefined,
  }),
}));

jest.mock('@react-navigation/native', () => ({
  ...jest.requireActual('@react-navigation/native'),
  useIsFocused: () => true,
}));

const caps = parseCameraCapabilities(rawCameraCaps()) as CameraCapabilities;
const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);

beforeEach(() => {
  jest.clearAllMocks();
  useAppStore.setState({ repos: null, settings: { ...DEFAULT_SETTINGS }, isForeground: true });
});

async function renderReadyCamera() {
  const navigation = { navigate: jest.fn(), goBack: jest.fn() };
  await render(<CameraScreen navigation={navigation as never} route={{ key: 'c', name: 'Camera' } as never} />);
  // The codegen component renders as a host element; fire its ready event.
  const host = await screen.findByTestId('camera-preview');
  await act(async () => {
    host.props.onCameraReady({ nativeEvent: { lensFacing: 'back' } });
  });
  await waitFor(() => expect(mockCamera.getCapabilities).toHaveBeenCalled());
  await screen.findByTestId('mode-pro');
  return { navigation };
}

describe('camera screen', () => {
  it('configures the session from settings and offers every supported mode', async () => {
    await renderReadyCamera();
    expect(mockCamera.configure).toHaveBeenCalledWith({
      aspectRatio: '4:3',
      resolution: 'max',
      raw: false,
      video: false,
    });
    for (const id of ['night', 'enhance', 'photo', 'superres', 'glide', 'pro']) {
      expect(screen.getByTestId(`mode-${id}`)).toBeTruthy();
    }
    expect(screen.getByRole('radio', { name: 'Zoom 1×' }).props.accessibilityState.selected).toBe(true);
    expect(screen.getByRole('radio', { name: 'Zoom 0.6×' })).toBeTruthy();
  });

  it('captures in the selected mode with the flash setting', async () => {
    await renderReadyCamera();
    await fireEvent.press(screen.getByLabelText('Flash off. Double tap to change'));
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.capturePhoto).toHaveBeenCalledTimes(1));
    // The camera opens in Clarity, the free single-photo enhancement.
    expect(mockCamera.capturePhoto.mock.calls[0][0]).toMatchObject({
      mode: 'enhance',
      flash: 'auto',
      mirrorFront: true,
    });

    await fireEvent.press(screen.getByTestId('mode-night'));
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.capturePhoto).toHaveBeenCalledTimes(2));
    expect(mockCamera.capturePhoto.mock.calls[1][0]).toMatchObject({ mode: 'night' });
    await screen.findByText('Moonlight photo saved');
  });

  it('shows the Pro panel and applies manual controls only in Pro', async () => {
    await renderReadyCamera();
    expect(screen.queryByTestId('pro-panel')).toBeNull();
    await fireEvent.press(screen.getByTestId('mode-pro'));
    expect(screen.getByTestId('pro-panel')).toBeTruthy();
    await waitFor(() => expect(mockCamera.setManual).toHaveBeenCalledWith({}));
    await fireEvent.press(screen.getByTestId('mode-photo'));
    await waitFor(() => expect(mockCamera.resetManual).toHaveBeenCalled());
  });

  it('reports capture failures without crashing', async () => {
    mockCamera.capturePhoto.mockImplementationOnce(async () => {
      throw Object.assign(new Error('Camera is not ready'), { code: 'NOT_READY' });
    });
    await renderReadyCamera();
    await fireEvent.press(screen.getByTestId('shutter'));
    // People see a plain-language message, never the native one.
    expect(await screen.findByText('The camera is still getting ready. Try again in a moment.')).toBeTruthy();
    expect(screen.queryByText(/Camera is not ready/)).toBeNull();
  });
});

describe('pro panel', () => {
  const props = {
    caps,
    manual: {},
    metadata: { iso: 100, exposureTimeNs: 16_666_667, focusDiopters: 0.5, aperture: 1.8, focusLocked: true },
    sharpness: 0.5,
    raw: false,
    peaking: false,
    onRaw: jest.fn(),
    onPeaking: jest.fn(),
    onPreset: jest.fn(),
  };

  it('lists only supported control groups', async () => {
    await wrap(<ProPanel {...props} onManual={jest.fn()} />);
    for (const name of ['ISO', 'Shutter', 'Focus', 'WB', 'Assist', 'Presets']) {
      expect(screen.getByRole('tab', { name })).toBeTruthy();
    }
    const fixedFocus = parseCameraCapabilities(
      rawCameraCaps({ manual: { ...rawCameraCaps().manual, focus: false, minFocusDiopters: 0 } }),
    ) as CameraCapabilities;
    await wrap(<ProPanel {...props} caps={fixedFocus} onManual={jest.fn()} />);
    // `screen` now queries the fixed-focus panel.
    expect(screen.queryByRole('tab', { name: 'Focus' })).toBeNull();
    expect(screen.getByRole('tab', { name: 'ISO' })).toBeTruthy();
  });

  it('shows the live auto value and steps the dial accessibly', async () => {
    const onManual = jest.fn();
    await wrap(<ProPanel {...props} onManual={onManual} />);
    const dial = screen.getByRole('adjustable', { name: 'ISO' });
    expect(dial.props.accessibilityValue).toEqual({ text: 'Auto · ISO 100' });
    await fireEvent(dial, 'accessibilityAction', { nativeEvent: { actionName: 'increment' } });
    expect(onManual).toHaveBeenCalledWith({ iso: 800 }); // from the middle of the dial
  });

  it('raises ISO when a fast shutter is picked so exposure is kept', async () => {
    const onManual = jest.fn();
    await wrap(<ProPanel {...props} onManual={onManual} />);
    await fireEvent.press(screen.getByRole('tab', { name: 'Shutter' }));
    const dial = screen.getByRole('adjustable', { name: 'Shutter speed' });
    await fireEvent(dial, 'accessibilityAction', { nativeEvent: { actionName: 'decrement' } });
    const next = onManual.mock.calls[0][0];
    expect(next.exposureTimeNs).toBe(4_000_000); // 1/250
    expect(next.iso).toBe(417); // 100 × (1/60) / (1/250)
  });

  it('toggles RAW and focus peaking', async () => {
    await wrap(<ProPanel {...props} onManual={jest.fn()} />);
    await fireEvent.press(screen.getByRole('tab', { name: 'Assist' }));
    await fireEvent.press(screen.getByRole('switch', { name: 'RAW + JPEG' }));
    await fireEvent.press(screen.getByRole('switch', { name: 'Focus peaking' }));
    expect(props.onRaw).toHaveBeenCalledWith(true);
    expect(props.onPeaking).toHaveBeenCalledWith(true);
  });
});

describe('dials and compare', () => {
  it('StopDial reports its value and clamps actions at the ends', async () => {
    const onChange = jest.fn();
    await wrap(
      <StopDial label="Kelvin" values={[3000, 4000, 5000]} value={5000} format={v => `${v}K`} onChange={onChange} />,
    );
    const dial = screen.getByRole('adjustable', { name: 'Kelvin' });
    expect(dial.props.accessibilityValue).toEqual({ text: '5000K' });
    await fireEvent(dial, 'accessibilityAction', { nativeEvent: { actionName: 'increment' } });
    expect(onChange).not.toHaveBeenCalled();
    await fireEvent(dial, 'accessibilityAction', { nativeEvent: { actionName: 'decrement' } });
    expect(onChange).toHaveBeenCalledWith(4000);
  });

  it('CompareView is an adjustable control', async () => {
    await wrap(<CompareView beforeUri="file:///a.jpg" afterUri="file:///b.jpg" aspect={0.75} />);
    const cmp = screen.getByRole('adjustable', { name: 'Before and after comparison' });
    expect(cmp.props.accessibilityValue).toEqual({ text: '50 percent original' });
    await fireEvent(cmp, 'accessibilityAction', { nativeEvent: { actionName: 'increment' } });
    expect(screen.getByRole('adjustable', { name: 'Before and after comparison' }).props.accessibilityValue).toEqual({
      text: '60 percent original',
    });
    expect(clampSplit(-1)).toBe(0.02);
    expect(clampSplit(2)).toBe(0.98);
  });
});

describe('camera session state', () => {
  it('re-selecting the current aspect ratio keeps the camera ready', async () => {
    await renderReadyCamera();
    await fireEvent.press(screen.getByLabelText('Aspect ratio 4:3'));
    const chips = screen.getAllByLabelText('Aspect ratio 4:3');
    await fireEvent.press(chips[chips.length - 1]); // the menu entry
    expect(screen.getByTestId('shutter').props.accessibilityState.disabled).toBe(false);
    expect(screen.queryByText('Starting camera…')).toBeNull();
  });

  it('choosing a new aspect ratio waits for the rebind', async () => {
    await renderReadyCamera();
    await fireEvent.press(screen.getByLabelText('Aspect ratio 4:3'));
    await fireEvent.press(screen.getByLabelText('Aspect ratio 16:9'));
    expect(screen.getByTestId('shutter').props.accessibilityState.disabled).toBe(true);
    await waitFor(() =>
      expect(mockCamera.configure).toHaveBeenLastCalledWith({
        aspectRatio: '16:9',
        resolution: 'max',
        raw: false,
        video: false,
      }),
    );
  });
});

describe('spotlight and vibe match', () => {
  it('taps choose what stands out and travel with the capture', async () => {
    await renderReadyCamera();
    await fireEvent.press(screen.getByTestId('mode-spotlight'));
    expect(screen.getByTestId('spotlight-panel')).toBeTruthy();
    const host = screen.getByTestId('camera-preview');
    await act(async () => {
      host.props.onFocusTap({ nativeEvent: { x: 0.3, y: 0.4 } });
    });
    await act(async () => {
      host.props.onFocusTap({ nativeEvent: { x: 0.7, y: 0.6 } });
    });
    // A second tap on a marker removes it.
    await act(async () => {
      host.props.onFocusTap({ nativeEvent: { x: 0.71, y: 0.61 } });
    });
    expect(screen.getByText('1 chosen · tap again to remove')).toBeTruthy();
    await fireEvent.press(screen.getByRole('radio', { name: 'Flawless' }));
    await fireEvent.press(screen.getByRole('radio', { name: 'Strong' }));
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.capturePhoto).toHaveBeenCalled());
    expect(mockCamera.capturePhoto.mock.calls[0][0]).toMatchObject({
      mode: 'spotlight',
      points: [0.3, 0.4],
      blur: 0.9,
      retouch: 1,
    });
    expect(await screen.findByText('Spotlight photo saved')).toBeTruthy();
  });

  it('a Vibe Match look shapes the camera and every shot', async () => {
    const warmBright = [
      0.08, 0.15, 0.3, 0.45, 0.62, 0.75, 0.85, 0.93, 0.98, -0.05, -0.04, -0.06, 0.05, 0.08, 0.06, 0.05, 0.06, 0.8, 0.01,
      0.6,
    ];
    useAppStore.setState(st => ({
      settings: { ...st.settings, vibeLook: { referencePath: '/looks/ref.jpg', look: warmBright, strength: 0.8 } },
    }));
    await renderReadyCamera();
    // Warm reference -> warmer white balance (Kelvin on this camera); brighter -> positive exposure.
    await waitFor(() =>
      expect(mockCamera.setManual).toHaveBeenCalledWith(expect.objectContaining({ whiteBalance: 'kelvin' })),
    );
    const wb = mockCamera.setManual.mock.calls.at(-1)?.[0] as { kelvin: number };
    expect(wb.kelvin).toBeGreaterThan(5500);
    await waitFor(() => expect(mockCamera.setExposureCompensation).toHaveBeenCalled());
    expect(mockCamera.setExposureCompensation.mock.calls.at(-1)?.[0]).toBeGreaterThan(0);
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.capturePhoto).toHaveBeenCalled());
    expect(mockCamera.capturePhoto.mock.calls[0][0]).toMatchObject({ look: warmBright, lookStrength: 0.8 });
    expect(await screen.findByText('Vibe Match · Clarity photo saved')).toBeTruthy();
    expect(screen.getByLabelText('Vibe Match on. Change style')).toBeTruthy();
  });

  it('a Spotlight + Vibe Match shot records how it was made, so Transform can re-make it', async () => {
    const look = [
      0.1, 0.2, 0.35, 0.5, 0.7, 0.8, 0.88, 0.94, 0.98, 0, 0, 0, 0.02, 0.02, 0.02, 0.06, 0.06, 0.9, 0.004, 0.6,
    ];
    useAppStore.setState(st => ({
      settings: { ...st.settings, vibeLook: { referencePath: '/looks/ref.jpg', look, strength: 0.6 } },
    }));
    const added: { metadata: Record<string, unknown> }[] = [];
    useAppStore.setState({
      repos: {
        projects: { ensureInbox: jest.fn(async () => ({ id: 'inbox' })) },
        images: {
          add: jest.fn(async (img: { metadata: Record<string, unknown> }) => {
            added.push(img);
            return { ...img, id: 'img_9' };
          }),
        },
      } as never,
      loadProjects: jest.fn(async () => undefined),
    });
    // Native reports the taps it used in the upright photo (here: phone turned).
    mockCamera.capturePhoto.mockImplementationOnce(async (o: { mode: string; captureId: string }) => ({
      path: `/captures/${o.captureId}.jpg`,
      width: 4032,
      height: 3024,
      sizeBytes: 1000,
      capturedAt: 1,
      mode: o.mode,
      look: true,
      originalPath: `/captures/${o.captureId}_original.jpg`,
      cleanPath: `/captures/${o.captureId}_clean.jpg`,
      portrait: { people: 1, spots: 0, skin: 0.02, subject: 0.3 },
      spotlight: { points: [0.6, 0.3], blur: 0.6, retouch: 0.75, brighten: 0.4, pop: 0.5 },
    }));
    await renderReadyCamera();
    await fireEvent.press(screen.getByTestId('mode-spotlight'));
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(added).toHaveLength(1));
    const id = (mockCamera.capturePhoto.mock.calls[0][0] as { captureId: string }).captureId;
    // Free-plan photos carry the chosen watermark (signature, bottom right); the clean copy is kept.
    expect(mockCamera.capturePhoto.mock.calls[0][0]).toMatchObject({
      watermark: expect.objectContaining({ style: 'signature', position: 'br' }),
    });
    expect(added[0].metadata.cleanPath).toBe(`/captures/${id}_clean.jpg`);
    expect(added[0].metadata.recipe).toEqual({
      basePath: `/captures/${id}_original.jpg`,
      baseStyled: false,
      steps: [
        { kind: 'spotlight', points: [0.6, 0.3], blur: 0.6, retouch: 0.75, brighten: 0.4, pop: 0.5 },
        { kind: 'look', look, strength: 0.6 },
      ],
    });
  });

  it('turning Vibe Match off brings exposure back to neutral', async () => {
    const bright = [
      0.1, 0.2, 0.35, 0.5, 0.7, 0.8, 0.88, 0.94, 0.98, 0, 0, 0, 0.02, 0.02, 0.02, 0.06, 0.06, 0.9, 0.004, 0.6,
    ];
    useAppStore.setState(st => ({
      settings: { ...st.settings, vibeLook: { referencePath: '/looks/ref.jpg', look: bright, strength: 0.8 } },
    }));
    await renderReadyCamera();
    await waitFor(() => expect(mockCamera.setExposureCompensation).toHaveBeenCalled());
    expect(mockCamera.setExposureCompensation.mock.calls.at(-1)?.[0]).toBeGreaterThan(0);
    await act(async () => {
      useAppStore.setState(st => ({ settings: { ...st.settings, vibeLook: null } }));
    });
    await waitFor(() => expect(mockCamera.setExposureCompensation).toHaveBeenLastCalledWith(0));
  });

  it('the Vibe Match chip explains the feature when no look is set', async () => {
    await renderReadyCamera();
    await fireEvent.press(screen.getByTestId('vibe-chip'));
    expect(await screen.findByRole('button', { name: 'Choose a style photo' })).toBeTruthy();
  });
});

describe('camera beauty (Glow outside Spotlight)', () => {
  const cleanups: (() => void)[] = [];
  const afterTest = (fn: () => void) => cleanups.push(fn);
  afterEach(() => cleanups.splice(0).forEach(fn => fn()));

  it('a Glow level travels with every photo and is remembered; Spotlight keeps its own', async () => {
    const updateSetting = jest.fn(async () => undefined);
    const real = useAppStore.getState().updateSetting;
    afterTest(() => useAppStore.setState({ updateSetting: real }));
    useAppStore.setState({
      settings: { ...DEFAULT_SETTINGS, cameraGlow: 'smooth' },
      updateSetting: updateSetting as never,
    });
    await renderReadyCamera();
    // Tapping the chip moves to the next level and saves it.
    await fireEvent.press(screen.getByTestId('glow-chip'));
    expect(updateSetting).toHaveBeenCalledWith('cameraGlow', 'flawless');
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.capturePhoto).toHaveBeenCalledTimes(1));
    expect(mockCamera.capturePhoto.mock.calls[0][0]).toMatchObject({
      beauty: { retouch: 0.75, brighten: 0.4, eyes: 0.3, underEye: 0.45, shine: 0.35, faceLight: 0.2 },
    });
    // Spotlight has its own Glow: no camera beauty there.
    await fireEvent.press(screen.getByTestId('mode-spotlight'));
    expect(screen.queryByTestId('glow-chip')).toBeNull();
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.capturePhoto).toHaveBeenCalledTimes(2));
    expect(mockCamera.capturePhoto.mock.calls[1][0]).not.toHaveProperty('beauty');
  });

  it('with Glow off, photos are taken as they are', async () => {
    await renderReadyCamera();
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.capturePhoto).toHaveBeenCalledTimes(1));
    expect(mockCamera.capturePhoto.mock.calls[0][0]).not.toHaveProperty('beauty');
    expect(screen.getByLabelText('Glow off. Turn on beauty for faces')).toBeTruthy();
  });
});
