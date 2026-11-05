import { describeLook, summariseLook, type LookSummary } from '../camera/model';
import { getProcessingService } from '../processing/ImageProcessingService';
import { getStorageService } from '../storage/StorageService';
import { useAppStore } from '../../state/store';
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
  const storage = getStorageService();
  const referencePath = await storage.pickImage('looks');
  if (!referencePath) {
    return null;
  }
  try {
    const look = await getProcessingService().analyzeLook(referencePath);
    return { referencePath, look, strength };
  } catch (e) {
    await storage.deleteFile(referencePath).catch(() => undefined); // don't keep an unusable copy
    throw e;
  }
}

/**
 * Saves the look in use and removes the previous reference photo's copy
 * when it is no longer needed (changed or turned off).
 */
export async function saveVibeLook(next: VibeLook | null, previous: VibeLook | null): Promise<void> {
  await useAppStore.getState().updateSetting('vibeLook', next);
  if (previous && previous.referencePath !== next?.referencePath) {
    await getStorageService()
      .deleteFile(previous.referencePath)
      .catch(() => undefined);
  }
}
