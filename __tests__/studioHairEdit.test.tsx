import { fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { HairEditor } from '../src/screens/studio/HairEditor';
import { HairPicker, type HairBrushState } from '../src/screens/studio/HairPicker';
import { NO_EDIT, editRequest, hairRequest, isEdited, type HairChoice } from '../src/services/studio/hair';
import { ThemeProvider } from '../src/theme';

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const brushes = (over: Partial<HairBrushState> = {}): HairBrushState => ({
  brushing: false,
  onBrushing: jest.fn(),
  brush: 'erase',
  onBrush: jest.fn(),
  radius: 0.03,
  onRadius: jest.fn(),
  ...over,
});

describe('hair editing requests', () => {
  it('sends only what was changed, and the brushes by number', () => {
    expect(editRequest(undefined)).toBeUndefined();
    expect(editRequest(NO_EDIT)).toBeUndefined();
    expect(editRequest({ ...NO_EDIT, length: 0.4, size: 1.2, part: -2 })).toEqual({ length: 0.4, size: 1.2, part: -1 });
    const r = hairRequest({
      style: 'long-waves',
      edit: { ...NO_EDIT, roots: 0.5 },
      strokes: [{ brush: 'colourOff', radius: 0.02, points: [0.1, 0.2, 0.3, 0.4] }],
    });
    expect(r).toEqual({
      style: 'long-waves',
      edit: { roots: 0.5 },
      strokes: [{ brush: 3, radius: 0.02, points: [0.1, 0.2, 0.3, 0.4] }],
    });
    // An edit alone (no style or colour) asks for nothing.
    expect(hairRequest({ edit: { ...NO_EDIT, length: 1 } })).toBeUndefined();
    expect(isEdited({ style: 'bob' })).toBe(false);
    expect(isEdited({ style: 'bob', strokes: [{ brush: 'erase', radius: 0.02, points: [0.5, 0.5] }] })).toBe(true);
  });
});

describe('hair editor', () => {
  it('shapes the style, fits it, finishes the colour, and paints', async () => {
    let value: HairChoice = { style: 'long-waves', color: '#6B2737' };
    const onChange = jest.fn((c: HairChoice) => {
      value = c;
    });
    const b = brushes();
    const ui = () => <HairEditor value={value} onChange={onChange} {...b} />;
    const { rerender } = await wrap(ui());
    const again = () => rerender(<ThemeProvider preference="dark">{ui()}</ThemeProvider>);
    // Shape: longer.
    await fireEvent(screen.getByLabelText('Length'), 'accessibilityAction', {
      nativeEvent: { actionName: 'increment' },
    });
    expect(value.edit).toMatchObject({ length: 0.05 });
    await again();
    // Fit: bigger; turned.
    await fireEvent(screen.getByLabelText('Size'), 'accessibilityAction', { nativeEvent: { actionName: 'increment' } });
    expect(value.edit?.size).toBeCloseTo(1.01, 5);
    await again();
    await fireEvent(screen.getByLabelText('Turn'), 'accessibilityAction', { nativeEvent: { actionName: 'decrement' } });
    expect(value.edit?.rotate).toBe(-1);
    await again();
    // Colour finish: darker roots and the colour's depth (a colour is chosen).
    await fireEvent(screen.getByLabelText('Darker roots'), 'accessibilityAction', {
      nativeEvent: { actionName: 'increment' },
    });
    expect(value.edit?.roots).toBeCloseTo(0.05, 5);
    expect(screen.getByLabelText('Depth')).toBeTruthy();
    await again();
    // The parting: off ("as styled") until moved, and back.
    expect(screen.getByLabelText('Parting').props.accessibilityValue).toMatchObject({ text: 'As styled' });
    await fireEvent(screen.getByLabelText('Parting'), 'accessibilityAction', {
      nativeEvent: { actionName: 'increment' },
    });
    expect(value.edit?.part).toBeCloseTo(0.05, 5);
    await again();
    await fireEvent.press(screen.getByTestId('hair-edit-part-reset'));
    expect(value.edit?.part).toBeNull();
    // Brushes: choosing one, painting, undo.
    await fireEvent.press(screen.getByTestId('hair-brush-colourOff'));
    expect(b.onBrush).toHaveBeenCalledWith('colourOff');
    expect(screen.getByText(/new hair should not be/)).toBeTruthy(); // the chosen brush's hint (erase)
    await fireEvent.press(screen.getByTestId('hair-paint'));
    expect(b.onBrushing).toHaveBeenCalledWith(true);
    value = {
      ...value,
      strokes: [
        { brush: 'erase', radius: 0.03, points: [0.2, 0.2] },
        { brush: 'restore', radius: 0.03, points: [0.3, 0.3] },
      ],
    };
    await again();
    await fireEvent.press(screen.getByLabelText('Undo the last stroke'));
    expect(value.strokes).toEqual([{ brush: 'erase', radius: 0.03, points: [0.2, 0.2] }]);
    await again();
    // Reset: style and colour kept, the hand edits gone.
    await fireEvent.press(screen.getByTestId('hair-edit-reset'));
    expect(value).toEqual({ style: 'long-waves', color: '#6B2737' });
  });

  it('shows only the colour finish for a colour alone', async () => {
    await wrap(<HairEditor value={{ color: '#121012' }} onChange={jest.fn()} {...brushes()} />);
    expect(screen.queryByLabelText('Length')).toBeNull();
    expect(screen.queryByLabelText('Size')).toBeNull();
    expect(screen.getByLabelText('Shine')).toBeTruthy();
    expect(screen.getByLabelText('Ends')).toBeTruthy();
  });
});

describe('hair picker edit tab', () => {
  it('appears once a style or colour is chosen, and opens Premium on the free plan', async () => {
    const onLocked = jest.fn();
    const { rerender } = await wrap(
      <HairPicker value={{}} onChange={jest.fn()} locked={false} onLocked={onLocked} brushes={brushes()} />,
    );
    expect(screen.queryByTestId('hair-tab-edit')).toBeNull();
    await rerender(
      <ThemeProvider preference="dark">
        <HairPicker
          value={{ style: 'bob' }}
          onChange={jest.fn()}
          locked={false}
          onLocked={onLocked}
          brushes={brushes()}
        />
      </ThemeProvider>,
    );
    await fireEvent.press(screen.getByTestId('hair-tab-edit'));
    expect(screen.getByTestId('hair-editor')).toBeTruthy();
    await rerender(
      <ThemeProvider preference="dark">
        <HairPicker value={{ style: 'bob' }} onChange={jest.fn()} locked onLocked={onLocked} brushes={brushes()} />
      </ThemeProvider>,
    );
    expect(onLocked).not.toHaveBeenCalled();
  });

  it('opens Premium instead of the editor when locked', async () => {
    const onLocked = jest.fn();
    await wrap(
      <HairPicker value={{ style: 'bob' }} onChange={jest.fn()} locked onLocked={onLocked} brushes={brushes()} />,
    );
    await fireEvent.press(screen.getByTestId('hair-tab-edit'));
    expect(onLocked).toHaveBeenCalledTimes(1);
    expect(screen.queryByTestId('hair-editor')).toBeNull();
  });
});
