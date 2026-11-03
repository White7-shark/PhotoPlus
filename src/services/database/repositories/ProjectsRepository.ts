import { createId } from '../ids';
import type { SqlExecutor } from '../SqlExecutor';
import { SettingsRepository } from './SettingsRepository';

export interface Project {
  id: string;
  name: string;
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

const toProject = (r: ProjectRow): Project => ({
  id: r.id,
  name: r.name,
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

  async create(name: string): Promise<Project> {
    const id = createId('prj', this.now());
    const ts = this.now();
    await this.db.execute('INSERT INTO projects (id, name, archived, created_at, updated_at) VALUES (?, ?, 0, ?, ?)', [
      id,
      normaliseProjectName(name),
      ts,
      ts,
    ]);
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
