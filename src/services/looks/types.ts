import { isLook } from '../camera/model';

/** A saved Vibe Match look: the reference photo and its measured style. */
export interface VibeLook {
  referencePath: string;
  look: number[];
  /** How strongly it is applied, 0..1. */
  strength: number;
}

export function isVibeLook(v: unknown): v is VibeLook {
  if (!v || typeof v !== 'object') {
    return false;
  }
  const o = v as Record<string, unknown>;
  return (
    typeof o.referencePath === 'string' &&
    o.referencePath.length > 0 &&
    isLook(o.look) &&
    typeof o.strength === 'number' &&
    o.strength >= 0 &&
    o.strength <= 1
  );
}
