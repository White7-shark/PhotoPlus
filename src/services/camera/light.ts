/**
 * Auto Light in the camera screen: what the chip says, whether the shutter
 * will take a processed shot, and when the flash button is shown. The
 * decisions themselves are made natively (AutoLight.kt); these mirror them
 * closely enough for the UI (a wrong guess only changes the progress view).
 */
import type { CaptureModeId, FlashMode, LightStatus } from './types';
import type { IconName } from '../../components/Icon';

/** Modes Auto Light may change: the everyday ones (not RAW, Ultra Detail or Spotlight). */
export function autoLightApplies(mode: CaptureModeId): boolean {
  return mode === 'standard' || mode === 'enhance' || mode === 'night';
}

/** The longest burst per phone class (AutoLight.maxFrames). */
export function maxLightFrames(tier: string | undefined): number {
  return tier === 'performance' ? 10 : tier === 'entry' ? 6 : 8;
}

export interface LightShot {
  /** The shot is processed (a burst, the flash fused, or tone). */
  processed: boolean;
  /** Frames to show progress for. */
  frames: number;
}

/**
 * What the shutter will do for this scene. `multiShot`: the plan includes
 * multi-shot modes (otherwise Auto Light takes one frame, then tone).
 */
export function lightShot(
  status: LightStatus | null,
  opts: { mode: CaptureModeId; flash: FlashMode; front: boolean; multiShot: boolean; enabled: boolean; tier?: string },
): LightShot {
  const none = { processed: false, frames: 1 };
  if (!opts.enabled || !autoLightApplies(opts.mode)) {
    return none;
  }
  if (opts.front && opts.flash === 'on' && status?.screenLight) {
    return { processed: true, frames: 1 };
  }
  if (!status || status.plan === 'normal') {
    return none;
  }
  if (!opts.multiShot) {
    return { processed: true, frames: 1 };
  }
  const cap = maxLightFrames(opts.tier);
  if (status.plan === 'flash') {
    if (opts.flash !== 'off' && opts.front && status.screenLight) {
      return { processed: true, frames: 1 };
    }
    if (opts.flash === 'off' || opts.front) {
      return { processed: true, frames: cap };
    }
  }
  return { processed: true, frames: Math.max(2, Math.min(status.frames, cap)) };
}

export interface LightChip {
  label: string;
  icon: IconName;
  /** Read by screen readers. */
  accessibilityLabel: string;
}

function luxWords(lux: number | null): string {
  if (lux == null) {
    return '';
  }
  if (lux < 1) {
    return 'Very dark. ';
  }
  if (lux < 10) {
    return 'Dark room. ';
  }
  if (lux < 50) {
    return 'Dim light. ';
  }
  return '';
}

/** The Auto Light chip for the scene in view, or null when the light is fine. */
export function lightChip(status: LightStatus | null, enabled: boolean): LightChip | null {
  if (!enabled || !status?.label || status.plan === 'normal') {
    return null;
  }
  const label = status.label;
  const icon: IconName =
    label === 'Natural Flash' || label === 'Selfie Light' ? 'flash' : label === 'Bright Balance' ? 'sun' : 'moon';
  const what =
    label === 'Moonlight'
      ? 'Brightened and cleaned for a clear, natural photo'
      : label === 'Natural Flash'
        ? 'The flash adds detail while the room keeps its own light'
        : label === 'Selfie Light'
          ? 'The screen lights your face'
          : status.backlit
            ? 'Your face and the bright background are both kept'
            : 'Bright skies and deep shadows are both kept';
  return { label, icon, accessibilityLabel: `Auto Light: ${label}. ${luxWords(status.lux)}${what}.` };
}

/** The flash button: shown for a flash unit, or for the screen light on the front camera. */
export function flashButtonVisible(hasFlash: boolean | undefined, status: LightStatus | null, front: boolean): boolean {
  if (front) {
    return !!status?.screenLight || hasFlash === true;
  }
  return hasFlash !== false;
}

/** The toast after a photo Auto Light changed ("Moonlight photo saved"). */
export function lightToast(label: string | null | undefined): string | null {
  return label ? `${label} photo saved` : null;
}
