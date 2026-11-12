import { fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { EDITOR_TOOLS } from '../src/screens/editor/panels';
import { SkinTonePanel } from '../src/screens/editor/SkinTonePanel';
import { EMPTY_DOCUMENT, isEditDocument } from '../src/services/edit/document';
import { FEATURES } from '../src/services/entitlements/features';
import { findShade, isSkinToneChoice, shadeCover, SKIN_SHADES } from '../src/services/skintones/catalog';
import { ThemeProvider } from '../src/theme';

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);

/** Relative luminance (sRGB) of #RRGGBB. */
function luminance(hex: string): number {
  const c = [1, 3, 5].map(i => parseInt(hex.slice(i, i + 2), 16) / 255);
  const lin = c.map(v => (v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4));
  return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2];
}

describe('skin tones', () => {
  it('offers more than ten shades, fairest to deepest, each with a cover', () => {
    expect(SKIN_SHADES.map(s => s.id)).toEqual([
      'porcelain_rose',
      'ivory',
      'peach',
      'golden_beige',
      'honey',
      'olive',
      'sun_kissed',
      'caramel',
      'bronze',
      'cinnamon',
      'mocha',
      'espresso',
      'ebony_glow',
    ]);
    for (const s of SKIN_SHADES) {
      expect(shadeCover(s.id)).toBeTruthy();
      expect(s.note.length).toBeGreaterThan(8);
    }
    // Ordered from fair to deep (allowing neighbours of similar depth but a different undertone).
    const lum = SKIN_SHADES.map(s => luminance(s.color));
    expect(lum[0]).toBeGreaterThan(lum[lum.length - 1] * 8);
    for (let i = 2; i < lum.length; i++) expect(lum[i]).toBeLessThan(lum[i - 2]);
    // The free shades span the range: fair, medium, deep and very deep.
    expect(SKIN_SHADES.filter(s => s.plan === 'free').map(s => s.id)).toEqual([
      'ivory',
      'honey',
      'caramel',
      'espresso',
    ]);
    expect(FEATURES.skinTones.plan).toBe('free');
    expect(FEATURES.skinTonesPremium.plan).toBe('premium');
    expect(EDITOR_TOOLS.find(t => t.id === 'skintone')?.label).toBe('Skin Tone');
  });

  it('choices are validated, also inside a saved edit', () => {
    expect(isSkinToneChoice({ id: 'honey', strength: 0.8 })).toBe(true);
    expect(isSkinToneChoice({ id: 'blue', strength: 0.8 })).toBe(false);
    expect(isSkinToneChoice({ id: 'honey', strength: 1.5 })).toBe(false);
    expect(isSkinToneChoice({ id: 'honey', strength: Number.NaN })).toBe(false);
    expect(isEditDocument({ ...EMPTY_DOCUMENT, skinTone: { id: 'mocha', strength: 1 } })).toBe(true);
    expect(isEditDocument({ ...EMPTY_DOCUMENT, skinTone: { id: 'nope', strength: 1 } })).toBe(false);
    // Edits saved before Skin Tone existed still load.
    const { skinTone: _dropped, ...older } = EMPTY_DOCUMENT;
    expect(isEditDocument(older)).toBe(true);
    expect(findShade('ebony_glow')?.glow).toBeGreaterThan(0.5);
  });

  it('the editor applies a shade, adjusts its strength, locks premium ones and removes it', async () => {
    const update = jest.fn();
    const onPremium = jest.fn();
    const { rerender } = await wrap(
      <SkinTonePanel
        doc={EMPTY_DOCUMENT}
        update={update}
        commit={jest.fn()}
        premiumAllowed={false}
        onPremium={onPremium}
      />,
    );
    expect(screen.getByText(/Hair, eyes, lips and clothes keep theirs/)).toBeTruthy();
    expect(screen.getByLabelText('Mocha, premium')).toBeTruthy();
    await fireEvent.press(screen.getByTestId('skintone-mocha'));
    expect(onPremium).toHaveBeenCalled();
    expect(update).not.toHaveBeenCalled();
    await fireEvent.press(screen.getByTestId('skintone-honey'));
    expect(update).toHaveBeenLastCalledWith({ skinTone: { id: 'honey', strength: 0.8 } });
    await rerender(
      <ThemeProvider preference="dark">
        <SkinTonePanel
          doc={{ ...EMPTY_DOCUMENT, skinTone: { id: 'honey', strength: 0.8 } }}
          update={update}
          commit={jest.fn()}
          premiumAllowed
          onPremium={onPremium}
        />
      </ThemeProvider>,
    );
    expect(screen.getByText('Medium with a golden-yellow undertone')).toBeTruthy();
    await fireEvent(screen.getByTestId('skintone-strength'), 'accessibilityAction', {
      nativeEvent: { actionName: 'decrement' },
    });
    expect(update.mock.calls.at(-1)?.[0]).toEqual({ skinTone: { id: 'honey', strength: 0.75 } });
    await fireEvent.press(screen.getByTestId('skintone-mocha'));
    expect(update).toHaveBeenLastCalledWith({ skinTone: { id: 'mocha', strength: 0.8 } });
    await fireEvent.press(screen.getByText('None'));
    expect(update).toHaveBeenLastCalledWith({ skinTone: null });
  });

  it('says so when the photo has no face to measure', async () => {
    await wrap(
      <SkinTonePanel
        doc={{ ...EMPTY_DOCUMENT, skinTone: { id: 'caramel', strength: 1 } }}
        update={jest.fn()}
        commit={jest.fn()}
        premiumAllowed
        onPremium={jest.fn()}
        noFace
      />,
    );
    expect(screen.getByText('No face found in this photo, so the skin was left as it is.')).toBeTruthy();
  });
});
