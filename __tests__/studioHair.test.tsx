import { fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { HairPicker } from '../src/screens/studio/HairPicker';
import { FEATURES } from '../src/services/entitlements/features';
import { HAIR_COLOURS, HAIR_STYLES, hairRequest, hairSummary, stylesFor, type HairChoice } from '../src/services/studio/hair';
import { StudioService } from '../src/services/studio/StudioService';
import { PLATES } from '../src/services/studio/scene';
import { ThemeProvider } from '../src/theme';

// Jest runs on Node: read the engine's source to compare the style tables.
declare const __dirname: string;
declare function require(name: string): unknown;
const fs = require('fs') as { readFileSync: (file: string, encoding: 'utf8') => string };
const path = require('path') as { join: (...parts: string[]) => string };

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);

describe('studio hair catalogue', () => {
  it('lists at least 20 styles each for women and men, exactly the engine’s styles', () => {
    expect(stylesFor('women').length).toBeGreaterThanOrEqual(20);
    expect(stylesFor('men').length).toBeGreaterThanOrEqual(20);
    expect(new Set(HAIR_STYLES.map(s => s.id)).size).toBe(HAIR_STYLES.length);
    // The engine's table (native/engine/src/studio/hair.cpp): same ids, same lists.
    const src = fs.readFileSync(path.join(__dirname, '../native/engine/src/studio/hair.cpp'), 'utf8');
    const engine = [...src.matchAll(/\{"([a-z0-9-]+)",\s+(true|false),/g)].map(m => ({ id: m[1], women: m[2] === 'true' }));
    expect(engine.length).toBe(HAIR_STYLES.length);
    for (const s of HAIR_STYLES) expect(engine).toContainEqual({ id: s.id, women: s.group === 'women' });
    expect(HAIR_COLOURS.length).toBeGreaterThanOrEqual(12);
    expect(HAIR_COLOURS.every(c => /^#[0-9A-F]{6}$/.test(c.color))).toBe(true);
    expect(FEATURES.studioHair.plan).toBe('premium');
  });

  it('asks the device only for what was chosen', () => {
    expect(hairRequest({})).toBeUndefined();
    expect(hairRequest(null)).toBeUndefined();
    expect(hairRequest({ style: 'bob' })).toEqual({ style: 'bob' });
    expect(hairRequest({ color: '#b85c2e' })).toEqual({ color: '#B85C2E' });
    expect(hairRequest({ style: 'no-such', color: 'red' })).toBeUndefined();
    expect(hairSummary({})).toBe('Your own hair');
    expect(hairSummary({ style: 'box-braids', color: '#B85C2E' })).toBe('Box braids · Copper');
    expect(hairSummary({ color: '#123456' })).toBe('Your colour');
  });

  it('sends the hair with a scene', async () => {
    const renderScene = jest.fn(async () => ({ outputPath: '/s.jpg', width: 10, height: 10, sizeBytes: 1, box: [0, 0, 1, 1], exposure: 1, grounded: true, format: 'jpeg' }));
    const service = new StudioService({ renderScene } as never);
    const scene = { background: { kind: 'plate' as const, plate: PLATES[0] }, crop: { x: 0, y: 0, w: 1, h: 1 }, placement: { x: 0.5, y: 1, height: 0.8, flip: false, tilt: 0, angle: 0 }, look: { colorMatch: 0.7, lightWrap: 0.3, shadow: 0.5, castShadow: 0, lightX: 0, horizon: 0.5, bgBlur: 0, grain: 1, rim: 0, spotlight: 0 } };
    await service.render('s1', { ...scene, hair: { style: 'afro', color: '#D8B26E' } }, 'preview');
    expect((renderScene.mock.calls[0] as unknown[])[1]).toMatchObject({ hair: { style: 'afro', color: '#D8B26E' } });
    await service.render('s1', scene, 'preview');
    expect((renderScene.mock.calls[1] as unknown[])[1]).not.toHaveProperty('hair');
  });
});

describe('hair picker', () => {
  it('picks a colour and a style, and offers Premium on the free plan', async () => {
    let value: HairChoice = {};
    const onChange = jest.fn((c: HairChoice) => {
      value = c;
    });
    const onLocked = jest.fn();
    const ui = (locked: boolean) => <HairPicker value={value} onChange={onChange} locked={locked} onLocked={onLocked} />;
    const { rerender } = await wrap(ui(false));
    await fireEvent.press(screen.getByTestId('hair-copper'));
    expect(onChange).toHaveBeenLastCalledWith({ color: '#B85C2E' });
    await rerender(<ThemeProvider preference="dark">{ui(false)}</ThemeProvider>);
    expect(screen.getByTestId('hair-summary').props.children).toBe('Copper');
    await fireEvent.press(screen.getByTestId('hair-tab-men'));
    expect(screen.getByTestId('hair-waves-360')).toBeTruthy();
    expect(screen.queryByTestId('hair-box-braids')).toBeNull();
    await fireEvent.press(screen.getByTestId('hair-tab-women'));
    await fireEvent.press(screen.getByTestId('hair-box-braids'));
    expect(onChange).toHaveBeenLastCalledWith({ color: '#B85C2E', style: 'box-braids' });
    await rerender(<ThemeProvider preference="dark">{ui(false)}</ThemeProvider>);
    await fireEvent.press(screen.getByTestId('hair-own-style'));
    expect(onChange).toHaveBeenLastCalledWith({ color: '#B85C2E', style: null });
    // Free plan: choosing opens Premium.
    onChange.mockClear();
    await rerender(<ThemeProvider preference="dark">{ui(true)}</ThemeProvider>);
    expect(screen.getByText('Premium')).toBeTruthy();
    await fireEvent.press(screen.getByTestId('hair-afro'));
    expect(onLocked).toHaveBeenCalled();
    expect(onChange).not.toHaveBeenCalled();
  });
});
