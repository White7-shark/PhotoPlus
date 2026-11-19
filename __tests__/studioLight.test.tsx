import { fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { FinishPanel, LightPanel, TemplateRow } from '../src/screens/studio/StudioLightPanel';
import { FEATURES } from '../src/services/entitlements/features';
import {
  FILM_GRADES,
  GRADIENTS,
  LIGHT_PRESETS,
  NO_FINISH,
  TEMPLATES,
  finishRequest,
  isFilmFinish,
  isPremiumFinish,
  isPremiumLight,
  lightRequest,
} from '../src/services/studio/light';
import { studioLightRequest } from '../src/services/studio/StudioService';
import { backgroundRequest, lookFor } from '../src/services/studio/scene';
import { ThemeProvider } from '../src/theme';

// Jest runs on Node: read the device code to compare the free/Premium rules.
declare const __dirname: string;
declare function require(name: string): unknown;
const fs = require('fs') as { readFileSync: (file: string, encoding: 'utf8') => string };
const path = require('path') as { join: (...parts: string[]) => string };

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const FULL = { x: 0, y: 0, w: 1, h: 1 };

describe('studio light catalogue', () => {
  it('has the looks, grades, gradients and templates, with ids unique', () => {
    for (const list of [LIGHT_PRESETS, FILM_GRADES, GRADIENTS, TEMPLATES]) {
      expect(new Set(list.map(x => x.id)).size).toBe(list.length);
    }
    expect(LIGHT_PRESETS.map(p => p.id)).toEqual(
      expect.arrayContaining(['soft-studio', 'rembrandt', 'butterfly', 'split', 'loop', 'clamshell', 'rim-light', 'low-key', 'high-key', 'golden-hour', 'neon-gels', 'noir']),
    );
    // Every look's backdrop exists; every colour is a colour.
    const backdrops = LIGHT_PRESETS.flatMap(p => (p.gradient ? [p.gradient] : []));
    expect(backdrops.every(id => GRADIENTS.some(g => g.id === id))).toBe(true);
    for (const g of GRADIENTS) expect(/^#[0-9A-F]{6}$/.test(g.from) && /^#[0-9A-F]{6}$/.test(g.to)).toBe(true);
    expect(FEATURES.studioLight.plan).toBe('premium');
    expect(FEATURES.studioBasics.plan).toBe('free');
  });

  it('agrees with the device on what is free', () => {
    // The device's free looks (SceneLight.FREE_LIGHT) are exactly the free ones here.
    const kt = fs.readFileSync(path.join(__dirname, '../android/app/src/main/java/com/photoplus/studio/SceneStudio.kt'), 'utf8');
    const m = kt.match(/FREE_LIGHT = setOf\(([^)]*)\)/);
    expect(m).not.toBeNull();
    const deviceFree = [...m![1].matchAll(/"([a-z0-9-]+)"/g)].map(x => x[1]).sort();
    expect(LIGHT_PRESETS.filter(p => !p.premium && p.light).map(p => p.id).sort()).toEqual(deviceFree);
    // The device treats film settings (fade, toning, grain, black and white) as Premium: so must the grades.
    for (const g of FILM_GRADES) expect(isFilmFinish(g.finish)).toBe(g.premium);
    // A free look never brings a film finish with it.
    expect(LIGHT_PRESETS.filter(p => !p.premium && p.finish && isFilmFinish(p.finish))).toEqual([]);
  });
});

describe('studio light requests', () => {
  it('sends a look by name, and a changed one as Premium "custom"', () => {
    expect(lightRequest(null)).toBeUndefined();
    expect(lightRequest({ preset: 'as-shot', strength: 1 })).toBeUndefined();
    const soft = lightRequest({ preset: 'soft-studio', strength: 0.6 })!;
    expect(soft).toMatchObject({ preset: 'soft-studio', strength: 0.6, angle: -20, softness: 0.85, rimLeft: 0, rimRight: 0 });
    expect(isPremiumLight({ preset: 'soft-studio', strength: 0.6 })).toBe(false);
    const turned = lightRequest({ preset: 'soft-studio', strength: 1, angle: 40 })!;
    expect(turned.preset).toBe('custom');
    expect(turned.angle).toBe(40);
    expect(isPremiumLight({ preset: 'soft-studio', strength: 1, angle: 40 })).toBe(true);
    const neon = lightRequest({ preset: 'neon-gels', strength: 1, rim: 0.3 })!;
    expect(neon.keyColour).toEqual([1.45, 0.55, 1.25]);
    expect(neon.rimLeft).toBe(0.3);
    expect(isPremiumLight({ preset: 'neon-gels', strength: 1 })).toBe(true);
    expect(isPremiumLight({ preset: 'no-such', strength: 1 })).toBe(true);
  });

  it('builds the finish from the grade, the look and the person’s own settings', () => {
    expect(finishRequest(NO_FINISH)).toBeUndefined();
    expect(finishRequest({ ...NO_FINISH, vignette: 0.4 })).toEqual({ vignette: 0.4 });
    const bw = finishRequest({ ...NO_FINISH, grade: 'black-white' })!;
    expect(bw.saturation).toBe(-1);
    expect(isPremiumFinish({ ...NO_FINISH, grade: 'black-white' })).toBe(false);
    expect(isPremiumFinish({ ...NO_FINISH, grade: 'noir' })).toBe(true);
    // The look's vignette and the person's add up, held in range.
    expect(finishRequest({ ...NO_FINISH, vignette: 0.9 }, { vignette: 0.55 })!.vignette).toBe(1);
    // A transparent cut-out gets the light but no finish.
    const r = studioLightRequest({ preset: 'low-key', strength: 1 }, NO_FINISH, true);
    expect(r.light).toBeDefined();
    expect(r.finish).toBeUndefined();
    const r2 = studioLightRequest({ preset: 'low-key', strength: 1 }, NO_FINISH, false);
    expect(r2.finish).toMatchObject({ vignette: 0.55, contrast: 0.25 });
    expect(studioLightRequest(null, NO_FINISH, false)).toEqual({});
  });

  it('sends gradient backdrops and lights them like studio paper', () => {
    const g = GRADIENTS[0];
    expect(backgroundRequest({ kind: 'gradient', gradient: g })).toEqual({ kind: 'gradient', from: g.from, to: g.to, radial: g.radial });
    expect(lookFor({ kind: 'gradient', gradient: g }, FULL).bounce).toBe(0.7);
  });
});

describe('studio light panels', () => {
  it('chooses free looks, opens Premium for the others and for turning the light', async () => {
    const onChoose = jest.fn(),
      onChange = jest.fn(),
      onLocked = jest.fn();
    const { rerender } = await wrap(<LightPanel value={null} onChoose={onChoose} onChange={onChange} premium={false} onLocked={onLocked} />);
    await fireEvent.press(screen.getByTestId('light-soft-studio'));
    expect(onChoose).toHaveBeenCalledWith('soft-studio');
    await fireEvent.press(screen.getByTestId('light-rembrandt'));
    expect(onLocked).toHaveBeenCalledTimes(1);
    expect(screen.getByLabelText('Rembrandt, Premium')).toBeTruthy();
    await rerender(
      <ThemeProvider preference="dark">
        <LightPanel value={{ preset: 'soft-studio', strength: 1 }} onChoose={onChoose} onChange={onChange} premium={false} onLocked={onLocked} />
      </ThemeProvider>,
    );
    expect(screen.getByText(/big soft light/)).toBeTruthy();
    // Strength is free; direction is Premium (the slider is off).
    await fireEvent(screen.getByLabelText('Strength'), 'accessibilityAction', { nativeEvent: { actionName: 'decrement' } });
    expect(onChange).toHaveBeenLastCalledWith(expect.objectContaining({ preset: 'soft-studio', strength: 0.95 }));
    expect(screen.getByLabelText('Direction').props.accessibilityState).toMatchObject({ disabled: true });
    await fireEvent.press(screen.getByTestId('light-reset'));
    expect(onChange).toHaveBeenLastCalledWith({ preset: 'soft-studio', strength: 1 });
  });

  it('turns and softens the light on Premium', async () => {
    const onChange = jest.fn();
    await wrap(<LightPanel value={{ preset: 'rembrandt', strength: 1 }} onChoose={jest.fn()} onChange={onChange} premium onLocked={jest.fn()} />);
    expect(screen.getByLabelText('Direction').props.accessibilityState).toMatchObject({ disabled: false });
    await fireEvent(screen.getByLabelText('Direction'), 'accessibilityAction', { nativeEvent: { actionName: 'increment' } });
    expect(onChange).toHaveBeenLastCalledWith({ preset: 'rembrandt', strength: 1, angle: -45 });
  });

  it('grades, with film looks behind Premium, and applies templates', async () => {
    const onChange = jest.fn(),
      onLocked = jest.fn();
    await wrap(<FinishPanel value={NO_FINISH} onChange={onChange} premium={false} onLocked={onLocked} />);
    await fireEvent.press(screen.getByTestId('grade-vivid'));
    expect(onChange).toHaveBeenLastCalledWith({ ...NO_FINISH, grade: 'vivid' });
    await fireEvent.press(screen.getByTestId('grade-warm-film'));
    expect(onLocked).toHaveBeenCalledTimes(1);
    await fireEvent(screen.getByLabelText('Warmth'), 'accessibilityAction', { nativeEvent: { actionName: 'increment' } });
    expect(onChange).toHaveBeenLastCalledWith(expect.objectContaining({ warmth: 0.05 }));
    const onApply = jest.fn();
    await wrap(<TemplateRow onApply={onApply} premium={false} onLocked={onLocked} />);
    await fireEvent.press(screen.getByTestId('template-close-up'));
    expect(onApply).toHaveBeenCalledWith('close-up');
    await fireEvent.press(screen.getByTestId('template-left-third'));
    expect(onLocked).toHaveBeenCalledTimes(2);
  });
});
