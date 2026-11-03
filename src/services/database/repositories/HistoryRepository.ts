import { createId } from '../ids';
import type { SqlExecutor } from '../SqlExecutor';

export type HistoryStatus = 'succeeded' | 'failed' | 'cancelled';

export interface HistoryEntry {
  id: string;
  imageId: string | null;
  jobId: string;
  operations: unknown[];
  status: HistoryStatus;
  errorCode: string | null;
  durationMs: number | null;
  outputPath: string | null;
  createdAt: number;
}

interface HistoryRow {
  id: string;
  image_id: string | null;
  job_id: string;
  operations_json: string;
  status: HistoryStatus;
  error_code: string | null;
  duration_ms: number | null;
  output_path: string | null;
  created_at: number;
}

export const HISTORY_RETENTION = 500;

export class HistoryRepository {
  constructor(private readonly db: SqlExecutor, private readonly now: () => number = Date.now) {}

  async record(entry: Omit<HistoryEntry, 'id' | 'createdAt'>): Promise<HistoryEntry> {
    const full: HistoryEntry = { ...entry, id: createId('job', this.now()), createdAt: this.now() };
    await this.db.execute(
      `INSERT INTO processing_history (id, image_id, job_id, operations_json, status, error_code, duration_ms, output_path, created_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`,
      [
        full.id,
        full.imageId,
        full.jobId,
        JSON.stringify(full.operations),
        full.status,
        full.errorCode,
        full.durationMs === null ? null : Math.round(full.durationMs),
        full.outputPath,
        full.createdAt,
      ],
    );
    return full;
  }

  async listRecent(limit = 50): Promise<HistoryEntry[]> {
    const rows = await this.db.query<HistoryRow>(
      'SELECT * FROM processing_history ORDER BY created_at DESC, rowid DESC LIMIT ?',
      [limit],
    );
    return rows.map(r => ({
      id: r.id,
      imageId: r.image_id,
      jobId: r.job_id,
      operations: safeArray(r.operations_json),
      status: r.status,
      errorCode: r.error_code,
      durationMs: r.duration_ms,
      outputPath: r.output_path,
      createdAt: r.created_at,
    }));
  }

  /** Keeps the newest `keep` entries so metadata cannot grow without bound. */
  async prune(keep = HISTORY_RETENTION): Promise<number> {
    const result = await this.db.execute(
      `DELETE FROM processing_history WHERE id NOT IN (
         SELECT id FROM processing_history ORDER BY created_at DESC, rowid DESC LIMIT ?
       )`,
      [keep],
    );
    return result.rowsAffected;
  }
}

function safeArray(json: string): unknown[] {
  try {
    const v: unknown = JSON.parse(json);
    return Array.isArray(v) ? v : [];
  } catch {
    return [];
  }
}
