import { createId } from '../ids';
import type { SqlExecutor } from '../SqlExecutor';
import { SettingsRepository } from './SettingsRepository';

export type ProjectKind = 'product' | 'portrait' | 'mixed';

/**
 * What a project reuses for its photos: a pack, a product setup (a catalogue
 * preset or a saved setup), a look, an export profile and size, and a brand
 * mark (a saved branding). Ids only; each is checked where it is used.
 */
export interface ProjectDefaults {
  packId?: string;
  setupId?: string;
  lookId?: string;
  profileId?: string;
  sizeId?: string;
  brandingId?: string;
}

const DEFAULT_KEYS = ['packId', 'setupId', 'lookId', 'profileId', 'sizeId', 'brandingId'] as const;

export function parseDefaults(json: string | null | undefined): ProjectDefaults {
  try {
    const v: unknown = JSON.parse(json ?? '{}');
    if (!v || typeof v !== 'object' || Array.isArray(v)) return {};
    const out: ProjectDefaults = {};
    for (const k of DEFAULT_KEYS) {
      const x = (v as Record<string, unknown>)[k];
      if (typeof x === 'string' && x.length > 0 && x.length <= 80) out[k] = x;
    }
    return out;
  } catch {
    return {};
  }
}

export interface Project {
  id: string;
  name: string;
  kind: ProjectKind;
  defaults: ProjectDefaults;
  coverImageId: string | null;
  archived: boolean;
  createdAt: number;
  updatedAt: number;
  imageCount: number;
  coverPath: string | null;
}

interface ProjectRow {
  id: string;
  name: string;
  kind: ProjectKind | null;
  defaults_json: string | null;
  cover_image_id: string | null;
  archived: number;
  created_at: number;
  updated_at: number;
  image_count: number;
  cover_path: string | null;
}

const SELECT = `
  SELECT p.*, 
    (SELECT COUNT(*) FROM images i WHERE i.project_id = p.id) AS image_count,
    COALESCE(
      (SELECT path FROM images WHERE id = p.cover_image_id),
      (SELECT path FROM images i2 WHERE i2.project_id = p.id ORDER BY i2.created_at DESC LIMIT 1)
    ) AS cover_path
  FROM projects p`;

const KINDS: readonly ProjectKind[] = ['product', 'portrait', 'mixed'];

const toProject = (r: ProjectRow): Project => ({
  id: r.id,
  name: r.name,
  kind: r.kind && KINDS.includes(r.kind) ? r.kind : 'mixed',
  defaults: parseDefaults(r.defaults_json),
  coverImageId: r.cover_image_id,
  archived: r.archived === 1,
  createdAt: r.created_at,
  updatedAt: r.updated_at,
  imageCount: r.image_count ?? 0,
  coverPath: r.cover_path ?? null,
});

export function normaliseProjectName(name: string): string {
  const trimmed = name.replace(/\s+/g, ' ').trim();
  if (trimmed.length === 0) {
    throw new Error('Project name cannot be empty');
  }
  return trimmed.slice(0, 120);
}

const INBOX_KEY = 'inboxProjectId';

export class ProjectsRepository {
  private readonly internal: SettingsRepository;

  constructor(
    private readonly db: SqlExecutor,
    private readonly now: () => number = Date.now,
  ) {
    this.internal = new SettingsRepository(db, now);
  }

  async create(name: string, kind: ProjectKind = 'mixed'): Promise<Project> {
    const id = createId('prj', this.now());
    const ts = this.now();
    await this.db.execute(
      'INSERT INTO projects (id, name, kind, archived, created_at, updated_at) VALUES (?, ?, ?, 0, ?, ?)',
      [id, normaliseProjectName(name), KINDS.includes(kind) ? kind : 'mixed', ts, ts],
    );
    const created = await this.get(id);
    if (!created) {
      throw new Error('Project was not persisted');
    }
    return created;
  }

  async get(id: string): Promise<Project | null> {
    const rows = await this.db.query<ProjectRow>(`${SELECT} WHERE p.id = ?`, [id]);
    return rows[0] ? toProject(rows[0]) : null;
  }

  async list(options: { includeArchived?: boolean } = {}): Promise<Project[]> {
    const where = options.includeArchived ? '' : 'WHERE p.archived = 0';
    const rows = await this.db.query<ProjectRow>(`${SELECT} ${where} ORDER BY p.updated_at DESC`);
    return rows.map(toProject);
  }

  async rename(id: string, name: string): Promise<void> {
    await this.db.execute('UPDATE projects SET name = ?, updated_at = ? WHERE id = ?', [
      normaliseProjectName(name),
      this.now(),
      id,
    ]);
  }

  async setKind(id: string, kind: ProjectKind): Promise<void> {
    if (!KINDS.includes(kind)) throw new Error('Unknown project kind');
    await this.db.execute('UPDATE projects SET kind = ?, updated_at = ? WHERE id = ?', [kind, this.now(), id]);
  }

  /** Merges into the project's defaults (undefined leaves a value, null clears it). */
  async setDefaults(id: string, patch: { [K in keyof ProjectDefaults]?: string | null }): Promise<ProjectDefaults> {
    const current = await this.get(id);
    if (!current) throw new Error('This project no longer exists');
    const next: Record<string, string> = { ...current.defaults };
    for (const [k, v] of Object.entries(patch)) {
      if (v === null) delete next[k];
      else if (typeof v === 'string') next[k] = v;
    }
    const clean = parseDefaults(JSON.stringify(next));
    await this.db.execute('UPDATE projects SET defaults_json = ?, updated_at = ? WHERE id = ?', [
      JSON.stringify(clean),
      this.now(),
      id,
    ]);
    return clean;
  }

  async setCover(id: string, imageId: string | null): Promise<void> {
    await this.db.execute('UPDATE projects SET cover_image_id = ?, updated_at = ? WHERE id = ?', [
      imageId,
      this.now(),
      id,
    ]);
  }

  async setArchived(id: string, archived: boolean): Promise<void> {
    await this.db.execute('UPDATE projects SET archived = ?, updated_at = ? WHERE id = ?', [
      archived ? 1 : 0,
      this.now(),
      id,
    ]);
  }

  async touch(id: string): Promise<void> {
    await this.db.execute('UPDATE projects SET updated_at = ? WHERE id = ?', [this.now(), id]);
  }

  /** Deletes the project row; images cascade. Returns image paths to remove from disk. */
  async delete(id: string): Promise<string[]> {
    const paths = await this.db.query<{ path: string }>('SELECT path FROM images WHERE project_id = ?', [id]);
    await this.db.execute('DELETE FROM projects WHERE id = ?', [id]);
    return paths.map(p => p.path);
  }

  /** Returns the id of the default "Captures" project, creating it on first use. */
  async ensureInbox(name = 'Captures'): Promise<Project> {
    const id = await this.internal.getInternal<unknown>(INBOX_KEY);
    if (typeof id === 'string') {
      const existing = await this.get(id);
      if (existing) {
        return existing;
      }
    }
    const project = await this.create(name);
    await this.internal.setInternal(INBOX_KEY, project.id);
    return project;
  }
}
