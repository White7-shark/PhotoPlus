import {
  autoLightApplies,
  flashButtonVisible,
  lightChip,
  lightShot,
  lightToast,
  maxLightFrames,
} from '../src/services/camera/light';
import { progressLabel } from '../src/services/camera/model';
import type { LightStatus } from '../src/services/camera/types';

const status = (over: Partial<LightStatus> = {}): LightStatus => ({
  plan: 'moonlight',
  label: 'Moonlight',
  lux: 5,
  frames: 9,
  backlit: false,
  screenLight: false,
  lowLightBoost: false,
  ...over,
});

const base = { mode: 'standard' as const, flash: 'auto' as const, front: false, multiShot: true, enabled: true, tier: 'standard' };

describe('Auto Light', () => {
  it('applies to the everyday modes only', () => {
    expect(autoLightApplies('standard')).toBe(true);
    expect(autoLightApplies('enhance')).toBe(true);
    expect(autoLightApplies('night')).toBe(true);
    expect(autoLightApplies('raw')).toBe(false);
    expect(autoLightApplies('superres')).toBe(false);
    expect(autoLightApplies('spotlight')).toBe(false);
  });

  it('leaves a normal scene, a disabled setting and other modes alone', () => {
    expect(lightShot(status({ plan: 'normal', label: null }), base)).toEqual({ processed: false, frames: 1 });
    expect(lightShot(null, base)).toEqual({ processed: false, frames: 1 });
    expect(lightShot(status(), { ...base, enabled: false })).toEqual({ processed: false, frames: 1 });
    expect(lightShot(status(), { ...base, mode: 'raw' })).toEqual({ processed: false, frames: 1 });
  });

  it('takes a burst in a dim room, capped by the phone', () => {
    expect(lightShot(status({ frames: 9 }), base)).toEqual({ processed: true, frames: 8 });
    expect(lightShot(status({ frames: 9 }), { ...base, tier: 'performance' })).toEqual({ processed: true, frames: 9 });
    expect(lightShot(status({ frames: 12 }), { ...base, tier: 'entry' })).toEqual({ processed: true, frames: 6 });
    expect(maxLightFrames(undefined)).toBe(8);
  });

  it('takes one shot (then tone) without multi-shot', () => {
    expect(lightShot(status(), { ...base, multiShot: false })).toEqual({ processed: true, frames: 1 });
    expect(lightShot(status({ plan: 'brightBalance', label: 'Bright Balance' }), { ...base, multiShot: false })).toEqual({
      processed: true,
      frames: 1,
    });
  });

  it('uses the screen light for selfies and the longest burst without a flash', () => {
    const dark = status({ plan: 'flash', label: 'Selfie Light', screenLight: true, frames: 6 });
    expect(lightShot(dark, { ...base, front: true })).toEqual({ processed: true, frames: 1 });
    expect(lightShot(dark, { ...base, front: true, flash: 'off' })).toEqual({ processed: true, frames: 8 });
    // Flash on with the front camera: the screen light even in a bright room.
    expect(lightShot(status({ plan: 'normal', label: null, screenLight: true }), { ...base, front: true, flash: 'on' })).toEqual({
      processed: true,
      frames: 1,
    });
    // Rear, very dark: the ambient burst for Natural Flash.
    expect(lightShot(status({ plan: 'flash', label: 'Natural Flash', frames: 6 }), base)).toEqual({ processed: true, frames: 6 });
  });

  it('names the light in plain words', () => {
    expect(lightChip(status({ plan: 'normal', label: null }), true)).toBeNull();
    expect(lightChip(status(), false)).toBeNull();
    const moon = lightChip(status({ lux: 3 }), true)!;
    expect(moon.label).toBe('Moonlight');
    expect(moon.icon).toBe('moon');
    expect(moon.accessibilityLabel).toContain('Dark room');
    expect(lightChip(status({ plan: 'flash', label: 'Natural Flash', lux: 0.3 }), true)!.icon).toBe('flash');
    expect(lightChip(status({ plan: 'flash', label: 'Natural Flash', lux: 0.3 }), true)!.accessibilityLabel).toContain('Very dark');
    const bright = lightChip(status({ plan: 'brightBalance', label: 'Bright Balance', lux: 20000, backlit: true }), true)!;
    expect(bright.icon).toBe('sun');
    expect(bright.accessibilityLabel).toContain('Your face and the bright background');
    // No jargon in anything the user reads.
    for (const chip of [moon, bright]) {
      expect(chip.accessibilityLabel).not.toMatch(/\b(lux|EV|ISO|HDR|exposure|tone map)\b/i);
    }
  });

  it('shows the flash button for the screen light on the front camera', () => {
    expect(flashButtonVisible(false, status({ screenLight: true }), true)).toBe(true);
    expect(flashButtonVisible(false, status({ screenLight: false }), true)).toBe(false);
    expect(flashButtonVisible(false, null, true)).toBe(false);
    expect(flashButtonVisible(true, null, false)).toBe(true);
    expect(flashButtonVisible(undefined, null, false)).toBe(true);
    expect(flashButtonVisible(false, status({ screenLight: true }), false)).toBe(false);
  });

  it('tells the user what happened', () => {
    expect(lightToast('Moonlight')).toBe('Moonlight photo saved');
    expect(lightToast(null)).toBeNull();
    expect(progressLabel('lighting', 8)).toBe('Balancing the light…');
  });
});
