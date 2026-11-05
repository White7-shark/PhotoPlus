import { describeLook, summariseLook, type LookSummary } from '../camera/model';
import { getProcessingService } from '../processing/ImageProcessingService';
import { getStorageService } from '../storage/StorageService';
import type { VibeLook } from './types';

export { isVibeLook, type VibeLook } from './types';

export const VIBE_STRENGTHS = [
  { id: 'subtle', label: 'Subtle', value: 0.5 },
  { id: 'balanced', label: 'Balanced', value: 0.8 },
  { id: 'full', label: 'Full', value: 1 },
] as const;

export function lookDescription(look: VibeLook): { summary: LookSummary; text: string } {
  const summary = summariseLook(look.look);
  return { summary, text: describeLook(summary) };
}

/** Lets the user pick a reference photo and measures its look. Null if cancelled. */
export async function pickVibeLook(strength = 0.8): Promise<VibeLook | null> {
  const referencePath = await getStorageService().pickImage('looks');
  if (!referencePath) {
    return null;
  }
  const look = await getProcessingService().analyzeLook(referencePath);
  return { referencePath, look, strength };
}
