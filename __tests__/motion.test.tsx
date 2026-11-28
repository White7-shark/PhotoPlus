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
import { FEATURES, FREE_DAILY_ALLOWANCE } from '../src/services/entitlements/features';
import {
  CAMERAS,
  DEFAULT_SETTINGS,
  EFFECTS,
  MAX_ARROWS,
  cameraName,
  clampSettings,
  effectName,
  nearest,
  packArrows,
  packPins,
  strokeToArrows,
} from '../src/services/motion/catalog';
import { MotionError, MotionService, UNKNOWN_ALLOWANCE, readAllowance } from '../src/services/motion/MotionService';
import { MotionScreen } from '../src/screens/studio/MotionScreen';
import { PhotoshootStudioScreen, STUDIO_TOOLS } from '../src/screens/studio/PhotoshootStudioScreen';
import { TOOL_TEXT } from '../src/services/rewards/RewardsService';
import { ThemeProvider } from '../src/theme';

// The device's count of today's free videos (MotionAllowance.kt), as the mocks play it.
const mockDay = { premium: false, remaining: 2, credits: 0, adFinishes: true };
const mockState = () =>
  mockDay.premium
    ? { remaining: -1, limit: 2, credits: 0, needsAd: false }
    : {
        remaining: mockDay.remaining,
        limit: 2,
        credits: mockDay.credits,
        needsAd: mockDay.remaining > 0 && mockDay.credits === 0,
      };
let mockRenders = 0;
const coded = (code: string) => Object.assign(new Error(code), { code });
const mockStudio = {
  startMotion: jest.fn(async (_id: string, _req: { inputPath: string }) => ({
    sessionId: 'motion_1',
    width: 800,
    height: 1000,
    sky: 0.2,
    depth: true,
    faces: 1,
  })),
  renderMotion: jest.fn(async (_id: string, req: Record<string, unknown>) => {
    if (req.quality === 'full' && !mockDay.premium) {
      if (mockDay.remaining <= 0) throw coded('PREMIUM_REQUIRED');
      if (mockDay.credits <= 0) throw coded('AD_REQUIRED');
      mockDay.remaining -= 1;
      mockDay.credits -= 1;
    }
    mockRenders += 1;
    const n = mockRenders;
    return {
      outputPath: `/cache/motion_${String(req.quality)}_${n}.mp4`,
      posterPath: `/cache/motion_${String(req.quality)}_${n}_poster.jpg`,
      width: req.quality === 'full' ? 1072 : 480,
      height: req.quality === 'full' ? 1344 : 608,
      durationMs: (req.duration as number) * 1000,
      remaining: mockState().remaining,
    };
  }),
  endMotion: jest.fn(),
  motionAllowance: jest.fn(async () => mockState()),
  cancel: jest.fn(),
};
const mockAds = {
  initialize: jest.fn(async () => ({ canRequestAds: true })),
  showRewardedAdForTool: jest.fn(async (_tool: string) => {
    if (mockDay.remaining <= 0) throw coded('DAILY_LIMIT');
    if (!mockDay.adFinishes) return { rewarded: false, allowance: mockState() };
    mockDay.credits += 1;
    return { rewarded: true, allowance: mockState() };
  }),
};
const mockStorage = {
  pickImage: jest.fn(async () => '/files/imports/lake.jpg'),
  shareImages: jest.fn(async () => true),
  saveVideoToGallery: jest.fn(async () => 'content://gallery/video/1'),
};
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

// The screen's flows wait for each preview's short pause.
jest.setTimeout(60000);

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const wait = (ms: number) => new Promise<void>(r => setTimeout(() => r(), ms));
// Lets promises settle and the preview's short pause pass.
const step = async (fn: () => unknown) =>
  act(async () => {
    await fn();
    for (let i = 0; i < 6; i++) await wait(0);
    await wait(300);
    for (let i = 0; i < 6; i++) await wait(0);
  });
const navigation = { navigate: jest.fn(), goBack: jest.fn(), setParams: jest.fn(), replace: jest.fn() };
const lastRender = () => mockStudio.renderMotion.mock.calls.at(-1)![1];
const fulls = () => mockStudio.renderMotion.mock.calls.filter(c => c[1].quality === 'full').length;

beforeEach(() => {
  jest.clearAllMocks();
  mockRenders = 0;
  Object.assign(mockDay, { premium: false, remaining: 2, credits: 0, adFinishes: true });
});

describe('the Photo Motion catalogue', () => {
  it('lists the camera moves and effects in the engine order, each with a name', () => {
    expect(CAMERAS.map(c => c.id)).toEqual([0, 1, 2, 3, 4, 5, 6, 7]);
    expect(EFFECTS.map(e => e.id)).toEqual([0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
    expect(CAMERAS.map(cameraName)).toEqual([
      'Still',
      'Push in',
      'Pull out',
      'Orbit',
      'Pan',
      'Dolly zoom',
      'Float',
      'Rise',
    ]);
    expect(effectName(EFFECTS[3])).toBe('Bokeh lights');
    expect(FEATURES.motion.plan).toBe('premium');
    expect(FREE_DAILY_ALLOWANCE.motion).toBe(2);
    expect(TOOL_TEXT.motion).toBe('Photo Motion');
  });

  it('turns a drawn stroke into arrows along its curve, and finds what a tap erases', () => {
    // A bend: right, then down.
    const pts = [
      ...Array.from({ length: 11 }, (_, i) => ({ x: 0.1 + i * 0.03, y: 0.2 })),
      ...Array.from({ length: 10 }, (_, i) => ({ x: 0.4, y: 0.23 + i * 0.03 })),
    ];
    const arrows = strokeToArrows(pts);
    expect(arrows.length).toBeGreaterThanOrEqual(4);
    expect(arrows[0]).toMatchObject({ x0: 0.1, y0: 0.2 });
    expect(arrows.at(-1)!.y1).toBeCloseTo(0.5);
    // Each arrow starts where the last one ended.
    for (let i = 1; i < arrows.length; i++) expect(arrows[i].x0).toBe(arrows[i - 1].x1);
    expect(strokeToArrows([{ x: 0.5, y: 0.5 }])).toEqual([]);

    const pins = [{ x: 0.8, y: 0.8, r: 0.06 }];
    expect(nearest(arrows, pins, 0.25, 0.21)).toEqual({ kind: 'arrow', index: expect.any(Number) });
    expect(nearest(arrows, pins, 0.81, 0.79)).toEqual({ kind: 'pin', index: 0 });
    expect(nearest(arrows, pins, 0.9, 0.1)).toBeNull();
  });

  it('packs arrows and pins the way the device takes them, within limits', () => {
    expect(packArrows([{ x0: 0.1, y0: 0.1, x1: 0.105, y1: 0.1 }])).toEqual([]);
    expect(packArrows([{ x0: -1, y0: 0.2, x1: 2, y1: 0.4 }])).toEqual([0, 0.2, 1, 0.4]);
    const many = Array.from({ length: 50 }, () => ({ x0: 0.1, y0: 0.1, x1: 0.3, y1: 0.3 }));
    expect(packArrows(many)).toHaveLength(MAX_ARROWS * 4);
    expect(packPins([{ x: 0.5, y: 0.5, r: 9 }])).toEqual([0.5, 0.5, 0.5]);
    expect(clampSettings({ camera: 42, duration: 7 as never, effectAmount: NaN, skySpeed: -3 })).toMatchObject({
      camera: 7,
      duration: DEFAULT_SETTINGS.duration,
      effectAmount: DEFAULT_SETTINGS.effectAmount,
      skySpeed: -1,
      loop: true,
      keepFaces: true,
    });
  });

  it('is written in all eight other languages, every word', () => {
    const keys = Object.keys(en).filter(
      k => k.startsWith('motion_') || k.startsWith('native_motion_') || k === 'feature_motion',
    );
    expect(keys.length).toBeGreaterThanOrEqual(100);
    const faq = Object.keys(en).find(k => (en as Record<string, string>)[k] === 'What is Photo Motion?')!;
    keys.push(faq, faq.replace(/_q$/, '_a'));
    for (const table of [es, ptBR, fr, ar, hi, id, ru, zhCN] as Record<string, string>[]) {
      for (const k of keys) {
        expect(typeof table[k]).toBe('string');
        expect(table[k].length).toBeGreaterThan(0);
        for (const p of (en as Record<string, string>)[k].match(/\{\w+\}/g) ?? []) expect(table[k]).toContain(p);
      }
    }
    expect((fr as Record<string, string>).motion_camera_dolly_zoom).toBe('Travelling compensé');
    expect((zhCN as Record<string, string>).motion_effect_bokeh).toBe('光斑');
    expect((en as Record<string, string>).feature_motion).toBe(FEATURES.motion.title);
  });
});

describe('the Photo Motion service', () => {
  it('reads the photo, sends every choice, checks results and maps errors', async () => {
    const s = new MotionService(mockStudio as never, mockAds as never, async () => true);
    const scene = await s.start('/a.jpg', 'job1');
    expect(mockStudio.startMotion).toHaveBeenCalledWith('job1', { inputPath: '/a.jpg' });
    expect(scene).toEqual({ sessionId: 'motion_1', width: 800, height: 1000, sky: 0.2, depth: true, faces: 1 });

    const v = await s.render(
      'motion_1',
      { ...DEFAULT_SETTINGS, camera: 3, effect: 1 },
      [{ x0: 0.1, y0: 0.5, x1: 0.4, y1: 0.5 }],
      [{ x: 0.5, y: 0.2, r: 0.06 }],
      'preview',
      'job2',
    );
    expect(lastRender()).toEqual({
      sessionId: 'motion_1',
      quality: 'preview',
      ...DEFAULT_SETTINGS,
      camera: 3,
      effect: 1,
      arrows: [0.1, 0.5, 0.4, 0.5],
      anchors: [0.5, 0.2, 0.06],
    });
    expect(v).toMatchObject({ outputPath: expect.stringMatching(/\.mp4$/), durationMs: 5000, remaining: 2 });
    // No arrows or pins: none are sent.
    await s.render('motion_1', DEFAULT_SETTINGS, [], [], 'preview');
    expect(lastRender()).not.toHaveProperty('arrows');
    expect(lastRender()).not.toHaveProperty('anchors');

    await expect(s.render('motion_1', DEFAULT_SETTINGS, [], [], 'full')).rejects.toMatchObject({ code: 'AD_REQUIRED' });
    mockStudio.renderMotion.mockResolvedValueOnce({ outputPath: '', width: 0, height: 0 } as never);
    await expect(s.render('motion_1', DEFAULT_SETTINGS, [], [], 'preview')).rejects.toBeInstanceOf(MotionError);
    mockStudio.startMotion.mockResolvedValueOnce({} as never);
    await expect(s.start('/a.jpg')).rejects.toBeInstanceOf(MotionError);
  });

  it('reads the allowance safely, and an ad unlocks a video only when watched to the end', async () => {
    const s = new MotionService(mockStudio as never, mockAds as never, async () => true);
    expect(await s.allowance()).toMatchObject({ remaining: 2, limit: 2, credits: 0, needsAd: true, owed: 0 });
    expect(readAllowance(null, UNKNOWN_ALLOWANCE)).toBe(UNKNOWN_ALLOWANCE);
    expect(readAllowance({ remaining: -1, limit: 2 }, UNKNOWN_ALLOWANCE)).toMatchObject({ needsAd: false });
    mockStudio.motionAllowance.mockRejectedValueOnce(new Error('gone'));
    expect(await s.allowance()).toEqual(UNKNOWN_ALLOWANCE);

    expect(await s.unlock()).toMatchObject({
      rewarded: true,
      offline: false,
      allowance: { remaining: 2, limit: 2, credits: 1, needsAd: false, owed: 0, frozen: false },
    });
    mockDay.adFinishes = false;
    expect((await s.unlock()).rewarded).toBe(false);
    mockDay.remaining = 0;
    await expect(s.unlock()).rejects.toMatchObject({ code: 'DAILY_LIMIT' });
    await expect(new MotionService(mockStudio as never, null).unlock()).rejects.toMatchObject({
      code: 'ADS_UNAVAILABLE',
    });
  });
});

describe('the Photo Motion screen', () => {
  it('is a Premium tile in the studio', async () => {
    expect(STUDIO_TOOLS.find(t => t.id === 'motion')).toMatchObject({
      route: 'Motion',
      premium: true,
      available: true,
      title: 'Photo Motion',
    });
    await wrap(
      <PhotoshootStudioScreen
        navigation={navigation as never}
        route={{ key: 'p', name: 'PhotoshootStudio' } as never}
      />,
    );
    // Every Photo Motion video starts from a project: the tile opens the studio's projects.
    await fireEvent.press(screen.getByTestId('studio-motion'));
    expect(navigation.navigate).toHaveBeenCalledWith('StudioProjects', { studio: 'motion' });
  });

  it('previews every change, draws motion, makes two free videos after an ad each, then offers Premium', async () => {
    await wrap(
      <MotionScreen
        navigation={navigation as never}
        route={{ key: 'm', name: 'Motion', params: { projectId: 'sp1' } } as never}
      />,
    );
    expect(screen.getByText('Bring your photo to life')).toBeTruthy();
    expect(screen.getByText(/make 2 videos a day, each after a short ad/)).toBeTruthy();

    // A photo: read once, a first preview (a float; the sky found drifts).
    await step(() => fireEvent.press(screen.getByTestId('motion-pick')));
    expect(mockStudio.startMotion.mock.calls[0][1]).toEqual({ inputPath: '/files/imports/lake.jpg' });
    expect(lastRender()).toMatchObject({
      sessionId: 'motion_1',
      quality: 'preview',
      camera: 6,
      sky: true,
      keepFaces: true,
    });
    expect(screen.getByTestId('motion-video').props.source).toBe('/cache/motion_preview_1.mp4');
    expect(screen.getByText('3D depth')).toBeTruthy();
    expect(screen.getByText('Sky found')).toBeTruthy();
    expect(screen.getByText('2 of 2 free videos left today')).toBeTruthy();

    // A camera move: a new preview.
    await step(() => fireEvent.press(screen.getByTestId('motion-camera-dolly_zoom')));
    expect(lastRender()).toMatchObject({ camera: 5, quality: 'preview' });
    expect(screen.getByText('You stay the same size as the world stretches behind you')).toBeTruthy();

    // Animate: draw a stroke, pin a spot, erase the pin.
    await step(() => fireEvent.press(screen.getByTestId('motion-tab-animate')));
    const canvas = screen.getByTestId('motion-canvas');
    await step(() => fireEvent(canvas, 'layout', { nativeEvent: { layout: { width: 400, height: 500 } } }));
    await step(() => {
      fireEvent(canvas, 'responderGrant', { nativeEvent: { locationX: 40, locationY: 400 } });
      for (let x = 60; x <= 360; x += 20)
        fireEvent(canvas, 'responderMove', { nativeEvent: { locationX: x, locationY: 400 } });
      fireEvent(canvas, 'responderRelease', { nativeEvent: { locationX: 360, locationY: 400 } });
    });
    const sent = lastRender().arrows as number[];
    expect(sent.length).toBeGreaterThanOrEqual(8);
    expect(sent.slice(0, 2)).toEqual([0.1, 0.8]);
    expect(sent.at(-2)).toBeCloseTo(0.9);
    await step(() => fireEvent.press(screen.getByTestId('motion-tool-anchor')));
    await step(() => {
      fireEvent(canvas, 'responderGrant', { nativeEvent: { locationX: 200, locationY: 100 } });
      fireEvent(canvas, 'responderRelease', { nativeEvent: { locationX: 200, locationY: 100 } });
    });
    expect(lastRender().anchors).toEqual([0.5, 0.2, 0.06]);
    expect(screen.getByTestId('motion-counts').props.children).toMatch(/Pins: 1/);
    await step(() => fireEvent.press(screen.getByTestId('motion-tool-erase')));
    await step(() => {
      fireEvent(canvas, 'responderGrant', { nativeEvent: { locationX: 202, locationY: 102 } });
      fireEvent(canvas, 'responderRelease', { nativeEvent: { locationX: 202, locationY: 102 } });
    });
    expect(lastRender()).not.toHaveProperty('anchors');

    // Effects and the video's length.
    await step(() => fireEvent.press(screen.getByTestId('motion-tab-effects')));
    await step(() => fireEvent.press(screen.getByTestId('motion-effect-snow')));
    expect(lastRender()).toMatchObject({ effect: 1 });
    await step(() => fireEvent.press(screen.getByTestId('motion-tab-video')));
    await step(() => fireEvent.press(screen.getByTestId('motion-length-8')));
    expect(lastRender()).toMatchObject({ duration: 8, loop: true });

    // The first free video: an ad first.
    expect(screen.getByText('Watch ad & create')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('motion-create')));
    expect(screen.getByTestId('motion-ad-sheet')).toBeTruthy();
    expect(fulls()).toBe(0);
    // An ad that doesn't finish unlocks nothing.
    mockDay.adFinishes = false;
    await step(() => fireEvent.press(screen.getByTestId('motion-ad-watch')));
    expect(screen.getByText(/The ad didn’t finish/)).toBeTruthy();
    expect(fulls()).toBe(0);
    mockDay.adFinishes = true;
    await step(() => fireEvent.press(screen.getByTestId('motion-ad-watch')));
    expect(mockAds.showRewardedAdForTool).toHaveBeenCalledTimes(2);
    expect(fulls()).toBe(1);
    expect(lastRender()).toMatchObject({ quality: 'full', camera: 5, effect: 1, duration: 8 });
    expect(screen.queryByTestId('motion-ad-sheet')).toBeNull();
    expect(screen.getByText('Your video is ready')).toBeTruthy();
    expect(screen.getByTestId('motion-video').props.source).toMatch(/motion_full_/);

    // Save and share it.
    await step(() => fireEvent.press(screen.getByTestId('motion-save')));
    expect(mockStorage.saveVideoToGallery).toHaveBeenCalledWith(expect.stringMatching(/motion_full_.*\.mp4$/));
    // Saved, here and on its card in the session's gallery.
    expect(screen.getAllByText('Saved to your gallery')).toHaveLength(2);
    await step(() => fireEvent.press(screen.getByTestId('motion-share')));
    expect(mockStorage.shareImages).toHaveBeenCalledWith([expect.stringMatching(/motion_full_/)], 'Photo Motion');
    expect(screen.getByTestId('motion-gallery')).toBeTruthy();

    // The second free video, after another ad.
    await step(() => fireEvent.press(screen.getByTestId('motion-edit')));
    expect(screen.getByText('1 of 2 free videos left today')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('motion-create')));
    await step(() => fireEvent.press(screen.getByTestId('motion-ad-watch')));
    expect(fulls()).toBe(2);

    // Today's two are made: Premium is offered, and no more ads or videos.
    await step(() => fireEvent.press(screen.getByTestId('motion-edit')));
    expect(screen.getByText(/You’ve made today’s 2 free videos/)).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('motion-create')));
    expect(screen.queryByTestId('motion-ad-sheet')).toBeNull();
    expect(screen.getByTestId('premium-sheet-plans')).toBeTruthy();
    expect(screen.getByText(FEATURES.motion.title)).toBeTruthy();
    expect(fulls()).toBe(2);
    expect(mockAds.showRewardedAdForTool).toHaveBeenCalledTimes(3);
  });

  it('makes videos straight away, with no ads, on Premium', async () => {
    mockDay.premium = true;
    await wrap(
      <MotionScreen
        navigation={navigation as never}
        route={{ key: 'm', name: 'Motion', params: { photo: '/p.jpg', projectId: 'sp1' } } as never}
      />,
    );
    await step(() => undefined);
    expect(navigation.setParams).toHaveBeenCalledWith({ photo: undefined, edit: undefined });
    expect(screen.getByText('Premium: unlimited videos, no ads, no mark')).toBeTruthy();
    for (let i = 0; i < 3; i++) {
      await step(() => fireEvent.press(screen.getByTestId('motion-create')));
      expect(screen.queryByTestId('motion-ad-sheet')).toBeNull();
      await step(() => fireEvent.press(screen.getByTestId('motion-edit')));
    }
    expect(fulls()).toBe(3);
    expect(mockAds.showRewardedAdForTool).not.toHaveBeenCalled();
  });

  it('asks for an ad again when the device says one is needed, and closes the photo when leaving', async () => {
    // The screen thinks a video is unlocked, but the device disagrees.
    mockStudio.motionAllowance.mockResolvedValueOnce({ remaining: 2, limit: 2, credits: 1, needsAd: false });
    const view = await wrap(
      <MotionScreen
        navigation={navigation as never}
        route={{ key: 'm', name: 'Motion', params: { photo: '/p.jpg', projectId: 'sp1' } } as never}
      />,
    );
    await step(() => undefined);
    expect(screen.getByText('Unlocked: your next video is ready to make')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('motion-create')));
    expect(fulls()).toBe(1);
    expect(screen.getByTestId('motion-ad-sheet')).toBeTruthy();
    await act(async () => {
      await view.unmount();
    });
    expect(mockStudio.endMotion).toHaveBeenCalledWith('motion_1');
  });
});
