import { act, fireEvent, render, screen, waitFor } from '@testing-library/react-native';
import React from 'react';
import { DeviceEventEmitter } from 'react-native';

import { CameraScreen } from '../src/screens/camera/CameraScreen';
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { findPose } from '../src/services/pose/library';
import { DEFAULT_POSE_ASSISTANT } from '../src/services/pose/settings';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';
import { rawCameraCaps } from './helpers/fixtures';

const chin = findPose('chin-rest')!;

const mockCamera = {
  isSessionReady: jest.fn(async () => true),
  setLiveBeauty: jest.fn(),
  setBeautyOriginal: jest.fn(),
  getCapabilities: jest.fn(async () => rawCameraCaps()),
  configure: jest.fn(),
  capturePhoto: jest.fn(async (o: { mode: string; captureId: string }) => ({
    path: `/captures/${o.captureId}.jpg`,
    width: 3000,
    height: 4000,
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
  setFaceTracking: jest.fn(),
  setPoseGuide: jest.fn(),
  setLevel: jest.fn(),
  analyzePosePhoto: jest.fn(async () => ({
    valid: true,
    aspect: 0.75,
    landmarks: (chin.landmarks as number[][]).flat(),
    shot: 'headAndShoulders',
    unit: chin.unit,
  })),
  setManual: jest.fn(async (s: object) => s),
  resetManual: jest.fn(),
  addListener: jest.fn(),
  removeListeners: jest.fn(),
};
const mockStorage = { pickImage: jest.fn(async () => '/data/poses/me.jpg') };

jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get camera() {
        return mockCamera;
      },
      get storage() {
        return mockStorage;
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

async function openCamera() {
  const navigation = { navigate: jest.fn(), goBack: jest.fn() };
  await render(
    <ThemeProvider preference="dark">
      <CameraScreen navigation={navigation as never} route={{ key: 'c', name: 'Camera' } as never} />
    </ThemeProvider>,
  );
  const host = await screen.findByTestId('camera-preview');
  await act(async () => {
    host.props.onCameraReady({ nativeEvent: { lensFacing: 'front' } });
  });
  await screen.findByTestId('mode-pro');
}

function livePose(score: number, still = 0.9) {
  return {
    person: true,
    mirrored: true,
    stillness: still,
    shot: 'headAndShoulders',
    anchor: [0.5, 0.6],
    unit: 0.3,
    headTilt: 0,
    face: [0.5, 0.35],
    points: chin.landmarks.map(p => [p[0], p[1], 0.9]),
    match: {
      score,
      coverage: 0.9,
      pose: score,
      framing: 90,
      offset: [0, 0],
      size: 1,
      bones: Array(12).fill(0.9),
      hands: [0.9, 0.9],
      advice: score >= 85 ? [] : [{ part: 'rightHand', action: 'up', amount: 0.8 }],
    },
  };
}

beforeEach(() => {
  jest.clearAllMocks();
  useAppStore.setState({ repos: null, settings: { ...DEFAULT_SETTINGS }, isForeground: true });
});

describe('photo assistant in the camera', () => {
  it('opens from the camera and sets the grid and the level', async () => {
    await openCamera();
    fireEvent.press(await screen.findByTestId('assistant-chip'));
    expect(await screen.findByText('Photo assistant')).toBeTruthy();
    fireEvent.press(screen.getByText('Golden'));
    await waitFor(() => expect(useAppStore.getState().settings.cameraGridStyle).toBe('golden'));
    expect(useAppStore.getState().settings.cameraGrid).toBe(true);
    fireEvent(screen.getByLabelText('Level'), 'valueChange', true);
    await waitFor(() => expect(useAppStore.getState().settings.poseAssistant.level).toBe(true));
  });

  it('follows a chosen pose: the reference goes to the camera and the coach speaks plainly', async () => {
    useAppStore.setState({
      settings: { ...DEFAULT_SETTINGS, poseAssistant: { ...DEFAULT_POSE_ASSISTANT, poseId: 'chin-rest', guided: false } },
    });
    await openCamera();
    await waitFor(() =>
      expect(mockCamera.setPoseGuide).toHaveBeenLastCalledWith(
        expect.objectContaining({ reference: expect.objectContaining({ aspect: chin.aspect }) }),
      ),
    );
    const last = mockCamera.setPoseGuide.mock.calls.at(-1)?.[0] as { reference: { landmarks: unknown[] } };
    expect(last.reference.landmarks).toHaveLength(33);
    await act(async () => {
      DeviceEventEmitter.emit('PhotoPlusPose', livePose(62));
    });
    // Mirrored selfie: the model's "right hand" is the user's left.
    expect(await screen.findByText('Raise your left hand')).toBeTruthy();
    expect(screen.getByText('62')).toBeTruthy();
  });

  it('takes the photo by itself once the pose has held', async () => {
    useAppStore.setState({
      settings: {
        ...DEFAULT_SETTINGS,
        poseAssistant: { ...DEFAULT_POSE_ASSISTANT, poseId: 'chin-rest', autoCapture: true, threshold: 80, holdMs: 300 },
      },
    });
    let now = 1_000_000;
    const spy = jest.spyOn(Date, 'now').mockImplementation(() => now);
    try {
      await openCamera();
      await waitFor(() => expect(mockCamera.setPoseGuide).toHaveBeenCalled());
      for (const t of [0, 150, 320]) {
        now = 1_000_000 + t;
        await act(async () => {
          DeviceEventEmitter.emit('PhotoPlusPose', livePose(92));
        });
      }
      await waitFor(() => expect(mockCamera.capturePhoto).toHaveBeenCalledTimes(1));
    } finally {
      spy.mockRestore();
    }
  });

  it('turns a photo into a pose to follow', async () => {
    await openCamera();
    fireEvent.press(await screen.findByTestId('assistant-chip'));
    fireEvent.press(await screen.findByLabelText('Use a pose from my photo'));
    await waitFor(() => expect(useAppStore.getState().settings.myPoses).toHaveLength(1));
    expect(mockStorage.pickImage).toHaveBeenCalledWith('poses');
    expect(mockCamera.analyzePosePhoto).toHaveBeenCalledWith('/data/poses/me.jpg');
    const mine = useAppStore.getState().settings.myPoses[0];
    expect(useAppStore.getState().settings.poseAssistant.poseId).toBe(mine.id);
    expect(await screen.findByText('My pose 1')).toBeTruthy();
  });
});
