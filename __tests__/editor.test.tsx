import { act, fireEvent, render, screen, waitFor } from '@testing-library/react-native';
import React from 'react';

import { EditorScreen } from '../src/screens/editor/EditorScreen';
import { aspectCrop, containRect, isFullCrop, viewToPhoto } from '../src/screens/editor/geometry';
import { DEFAULT_SETTINGS, isValidSetting } from '../src/services/database/repositories/SettingsRepository';
import { PROPS } from '../src/services/edit/catalog';
import {
  EMPTY_DOCUMENT,
  geometryOf,
  hasEdits,
  isEditDocument,
  portraitFromGlow,
  withDefaults,
} from '../src/services/edit/document';
import { canRedo, canUndo, historyReducer, MAX_STEPS, startHistory } from '../src/services/edit/history';
import { defaultPlacement, fitToFace, mainFace, type FaceGeometry } from '../src/services/edit/placement';
import { DEFAULT_WATERMARK, freeSpec, isBrandingPresets, isWatermarkSpec } from '../src/services/edit/watermark';
import { FREE_STATE } from '../src/services/entitlements/EntitlementService';
import { captureRecipe, transformPlan } from '../src/services/processing/recipe';
import { cleanOf, filesToDelete } from '../src/services/processing/quality';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';

const face: FaceGeometry = {
  top: { x: 0.5, y: 0.2 },
  chin: { x: 0.5, y: 0.5 },
  eyeL: { x: 0.42, y: 0.32 },
  eyeR: { x: 0.58, y: 0.32 },
  width: 0.3,
};

const mockEngine = {
  renderEdit: jest.fn(
    async (
      _id: string,
      r: {
        outputPath: string;
        cleanOutputPath?: string;
        preview?: boolean;
        doc: typeof EMPTY_DOCUMENT;
        skipProps?: number[];
        skipFrame?: boolean;
      },
    ) => ({
      outputPath: r.outputPath,
      cleanPath: r.preview === false ? (r.cleanOutputPath ?? null) : null,
      width: 900,
      height: 1200,
      sizeBytes: 500_000,
      photoRect: [0, 0, 1, 1],
      portrait: null,
    }),
  ),
  exportImage: jest.fn(async () => ({
    outputPath: '/data/exports/PhotoPlus_2.jpg',
    width: 3000,
    height: 4000,
    sizeBytes: 1_000_000,
    galleryUri: null,
    watermark: { applied: true, enforced: false, plan: 'free' },
  })),
  detectFaces: jest.fn(async () => ({ faces: [face] })),
  detectPerspective: jest.fn(async () => ({ vertical: 0.42 })),
  cancel: jest.fn(),
};
const mockStorage = {
  getPaths: jest.fn(async () => ({ processingCache: '/cache/processing' })),
  fileExists: jest.fn(async () => false),
  deleteFile: jest.fn(async () => true),
  shareImages: jest.fn(async () => true),
  pickImage: jest.fn(async () => null),
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

// A free-plan camera photo: carries the mark, with its clean companion.
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
  createdAt: 1,
  metadata: { captureMode: 'photo', lens: 'back-wide', cleanPath: '/data/captures/PP_1_clean.jpg' } as Record<
    string,
    unknown
  >,
};

function setup() {
  const added: { path: string; metadata: Record<string, unknown> }[] = [];
  const internal = new Map<string, unknown>();
  const updateSetting = jest.fn(async () => undefined);
  useAppStore.setState({
    repos: {
      images: {
        get: jest.fn(async () => photo),
        add: jest.fn(async (img: { path: string; metadata: Record<string, unknown> }) => {
          added.push(img);
          return { ...img, id: 'img_9' };
        }),
      },
      settings: {
        getInternal: jest.fn(async (k: string) => internal.get(k) ?? null),
        setInternal: jest.fn(async (k: string, v: unknown) => {
          internal.set(k, v);
        }),
      },
    } as never,
    settings: { ...DEFAULT_SETTINGS },
    entitlements: FREE_STATE,
    loadProjects: jest.fn(async () => undefined),
    updateSetting: updateSetting as never,
  });
  return { added, internal, updateSetting };
}

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);

beforeEach(() => jest.clearAllMocks());

describe('undo and redo', () => {
  it('every change can be undone and redone; a drag is one step', () => {
    let h = startHistory(EMPTY_DOCUMENT);
    h = historyReducer(h, { type: 'set', doc: { ...h.present, exposure: 0.1 }, group: 'exposure' });
    h = historyReducer(h, { type: 'set', doc: { ...h.present, exposure: 0.4 }, group: 'exposure' });
    h = historyReducer(h, { type: 'commit' });
    h = historyReducer(h, { type: 'set', doc: { ...h.present, contrast: 0.2 } });
    expect(h.past).toHaveLength(2); // the drag counted once
    h = historyReducer(h, { type: 'undo' });
    expect(h.present.contrast).toBe(0);
    expect(h.present.exposure).toBe(0.4);
    h = historyReducer(h, { type: 'undo' });
    expect(h.present).toEqual(EMPTY_DOCUMENT); // back to the untouched photo
    expect(canUndo(h)).toBe(false);
    h = historyReducer(h, { type: 'redo' });
    h = historyReducer(h, { type: 'redo' });
    expect(h.present.contrast).toBe(0.2);
    expect(canRedo(h)).toBe(false);
    // A new change after undo drops the redo branch.
    h = historyReducer(h, { type: 'undo' });
    h = historyReducer(h, { type: 'set', doc: { ...h.present, warmth: 0.3 } });
    expect(canRedo(h)).toBe(false);
    // The history is bounded.
    for (let i = 0; i < 100; i++) h = historyReducer(h, { type: 'set', doc: { ...h.present, sharpen: i / 100 } });
    expect(h.past.length).toBe(MAX_STEPS);
  });
});

describe('editor geometry', () => {
  it('maps taps through letterboxing and frames, and crops to a shape', () => {
    expect(containRect(300, 400, 900, 600)).toEqual({ x: 0, y: 100, w: 300, h: 200 });
    expect(viewToPhoto({ x: 150, y: 200 }, { width: 300, height: 400 }, { width: 900, height: 600 })).toEqual({
      x: 0.5,
      y: 0.5,
    });
    expect(viewToPhoto({ x: 150, y: 50 }, { width: 300, height: 400 }, { width: 900, height: 600 })).toBeNull(); // letterbox
    // Inside a frame window covering the middle half.
    const p = viewToPhoto(
      { x: 150, y: 200 },
      { width: 300, height: 400 },
      { width: 300, height: 400 },
      [0.25, 0.25, 0.5, 0.5],
    );
    expect(p).toEqual({ x: 0.5, y: 0.5 });
    expect(
      viewToPhoto({ x: 10, y: 10 }, { width: 300, height: 400 }, { width: 300, height: 400 }, [0.25, 0.25, 0.5, 0.5]),
    ).toBeNull();
    const sq = aspectCrop(1, { width: 3000, height: 4000 });
    expect(sq.w * 3000).toBeCloseTo(sq.h * 4000);
    expect(sq.w).toBe(1);
    const small = aspectCrop(1, { width: 3000, height: 4000 }, 0.5, 0, 0);
    expect(small.x).toBe(0); // kept inside the photo
    expect(isFullCrop(aspectCrop(null, { width: 3000, height: 4000 }))).toBe(true);
  });
});

describe('portrait settings in saved edits', () => {
  it('older documents open without them; bad values are refused', () => {
    expect(isEditDocument({ ...EMPTY_DOCUMENT })).toBe(true);
    const { portrait: _p, ...old } = EMPTY_DOCUMENT;
    expect(isEditDocument(old)).toBe(true);
    expect(withDefaults(old as never).portrait).toBeNull();
    const good = { ...EMPTY_DOCUMENT, portrait: { ...portraitFromGlow(0.5), bgColor: -1, lightAngle: 1 } };
    expect(isEditDocument(good)).toBe(true);
    expect(isEditDocument({ ...good, portrait: { ...good.portrait, eyes: 2 } })).toBe(false);
    expect(isEditDocument({ ...good, portrait: { ...good.portrait, smooth: -0.1 } })).toBe(false);
    expect(isEditDocument({ ...good, portrait: { eyes: 0.5 } })).toBe(false);
  });
});

describe('perspective in saved edits', () => {
  it('older documents without perspective still open, and gain it unchanged', () => {
    const { perspectiveV: _v, perspectiveH: _h, ...old } = { ...EMPTY_DOCUMENT, exposure: 0.4 };
    expect(isEditDocument(old)).toBe(true);
    const filled = withDefaults(old as never);
    expect(filled.perspectiveV).toBe(0);
    expect(filled.perspectiveH).toBe(0);
    expect(filled.exposure).toBe(0.4);
    expect(hasEdits(withDefaults({ ...EMPTY_DOCUMENT }))).toBe(false);
    expect(isEditDocument({ ...EMPTY_DOCUMENT, perspectiveV: 2 })).toBe(false);
    expect(isEditDocument({ ...EMPTY_DOCUMENT, perspectiveH: Number.NaN })).toBe(false);
  });
  it('perspective changes the geometry that faces and props are measured on', () => {
    expect(geometryOf({ ...EMPTY_DOCUMENT, perspectiveV: 0.3 })).not.toEqual(geometryOf(EMPTY_DOCUMENT));
    expect(geometryOf({ ...EMPTY_DOCUMENT, exposure: 1 })).toEqual(geometryOf(EMPTY_DOCUMENT));
  });
});

describe('props fit the face', () => {
  it('glasses sit between the eyes; hats above the forehead; outfit below the chin; tilted heads tilt them', () => {
    const glasses = PROPS.find(p => p.id === 'aviators')!;
    const g = fitToFace(glasses, face, 0.75);
    expect(g.cx).toBeCloseTo(0.5);
    expect(Math.abs(g.cy - 0.32)).toBeLessThan(0.01);
    expect(g.width).toBeCloseTo(0.16 / 0.4); // lens centres span the eye distance
    expect(g.rotation).toBeCloseTo(0);
    const hat = fitToFace(
      PROPS.find(p => p.id === 'fedora')!,
      face,
      0.75,
    );
    expect(hat.cy).toBeLessThan(face.top.y + 0.05);
    const tie = fitToFace(
      PROPS.find(p => p.id === 'bowtie')!,
      face,
      0.75,
    );
    expect(tie.cy).toBeGreaterThan(face.chin.y);
    // Head tilted: everything turns with it.
    const tilted = fitToFace(glasses, { ...face, eyeL: { x: 0.42, y: 0.3 }, eyeR: { x: 0.58, y: 0.34 } }, 0.75);
    expect(tilted.rotation).toBeGreaterThan(5);
    expect(defaultPlacement(glasses)).toMatchObject({ cx: 0.5, width: 0.35 });
    expect(mainFace([face, { ...face, width: 0.1 }])).toBe(face);
    expect(mainFace([])).toBeNull();
  });
});

describe('watermark rules (app side)', () => {
  it('defaults to the signature mark at the bottom right; bad settings are refused', () => {
    expect(DEFAULT_WATERMARK).toMatchObject({ style: 'signature', position: 'br', enabled: true, custom: null });
    expect(isWatermarkSpec(DEFAULT_WATERMARK)).toBe(true);
    expect(isWatermarkSpec({ ...DEFAULT_WATERMARK, style: 'neon' })).toBe(false);
    expect(isWatermarkSpec({ ...DEFAULT_WATERMARK, size: 2 })).toBe(false);
    expect(
      isWatermarkSpec({ ...DEFAULT_WATERMARK, custom: { text: 'x', color: 'red', logoPath: null, align: 'left' } }),
    ).toBe(false);
    expect(
      freeSpec({
        ...DEFAULT_WATERMARK,
        enabled: false,
        custom: { text: 'Me', color: '#FFFFFF', logoPath: null, align: 'left' },
      }),
    ).toMatchObject({
      enabled: true,
      custom: null,
    });
    expect(isValidSetting('watermark', DEFAULT_WATERMARK)).toBe(true);
    expect(isValidSetting('brandingPresets', [{ id: 'a', name: 'Studio', spec: DEFAULT_WATERMARK }])).toBe(true);
    expect(isBrandingPresets([{ id: 'a', name: 'x'.repeat(40), spec: DEFAULT_WATERMARK }])).toBe(false);
  });

  it('edits start from the clean photo, so the mark is never doubled; deleting removes both', () => {
    expect(cleanOf(photo)).toBe('/data/captures/PP_1_clean.jpg');
    expect(cleanOf({ path: '/a.jpg', metadata: {} })).toBe('/a.jpg');
    expect(filesToDelete(photo)).toEqual(['/data/captures/PP_1.jpg', '/data/captures/PP_1_clean.jpg']);
    expect(transformPlan(photo).inputPath).toBe('/data/captures/PP_1_clean.jpg');
    expect(captureRecipe({ path: '/n.jpg', mode: 'night', cleanPath: '/n_clean.jpg' })?.basePath).toBe('/n_clean.jpg');
  });

  it('documents are validated when read back', () => {
    expect(isEditDocument(EMPTY_DOCUMENT)).toBe(true);
    expect(isEditDocument({ ...EMPTY_DOCUMENT, exposure: 9 })).toBe(false);
    expect(isEditDocument({ ...EMPTY_DOCUMENT, quarterTurns: 5 })).toBe(false);
    expect(hasEdits(EMPTY_DOCUMENT)).toBe(false);
    expect(hasEdits({ ...EMPTY_DOCUMENT, contrast: 0.1 })).toBe(true);
  });
});

describe('the photo editor', () => {
  const lastPreviewDoc = () => (mockEngine.renderEdit.mock.calls.at(-1)?.[1] as { doc: typeof EMPTY_DOCUMENT }).doc;

  it('previews live, undoes and redoes, fits props, and saves a new copy (original untouched)', async () => {
    const { added } = setup();
    const navigation = { goBack: jest.fn(), replace: jest.fn() };
    await wrap(<EditorScreen navigation={navigation as never} route={{ params: { imageId: 'img_1' } } as never} />);
    await waitFor(() => expect(mockEngine.renderEdit).toHaveBeenCalled());
    // Works on the clean photo, with the watermark shown as export draws it.
    const first = mockEngine.renderEdit.mock.calls[0][1] as Record<string, unknown>;
    expect(first).toMatchObject({
      inputPath: '/data/captures/PP_1_clean.jpg',
      preview: true,
      watermark: DEFAULT_WATERMARK,
    });

    // Adjust exposure (slider step), then undo / redo.
    await act(async () => {
      fireEvent(screen.getByTestId('slider-exposure'), 'accessibilityAction', {
        nativeEvent: { actionName: 'increment' },
      });
    });
    await waitFor(() => expect(lastPreviewDoc().exposure).toBeGreaterThan(0));
    await fireEvent.press(screen.getByTestId('undo'));
    await waitFor(() => expect(lastPreviewDoc().exposure).toBe(0));
    await fireEvent.press(screen.getByTestId('redo'));
    await waitFor(() => expect(lastPreviewDoc().exposure).toBeGreaterThan(0));

    // Props: a pair of glasses fits itself to the face.
    await fireEvent.press(screen.getByTestId('editor-tool-props'));
    await fireEvent.press(screen.getByRole('radio', { name: 'Glasses' }));
    await fireEvent.press(screen.getByTestId('prop-aviators'));
    await waitFor(() => expect(mockEngine.detectFaces).toHaveBeenCalled());
    await waitFor(() => expect(lastPreviewDoc().props).toHaveLength(1));
    const placed = lastPreviewDoc().props[0];
    expect(placed.id).toBe('aviators');
    expect(Math.abs(placed.cy - 0.32)).toBeLessThan(0.02);
    // While a prop is selected the preview leaves it out (it is shown live on top) and hides frames.
    const call = mockEngine.renderEdit.mock.calls.at(-1)?.[1] as { skipProps: number[]; skipFrame: boolean };
    expect(call.skipProps).toEqual([0]);
    expect(call.skipFrame).toBe(true);

    // Save: a new library photo with the edits; the source photo stays as it is.
    await fireEvent.press(screen.getByTestId('editor-save'));
    await waitFor(() => expect(navigation.replace).toHaveBeenCalledWith('PhotoPreview', { imageId: 'img_9' }));
    const saveCall = mockEngine.renderEdit.mock.calls.find(
      c => (c[1] as { preview?: boolean }).preview === false,
    )?.[1] as Record<string, unknown>;
    expect(saveCall).toMatchObject({
      inputPath: '/data/captures/PP_1_clean.jpg',
      outputPath: '/data/captures/PP_1_clean_edit.jpg',
      cleanOutputPath: '/data/captures/PP_1_clean_edit_clean.jpg',
      watermark: DEFAULT_WATERMARK,
    });
    expect(added[0].metadata).toMatchObject({
      captureMode: 'edit',
      editBase: '/data/captures/PP_1_clean.jpg',
      cleanPath: '/data/captures/PP_1_clean_edit_clean.jpg',
      edit: expect.objectContaining({ props: [expect.objectContaining({ id: 'aviators' })] }),
    });
  });

  it('shares through export; Spotlight taps choose the subject; Transform counts a free use', async () => {
    const { internal } = setup();
    await wrap(
      <EditorScreen navigation={{ goBack: jest.fn() } as never} route={{ params: { imageId: 'img_1' } } as never} />,
    );
    await waitFor(() => expect(mockEngine.renderEdit).toHaveBeenCalled());
    const photoView = screen.getByTestId('editor-photo');
    await act(async () => {
      fireEvent(photoView, 'layout', { nativeEvent: { layout: { width: 300, height: 400 } } });
    });
    await fireEvent.press(screen.getByTestId('editor-tool-spotlight'));
    // A tap (touch without movement) in the middle of the photo.
    await act(async () => {
      fireEvent(photoView, 'responderGrant', {
        nativeEvent: { locationX: 150, locationY: 200, pageX: 150, pageY: 200 },
      });
      fireEvent(photoView, 'responderRelease', { nativeEvent: {} });
    });
    await waitFor(() => expect(lastPreviewDoc().spotlight?.points).toEqual([0.5, 0.5]));

    await fireEvent.press(screen.getByTestId('editor-tool-transform'));
    expect(screen.getByText('2 free Transforms left today')).toBeTruthy();
    await fireEvent.press(screen.getByRole('radio', { name: 'On' }));
    await fireEvent.press(screen.getByTestId('editor-share'));
    await waitFor(() =>
      expect(mockStorage.shareImages).toHaveBeenCalledWith(['/data/exports/PhotoPlus_2.jpg'], 'Share your photo'),
    );
    expect(mockEngine.exportImage).toHaveBeenCalledWith(
      expect.any(String),
      expect.objectContaining({
        inputPath: '/data/captures/PP_1_clean.jpg',
        destination: 'share',
        watermark: DEFAULT_WATERMARK,
        doc: expect.objectContaining({ transform: true, spotlight: { points: [0.5, 0.5], blur: 0.6 } }),
      }),
    );
    expect(internal.get('usage.daily')).toMatchObject({ counts: { transform: 1 } });
  });

  it('Auto straighten lines applies the detected perspective, then sliders fine-tune and undo it', async () => {
    setup();
    await wrap(
      <EditorScreen navigation={{ goBack: jest.fn() } as never} route={{ params: { imageId: 'img_1' } } as never} />,
    );
    await waitFor(() => expect(mockEngine.renderEdit).toHaveBeenCalled());
    await fireEvent.press(screen.getByTestId('editor-tool-crop'));
    await fireEvent.press(screen.getByTestId('perspective-auto'));
    await waitFor(() => expect(lastPreviewDoc().perspectiveV).toBe(0.42));
    expect(mockEngine.detectPerspective).toHaveBeenCalledWith(
      expect.objectContaining({ inputPath: '/data/captures/PP_1_clean.jpg' }),
    );
    await act(async () => {
      fireEvent(screen.getByTestId('perspective-h'), 'accessibilityAction', {
        nativeEvent: { actionName: 'increment' },
      });
    });
    await waitFor(() => expect(lastPreviewDoc().perspectiveH).toBeGreaterThan(0));
    await fireEvent.press(screen.getByTestId('undo'));
    await fireEvent.press(screen.getByTestId('undo'));
    await waitFor(() => expect(lastPreviewDoc().perspectiveV).toBe(0));
  });

  it('Portrait: Glow presets, then every control on its own, live, undoable and restorable', async () => {
    setup();
    await wrap(
      <EditorScreen navigation={{ goBack: jest.fn() } as never} route={{ params: { imageId: 'img_1' } } as never} />,
    );
    await waitFor(() => expect(mockEngine.renderEdit).toHaveBeenCalled());
    await fireEvent.press(screen.getByTestId('editor-tool-retouch'));
    // Basic: a Glow preset.
    await fireEvent.press(screen.getByRole('radio', { name: 'Smooth' }));
    await waitFor(() => expect(lastPreviewDoc().retouch).toBe(0.75));
    // Advanced starts where the preset left the photo.
    await fireEvent.press(screen.getByRole('radio', { name: 'Advanced' }));
    const step = async (id: string) =>
      act(async () => {
        fireEvent(screen.getByTestId(id), 'accessibilityAction', { nativeEvent: { actionName: 'increment' } });
      });
    await step('portrait-eyes');
    await waitFor(() => expect(lastPreviewDoc().portrait?.eyes).toBeGreaterThan(0));
    expect(lastPreviewDoc().portrait).toMatchObject({ smooth: 0.75, blemish: 0.75, even: 0.75 });
    await step('portrait-studio');
    // Studio light reveals its direction control.
    await step('portrait-lightAngle');
    await waitFor(() => expect(lastPreviewDoc().portrait?.lightAngle).toBeGreaterThan(-0.5));
    // Undo walks back one control at a time.
    await fireEvent.press(screen.getByTestId('undo'));
    await waitFor(() => expect(lastPreviewDoc().portrait?.lightAngle).toBe(-0.5));
    // Reset to Glow keeps the preset's skin and clears the fine-tuning.
    await fireEvent.press(screen.getByTestId('portrait-reset'));
    await waitFor(() => expect(lastPreviewDoc().portrait).toMatchObject({ eyes: 0, studio: 0, smooth: 0.75 }));
    // Restore original: the face as it was.
    await fireEvent.press(screen.getByTestId('portrait-restore'));
    await waitFor(() => expect(lastPreviewDoc().retouch).toBe(0));
    expect(lastPreviewDoc().portrait).toBeNull();
  });

  it('Spotlight background controls: blur amount, light, black-and-white, edges', async () => {
    setup();
    await wrap(
      <EditorScreen navigation={{ goBack: jest.fn() } as never} route={{ params: { imageId: 'img_1' } } as never} />,
    );
    await waitFor(() => expect(mockEngine.renderEdit).toHaveBeenCalled());
    await fireEvent.press(screen.getByTestId('editor-tool-spotlight'));
    await fireEvent.press(screen.getByRole('radio', { name: 'Medium' }));
    await waitFor(() => expect(lastPreviewDoc().spotlight?.blur).toBe(0.6));
    const step = async (id: string, actionName: 'increment' | 'decrement') =>
      act(async () => {
        fireEvent(screen.getByTestId(id), 'accessibilityAction', { nativeEvent: { actionName } });
      });
    await step('spotlight-blur', 'increment');
    await waitFor(() => expect(lastPreviewDoc().spotlight?.blur).toBeGreaterThan(0.6));
    await step('portrait-bgColor', 'decrement');
    await step('portrait-edge', 'increment');
    await waitFor(() => expect(lastPreviewDoc().portrait).toMatchObject({ edge: expect.any(Number) }));
    expect(lastPreviewDoc().portrait?.bgColor).toBeLessThan(0);
    expect(lastPreviewDoc().portrait?.edge).toBeGreaterThan(0);
  });

  it('free users style the Photo Plus mark; custom branding asks for Premium', async () => {
    const { updateSetting } = setup();
    await wrap(
      <EditorScreen navigation={{ goBack: jest.fn() } as never} route={{ params: { imageId: 'img_1' } } as never} />,
    );
    await waitFor(() => expect(mockEngine.renderEdit).toHaveBeenCalled());
    await fireEvent.press(screen.getByTestId('editor-tool-watermark'));
    await fireEvent.press(screen.getByTestId('wm-pos-tl'));
    expect(updateSetting).toHaveBeenLastCalledWith('watermark', expect.objectContaining({ position: 'tl' }));
    await fireEvent.press(screen.getByTestId('wm-style-pill'));
    expect(updateSetting).toHaveBeenLastCalledWith('watermark', expect.objectContaining({ style: 'pill' }));
    await fireEvent.press(screen.getByTestId('wm-restore'));
    expect(updateSetting).toHaveBeenLastCalledWith('watermark', expect.objectContaining({ position: 'br' }));
    const calls = updateSetting.mock.calls.length;
    await fireEvent.press(screen.getByTestId('wm-logo'));
    expect(await screen.findByText('Custom watermark & branding')).toBeTruthy(); // the Premium sheet
    expect(updateSetting.mock.calls.length).toBe(calls);
    expect(screen.getByText('Photos you share carry the Photo Plus mark. Your original stays untouched.')).toBeTruthy();
  });
});
