import { fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { BeautyTray, PortraitControlsPanel } from '../src/screens/camera/BeautyControls';
import { BeautyEditPanel } from '../src/screens/editor/BeautyPanel';
import { CameraService } from '../src/services/camera/CameraService';
import { BEAUTY_PRESETS, DEFAULT_PRESET_ID, findPreset, presetCover, presetTier } from '../src/services/beauty/catalog';
import {
  beautyChipLabel,
  cameraRecipe,
  DEFAULT_CAMERA_BEAUTY,
  effectivePreset,
  isCameraBeauty,
  isPortraitControls,
  type CameraBeauty,
} from '../src/services/beauty/camera';
import {
  DEFAULT_ADJUST,
  DEFAULT_PORTRAIT,
  GRADE_FLOATS,
  GRADE_ORDER,
  gradeFloats,
  hexToRgb,
  isActive,
  neutralRecipe,
  resolveRecipe,
  type BeautyRecipe,
} from '../src/services/beauty/recipe';
import { isValidSetting, DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { EMPTY_DOCUMENT, isEditDocument } from '../src/services/edit/document';
import { FEATURES } from '../src/services/entitlements/features';
import { ThemeProvider } from '../src/theme';

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const preset = (id: string) => findPreset(id)!;

/** Every number in a recipe, flattened (for comparing processing profiles). */
function numbers(r: BeautyRecipe): number[] {
  return [
    ...Object.values(r.portrait),
    ...Object.values(r.face).flatMap(v => (Array.isArray(v) ? v : [v])),
    ...gradeFloats(r.grade),
  ] as number[];
}

describe('the 13 beauty presets', () => {
  it('match the screenshots in order: 2 free, then 11 premium, each with its cover', () => {
    expect(BEAUTY_PRESETS.map(p => p.name)).toEqual([
      'Natural Glow',
      'Soft Radiance',
      'Velvet Skin',
      'Golden Goddess',
      'Crystal Beauty',
      'Blush Romance',
      'Bronze Luxe',
      'Pure Elegance',
      'Midnight Glam',
      'Peach Perfection',
      'Emerald Aura',
      'Dreamlight',
      'Studio Royal',
    ]);
    expect(BEAUTY_PRESETS.filter(p => p.tier === 'free').map(p => p.id)).toEqual(['natural_glow', 'soft_radiance']);
    expect(BEAUTY_PRESETS.filter(p => p.tier === 'premium')).toHaveLength(11);
    for (const p of BEAUTY_PRESETS) {
      expect(presetCover(p.id)).toBeTruthy();
      expect(p.description.length).toBeGreaterThan(10);
    }
    expect(new Set(BEAUTY_PRESETS.map(p => p.id)).size).toBe(13);
    expect(DEFAULT_PRESET_ID).toBe('natural_glow');
    expect(presetTier('golden_goddess')).toBe('premium');
    expect(presetTier('nope')).toBeNull();
    expect(FEATURES.beautyFilters.plan).toBe('free');
    expect(FEATURES.beautyPremium.plan).toBe('premium');
  });

  it('are distinct processing profiles, not colour overlays', () => {
    const recipes = BEAUTY_PRESETS.map(p => ({ id: p.id, r: resolveRecipe(p, DEFAULT_ADJUST) }));
    for (let i = 0; i < recipes.length; i++) {
      for (let j = i + 1; j < recipes.length; j++) {
        const a = numbers(recipes[i].r);
        const b = numbers(recipes[j].r);
        const differing = a.filter((v, k) => Math.abs(v - b[k]) >= 0.05).length;
        expect({ pair: `${recipes[i].id}/${recipes[j].id}`, differing: differing >= 5 }).toEqual({
          pair: `${recipes[i].id}/${recipes[j].id}`,
          differing: true,
        });
      }
    }
    // Every preset works on more than colour: skin, face or light are part of it.
    for (const { id, r } of recipes) {
      const skinOrFace = r.portrait.smooth + r.face.eyes + r.face.highlight + r.face.blush + r.face.contour;
      expect({ id, layered: skinOrFace > 0.3 }).toEqual({ id, layered: true });
    }
    // Portrait blur is never part of a preset.
    expect(recipes.every(({ r }) => r.portrait.blur === 0)).toBe(true);
  });
});

describe('resolving a recipe', () => {
  it('a neutral recipe changes nothing; intensity 0 is the photo as taken', () => {
    expect(isActive(neutralRecipe())).toBe(false);
    expect(isActive(resolveRecipe(preset('golden_goddess'), { ...DEFAULT_ADJUST, intensity: 0 }))).toBe(false);
    expect(isActive(resolveRecipe(preset('natural_glow'), DEFAULT_ADJUST))).toBe(true);
  });

  it('intensity scales the whole look; group sliders scale only their own layer', () => {
    const full = resolveRecipe(preset('golden_goddess'), DEFAULT_ADJUST);
    const half = resolveRecipe(preset('golden_goddess'), { ...DEFAULT_ADJUST, intensity: 0.5 });
    expect(half.grade.temperature).toBeCloseTo(full.grade.temperature / 2, 6);
    expect(half.portrait.smooth).toBeCloseTo(full.portrait.smooth / 2, 6);
    // Colours, positions and protection are the look itself: never scaled.
    expect(half.grade.bgGlowColor).toEqual(full.grade.bgGlowColor);
    expect(half.grade.bgGlowX).toBe(full.grade.bgGlowX);
    expect(half.grade.skinProtect).toBe(full.grade.skinProtect);
    const noBackground = resolveRecipe(preset('golden_goddess'), { ...DEFAULT_ADJUST, background: 0 });
    expect(noBackground.grade.bgGlow).toBe(0);
    expect(noBackground.grade.bgTintAmount).toBe(0);
    expect(noBackground.grade.temperature).toBe(full.grade.temperature); // colour is not "background"
    expect(noBackground.portrait.smooth).toBe(full.portrait.smooth);
    const moreSkin = resolveRecipe(preset('velvet_skin'), { ...DEFAULT_ADJUST, skin: 1.5 });
    expect(moreSkin.portrait.smooth).toBe(1); // 0.8 x 1.5, clamped
    const noLight = resolveRecipe(preset('midnight_glam'), { ...DEFAULT_ADJUST, lighting: 0 });
    expect(noLight.face.studio).toBe(0);
    expect(noLight.grade.rim).toBe(0);
    expect(noLight.face.contour).toBeGreaterThan(0); // contour is make-up (Face)
  });

  it('Portrait mode is independent of the preset', () => {
    // Natural Glow with strong blur…
    const glow = resolveRecipe(preset('natural_glow'), DEFAULT_ADJUST, { ...DEFAULT_PORTRAIT, depthBlur: 0.9 });
    expect(glow.portrait.blur).toBe(0.9);
    expect(glow.portrait.smooth).toBe(0.35);
    // …or Midnight Glam with none.
    const glam = resolveRecipe(preset('midnight_glam'), DEFAULT_ADJUST, { ...DEFAULT_PORTRAIT, depthBlur: 0 });
    expect(glam.portrait.blur).toBe(0);
    expect(glam.grade.bgExposure).toBe(-0.8);
    // Portrait's light and tone add to the preset's, within range.
    const lit = resolveRecipe(preset('midnight_glam'), DEFAULT_ADJUST, {
      ...DEFAULT_PORTRAIT,
      subjectLight: 0.5,
      backgroundLight: -0.5,
      backgroundTone: 0.7,
      edge: 1,
    });
    expect(lit.grade.subjectLight).toBeCloseTo(0.55, 6);
    expect(lit.grade.bgExposure).toBe(-1);
    expect(lit.grade.bgWarmth).toBe(0.7);
    expect(lit.portrait.edge).toBe(1);
    // Portrait alone (beauty off) still works.
    expect(isActive(resolveRecipe(null, DEFAULT_ADJUST, DEFAULT_PORTRAIT))).toBe(true);
  });

  it('flattens the grade in the engine order', () => {
    const r = resolveRecipe(preset('golden_goddess'), DEFAULT_ADJUST);
    const f = gradeFloats(r.grade);
    expect(f).toHaveLength(GRADE_FLOATS);
    expect(GRADE_ORDER[0]).toBe('temperature');
    expect(f[0]).toBe(r.grade.temperature);
    expect(f.slice(7, 10)).toEqual(r.grade.shadowColor);
    expect(f[41]).toBe(r.grade.bgGlow);
    expect(f[50]).toBe(r.grade.bgWarmth);
    expect(f.slice(51)).toEqual([0, 0, 0, 0, 0, 0, 0]);
  });

  it('reads colours safely', () => {
    expect(hexToRgb('#FF8000')).toEqual([1, 128 / 255, 0]);
    expect(hexToRgb('nope')).toEqual([0.5, 0.5, 0.5]);
    expect(hexToRgb(undefined, [1, 0, 0])).toEqual([1, 0, 0]);
  });
});

describe('beauty in the camera', () => {
  const on: CameraBeauty = { on: true, preset: 'golden_goddess', adjust: DEFAULT_ADJUST };

  it('falls back to a free preset when the plan does not include the chosen one', () => {
    expect(effectivePreset(on, true)?.id).toBe('golden_goddess');
    expect(effectivePreset(on, false)?.id).toBe('natural_glow');
    expect(beautyChipLabel(on, false)).toBe('Natural Glow');
    expect(beautyChipLabel({ ...on, on: false }, true)).toBeUndefined();
    expect(cameraRecipe({ ...on, on: false }, true, null)).toBeNull();
    expect(cameraRecipe({ ...on, on: false }, true, DEFAULT_PORTRAIT)?.portrait.blur).toBe(0.6);
  });

  it('settings are validated', () => {
    expect(isCameraBeauty(DEFAULT_CAMERA_BEAUTY)).toBe(true);
    expect(isCameraBeauty({ ...on, preset: 'made_up' })).toBe(false);
    expect(isCameraBeauty({ ...on, adjust: { ...DEFAULT_ADJUST, skin: 9 } })).toBe(false);
    expect(isPortraitControls(DEFAULT_PORTRAIT)).toBe(true);
    expect(isPortraitControls({ ...DEFAULT_PORTRAIT, depthBlur: -1 })).toBe(false);
    expect(isValidSetting('cameraBeauty', DEFAULT_SETTINGS.cameraBeauty)).toBe(true);
    expect(isValidSetting('portraitControls', { depthBlur: 'lots' })).toBe(false);
  });

  it('the native camera gets the live recipe and the Original switch', () => {
    const native = { setLiveBeauty: jest.fn(), setBeautyOriginal: jest.fn() };
    const service = new CameraService(native as never, { addListener: jest.fn() });
    const r = resolveRecipe(preset('dreamlight'), DEFAULT_ADJUST);
    service.setLiveBeauty(r);
    service.setLiveBeauty(null);
    service.setBeautyOriginal(true);
    expect(native.setLiveBeauty.mock.calls).toEqual([[r], [null]]);
    expect(native.setBeautyOriginal).toHaveBeenCalledWith(true);
  });

  it('the tray offers all filters, locks premium ones on the free plan and adjusts each layer', async () => {
    const onChange = jest.fn();
    const onLocked = jest.fn();
    await wrap(
      <BeautyTray
        beauty={{ ...on, preset: 'natural_glow' }}
        premiumAllowed={false}
        liveNote={null}
        onChange={onChange}
        onLocked={onLocked}
      />,
    );
    expect(screen.getByLabelText('Golden Goddess, premium')).toBeTruthy();
    expect(screen.getByLabelText('Natural Glow')).toBeTruthy();
    await fireEvent.press(screen.getByTestId('beauty-golden_goddess'));
    expect(onLocked).toHaveBeenCalled();
    expect(onChange).not.toHaveBeenCalled();
    await fireEvent.press(screen.getByTestId('beauty-soft_radiance'));
    expect(onChange).toHaveBeenLastCalledWith(expect.objectContaining({ on: true, preset: 'soft_radiance' }));
    await fireEvent.press(screen.getByTestId('beauty-off'));
    expect(onChange).toHaveBeenLastCalledWith(expect.objectContaining({ on: false }));
    // Intensity, then the four layers.
    await fireEvent(screen.getByTestId('beauty-intensity'), 'accessibilityAction', {
      nativeEvent: { actionName: 'decrement' },
    });
    expect(onChange).toHaveBeenLastCalledWith(
      expect.objectContaining({ adjust: expect.objectContaining({ intensity: 0.95 }) }),
    );
    expect(screen.queryByTestId('beauty-skin')).toBeNull();
    await fireEvent.press(screen.getByTestId('beauty-adjust'));
    await fireEvent(screen.getByTestId('beauty-background'), 'accessibilityAction', {
      nativeEvent: { actionName: 'decrement' },
    });
    expect(onChange).toHaveBeenLastCalledWith(
      expect.objectContaining({ adjust: expect.objectContaining({ background: 0.95, intensity: 1 }) }),
    );
  });

  it('Portrait controls: depth blur, beauty, then light, tone and edges', async () => {
    const onControls = jest.fn();
    const onIntensity = jest.fn();
    await wrap(
      <PortraitControlsPanel
        controls={DEFAULT_PORTRAIT}
        intensity={0.8}
        points={0}
        onControls={onControls}
        onIntensity={onIntensity}
        onClear={jest.fn()}
      />,
    );
    await fireEvent(screen.getByTestId('portrait-blur'), 'accessibilityAction', {
      nativeEvent: { actionName: 'increment' },
    });
    expect(onControls).toHaveBeenLastCalledWith({ ...DEFAULT_PORTRAIT, depthBlur: 0.65 });
    await fireEvent(screen.getByTestId('portrait-beauty'), 'accessibilityAction', {
      nativeEvent: { actionName: 'increment' },
    });
    expect(onIntensity.mock.calls.at(-1)?.[0]).toBeCloseTo(0.85, 6);
    expect(screen.queryByTestId('portrait-bg-tone')).toBeNull();
    await fireEvent.press(screen.getByTestId('portrait-more'));
    await fireEvent(screen.getByTestId('portrait-bg-tone'), 'accessibilityAction', {
      nativeEvent: { actionName: 'increment' },
    });
    expect(onControls).toHaveBeenLastCalledWith({ ...DEFAULT_PORTRAIT, backgroundTone: 0.05 });
    expect(screen.getByLabelText('Background tone')).toBeTruthy();
  });
});

describe('beauty in the editor', () => {
  it('a filter goes into the edit with its resolved recipe; None removes it', async () => {
    const update = jest.fn();
    const onPremium = jest.fn();
    const { rerender } = await wrap(
      <BeautyEditPanel
        doc={EMPTY_DOCUMENT}
        update={update}
        commit={jest.fn()}
        premiumAllowed={false}
        onPremium={onPremium}
      />,
    );
    await fireEvent.press(screen.getByTestId('edit-beauty-dreamlight'));
    expect(onPremium).toHaveBeenCalled();
    await fireEvent.press(screen.getByTestId('edit-beauty-natural_glow'));
    const patch = update.mock.calls.at(-1)?.[0];
    expect(patch.beauty.preset).toBe('natural_glow');
    expect(patch.beauty.recipe).toEqual(resolveRecipe(preset('natural_glow'), DEFAULT_ADJUST));
    const doc = { ...EMPTY_DOCUMENT, beauty: patch.beauty };
    expect(isEditDocument(doc)).toBe(true);
    await rerender(
      <ThemeProvider preference="dark">
        <BeautyEditPanel doc={doc} update={update} commit={jest.fn()} premiumAllowed onPremium={onPremium} />
      </ThemeProvider>,
    );
    await fireEvent(screen.getByTestId('edit-beauty-intensity'), 'accessibilityAction', {
      nativeEvent: { actionName: 'decrement' },
    });
    expect(update.mock.calls.at(-1)?.[0].beauty.recipe.portrait.smooth).toBeCloseTo(0.35 * 0.95, 6);
    await fireEvent.press(screen.getByText('None'));
    expect(update.mock.calls.at(-1)?.[0]).toEqual({ beauty: null });
    expect(isEditDocument({ ...EMPTY_DOCUMENT, beauty: { preset: 'x', adjust: {}, recipe: {} } })).toBe(false);
  });
});
