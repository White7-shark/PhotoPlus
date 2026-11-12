import { fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { BackdropEditPanel } from '../src/screens/editor/BackdropPanel';
import { EDITOR_TOOLS } from '../src/screens/editor/panels';
import {
  backdropCover,
  BACKDROPS,
  cameraBackdrop,
  DEFAULT_BACKDROP_ID,
  findBackdrop,
  isBackdropChoice,
} from '../src/services/backdrops/catalog';
import { EMPTY_DOCUMENT, isEditDocument } from '../src/services/edit/document';
import { FEATURES } from '../src/services/entitlements/features';
import { ThemeProvider } from '../src/theme';

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);

describe('backdrops', () => {
  it('offers more than ten designs in the engine order, each with a cover', () => {
    expect(BACKDROPS.map(b => b.id)).toEqual([
      'signature',
      'soft_studio',
      'stage',
      'noir',
      'porcelain',
      'canvas',
      'golden_hour',
      'city_lights',
      'neon',
      'color_pop',
      'velocity',
      'swirl',
      'aurora',
      'dream',
    ]);
    for (const b of BACKDROPS) {
      expect(backdropCover(b.id)).toBeTruthy();
      expect(b.description.length).toBeGreaterThan(10);
    }
    expect(BACKDROPS.filter(b => b.tier === 'free').map(b => b.id)).toEqual(['signature', 'soft_studio', 'stage']);
    expect(FEATURES.backdrops.plan).toBe('free');
    expect(FEATURES.backdropsPremium.plan).toBe('premium');
    expect(EDITOR_TOOLS.some(t => t.id === 'backdrop')).toBe(true);
  });

  it('the camera uses one backdrop, Signature, as strong as the Depth blur slider', () => {
    expect(DEFAULT_BACKDROP_ID).toBe('signature');
    expect(findBackdrop(DEFAULT_BACKDROP_ID)?.tier).toBe('free');
    expect(cameraBackdrop(0.6)).toEqual({ id: 'signature', strength: 0.6 });
    expect(cameraBackdrop(4)).toEqual({ id: 'signature', strength: 1 });
    expect(cameraBackdrop(0)).toBeNull();
  });

  it('choices are validated, also inside a saved edit', () => {
    expect(isBackdropChoice({ id: 'aurora', strength: 0.8 })).toBe(true);
    expect(isBackdropChoice({ id: 'beach', strength: 0.8 })).toBe(false);
    expect(isBackdropChoice({ id: 'aurora', strength: 2 })).toBe(false);
    expect(isEditDocument({ ...EMPTY_DOCUMENT, backdrop: { id: 'neon', strength: 1 } })).toBe(true);
    expect(isEditDocument({ ...EMPTY_DOCUMENT, backdrop: { id: 'nope', strength: 1 } })).toBe(false);
    // Edits saved before backdrops existed still load.
    const { backdrop: _dropped, ...older } = EMPTY_DOCUMENT;
    expect(isEditDocument(older)).toBe(true);
  });

  it('the editor applies a design, adjusts its strength, locks premium ones and removes it', async () => {
    const update = jest.fn();
    const onPremium = jest.fn();
    const { rerender } = await wrap(
      <BackdropEditPanel
        doc={EMPTY_DOCUMENT}
        update={update}
        commit={jest.fn()}
        premiumAllowed={false}
        onPremium={onPremium}
      />,
    );
    expect(screen.getByLabelText('Aurora, premium')).toBeTruthy();
    await fireEvent.press(screen.getByTestId('backdrop-aurora'));
    expect(onPremium).toHaveBeenCalled();
    expect(update).not.toHaveBeenCalled();
    await fireEvent.press(screen.getByTestId('backdrop-stage'));
    expect(update).toHaveBeenLastCalledWith({ backdrop: { id: 'stage', strength: 1 } });
    await rerender(
      <ThemeProvider preference="dark">
        <BackdropEditPanel
          doc={{ ...EMPTY_DOCUMENT, backdrop: { id: 'stage', strength: 1 } }}
          update={update}
          commit={jest.fn()}
          premiumAllowed
          onPremium={onPremium}
        />
      </ThemeProvider>,
    );
    expect(screen.getByText('A black stage, light falling off the shoulders')).toBeTruthy();
    await fireEvent(screen.getByTestId('backdrop-strength'), 'accessibilityAction', {
      nativeEvent: { actionName: 'decrement' },
    });
    expect(update.mock.calls.at(-1)?.[0]).toEqual({ backdrop: { id: 'stage', strength: 0.95 } });
    await fireEvent.press(screen.getByTestId('backdrop-neon'));
    expect(update).toHaveBeenLastCalledWith({ backdrop: { id: 'neon', strength: 1 } });
    await fireEvent.press(screen.getByText('None'));
    expect(update).toHaveBeenLastCalledWith({ backdrop: null });
  });
});
