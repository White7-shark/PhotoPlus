import { act, fireEvent, render, screen, waitFor } from '@testing-library/react-native';
import React from 'react';
import { DeviceEventEmitter, Image } from 'react-native';

import { CameraScreen } from '../src/screens/camera/CameraScreen';
import {
  dragAdjust,
  livePlacement,
  NO_ADJUST,
  propRequest,
  smoothFace,
  type Frame,
} from '../src/screens/camera/CameraProps';
import { CreatePropScreen } from '../src/screens/CreatePropScreen';
import { WatermarkUnlock } from '../src/screens/WatermarkUnlock';
import { DEFAULT_SETTINGS, isValidSetting } from '../src/services/database/repositories/SettingsRepository';
import {
  allProps,
  customPropDef,
  FRAMES,
  findProp,
  isCustomProps,
  PROPS,
  type CustomProp,
} from '../src/services/edit/catalog';
import { fitToFace, type FaceGeometry } from '../src/services/edit/placement';
import { DEFAULT_WATERMARK } from '../src/services/edit/watermark';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';
import { rawCameraCaps } from './helpers/fixtures';

// Node's fs/path, loaded through Jest (the app's types don't include Node's).
const { readFileSync, readdirSync } = jest.requireActual<{
  readFileSync: (path: string, encoding: 'utf8') => string;
  readdirSync: (path: string) => string[];
}>('fs');
const { join } = jest.requireActual<{ join: (...parts: string[]) => string }>('path');
const root = jest.requireActual<{ cwd: () => string }>('process').cwd();
const frame: Frame = { width: 400, height: 533 };
const face: FaceGeometry = {
  top: { x: 0.5, y: 0.25 },
  chin: { x: 0.5, y: 0.55 },
  eyeL: { x: 0.43, y: 0.36 },
  eyeR: { x: 0.57, y: 0.36 },
  width: 0.32,
};
const fedora = PROPS.find(p => p.id === 'fedora')!;

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
  setManual: jest.fn(async (s: object) => s),
  resetManual: jest.fn(),
  addListener: jest.fn(),
  removeListeners: jest.fn(),
};
const mockAds = {
  initialize: jest.fn(async () => ({ canRequestAds: true, privacyOptionsRequired: false })),
  getRewardProgress: jest.fn(async () => ({ watched: 3, required: 4, unlocked: false })),
  showRewardedAd: jest.fn(async () => ({ watched: 4, required: 4, unlocked: true, rewarded: true })),
  privacyOptionsRequired: jest.fn(async () => false),
  showPrivacyOptions: jest.fn(async () => true),
};
const mockEngine = {
  createCustomProp: jest.fn(async () => ({
    id: 'custom-abc123def456',
    path: '/data/props/custom-abc123def456.png',
    width: 400,
    height: 300,
  })),
  deleteCustomProp: jest.fn(async () => true),
  frameThumbnails: jest.fn(async () => ({ thumbnails: {} })),
  cancel: jest.fn(),
};
const mockStorage = {
  pickImage: jest.fn(async () => '/data/imports/hat.jpg'),
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
      get ads() {
        return mockAds;
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

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);

beforeEach(() => {
  jest.clearAllMocks();
  useAppStore.setState({ repos: null, settings: { ...DEFAULT_SETTINGS }, isForeground: true });
});

// ---- the catalogue matches the native side and the bundled art --------------------

describe('catalogue', () => {
  it('has 20+ props and 20+ frames, each with artwork and a native counterpart', () => {
    expect(PROPS.length).toBeGreaterThanOrEqual(20);
    expect(FRAMES.length).toBeGreaterThanOrEqual(20);
    expect(new Set(PROPS.map(p => p.id)).size).toBe(PROPS.length);
    expect(new Set(FRAMES.map(f => f.id)).size).toBe(FRAMES.length);
    const art = readdirSync(join(root, 'android/app/src/main/assets/props'));
    const kotlinDoc = readFileSync(join(root, 'android/app/src/main/java/com/photoplus/edit/EditDocument.kt'), 'utf8');
    const kotlinFrames = readFileSync(
      join(root, 'android/app/src/main/java/com/photoplus/edit/FrameStyles.kt'),
      'utf8',
    );
    for (const p of PROPS) {
      expect(art).toContain(`${p.id}.png`);
      expect(kotlinDoc).toContain(`"${p.id}"`);
    }
    for (const f of FRAMES) expect(kotlinFrames).toContain(`FrameStyle("${f.id}"`);
    // Free and premium in every category.
    for (const cat of ['hats', 'glasses', 'outfit'] as const) {
      expect(PROPS.some(p => p.category === cat && !p.premium)).toBe(true);
      expect(PROPS.some(p => p.category === cat && p.premium)).toBe(true);
    }
    expect(FRAMES.filter(f => !f.premium).length).toBeGreaterThanOrEqual(10);
  });

  it('turns props people made into props that fit faces', () => {
    const made: CustomProp = {
      id: 'custom-abc123def456',
      name: 'Grandpa’s hat',
      anchor: 'head',
      path: '/data/props/custom-abc123def456.png',
      aspect: 0.75,
      createdAt: 1,
    };
    expect(isCustomProps([made])).toBe(true);
    expect(isValidSetting('customProps', [made])).toBe(true);
    expect(isCustomProps([{ ...made, id: '../../etc' }])).toBe(false);
    expect(isCustomProps([{ ...made, anchor: 'tail' }])).toBe(false);
    const def = customPropDef(made);
    expect(def).toMatchObject({ category: 'mine', premium: true, anchor: 'head', uri: `file://${made.path}` });
    expect(allProps([made]).length).toBe(PROPS.length + 1);
    expect(findProp(made.id, [made])?.name).toBe('Grandpa’s hat');
    expect(findProp('fedora')?.name).toBe('Fedora');
    // A hat-type made prop sits on the head like a bundled hat.
    const p = fitToFace(def, face, 0.75);
    expect(p.cy).toBeLessThan(face.top.y + 0.05);
    // "Anywhere" starts on the face.
    const free = fitToFace(customPropDef({ ...made, anchor: 'free' }), face, 0.75);
    expect(free.cy).toBeCloseTo((face.top.y + face.chin.y) / 2, 2);
  });

  it('defaults to the brand mark at the reference size', () => {
    expect(DEFAULT_WATERMARK).toMatchObject({ style: 'signature', position: 'br', size: 0.78 });
  });
});

// ---- live props geometry (mirrors CameraProps.kt / PropFit.kt) ---------------------

describe('live props', () => {
  it('fits the prop to the face, then keeps the person’s adjustment as the face moves', () => {
    const fitted = livePlacement(fedora, face, frame, NO_ADJUST, null);
    const base = fitToFace(fedora, face, frame.width / frame.height);
    expect(fitted.cx).toBeCloseTo(base.cx * frame.width, 5);
    expect(fitted.width).toBeCloseTo(base.width * frame.width, 5);
    // Drag 20 px down and to the right: stored along the face, so it follows it.
    const adj = dragAdjust(NO_ADJUST, face, frame, 20, 20);
    const moved = livePlacement(fedora, face, frame, adj, null);
    expect(moved.cx - fitted.cx).toBeCloseTo(20, 4);
    expect(moved.cy - fitted.cy).toBeCloseTo(20, 4);
    const shifted: FaceGeometry = {
      ...face,
      top: { x: 0.6, y: 0.25 },
      chin: { x: 0.6, y: 0.55 },
      eyeL: { x: 0.53, y: 0.36 },
      eyeR: { x: 0.67, y: 0.36 },
    };
    const followed = livePlacement(fedora, shifted, frame, adj, null);
    expect(followed.cx - moved.cx).toBeCloseTo(0.1 * frame.width, 3);
  });

  it('without a face, the prop stays where it was put', () => {
    const free = { cx: 100, cy: 120, width: 150, rotation: 5 };
    expect(livePlacement(fedora, null, frame, NO_ADJUST, free)).toEqual(free);
    expect(livePlacement(fedora, null, frame, NO_ADJUST, null).cx).toBe(frame.width / 2);
  });

  it('smooths jitter but jumps to a different face', () => {
    const near = { ...face, top: { x: 0.52, y: 0.25 } };
    expect(smoothFace(face, near, 0.5).top.x).toBeCloseTo(0.51, 6);
    const far = { ...face, top: { x: 0.95, y: 0.9 } };
    expect(smoothFace(face, far)).toBe(far);
    expect(smoothFace(null, face)).toBe(face);
  });

  it('asks the camera to fit the prop to the face in the photo', () => {
    const at = livePlacement(fedora, face, frame, { du: 0.1, dv: 0, scale: 1.2, turn: 3 }, null);
    const r = propRequest(fedora, at, frame, face, { du: 0.1, dv: 0, scale: 1.2, turn: 3 });
    expect(r).toMatchObject({ id: 'fedora', anchor: 'head', tracked: true, du: 0.1, scale: 1.2, turn: 3, flip: false });
    expect(r.cx).toBeCloseTo(at.cx / frame.width, 6);
    const untracked = propRequest(fedora, at, frame, null, NO_ADJUST);
    expect(untracked.tracked).toBe(false);
    expect(untracked.du).toBeUndefined();
  });
});

// ---- camera: props tray, live prop, capture -------------------------------------------

async function renderReadyCamera() {
  const navigation = { navigate: jest.fn(), goBack: jest.fn() };
  await render(<CameraScreen navigation={navigation as never} route={{ key: 'c', name: 'Camera' } as never} />);
  const host = await screen.findByTestId('camera-preview');
  await act(async () => {
    host.props.onCameraReady({ nativeEvent: { lensFacing: 'back' } });
  });
  await screen.findByTestId('mode-pro');
  return { navigation };
}

describe('props in the camera', () => {
  it('shows previews, follows the face and puts the prop into the photo', async () => {
    const { navigation } = await renderReadyCamera();
    await fireEvent.press(screen.getByTestId('props-chip'));
    await screen.findByTestId('prop-tray');
    // Every hat has a preview tile; choose one.
    for (const p of PROPS.filter(d => d.category === 'hats')) {
      expect(screen.getByTestId(`camera-prop-${p.id}`)).toBeTruthy();
    }
    await fireEvent.press(screen.getByTestId('camera-prop-fedora'));
    await waitFor(() => expect(mockCamera.setFaceTracking).toHaveBeenCalledWith(true));
    expect(screen.getByTestId('live-prop')).toBeTruthy();

    // A face arrives from the camera: the prop moves onto it.
    const before = screen.getByTestId('live-prop').props.style.left;
    await act(async () => {
      DeviceEventEmitter.emit('PhotoPlusCameraFaces', { faces: [face] });
    });
    await waitFor(() => expect(screen.getByTestId('live-prop').props.style.left).not.toBe(before));

    await fireEvent.press(screen.getByTestId('shutter'));
    await waitFor(() => expect(mockCamera.capturePhoto).toHaveBeenCalledTimes(1));
    const props = (mockCamera.capturePhoto.mock.calls[0][0] as { props?: unknown[] }).props;
    expect(props).toHaveLength(1);
    expect(props?.[0]).toMatchObject({ id: 'fedora', anchor: 'head', tracked: true });

    // Taking the prop off stops tracking.
    await fireEvent.press(screen.getByLabelText('None'));
    await waitFor(() => expect(mockCamera.setFaceTracking).toHaveBeenLastCalledWith(false));
    expect(screen.queryByTestId('live-prop')).toBeNull();
    expect(navigation.navigate).not.toHaveBeenCalled();
  });

  it('offers to edit the photo just taken', async () => {
    useAppStore.setState({
      repos: {
        projects: { list: async () => [], ensureInbox: async () => ({ id: 'prj_1' }) },
        images: { add: async () => ({ id: 'img_9' }), list: async () => [] },
      } as never,
    });
    const { navigation } = await renderReadyCamera();
    await fireEvent.press(screen.getByTestId('shutter'));
    const edit = await screen.findByTestId('edit-last-shot');
    await fireEvent.press(edit);
    expect(navigation.navigate).toHaveBeenCalledWith('Editor', { imageId: 'img_9' });
  });
});

// ---- watching ads for a watermark-free photo -----------------------------------------------

describe('watermark-free unlock', () => {
  it('shows progress, and unlocks the photo after the fourth ad', async () => {
    const onUnlocked = jest.fn();
    await wrap(<WatermarkUnlock photoPath="/data/captures/PP_1_clean.jpg" onUnlocked={onUnlocked} />);
    await screen.findByText('Remove the watermark');
    expect(screen.getByLabelText('3 of 4 ads watched')).toBeTruthy();
    await fireEvent.press(screen.getByTestId('watermark-unlock-watch'));
    await screen.findByText('This photo goes out without the watermark.');
    expect(mockAds.showRewardedAd).toHaveBeenCalledWith('/data/captures/PP_1_clean.jpg');
    expect(onUnlocked).toHaveBeenCalled();
  });

  it('only counts ads watched to the end', async () => {
    mockAds.getRewardProgress.mockResolvedValueOnce({ watched: 0, required: 4, unlocked: false });
    mockAds.showRewardedAd.mockResolvedValueOnce({ watched: 0, required: 4, unlocked: false, rewarded: false });
    await wrap(<WatermarkUnlock photoPath="/data/captures/PP_2.jpg" />);
    await fireEvent.press(await screen.findByTestId('watermark-unlock-watch'));
    expect(await screen.findByText('Watch the ad to the end for it to count.')).toBeTruthy();
    expect(screen.getByLabelText('0 of 4 ads watched')).toBeTruthy();
  });
});

// ---- making a prop ---------------------------------------------------------------------------

describe('make a prop', () => {
  it('cuts out the tapped object and saves it to the library', async () => {
    jest.spyOn(Image, 'getSize').mockImplementation((_uri, ok) => ok?.(1200, 1600));
    const updateSetting = jest.fn(async () => undefined);
    useAppStore.setState({ updateSetting } as never);
    const navigation = { navigate: jest.fn(), goBack: jest.fn() };
    await wrap(<CreatePropScreen navigation={navigation as never} route={{ key: 'p', name: 'CreateProp' } as never} />);
    await fireEvent.press(screen.getByTestId('create-prop-pick'));
    const photo = await screen.findByTestId('create-prop-photo');
    await act(async () => {
      photo.props.onLayout({ nativeEvent: { layout: { width: 360, height: 400 } } });
    });
    await act(async () => {
      photo.props.onResponderRelease({ nativeEvent: { locationX: 180, locationY: 200 } });
    });
    await fireEvent.press(screen.getByTestId('create-prop-cut'));
    await waitFor(() => expect(mockEngine.createCustomProp).toHaveBeenCalled());
    const req = (mockEngine.createCustomProp.mock.calls[0] as unknown[])[1] as { inputPath: string; points: number[] };
    expect(req.inputPath).toBe('/data/imports/hat.jpg');
    expect(req.points[0]).toBeCloseTo(0.5, 2);
    await fireEvent.press(await screen.findByTestId('create-prop-save'));
    await waitFor(() => expect(updateSetting).toHaveBeenCalled());
    const saved = (updateSetting.mock.calls[0] as unknown[])[1] as CustomProp[];
    expect(saved[0]).toMatchObject({ id: 'custom-abc123def456', anchor: 'head', aspect: 0.75 });
    expect(navigation.goBack).toHaveBeenCalled();
  });
});
