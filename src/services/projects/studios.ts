/**
 * The studios that work in projects (Photoshoot, Photo Arts, Photo Motion):
 * their names, icons and tool screens, and the project library's actions —
 * adding photos, keeping each finished work in its project's gallery,
 * removing what is deleted from the phone.
 */
import type { IconName } from '../../components/Icon';
import { t, type StringKey } from '../../i18n';
import type { Repositories } from '../database/Database';
import type {
  NewStudioItem,
  StudioItem,
  StudioKind,
  StudioProject,
} from '../database/repositories/StudioProjectsRepository';
import { getStorageService } from '../storage/StorageService';

export interface StudioDef {
  kind: StudioKind;
  /** The tool a project's photo opens in. */
  route: 'SceneSwap' | 'Masters' | 'Motion' | 'Toon';
  /** A brand name (the same in every language). */
  title: string;
  icon: IconName;
  about: StringKey;
}

export const STUDIOS: Record<StudioKind, StudioDef> = {
  photoshoot: {
    kind: 'photoshoot',
    route: 'SceneSwap',
    title: 'Scene Swap',
    icon: 'layers',
    about: 'photoshoot_studio_you_in_a_new_place',
  },
  arts: { kind: 'arts', route: 'Masters', title: 'Photo Arts', icon: 'brush', about: 'masters_tile_about' },
  motion: { kind: 'motion', route: 'Motion', title: 'Photo Motion', icon: 'motion', about: 'motion_tile_about' },
  toon: { kind: 'toon', route: 'Toon', title: 'Toon Studio', icon: 'sparkles', about: 'toon_tile_about' },
};

export const studioAbout = (kind: StudioKind) => t(STUDIOS[kind].about);

/** A name for a new project: "Project 3" (the next free number in this studio). */
export function nextProjectName(existing: readonly StudioProject[]): string {
  const taken = new Set(existing.map(p => p.name));
  for (let n = existing.length + 1; ; n++) {
    const name = t('sp_untitled', { n });
    if (!taken.has(name)) return name;
  }
}

/** Adds photos (already copied into the app) to a project, titled "Photo 1", "Photo 2", … */
export async function addPhotos(
  repos: Repositories,
  projectId: string,
  paths: readonly string[],
): Promise<StudioItem[]> {
  if (!paths.length) return [];
  const have = (await repos.studioProjects.items(projectId, 'photo')).length;
  return repos.studioProjects.addItems(
    projectId,
    paths.map((path, i) => ({ kind: 'photo', path, title: t('sp_photo_title', { n: have + i + 1 }) })),
  );
}

/** Keeps a finished work in its project's gallery. */
export async function saveWork(
  repos: Repositories,
  projectId: string,
  work: Omit<NewStudioItem, 'kind'>,
): Promise<StudioItem> {
  const [item] = await repos.studioProjects.addItems(projectId, [{ ...work, kind: 'work' }]);
  return item;
}

/** Removes files from the phone (what is still in the gallery stays there). */
export async function removeFiles(paths: readonly string[]): Promise<void> {
  const storage = getStorageService();
  await Promise.allSettled(paths.map(p => storage.remove(p)));
}

export async function deleteProject(repos: Repositories, id: string): Promise<void> {
  await removeFiles(await repos.studioProjects.delete(id));
}

export async function deleteItem(repos: Repositories, id: string): Promise<void> {
  await removeFiles(await repos.studioProjects.deleteItem(id));
}
