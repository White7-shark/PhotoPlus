import type { Repositories } from '../database/Database';
import { getProcessingService } from '../processing/ImageProcessingService';
import { getStorageService } from '../storage/StorageService';

/**
 * Brings a photo in from the gallery (system photo picker, no permission
 * needed) and files it under Captures so every tool can edit it.
 * Resolves with the new image id, or null if the user cancelled.
 */
export async function importPhoto(repos: Repositories): Promise<string | null> {
  const path = await getStorageService().pickImage('imports');
  if (!path) {
    return null;
  }
  const meta = await getProcessingService().metadata(path);
  const n = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : 0);
  const inbox = await repos.projects.ensureInbox();
  const record = await repos.images.add({
    projectId: inbox.id,
    path,
    source: 'import',
    width: n(meta.width),
    height: n(meta.height),
    sizeBytes: n(meta.sizeBytes),
    mimeType: typeof meta.mimeType === 'string' ? meta.mimeType : 'image/jpeg',
    capturedAt: Date.now(),
  });
  return record.id;
}
