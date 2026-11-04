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
    expect(mockCamera.configure).toHaveBeenCalledWith({ aspectRatio: '4:3', resolution: 'max', raw: false });
    for (const id of ['night', 'enhance', 'photo', 'superres', 'pro']) {
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
    expect(mockCamera.capturePhoto.mock.calls[0][0]).toMatchObject({
      mode: 'standard',
      flash: 'auto',
      mirrorFront: true,
    });

    await fireEvent.press(screen.getByTestId('mode-night'));
    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.capturePhoto).toHaveBeenCalledTimes(2));
    expect(mockCamera.capturePhoto.mock.calls[1][0]).toMatchObject({ mode: 'night' });
    await screen.findByText('Enhanced photo saved');
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
    expect(await screen.findByText('Capture failed: Camera is not ready')).toBeTruthy();
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
