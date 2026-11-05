import { act, fireEvent, render, screen, waitFor } from '@testing-library/react-native';
import React from 'react';

import { PhotoPreviewScreen } from '../src/screens/PhotoPreviewScreen';
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { FREE_STATE } from '../src/services/entitlements/EntitlementService';
import { allowanceLabel, checkAllowance, localDay, recordUse } from '../src/services/entitlements/allowance';
import { FEATURES } from '../src/services/entitlements/features';
import { lensIsBasic, lensNotice, rememberReading, type LensReport } from '../src/services/processing/lens';
import { captureRecipe, editRecipe, effectiveRecipe, isRecipe, transformPlan } from '../src/services/processing/recipe';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';

const mockEngine = {
  readImageMetadata: jest.fn(async () => ({ width: 1600, height: 1200, sizeBytes: 600_000, mimeType: 'image/jpeg' })),
  analyzeQuality: jest.fn(async () => ({
    noise: 0.004,
    blurPx: 0.5,
    sharpness: 0.4,
    meanLuma: 0.5,
    highlightsClipped: 0,
    shadowsClipped: 0,
  })),
  analyzeLens: jest.fn(async (): Promise<LensReport> => basicLens),
  transform: jest.fn(async (_id: string, r: { outputPath: string; upscale?: number }) => ({
    outputPath: r.outputPath,
    width: 1600 * (r.upscale ?? 1),
    height: 1200 * (r.upscale ?? 1),
    sizeBytes: 900_000,
    transform: {
      fringesFixedPx: 1.6,
      deblur: 1.2,
      deblurCorners: 1.0,
      noiseBefore: 0.012,
      noiseAfter: 0.002,
      gradeBefore: 0.35,
      upscale: r.upscale ?? 1,
    },
    portrait: { people: 1, spots: 2, skin: 0.03, subject: 0.3 },
  })),
  portrait: jest.fn(),
  enhance: jest.fn(),
  applyLook: jest.fn(),
  analyzeLook: jest.fn(),
  cancel: jest.fn(),
};
const mockStorage = {
  fileExists: jest.fn(async (path: string) => path.endsWith('_original.jpg')),
  deleteFile: jest.fn(async () => true),
  shareImages: jest.fn(async () => true),
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

const basicLens: LensReport = {
  detailBlur: 1.4,
  cornerBlur: 2.4,
  fringePx: 1.8,
  noise: 0.012,
  grade: 0.35,
  reliable: true,
  width: 1600,
  height: 1200,
};
const goodLens: LensReport = {
  ...basicLens,
  detailBlur: 0.4,
  cornerBlur: 0.6,
  fringePx: 0.1,
  grade: 0.86,
};

const spotlight = { points: [0.4, 0.5], blur: 0.6, retouch: 0.75, brighten: 0.4, pop: 0.5 };
const look = { look: Array.from({ length: 20 }, () => 0.1), strength: 0.8 };

// A Spotlight + Vibe Match capture: the untouched frame is its base.
const photo = {
  id: 'img_1',
  projectId: 'prj_1',
  path: '/data/captures/PP_1.jpg',
  source: 'camera' as const,
  width: 1600,
  height: 1200,
  sizeBytes: 600_000,
  mimeType: 'image/jpeg',
  capturedAt: 1,
  createdAt: 1,
  metadata: {
    captureMode: 'spotlight',
    lens: 'back-wide',
    look: true,
    portrait: { people: 1, spots: 2, skin: 0.03, subject: 0.3 },
    originalPath: '/data/captures/PP_1_original.jpg',
    originalOwned: true,
    recipe: captureRecipe({
      path: '/data/captures/PP_1.jpg',
      mode: 'spotlight',
      originalPath: '/data/captures/PP_1_original.jpg',
      spotlight,
      look,
    }),
  } as Record<string, unknown>,
};

function fakeRepos() {
  const internal = new Map<string, unknown>();
  const added: { metadata: Record<string, unknown> }[] = [];
  return {
    internal,
    added,
    repos: {
      images: {
        get: jest.fn(async () => photo),
        add: jest.fn(async (img: { metadata: Record<string, unknown> }) => {
          added.push(img);
          return { ...img, id: 'img_2', createdAt: 2 };
        }),
        remove: jest.fn(),
      },
      projects: { ensureInbox: jest.fn(async () => ({ id: 'prj_1' })) },
      settings: {
        getInternal: jest.fn(async (k: string) => internal.get(k) ?? null),
        setInternal: jest.fn(async (k: string, v: unknown) => {
          internal.set(k, v);
        }),
      },
    } as never,
  };
}

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);

beforeEach(() => {
  jest.clearAllMocks();
  mockEngine.analyzeLens.mockImplementation(async () => basicLens);
});

describe('daily allowance', () => {
  const day1 = new Date(2026, 10, 5, 9).getTime();
  const day1Late = new Date(2026, 10, 5, 23, 59).getTime();
  const day2 = new Date(2026, 10, 6, 0, 1).getTime();

  it('Transform is Premium, with 2 a day on the free plan', () => {
    expect(FEATURES.transform.plan).toBe('premium');
    let usage = null;
    expect(checkAllowance('transform', FREE_STATE, usage, day1, true)).toMatchObject({ allowed: true, remaining: 2 });
    usage = recordUse(usage, 'transform', day1);
    expect(checkAllowance('transform', FREE_STATE, usage, day1, true)).toMatchObject({ allowed: true, remaining: 1 });
    usage = recordUse(usage, 'transform', day1Late);
    const spent = checkAllowance('transform', FREE_STATE, usage, day1Late, true);
    expect(spent).toMatchObject({ allowed: false, remaining: 0, wouldBeBlocked: true });
    expect(allowanceLabel(spent, 'Transform')).toBe('You’ve used today’s free Transforms');
    // A new day, a new allowance.
    expect(checkAllowance('transform', FREE_STATE, usage, day2, true)).toMatchObject({ allowed: true, remaining: 2 });
    expect(localDay(day2)).toBe('2026-11-06');
  });

  it('Premium has no limit, and other Premium features have no free allowance', () => {
    let usage = null;
    for (let i = 0; i < 10; i++) usage = recordUse(usage, 'transform', day1);
    const premium = checkAllowance('transform', { ...FREE_STATE, plan: 'premium' }, usage, day1, true);
    expect(premium).toMatchObject({ allowed: true, remaining: null });
    expect(allowanceLabel(premium, 'Transform')).toBeNull();
    expect(checkAllowance('vibeMatch', FREE_STATE, null, day1, true).allowed).toBe(false);
    expect(checkAllowance('clarity', FREE_STATE, null, day1, true).allowed).toBe(true);
  });

  it('while enforcement is off, nothing is blocked but the count stays honest', () => {
    const usage = recordUse(recordUse(null, 'transform', day1), 'transform', day1);
    const d = checkAllowance('transform', FREE_STATE, usage, day1);
    expect(d.allowed).toBe(true);
    expect(d.wouldBeBlocked).toBe(true);
    expect(allowanceLabel(checkAllowance('transform', FREE_STATE, null, day1), 'Transform')).toBe(
      '2 free Transforms left today',
    );
  });
});

describe('recipes: Transform keeps every effect', () => {
  it('a Spotlight + Vibe Match capture is re-made from its untouched frame', () => {
    const plan = transformPlan(photo);
    expect(plan.inputPath).toBe('/data/captures/PP_1_original.jpg');
    expect(plan.keepStyle).toBe(false);
    expect(plan.spotlight).toEqual(spotlight);
    expect(plan.look).toEqual(look);
    expect(plan.replayed).toBe(2);
  });

  it('edits chain onto the same base; the latest Spotlight and look win; Clarity is built in', () => {
    const edited = {
      path: '/data/captures/PP_1_look.jpg',
      metadata: { recipe: editRecipe(photo, { kind: 'look', look: [0.2, 0.3], strength: 0.5 }) },
    };
    const twice = { path: '/x.jpg', metadata: { recipe: editRecipe(edited, { kind: 'clarity' }) } };
    const plan = transformPlan(twice);
    expect(plan.inputPath).toBe('/data/captures/PP_1_original.jpg');
    expect(plan.look).toEqual({ look: [0.2, 0.3], strength: 0.5 });
    expect(plan.spotlight).toEqual(spotlight);
    // A kept Transform result starts over from the same untouched photo.
    const transformed = { path: '/t.jpg', metadata: { captureMode: 'transform', recipe: effectiveRecipe(twice) } };
    expect(transformPlan(transformed).inputPath).toBe('/data/captures/PP_1_original.jpg');
  });

  it('multi-shot photos are their own base and keep their look', () => {
    const r = captureRecipe({ path: '/n.jpg', mode: 'night', originalPath: '/n_original.jpg', look });
    expect(r).toEqual({ basePath: '/n.jpg', baseStyled: true, steps: [] });
    expect(transformPlan({ path: '/n.jpg', metadata: { recipe: r } })).toMatchObject({ keepStyle: true, look: null });
  });

  it('photos saved before recipes: Clarity restarts from its original, styled ones keep their style', () => {
    expect(
      transformPlan({ path: '/c.jpg', metadata: { captureMode: 'enhance', originalPath: '/c_original.jpg' } }),
    ).toMatchObject({ inputPath: '/c_original.jpg', keepStyle: false });
    expect(
      transformPlan({ path: '/s.jpg', metadata: { captureMode: 'spotlight', originalPath: '/so.jpg' } }),
    ).toMatchObject({ inputPath: '/s.jpg', keepStyle: true });
    expect(transformPlan({ path: '/p.jpg', metadata: {} })).toMatchObject({ inputPath: '/p.jpg', keepStyle: false });
    // Corrupt recipes are ignored.
    expect(isRecipe({ basePath: '/a', baseStyled: false, steps: [{ kind: 'spotlight', points: [2] }] })).toBe(false);
    expect(transformPlan({ path: '/p.jpg', metadata: { recipe: { basePath: 5 } } }).inputPath).toBe('/p.jpg');
  });
});

describe('lens notice', () => {
  it('a good camera gets no notice; one soft photo speaks about the photo, not the lens', () => {
    expect(lensNotice(goodLens)).toBeNull();
    expect(lensNotice({ ...basicLens, reliable: false })).toBeNull();
    const n = lensNotice(basicLens);
    expect(n?.scope).toBe('photo');
    expect(n?.signs).toEqual(expect.arrayContaining(['soft fine detail', 'blurry edges', 'colour fringes on edges']));
  });

  it('when most recent photos agree, it speaks about the lens; a photo counts once', () => {
    let profiles = {};
    profiles = rememberReading(profiles, 'back', 'a', basicLens);
    profiles = rememberReading(profiles, 'back', 'a', basicLens); // re-opened: still one reading
    profiles = rememberReading(profiles, 'back', 'b', basicLens);
    expect(lensIsBasic((profiles as Record<string, never>).back)).toBe(false);
    profiles = rememberReading(profiles, 'back', 'c', basicLens);
    const back = (profiles as Record<string, { readings: unknown[] }>).back;
    expect(back.readings).toHaveLength(3);
    expect(lensNotice(basicLens, back as never)?.scope).toBe('lens');
    // Good photos outvote the old soft ones.
    for (const id of ['d', 'e', 'f']) profiles = rememberReading(profiles, 'back', id, goodLens);
    expect(lensIsBasic((profiles as Record<string, never>).back)).toBe(false);
  });

  it('speaks plainly', () => {
    const copy = [
      lensNotice(basicLens),
      lensNotice(basicLens, { readings: [1, 2, 3].map(i => ({ id: `${i}`, grade: 0.3 })) }),
    ]
      .flatMap(n => [n?.title, n?.body, ...(n?.signs ?? [])])
      .join(' ');
    for (const word of [
      /aberration/i,
      /vignett/i,
      /deconvol/i,
      /psf/i,
      /engine/i,
      /noise/i,
      /model/i,
      /sigma/i,
      /grade/i,
    ]) {
      expect(copy).not.toMatch(word);
    }
  });
});

describe('Transform on a photo', () => {
  it('warns about the lens, re-makes the photo with its effects, counts the use, and keeps the recipe', async () => {
    const { repos, internal, added } = fakeRepos();
    useAppStore.setState({ repos, settings: { ...DEFAULT_SETTINGS }, entitlements: FREE_STATE });
    const navigation = { goBack: jest.fn(), replace: jest.fn() };
    await wrap(
      <PhotoPreviewScreen navigation={navigation as never} route={{ params: { imageId: 'img_1' } } as never} />,
    );

    // Lens read on the untouched frame; this camera's reading is remembered.
    expect(await screen.findByText('This photo could look sharper')).toBeTruthy();
    expect(mockEngine.analyzeLens).toHaveBeenCalledWith('/data/captures/PP_1_original.jpg');
    await waitFor(() => expect(internal.get('lens.profiles')).toBeTruthy());
    expect(screen.getByText('2 free Transforms left today')).toBeTruthy();
    expect(screen.getByRole('tab', { name: 'Transform, premium' })).toBeTruthy();

    await fireEvent.press(screen.getByTestId('try-transform'));
    expect(screen.getByText(/Your Spotlight and Vibe Match effects are re-applied on top/)).toBeTruthy();
    await fireEvent.press(screen.getByTestId('apply-tool'));
    await waitFor(() => expect(mockEngine.transform).toHaveBeenCalled());
    const request = mockEngine.transform.mock.calls[0][1] as Record<string, unknown>;
    expect(request).toMatchObject({
      inputPath: '/data/captures/PP_1_original.jpg',
      outputPath: '/data/captures/PP_1_transform.jpg',
      upscale: 2, // a small photo gets twice the detail by default
      keepStyle: false,
      spotlight,
      look: look.look,
      lookStrength: 0.8,
    });
    expect(await screen.findByText('High-end lens look')).toBeTruthy();
    expect(screen.getByText('Colour fringes removed')).toBeTruthy();
    expect(screen.getByText('Glow · 2 blemishes cleared')).toBeTruthy();
    expect(internal.get('usage.daily')).toMatchObject({ counts: { transform: 1 } });

    await fireEvent.press(screen.getByRole('button', { name: 'Keep' }));
    await waitFor(() => expect(navigation.replace).toHaveBeenCalledWith('PhotoPreview', { imageId: 'img_2' }));
    expect(added[0].metadata).toMatchObject({
      captureMode: 'transform',
      originalPath: '/data/captures/PP_1.jpg',
      look: true,
      portrait: { spots: 2 },
      recipe: { basePath: '/data/captures/PP_1_original.jpg' },
    });
  });

  it('no notice for a good camera, and the count shows what is left', async () => {
    mockEngine.analyzeLens.mockImplementation(async () => goodLens);
    const { repos, internal } = fakeRepos();
    internal.set('usage.daily', recordUse(null, 'transform', Date.now()));
    useAppStore.setState({ repos, settings: { ...DEFAULT_SETTINGS }, entitlements: FREE_STATE });
    await wrap(
      <PhotoPreviewScreen
        navigation={{ goBack: jest.fn() } as never}
        route={{ params: { imageId: 'img_1' } } as never}
      />,
    );
    await screen.findByText('Make it shine');
    await act(async () => {
      await Promise.resolve();
    });
    expect(screen.queryByTestId('lens-notice')).toBeNull();
    await fireEvent.press(screen.getByTestId('tool-transform'));
    expect(screen.getByText('1 free Transform left today')).toBeTruthy();
  });
});
