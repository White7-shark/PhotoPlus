import { fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { LipstickPanel, PinkLipsPanel } from '../src/screens/studio/LipsPanel';
import { LIPSTICKS, NATURAL_PINK, lipsRequest, type LipsChoice } from '../src/services/studio/lips';
import { ThemeProvider } from '../src/theme';

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const again = (ui: React.ReactElement) => <ThemeProvider preference="dark">{ui}</ThemeProvider>;

describe('lips requests', () => {
  it('asks for natural pink (adapting to the person) or a lipstick, or nothing', () => {
    expect(lipsRequest(null)).toBeUndefined();
    expect(lipsRequest({ kind: 'natural', strength: 0 })).toBeUndefined();
    expect(lipsRequest({ kind: 'natural', strength: 0.85 })).toEqual({
      color: NATURAL_PINK,
      strength: 0.85,
      natural: true,
    });
    expect(lipsRequest({ kind: 'lipstick', shade: 'red', strength: 2 })).toEqual({
      color: '#C21E2F',
      strength: 1,
      natural: false,
    });
    expect(lipsRequest({ kind: 'lipstick', shade: 'unknown', strength: 1 })).toBeUndefined();
    expect(LIPSTICKS.length).toBeGreaterThanOrEqual(10);
    for (const s of LIPSTICKS) expect(s.color).toMatch(/^#[0-9A-F]{6}$/);
  });
});

describe('pink lips panel', () => {
  it('turns natural pink on, sets its strength and turns it off', async () => {
    let value: LipsChoice | null = null;
    const onChange = jest.fn((c: LipsChoice | null) => {
      value = c;
    });
    const { rerender } = await wrap(<PinkLipsPanel value={value} onChange={onChange} />);
    expect(screen.queryByLabelText('Strength')).toBeNull();
    await fireEvent.press(screen.getByTestId('pink-lips-on'));
    expect(value).toEqual({ kind: 'natural', strength: 0.85 });
    await rerender(again(<PinkLipsPanel value={value} onChange={onChange} />));
    await fireEvent(screen.getByLabelText('Strength'), 'accessibilityAction', {
      nativeEvent: { actionName: 'decrement' },
    });
    expect(value).toMatchObject({ kind: 'natural' });
    expect((value as LipsChoice | null)?.strength).toBeCloseTo(0.8, 5);
    await rerender(again(<PinkLipsPanel value={value} onChange={onChange} />));
    await fireEvent.press(screen.getByTestId('pink-lips-off'));
    expect(value).toBeNull();
  });
});

describe('lipstick panel', () => {
  it('chooses a shade, keeps the strength when changing shade, and removes it', async () => {
    let value: LipsChoice | null = { kind: 'natural', strength: 0.85 };
    const onChange = jest.fn((c: LipsChoice | null) => {
      value = c;
    });
    const { rerender } = await wrap(<LipstickPanel value={value} onChange={onChange} />);
    // Natural pink is on: no lipstick is shown as chosen.
    expect(screen.queryByLabelText('Strength')).toBeNull();
    await fireEvent.press(screen.getByTestId('lipstick-wine'));
    expect(value).toEqual({ kind: 'lipstick', shade: 'wine', strength: 0.9 });
    await rerender(again(<LipstickPanel value={value} onChange={onChange} />));
    expect(screen.getByRole('radio', { name: 'Wine' }).props.accessibilityState.selected).toBe(true);
    await fireEvent(screen.getByLabelText('Strength'), 'accessibilityAction', {
      nativeEvent: { actionName: 'decrement' },
    });
    await rerender(again(<LipstickPanel value={value} onChange={onChange} />));
    await fireEvent.press(screen.getByTestId('lipstick-coral'));
    expect(value).toMatchObject({ kind: 'lipstick', shade: 'coral' });
    expect((value as LipsChoice | null)?.strength).toBeCloseTo(0.85, 5);
    await rerender(again(<LipstickPanel value={value} onChange={onChange} />));
    await fireEvent.press(screen.getByTestId('lipstick-off'));
    expect(value).toBeNull();
  });
});
