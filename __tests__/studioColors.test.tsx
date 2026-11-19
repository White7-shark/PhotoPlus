import { fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { BackgroundPicker } from '../src/screens/studio/BackgroundPicker';
import {
  STUDIO_COLOURS,
  hexToHsv,
  hsvToHex,
  hueDistance,
  isOutfit,
  luminance,
  normalizeHex,
  suggestForOutfit,
  suitScore,
} from '../src/services/studio/colors';
import { PLAINS, customPlain, lookFor, withLight, type Background } from '../src/services/studio/scene';
import { ThemeProvider } from '../src/theme';

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const plainOf = (b: Background) => (b.kind === 'plain' ? b.plain : null);
const ids = (o: { color: string; share: number }[]) => suggestForOutfit(o).map(c => c.id);

describe('studio colours', () => {
  it('reads and writes colours exactly', () => {
    expect(normalizeHex('c4962f')).toBe('#C4962F');
    expect(normalizeHex('#abc')).toBe('#AABBCC');
    expect(normalizeHex('#12345')).toBeNull();
    expect(normalizeHex('mustard')).toBeNull();
    for (const c of STUDIO_COLOURS) expect(hsvToHex(hexToHsv(c.color))).toBe(c.color);
    expect(hexToHsv('#FF0000')).toEqual({ h: 0, s: 1, v: 1 });
    expect(hsvToHex({ h: 240, s: 1, v: 1 })).toBe('#0000FF');
    expect(hsvToHex({ h: 0, s: 0, v: 0.5 })).toBe('#808080');
    expect(hueDistance(350, 10)).toBe(20);
    expect(luminance('#FFFFFF')).toBeCloseTo(1, 5);
    expect(luminance('#000000')).toBe(0);
  });

  it('suggests papers that suit the outfit, never its own colour', () => {
    // White shirts and denim (the couple in the reference): a bold warm paper.
    const denim = ids([
      { color: '#EDEDEA', share: 0.55 },
      { color: '#4A6A93', share: 0.45 },
    ]);
    expect(denim.slice(0, 2)).toContain('mustard');
    expect(denim).not.toContain('dusty-blue');
    // A navy outfit: mustard (opposite) above navy (its own colour).
    const navy = [{ color: '#1E285A', share: 1 }];
    expect(ids(navy)).toContain('mustard');
    expect(ids(navy)).not.toContain('navy');
    expect(suitScore('#C4962F', navy)).toBeGreaterThan(suitScore('#22304F', navy));
    // An emerald dress: not a green paper.
    const emerald = ids([{ color: '#1F5B47', share: 1 }]);
    expect(emerald).not.toContain('teal');
    expect(emerald).not.toContain('olive');
    expect(ids([])).toEqual([]);
    expect(isOutfit([{ color: '#112233', share: 0.5 }])).toBe(true);
    expect(isOutfit([{ color: 'blue', share: 0.5 }])).toBe(false);
    expect(isOutfit(null)).toBe(false);
  });

  it('lights studio colours and bounces their colour onto the person', () => {
    const mustard = PLAINS.find(p => p.id === 'plain-mustard')!;
    expect(mustard.light).toBe(1);
    expect(lookFor({ kind: 'plain', plain: mustard }, { x: 0, y: 0, w: 1, h: 1 }).bounce).toBe(0.7);
    const flat = withLight(mustard, false);
    expect(flat.light).toBe(0);
    expect(lookFor({ kind: 'plain', plain: flat }, { x: 0, y: 0, w: 1, h: 1 }).bounce).toBeUndefined();
    expect(customPlain('#abc')).toMatchObject({ id: 'plain-custom', color: '#AABBCC', light: 1 });
    expect(customPlain('nope').color).toBe('#C4962F');
    expect(new Set(PLAINS.map(p => p.id)).size).toBe(PLAINS.length);
  });
});

describe('studio colour picker', () => {
  it('picks any colour, suggests papers for the outfit and turns studio light off and on', async () => {
    let value: Background = { kind: 'plain', plain: PLAINS[0] };
    const onChoose = jest.fn((b: Background) => {
      value = b;
    });
    const ui = () => (
      <BackgroundPicker value={value} mine={[]} onChoose={onChoose} outfit={[{ color: '#1E285A', share: 1 }]} />
    );
    const { rerender } = await wrap(ui());
    const again = () => rerender(<ThemeProvider preference="dark">{ui()}</ThemeProvider>);

    // Suggestions for a navy outfit.
    expect(screen.getByText('Suits your outfit')).toBeTruthy();
    await fireEvent.press(screen.getByTestId('suit-mustard'));
    expect(onChoose).toHaveBeenLastCalledWith({
      kind: 'plain',
      plain: expect.objectContaining({ color: '#C4962F', light: 0 }), // white was flat, so this stays flat
    });
    await again();

    // Studio light on.
    expect(screen.getByRole('switch', { name: 'Studio light' }).props.accessibilityState).toMatchObject({ checked: false });
    await fireEvent.press(screen.getByTestId('studio-light'));
    expect(plainOf(onChoose.mock.calls[onChoose.mock.calls.length - 1][0])?.light).toBe(1);
    await again();
    expect(screen.getByText(/its colour glowing softly on you/)).toBeTruthy();

    // Pick a colour: the picker opens with the current colour; a code sets it.
    await fireEvent.press(screen.getByTestId('bg-pick'));
    await again();
    expect(screen.getByTestId('color-picker')).toBeTruthy();
    const code = screen.getByTestId('color-code');
    expect(code.props.value).toBe('#C4962F');
    await fireEvent.changeText(code, '#6b2737');
    await fireEvent(code, 'endEditing');
    expect(onChoose).toHaveBeenLastCalledWith({
      kind: 'plain',
      plain: expect.objectContaining({ id: 'plain-custom', color: '#6B2737', light: 1 }),
    });
    await again();
    // A bad code is not taken.
    const calls = onChoose.mock.calls.length;
    await fireEvent.changeText(screen.getByTestId('color-code'), 'zz');
    await fireEvent(screen.getByTestId('color-code'), 'endEditing');
    expect(onChoose.mock.calls.length).toBe(calls);
    expect(screen.getByTestId('color-code').props.value).toBe('#6B2737');
    // The sliders are adjustable controls with colour tracks.
    expect(screen.getByLabelText('Colour')).toBeTruthy();
    expect(screen.getByLabelText('Richness')).toBeTruthy();
    expect(screen.getByLabelText('Brightness')).toBeTruthy();
    expect(screen.getByTestId('color-hue-track')).toBeTruthy();
    await fireEvent(screen.getByLabelText('Brightness'), 'accessibilityAction', { nativeEvent: { actionName: 'decrement' } });
    const last = onChoose.mock.calls[onChoose.mock.calls.length - 1][0] as Extract<Background, { kind: 'plain' }>;
    expect(hexToHsv(last.plain.color).v).toBeLessThan(hexToHsv('#6B2737').v);
  });

  it('shows no suggestions without an outfit', async () => {
    await wrap(<BackgroundPicker value={{ kind: 'plain', plain: PLAINS[0] }} mine={[]} onChoose={jest.fn()} />);
    expect(screen.queryByTestId('outfit-suggestions')).toBeNull();
  });
});
