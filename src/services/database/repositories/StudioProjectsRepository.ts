import { createId } from '../ids';
import type { SqlExecutor } from '../SqlExecutor';

/** The studios that work in projects of their own. */
export type StudioKind = 'photoshoot' | 'arts' | 'motion';
export const STUDIO_KINDS: readonly StudioKind[] = ['photoshoot', 'arts', 'motion'];

export interface StudioProject {
  id: string;
  studio: StudioKind;
  name: string;
  createdAt: number;
  updatedAt: number;
  photoCount: number;
  workCount: number;
  /** The newest work's picture (or, before any, the newest photo); null when empty. */
  coverPath: string | null;
}

export type StudioItemKind = 'photo' | 'work';

export interface StudioItem {
  id: string;
  projectId: string;
  kind: StudioItemKind;
  title: string;
  /** The photo, or the finished work (an image or a video). */
  path: string;
  /** A still for a video (its first frame); null for images. */
  posterPath: string | null;
  media: 'image' | 'video';
  /** For a work: the photo it was made from. */
  sourcePath: string | null;
  /** For a work: what made it (to edit it again). */
  settings: Record<string, unknown>;
  width: number;
  height: number;
  createdAt: number;
  updatedAt: number;
}

export type NewStudioItem = Pick<StudioItem, 'kind' | 'path'> &
  Partial<Pick<StudioItem, 'title' | 'posterPath' | 'media' | 'sourcePath' | 'settings' | 'width' | 'height'>>;

interface ProjectRow {
  id: string;
  studio: StudioKind;
  name: string;
  created_at: number;
  updated_at: number;
  photo_count: number | null;
  work_count: number | null;
  cover_path: string | null;
}

interface ItemRow {
  id: string;
  project_id: string;
  kind: StudioItemKind;
  title: string;
  path: string;
  poster_path: string | null;
  media: 'image' | 'video';
  source_path: string | null;
  settings_json: string | null;
  width: number;
  height: number;
  created_at: number;
  updated_at: number;
}

const SELECT = `
  SELECT p.*,
    (SELECT COUNT(*) FROM studio_items i WHERE i.project_id = p.id AND i.kind = 'photo') AS photo_count,
    (SELECT COUNT(*) FROM studio_items i WHERE i.project_id = p.id AND i.kind = 'work') AS work_count,
    COALESCE(
      (SELECT COALESCE(poster_path, path) FROM studio_items w WHERE w.project_id = p.id AND w.kind = 'work' ORDER BY w.created_at DESC LIMIT 1),
      (SELECT path FROM studio_items f WHERE f.project_id = p.id AND f.kind = 'photo' ORDER BY f.created_at DESC LIMIT 1)
    ) AS cover_path
  FROM studio_projects p`;

const toProject = (r: ProjectRow): StudioProject => ({
  id: r.id,
  studio: r.studio,
  name: r.name,
  createdAt: r.created_at,
  updatedAt: r.updated_at,
  photoCount: r.photo_count ?? 0,
  workCount: r.work_count ?? 0,
  coverPath: r.cover_path ?? null,
});

function parseSettings(json: string | null): Record<string, unknown> {
  try {
    const v: unknown = JSON.parse(json ?? '{}');
    return v && typeof v === 'object' && !Array.isArray(v) ? (v as Record<string, unknown>) : {};
  } catch {
    return {};
  }
}

const toItem = (r: ItemRow): StudioItem => ({
  id: r.id,
  projectId: r.project_id,
  kind: r.kind,
  title: r.title,
  path: r.path,
  posterPath: r.poster_path,
  media: r.media === 'video' ? 'video' : 'image',
  sourcePath: r.source_path,
  settings: parseSettings(r.settings_json),
  width: r.width,
  height: r.height,
  createdAt: r.created_at,
  updatedAt: r.updated_at,
});

/** A title: one line, trimmed, 1..80 characters. */
export function normaliseTitle(name: string): string {
  const t = name.replace(/\s+/g, ' ').trim();
  if (!t) throw new Error('A name cannot be empty');
  return t.slice(0, 80);
}

/**
 * Projects for each studio, and their photos and works. Deleting returns
 * the files that belonged to what was deleted (for the caller to remove).
 */
export class StudioProjectsRepository {
  constructor(
    private readonly db: SqlExecutor,
    private readonly now: () => number = Date.now,
  ) {}

  async create(studio: StudioKind, name: string): Promise<StudioProject> {
    if (!STUDIO_KINDS.includes(studio)) throw new Error('Unknown studio');
    const id = createId('sp', this.now());
    const ts = this.now();
    await this.db.execute(
      'INSERT INTO studio_projects (id, studio, name, created_at, updated_at) VALUES (?, ?, ?, ?, ?)',
      [id, studio, normaliseTitle(name), ts, ts],
    );
    const p = await this.get(id);
    if (!p) throw new Error('The project was not saved');
    return p;
  }

  async get(id: string): Promise<StudioProject | null> {
    const rows = await this.db.query<ProjectRow>(`${SELECT} WHERE p.id = ?`, [id]);
    return rows[0] ? toProject(rows[0]) : null;
  }

  async list(studio: StudioKind): Promise<StudioProject[]> {
    const rows = await this.db.query<ProjectRow>(`${SELECT} WHERE p.studio = ? ORDER BY p.updated_at DESC`, [studio]);
    return rows.map(toProject);
  }

  async rename(id: string, name: string): Promise<void> {
    await this.db.execute('UPDATE studio_projects SET name = ?, updated_at = ? WHERE id = ?', [
      normaliseTitle(name),
      this.now(),
      id,
    ]);
  }

  /** Deletes the project and everything in it; returns their files. */
  async delete(id: string): Promise<string[]> {
    const items = await this.items(id);
    await this.db.transaction([
      { sql: 'DELETE FROM studio_items WHERE project_id = ?', params: [id] },
      { sql: 'DELETE FROM studio_projects WHERE id = ?', params: [id] },
    ]);
    return items.flatMap(i => [i.path, ...(i.posterPath ? [i.posterPath] : [])]);
  }

  async addItems(projectId: string, items: NewStudioItem[]): Promise<StudioItem[]> {
    const ts = this.now();
    const made: StudioItem[] = items.map((it, k) => ({
      id: createId(it.kind === 'photo' ? 'sph' : 'spw', ts + k),
      projectId,
      kind: it.kind,
      title: normaliseTitle(it.title ?? (it.kind === 'photo' ? 'Photo' : 'Work')),
      path: it.path,
      posterPath: it.posterPath ?? null,
      media: it.media ?? 'image',
      sourcePath: it.sourcePath ?? null,
      settings: it.settings ?? {},
      width: Math.round(it.width ?? 0),
      height: Math.round(it.height ?? 0),
      // In the order given, newest last.
      createdAt: ts + k,
      updatedAt: ts + k,
    }));
    await this.db.transaction([
      ...made.map(m => ({
        sql: `INSERT INTO studio_items (id, project_id, kind, title, path, poster_path, media, source_path, settings_json, width, height, created_at, updated_at)
              VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
        params: [
          m.id,
          m.projectId,
          m.kind,
          m.title,
          m.path,
          m.posterPath,
          m.media,
          m.sourcePath,
          JSON.stringify(m.settings),
          m.width,
          m.height,
          m.createdAt,
          m.updatedAt,
        ],
      })),
      { sql: 'UPDATE studio_projects SET updated_at = ? WHERE id = ?', params: [ts + made.length, projectId] },
    ]);
    return made;
  }

  async items(projectId: string, kind?: StudioItemKind): Promise<StudioItem[]> {
    const rows = await this.db.query<ItemRow>(
      kind
        ? 'SELECT * FROM studio_items WHERE project_id = ? AND kind = ? ORDER BY created_at DESC'
        : 'SELECT * FROM studio_items WHERE project_id = ? ORDER BY created_at DESC',
      kind ? [projectId, kind] : [projectId],
    );
    return rows.map(toItem);
  }

  async item(id: string): Promise<StudioItem | null> {
    const rows = await this.db.query<ItemRow>('SELECT * FROM studio_items WHERE id = ?', [id]);
    return rows[0] ? toItem(rows[0]) : null;
  }

  async renameItem(id: string, title: string): Promise<void> {
    await this.db.execute('UPDATE studio_items SET title = ?, updated_at = ? WHERE id = ?', [
      normaliseTitle(title),
      this.now(),
      id,
    ]);
  }

  /**
   * Deletes a photo or a work; returns the files no longer needed. A photo's
   * works stay, and while any of them may be edited again, the photo's file is kept for them.
   */
  async deleteItem(id: string): Promise<string[]> {
    const it = await this.item(id);
    if (!it) return [];
    await this.db.transaction([
      { sql: 'DELETE FROM studio_items WHERE id = ?', params: [id] },
      { sql: 'UPDATE studio_projects SET updated_at = ? WHERE id = ?', params: [this.now(), it.projectId] },
    ]);
    const files = [it.path, ...(it.posterPath ? [it.posterPath] : [])];
    if (it.kind === 'photo') {
      const used = await this.db.query<{ n: number }>('SELECT COUNT(*) AS n FROM studio_items WHERE source_path = ?', [
        it.path,
      ]);
      if ((used[0]?.n ?? 0) > 0) return files.filter(f => f !== it.path);
    }
    return files;
  }
}
