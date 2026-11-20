import { fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { LipsPanel } from '../src/screens/studio/LipsPanel';
import { DEFAULT_LIPS, LIP_SHADES, lipsRequest, type LipsChoice } from '../src/services/studio/lips';
import { ThemeProvider } from '../src/theme';

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);

describe('pink lips requests', () => {
  it('sends the shade colour and a clamped strength, or nothing', () => {
    expect(lipsRequest(null)).toBeUndefined();
    expect(lipsRequest({ shade: 'unknown', strength: 1 })).toBeUndefined();
    expect(lipsRequest({ shade: 'soft', strength: 0 })).toBeUndefined();
    expect(lipsRequest({ shade: 'hot', strength: 2 })).toEqual({ color: '#E8508A', strength: 1 });
    expect(lipsRequest(DEFAULT_LIPS)).toEqual({ color: '#F4A6B8', strength: 0.85 });
    for (const s of LIP_SHADES) expect(s.color).toMatch(/^#[0-9A-F]{6}$/);
  });
});

describe('pink lips panel', () => {
  it('chooses a shade, sets the strength and goes back to natural lips', async () => {
    let value: LipsChoice | null = null;
    const onChange = jest.fn((c: LipsChoice | null) => {
      value = c;
    });
    const { rerender } = await wrap(<LipsPanel value={value} onChange={onChange} />);
    expect(screen.queryByLabelText('Strength')).toBeNull();
    await fireEvent.press(screen.getByTestId('lips-hot'));
    expect(value).toEqual({ shade: 'hot', strength: 0.85 });
    await rerender(
      <ThemeProvider preference="dark">
        <LipsPanel value={value} onChange={onChange} />
      </ThemeProvider>,
    );
    expect(screen.getByRole('radio', { name: 'Hot pink' }).props.accessibilityState.selected).toBe(true);
    await fireEvent(screen.getByLabelText('Strength'), 'accessibilityAction', {
      nativeEvent: { actionName: 'decrement' },
    });
    expect(value).toMatchObject({ shade: 'hot' });
    expect((value as LipsChoice | null)?.strength).toBeCloseTo(0.8, 5);
    await fireEvent.press(screen.getByTestId('lips-off'));
    expect(value).toBeNull();
  });
});
