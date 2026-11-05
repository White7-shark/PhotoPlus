import type { CameraSpec } from '../src/native';
import { CameraError, CameraService } from '../src/services/camera/CameraService';
import {
  DEFAULT_MODE,
  describeLook,
  flattenPoints,
  isLook,
  lookCameraSettings,
  MAX_SPOTLIGHT_POINTS,
  modeLabel,
  MODES,
  spotlightSettings,
  summariseLook,
  toggleSpotlightPoint,
  activeZoomPreset,
  availableModes,
  availablePresets,
  captureModeFor,
  compensatedIso,
  exposureIndices,
  focusStops,
  formatEv,
  formatFocusDistance,
  formatShutter,
  formatZoom,
  isManualActive,
  KELVIN_STOPS,
  nearestIndex,
  portraitAspect,
  progressLabel,
  resolutionLabel,
  zoomPresets,
} from '../src/services/camera/model';
import { parseCameraCapabilities } from '../src/services/camera/parse';
import { CameraEvents, type CameraCapabilities } from '../src/services/camera/types';
import { checkFeature, FREE_STATE } from '../src/services/entitlements/EntitlementService';
import { FEATURES } from '../src/services/entitlements/features';
import { friendlyMessage } from '../src/services/errors';
import { isVibeLook } from '../src/services/looks/types';
import { isValidSetting } from '../src/services/database/repositories/SettingsRepository';
import { improvementBadges, TOOLS } from '../src/screens/PhotoPreviewScreen';
import { rawCameraCaps } from './helpers/fixtures';
import {
  editedPathFor,
  enhancedCopyMetadata,
  enhancedPathFor,
  enhanceSuggestion,
  filesToDelete,
  formatMegapixels,
  noiseReduction,
} from '../src/services/processing/quality';

const caps = (o: Record<string, unknown> = {}) => parseCameraCapabilities(rawCameraCaps(o)) as CameraCapabilities;

describe('camera capability parsing', () => {
  it('accepts the native map', () => {
    const c = caps();
    expect(c.manual.exposure).toBe(true);
    expect(c.manual.iso).toEqual([50, 3200]);
    expect(c.resolutions['4:3']).toHaveLength(2);
    expect(c.capturePlans.night?.frames).toBe(6);
    expect(c.tier).toBe('standard');
  });

  it('treats malformed or missing fields as unsupported', () => {
    expect(parseCameraCapabilities(null)).toBeNull();
    expect(parseCameraCapabilities('nope')).toBeNull();
    const c = parseCameraCapabilities({
      zoom: { min: 'x' },
      manual: {
        exposure: true,
        iso: [3200, 50],
        focus: true,
        minFocusDiopters: 0,
        whiteBalanceModes: ['sodium', 'auto'],
      },
      captureModes: ['standard', 'hdr'],
      resolutions: {
        '4:3': [
          { id: 'huge', width: 1, height: 1 },
          { id: 'max', width: 0, height: 0 },
        ],
      },
      tier: 'ultra',
    });
    expect(c).not.toBeNull();
    expect(c?.manual.exposure).toBe(false); // inverted ISO range
    expect(c?.manual.focus).toBe(false); // fixed-focus lens
    expect(c?.manual.whiteBalanceModes).toEqual(['auto']);
    expect(c?.captureModes).toEqual(['standard']);
    expect(c?.resolutions['4:3']).toEqual([]);
    expect(c?.zoom).toEqual({ min: 1, max: 1 });
    expect(c?.tier).toBe('entry');
    expect(c?.exposureCompensation).toBeNull();
  });
});

describe('camera modes and presets', () => {
  it('offers only modes the camera can run', () => {
    expect(availableModes(caps()).map(m => m.id)).toEqual([
      'night',
      'spotlight',
      'enhance',
      'photo',
      'superres',
      'pro',
    ]);
    const basic = caps({
      captureModes: ['standard', 'enhance'],
      raw: false,
      manual: { ...rawCameraCaps().manual, exposure: false, focus: false, kelvin: false, whiteBalanceModes: ['auto'] },
    });
    expect(availableModes(basic).map(m => m.id)).toEqual(['enhance', 'photo']);
    expect(availableModes(null).map(m => m.id)).toEqual(['photo']);
  });

  it('maps UI modes to capture pipelines', () => {
    expect(captureModeFor('photo', true)).toBe('standard');
    expect(captureModeFor('pro', false)).toBe('standard');
    expect(captureModeFor('pro', true)).toBe('raw');
    expect(captureModeFor('night', true)).toBe('night');
  });

  it('hides presets the hardware cannot honour', () => {
    expect(availablePresets(caps()).map(p => p.id)).toEqual(['portrait', 'night', 'sport', 'landscape', 'document']);
    const slowSensor = caps({ manual: { ...rawCameraCaps().manual, exposureTimeNs: [2000000, 500000000] } });
    expect(availablePresets(slowSensor).map(p => p.id)).not.toContain('sport'); // cannot do 1/1000 s
    expect(availablePresets(caps({ captureModes: ['standard'] })).map(p => p.id)).toEqual(['sport']);
  });

  it('detects manual overrides', () => {
    expect(isManualActive({})).toBe(false);
    expect(isManualActive({ whiteBalance: 'auto', iso: null })).toBe(false);
    expect(isManualActive({ iso: 400 })).toBe(true);
    expect(isManualActive({ whiteBalance: 'daylight' })).toBe(true);
    expect(isManualActive({ aeLock: true })).toBe(true);
  });
});

describe('zoom', () => {
  it('builds pills within the lens range', () => {
    expect(zoomPresets(0.6, 10)).toEqual([0.6, 1, 2, 5]);
    expect(zoomPresets(1, 8)).toEqual([1, 2, 5]);
    expect(zoomPresets(1, 1)).toEqual([1]);
    expect(zoomPresets(1, 3)).toEqual([1, 2]);
  });

  it('labels and highlights like a camera', () => {
    expect(formatZoom(1)).toBe('1×');
    expect(formatZoom(0.6)).toBe('0.6×');
    expect(formatZoom(2.46)).toBe('2.5×');
    expect(formatZoom(12.4)).toBe('12×');
    expect(activeZoomPreset([0.6, 1, 2, 5], 1.4)).toBe(1);
    expect(activeZoomPreset([0.6, 1, 2, 5], 1.97)).toBe(2);
    expect(activeZoomPreset([0.6, 1, 2, 5], 0.7)).toBe(0.6);
  });
});

describe('exposure and manual dials', () => {
  it('spaces EV stops at about a third of a stop', () => {
    const idx = exposureIndices({ minIndex: -12, maxIndex: 12, step: 1 / 6 });
    expect(idx[0]).toBe(-12);
    expect(idx[idx.length - 1]).toBe(12);
    expect(idx).toContain(0);
    expect(idx[1] - idx[0]).toBe(2);
    expect(exposureIndices(null)).toEqual([]);
    expect(formatEv(6, 1 / 6)).toBe('+1.0');
    expect(formatEv(-2, 1 / 6)).toBe('−0.3');
    expect(formatEv(0, 1 / 6)).toBe('0');
  });

  it('formats shutter, focus and colour temperature', () => {
    expect(formatShutter(4_000_000)).toBe('1/250');
    expect(formatShutter(2_000_000_000)).toBe('2″');
    expect(formatShutter(1_500_000_000)).toBe('1.5″');
    expect(formatFocusDistance(0)).toBe('∞');
    expect(formatFocusDistance(0.5)).toBe('2 m');
    expect(formatFocusDistance(4)).toBe('25 cm');
    expect(formatFocusDistance(0.05)).toBe('20 m');
    expect(KELVIN_STOPS[0]).toBe(2000);
    expect(KELVIN_STOPS[KELVIN_STOPS.length - 1]).toBe(9000);
  });

  it('offers focus stops down to the lens minimum', () => {
    const stops = focusStops(10);
    expect(stops[0]).toBe(0);
    expect(stops[stops.length - 1]).toBeCloseTo(10);
    expect(stops.every((d, i) => i === 0 || d > stops[i - 1])).toBe(true);
    expect(focusStops(null)).toEqual([]);
    // A lens that focuses to 30 cm ends with its own minimum.
    expect(focusStops(3.3).at(-1)).toBeCloseTo(1 / 0.35, 1);
  });

  it('keeps brightness when the shutter is chosen', () => {
    // Auto metered ISO 100 at 1/60; 1/1000 needs ~16x the gain.
    expect(compensatedIso(100, 16_666_667, 1_000_000, [50, 3200])).toBe(1667);
    expect(compensatedIso(800, 33_333_333, 1_000_000, [50, 3200])).toBe(3200); // clamped
    expect(compensatedIso(null, 16_666_667, 1_000_000, [50, 3200])).toBeNull();
    expect(nearestIndex([50, 100, 200, 400], 180)).toBe(2);
  });

  it('labels resolutions and preview shapes', () => {
    expect(resolutionLabel({ id: 'max', width: 4032, height: 3024, megapixels: 12.2 })).toBe('Max · 12 MP');
    expect(resolutionLabel({ id: '2mp', width: 1600, height: 1200, megapixels: 1.92 })).toBe('1.9 MP');
    expect(portraitAspect('4:3')).toBe(0.75);
    expect(portraitAspect('16:9')).toBeCloseTo(0.5625);
    expect(portraitAspect('1:1')).toBe(1);
    expect(progressLabel('capturing', 6)).toBe('Hold still…');
    expect(progressLabel('processing', 1)).toBe('Perfecting your photo…');
    expect(progressLabel('styling', 1)).toBe('Adding the spotlight…');
  });
});

describe('enhancement helpers', () => {
  it('suggests Enhance for noisy, soft or dark photos', () => {
    const clean = {
      noise: 0.004,
      blurPx: 0.6,
      sharpness: 0.4,
      meanLuma: 0.45,
      highlightsClipped: 0,
      shadowsClipped: 0,
    };
    expect(enhanceSuggestion(clean).suggested).toBe(false);
    expect(enhanceSuggestion({ ...clean, noise: 0.03, blurPx: 2 }).reasons).toEqual(['visible noise', 'soft focus']);
    expect(enhanceSuggestion({ ...clean, meanLuma: 0.15 }).reasons).toEqual(['underexposed']);
    expect(enhanceSuggestion(null).suggested).toBe(false);
  });

  it('names enhanced copies next to the original', () => {
    expect(enhancedPathFor('/data/captures/PP_1.jpg', 1)).toBe('/data/captures/PP_1_enhanced.jpg');
    expect(enhancedPathFor('/data/captures/PP_1.jpg', 2, 3)).toBe('/data/captures/PP_1_superres_3.jpg');
    expect(enhancedPathFor('/data/v1.2/photo', 1)).toBe('/data/v1.2/photo_enhanced.jpg');
  });

  it('never lets an enhanced copy own its source files', () => {
    const night = {
      id: 'img_1',
      path: '/c/PP_1.jpg',
      metadata: {
        captureMode: 'night',
        rawPath: '/c/PP_1.dng',
        originalPath: '/c/PP_1_original.jpg',
        originalOwned: true,
      },
    };
    // Deleting the capture removes its own companions...
    expect(filesToDelete(night)).toEqual(['/c/PP_1.jpg', '/c/PP_1.dng', '/c/PP_1_original.jpg']);
    // ...but a kept enhanced copy points at the source without owning it.
    const copy = { path: '/c/PP_1_enhanced.jpg', metadata: enhancedCopyMetadata(night, 1, { noiseBefore: 1 }) };
    expect(copy.metadata).toMatchObject({ originalPath: '/c/PP_1.jpg', originalOwned: false, sourceImageId: 'img_1' });
    expect(copy.metadata.rawPath).toBeUndefined();
    expect(filesToDelete(copy)).toEqual(['/c/PP_1_enhanced.jpg']);
    expect(enhancedCopyMetadata(night, 2, null).captureMode).toBe('superres');
  });

  it('summarises results', () => {
    expect(noiseReduction(0.02, 0.005)).toBe(75);
    expect(noiseReduction(0, 0)).toBe(0);
    expect(noiseReduction(0.01, 0.02)).toBe(0);
    expect(formatMegapixels(4032, 3024)).toBe('12 MP');
    expect(formatMegapixels(1600, 1200)).toBe('1.9 MP');
  });
});

function fakeCamera(overrides: Partial<Record<keyof CameraSpec, unknown>> = {}) {
  const native = {
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
    setZoom: jest.fn(async (r: number) => Math.min(r, 10)),
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
    ...overrides,
  };
  const listeners: Record<string, ((p: unknown) => void)[]> = {};
  const events = {
    addListener: (name: string, l: (p: unknown) => void) => {
      (listeners[name] ??= []).push(l);
      return { remove: () => (listeners[name] = listeners[name].filter(x => x !== l)) };
    },
  };
  const emit = (name: string, payload: unknown) => listeners[name]?.forEach(l => l(payload));
  return { native, service: new CameraService(native as unknown as CameraSpec, events as never), emit, listeners };
}

describe('CameraService', () => {
  it('passes a capture id through and returns it', async () => {
    const { native, service } = fakeCamera();
    const shot = await service.capture({ mode: 'night', flash: 'off' }, 'cap-1');
    expect(native.capturePhoto).toHaveBeenCalledWith({ mode: 'night', flash: 'off', captureId: 'cap-1' });
    expect(shot.captureId).toBe('cap-1');
    expect(shot.path).toBe('/captures/cap-1.jpg');
    expect(service.pendingCaptures).toBe(0);
  });

  it('generates unique ids and rejects duplicates in flight', async () => {
    let release: (v: unknown) => void = () => undefined;
    const { service } = fakeCamera({ capturePhoto: jest.fn(() => new Promise(r => (release = r))) });
    expect(service.newCaptureId()).not.toBe(service.newCaptureId());
    const first = service.capture({ mode: 'standard', flash: 'off' }, 'same');
    await expect(service.capture({ mode: 'standard', flash: 'off' }, 'same')).rejects.toMatchObject({ code: 'BUSY' });
    expect(service.pendingCaptures).toBe(1);
    release({ path: '/x.jpg', width: 1, height: 1, sizeBytes: 1, capturedAt: 1, mode: 'standard' });
    await first;
    expect(service.pendingCaptures).toBe(0);
  });

  it('maps native rejections to CameraError codes', async () => {
    const { service } = fakeCamera({
      capturePhoto: jest.fn(async () => {
        throw Object.assign(new Error('Still processing'), { code: 'BUSY' });
      }),
    });
    const error = await service.capture({ mode: 'enhance', flash: 'off' }).catch(e => e);
    expect(error).toBeInstanceOf(CameraError);
    expect(error.code).toBe('BUSY');
    const { service: s2 } = fakeCamera({
      capturePhoto: jest.fn(async () => {
        throw new Error('no code');
      }),
    });
    await expect(s2.capture({ mode: 'standard', flash: 'off' })).rejects.toMatchObject({ code: 'CAPTURE_FAILED' });
  });

  it('validates capabilities and forwards controls', async () => {
    const { native, service } = fakeCamera({ getCapabilities: jest.fn(async () => null) });
    expect(await service.getCapabilities()).toBeNull();
    service.configure({ aspectRatio: '1:1', raw: true });
    expect(native.configure).toHaveBeenCalledWith({ aspectRatio: '1:1', raw: true });
    service.setFocusPeaking(true);
    expect(native.setFocusPeaking).toHaveBeenCalledWith(true, 0xffff3b6b, 0.4);
    expect(await service.setZoom(20)).toBe(10);
    expect(await service.setManual({ iso: 400 })).toEqual({ iso: 400 });
    service.cancelCapture('c');
    expect(native.cancelCapture).toHaveBeenCalledWith('c');
  });

  it('delivers typed events and unsubscribes', () => {
    const { service, emit, listeners } = fakeCamera();
    const seen: number[] = [];
    const sub = service.on(CameraEvents.zoom, z => seen.push(z.ratio));
    emit(CameraEvents.zoom, { ratio: 2, linear: 0.1, min: 1, max: 10 });
    sub.remove();
    emit(CameraEvents.zoom, { ratio: 3, linear: 0.2, min: 1, max: 10 });
    expect(seen).toEqual([2]);
    expect(listeners[CameraEvents.zoom]).toHaveLength(0);
  });
});

describe('plans and names', () => {
  it('Clarity and Spotlight are free; multi-shot modes and Vibe Match are Premium', () => {
    const enforced = (key: keyof typeof FEATURES) => checkFeature(key, FREE_STATE, true).allowed;
    expect(enforced('clarity')).toBe(true);
    expect(enforced('spotlight')).toBe(true);
    expect(enforced('shareTo')).toBe(true);
    expect(enforced('multiShot')).toBe(false);
    expect(enforced('vibeMatch')).toBe(false);
    expect(checkFeature('multiShot', { ...FREE_STATE, plan: 'premium' }, true).allowed).toBe(true);
    // Moonlight and Ultra Detail are the multi-shot modes.
    expect(MODES.filter(m => m.feature === 'multiShot').map(m => m.label)).toEqual(['Moonlight', 'Ultra Detail']);
    expect(MODES.find(m => m.id === 'enhance')?.feature).toBe('clarity');
    expect(MODES.find(m => m.id === 'spotlight')?.feature).toBe('spotlight');
    expect(DEFAULT_MODE).toBe('enhance');
  });

  it('never shows technical names', () => {
    const shown = [
      ...MODES.flatMap(m => [m.label, m.description]),
      ...TOOLS.flatMap(t => [t.label, t.description]),
      ...['standard', 'raw', 'enhance', 'spotlight', 'night', 'superres'].map(modeLabel),
    ].join(' ');
    for (const word of [
      /engine/i,
      /frame/i,
      /fus/i,
      /burst/i,
      /super-?res/i,
      /noise/i,
      /algorithm/i,
      /segment/i,
      /model/i,
      /neural/i,
    ]) {
      expect(shown).not.toMatch(word);
    }
    expect(modeLabel('superres')).toBe('Ultra Detail');
    expect(modeLabel('night')).toBe('Moonlight');
    expect(friendlyMessage('BUDGET_EXCEEDED')).not.toMatch(/budget|memory budget|engine/i);
    expect(friendlyMessage('SOMETHING_NEW')).toBe('Something went wrong. Please try again.');
  });
});

describe('spotlight choices', () => {
  it('taps add, re-taps remove, and the list is capped', () => {
    let pts = toggleSpotlightPoint([], { x: 0.2, y: 0.2 });
    pts = toggleSpotlightPoint(pts, { x: 0.8, y: 0.8 });
    expect(pts).toHaveLength(2);
    pts = toggleSpotlightPoint(pts, { x: 0.21, y: 0.19 });
    expect(pts).toEqual([{ x: 0.8, y: 0.8 }]);
    for (let i = 0; i < 6; i++) pts = toggleSpotlightPoint(pts, { x: 0.1 * i, y: 0.5 });
    expect(pts).toHaveLength(MAX_SPOTLIGHT_POINTS);
    expect(flattenPoints([{ x: 0.1, y: 0.2 }])).toEqual([0.1, 0.2]);
  });

  it('levels map to gentle, smooth and flawless retouching', () => {
    expect(spotlightSettings('medium', 'smooth')).toEqual({ blur: 0.6, retouch: 0.75, brighten: 0.4, pop: 0.5 });
    expect(spotlightSettings('light', 'off').retouch).toBe(0);
    expect(spotlightSettings('strong', 'flawless')).toMatchObject({ blur: 0.9, retouch: 1 });
  });
});

describe('vibe match looks', () => {
  const neutral = [
    0.02, 0.06, 0.15, 0.3, 0.45, 0.6, 0.75, 0.88, 0.97, 0, 0, 0, 0.02, 0.02, 0.02, 0.06, 0.06, 0.9, 0.004, 0.45,
  ];
  const warm = neutral.map((v, i) => (i >= 12 && i <= 14 ? v + 0.06 : i >= 9 && i <= 11 ? v - 0.03 : v));
  const cool = neutral.map((v, i) => (i >= 12 && i <= 14 ? v - 0.06 : i >= 9 && i <= 11 ? v + 0.04 : v));
  const bright = neutral.map((v, i) => (i < 9 ? Math.min(1, v + 0.15) : v));

  it('validates looks and saved settings', () => {
    expect(isLook(neutral)).toBe(true);
    expect(isLook([1, 2])).toBe(false);
    expect(isLook([...neutral.slice(0, 19), NaN])).toBe(false);
    expect(isVibeLook({ referencePath: '/l.jpg', look: neutral, strength: 0.8 })).toBe(true);
    expect(isVibeLook({ referencePath: '', look: neutral, strength: 0.8 })).toBe(false);
    expect(isValidSetting('vibeLook', null)).toBe(true);
    expect(isValidSetting('vibeLook', { referencePath: '/l.jpg', look: neutral, strength: 2 })).toBe(false);
  });

  it('describes a look in plain words', () => {
    expect(describeLook(summariseLook(warm))).toMatch(/warm/);
    expect(describeLook(summariseLook(cool))).toMatch(/cool/);
    expect(describeLook(summariseLook(bright))).toMatch(/^Bright/);
    expect(describeLook(summariseLook(neutral))).toBe('Balanced, neutral');
  });

  it('turns a look into camera settings', () => {
    const c = caps();
    expect(lookCameraSettings(neutral, c)).toMatchObject({ whiteBalance: 'auto', evIndex: 0 });
    const w = lookCameraSettings(warm, c);
    expect(w.whiteBalance).toBe('kelvin');
    expect(w.kelvin).toBeGreaterThan(6000);
    expect(lookCameraSettings(cool, c).kelvin).toBeLessThan(5000);
    expect(lookCameraSettings(bright, c).evIndex).toBeGreaterThan(0);
    // Without Kelvin control, presets stand in.
    const noKelvin = caps({
      manual: {
        ...rawCameraCaps().manual,
        kelvin: false,
        whiteBalanceModes: ['auto', 'incandescent', 'cloudy', 'shade'],
      },
    });
    expect(lookCameraSettings(warm, noKelvin).whiteBalance).toBe('shade'); // strongly warm
    const mild = neutral.map((v, i) => (i === 13 ? v + 0.04 : v));
    expect(lookCameraSettings(mild, noKelvin).whiteBalance).toBe('cloudy'); // gently warm
    expect(lookCameraSettings(cool, noKelvin).whiteBalance).toBe('incandescent');
    expect(lookCameraSettings(warm, null)).toMatchObject({ evIndex: 0, whiteBalance: 'auto' });
  });
});

describe('photo results in plain words', () => {
  it('summarises captures and edits', () => {
    const night = improvementBadges(
      'night',
      { noiseBefore: 0.02, noiseAfter: 0.008, framesInput: 6, framesUsed: 6 },
      null,
      false,
    );
    expect(night.map(b => b.label)).toEqual(['Grain −60%', '6 shots blended']);
    const spot = improvementBadges('spotlight', null, { people: 1, spots: 4, skin: 0.05, subject: 0.4 }, true);
    expect(spot.map(b => b.label)).toEqual(['Background softened', 'Glow · 4 blemishes cleared', 'Vibe Match style']);
    expect(improvementBadges('standard', null, null, false)).toEqual([]);
    expect(editedPathFor('/c/PP_1.jpg', 'spotlight')).toBe('/c/PP_1_spotlight.jpg');
    expect(editedPathFor('/c/PP_1.jpg', 'look', 2)).toBe('/c/PP_1_look_2.jpg');
    expect(editedPathFor('/c/PP_1.jpg', 'superres')).toBe('/c/PP_1_superres.jpg');
  });
});
