import { computeResponsive } from '../src/hooks/useResponsive';
import { contrastRatio } from '../src/theme/contrast';
import { resolveThemeMode } from '../src/theme/ThemeProvider';
import { createTheme, gradients } from '../src/theme/tokens';

const hexStops = (gradient: string) => gradient.match(/#[0-9A-Fa-f]{6}/g) ?? [];

describe('theme contrast (WCAG 2.x)', () => {
  for (const mode of ['dark', 'light'] as const) {
    const t = createTheme(mode).colors;
    // Text sits on the page background, and on glass over that background.
    const surfaces: Array<[string, string[]]> = [
      ['background', [t.background]],
      ['glass', [t.background, t.glassFill]],
      ['strong glass', [t.background, t.glassFillStrong]],
    ];
    for (const [name, layers] of surfaces) {
      it(`${mode}: primary and secondary text are AA (4.5:1) on ${name}`, () => {
        expect(contrastRatio(t.textPrimary, ...layers)).toBeGreaterThanOrEqual(7);
        expect(contrastRatio(t.textSecondary, ...layers)).toBeGreaterThanOrEqual(4.5);
      });
      it(`${mode}: tertiary text meets 3:1 large-text minimum on ${name}`, () => {
        expect(contrastRatio(t.textTertiary, ...layers)).toBeGreaterThanOrEqual(3);
      });
      it(`${mode}: status colours are legible on ${name}`, () => {
        for (const c of [t.accent, t.premium, t.success, t.warning, t.danger, t.info]) {
          expect(contrastRatio(c, ...layers)).toBeGreaterThanOrEqual(3);
        }
      });
    }
  }

  it('white labels on the action gradient meet AA at every stop', () => {
    const stops = hexStops(gradients.action);
    expect(stops).toHaveLength(3);
    for (const stop of stops) {
      expect(contrastRatio('#FFFFFF', stop)).toBeGreaterThanOrEqual(4.5);
    }
  });

  it('dark labels on the premium gradient meet AA at every stop', () => {
    for (const stop of hexStops(gradients.premium)) {
      expect(contrastRatio('#1A1206', stop)).toBeGreaterThanOrEqual(4.5);
    }
  });

  it('contrastRatio is symmetric-free of order of identical colours and bounded', () => {
    expect(contrastRatio('#000000', '#FFFFFF')).toBeCloseTo(21, 0);
    expect(contrastRatio('#777777', '#777777')).toBeCloseTo(1, 5);
    expect(() => contrastRatio('hsl(0,0%,0%)', '#fff')).toThrow();
  });
});

describe('theme resolution', () => {
  it('follows explicit preferences and the system for "system"', () => {
    expect(resolveThemeMode('dark', 'light')).toBe('dark');
    expect(resolveThemeMode('light', 'dark')).toBe('light');
    expect(resolveThemeMode('system', 'light')).toBe('light');
    expect(resolveThemeMode('system', 'dark')).toBe('dark');
    expect(resolveThemeMode('system', null)).toBe('dark');
  });

  it('every theme meets the 48dp touch-target minimum', () => {
    expect(createTheme('dark').layout.minTouchTarget).toBeGreaterThanOrEqual(48);
  });
});

describe('responsive layout', () => {
  it('maps widths to Material window size classes', () => {
    expect(computeResponsive(360, 800).breakpoint).toBe('compact');
    expect(computeResponsive(700, 1000).breakpoint).toBe('medium');
    expect(computeResponsive(1200, 800).breakpoint).toBe('expanded');
  });

  it('clamps content width and scales columns', () => {
    const phone = computeResponsive(360, 800);
    expect(phone.contentWidth).toBe(360 - 32);
    expect(phone.columns).toBe(2);
    const tablet = computeResponsive(1280, 800);
    expect(tablet.contentWidth).toBe(720);
    expect(tablet.columns).toBe(4);
    expect(tablet.isLandscape).toBe(true);
  });

  it('reduces columns for large accessibility font scales', () => {
    expect(computeResponsive(360, 800, 2).columns).toBe(1);
    expect(computeResponsive(1280, 800, 1.8).columns).toBe(3);
  });
});
