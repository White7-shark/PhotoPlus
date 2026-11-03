import { createId } from '../ids';
import type { SqlExecutor } from '../SqlExecutor';

export type ImageSource = 'camera' | 'import' | 'export' | 'test';

export interface ImageMetadata {
  orientation?: number;
  make?: string | null;
  model?: string | null;
  iso?: number | null;
  exposureTime?: number | null;
  fNumber?: number | null;
  focalLength?: number | null;
  [key: string]: unknown;
}

export interface ImageRecord {
  id: string;
  projectId: string | null;
  path: string;
  source: ImageSource;
  width: number;
  height: number;
  sizeBytes: number;
  mimeType: string | null;
  capturedAt: number | null;
  metadata: ImageMetadata;
  createdAt: number;
}

export type NewImage = Omit<ImageRecord, 'id' | 'createdAt' | 'metadata'> & { metadata?: ImageMetadata };

interface ImageRow {
  id: string;
  project_id: string | null;
  path: string;
  source: ImageSource;
  width: number;
  height: number;
  size_bytes: number;
  mime_type: string | null;
  captured_at: number | null;
  metadata_json: string;
  created_at: number;
}

function parseMetadata(json: string): ImageMetadata {
  try {
    const value: unknown = JSON.parse(json);
    return value && typeof value === 'object' && !Array.isArray(value) ? (value as ImageMetadata) : {};
  } catch {
    return {};
  }
}

const toImage = (r: ImageRow): ImageRecord => ({
  id: r.id,
  projectId: r.project_id,
  path: r.path,
  source: r.source,
  width: r.width,
  height: r.height,
  sizeBytes: r.size_bytes,
  mimeType: r.mime_type,
  capturedAt: r.captured_at,
  metadata: parseMetadata(r.metadata_json),
  createdAt: r.created_at,
});

export class ImagesRepository {
  constructor(
    private readonly db: SqlExecutor,
    private readonly now: () => number = Date.now,
  ) {}

  async add(image: NewImage): Promise<ImageRecord> {
    const record: ImageRecord = {
      ...image,
      id: createId('img', this.now()),
      metadata: image.metadata ?? {},
      createdAt: this.now(),
    };
    await this.db.transaction([
      {
        sql: `INSERT INTO images (id, project_id, path, source, width, height, size_bytes, mime_type, captured_at, metadata_json, created_at)
              VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
        params: [
          record.id,
          record.projectId,
          record.path,
          record.source,
          Math.round(record.width),
          Math.round(record.height),
          Math.round(record.sizeBytes),
          record.mimeType,
          record.capturedAt,
          JSON.stringify(record.metadata),
          record.createdAt,
        ],
      },
      ...(record.projectId
        ? [{ sql: 'UPDATE projects SET updated_at = ? WHERE id = ?', params: [record.createdAt, record.projectId] }]
        : []),
    ]);
    return record;
  }

  async get(id: string): Promise<ImageRecord | null> {
    const rows = await this.db.query<ImageRow>('SELECT * FROM images WHERE id = ?', [id]);
    return rows[0] ? toImage(rows[0]) : null;
  }

  async listByProject(projectId: string, limit = 200): Promise<ImageRecord[]> {
    const rows = await this.db.query<ImageRow>(
      'SELECT * FROM images WHERE project_id = ? ORDER BY created_at DESC LIMIT ?',
      [projectId, limit],
    );
    return rows.map(toImage);
  }

  async listRecent(limit = 20): Promise<ImageRecord[]> {
    const rows = await this.db.query<ImageRow>('SELECT * FROM images ORDER BY created_at DESC LIMIT ?', [limit]);
    return rows.map(toImage);
  }

  async remove(id: string): Promise<string | null> {
    const image = await this.get(id);
    if (!image) {
      return null;
    }
    await this.db.execute('DELETE FROM images WHERE id = ?', [id]);
    return image.path;
  }

  async totalBytes(): Promise<number> {
    const rows = await this.db.query<{ total: number | null }>('SELECT SUM(size_bytes) AS total FROM images');
    return rows[0]?.total ?? 0;
  }
}
