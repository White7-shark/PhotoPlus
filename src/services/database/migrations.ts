import { createLogger } from '../logging/logger';
import type { SqlExecutor, SqlStatement } from './SqlExecutor';

const log = createLogger('migrations');

export interface Migration {
  version: number;
  name: string;
  statements: string[];
}

/**
 * Append-only list. Never edit a shipped migration — add a new one.
 * Timestamps are epoch milliseconds; JSON columns hold structured metadata
 * that does not need to be queried.
 */
export const MIGRATIONS: readonly Migration[] = [
  {
    version: 1,
    name: 'initial_schema',
    statements: [
      `CREATE TABLE settings (
        key TEXT PRIMARY KEY NOT NULL,
        value TEXT NOT NULL,
        updated_at INTEGER NOT NULL
      )`,
      `CREATE TABLE projects (
        id TEXT PRIMARY KEY NOT NULL,
        name TEXT NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
        cover_image_id TEXT,
        archived INTEGER NOT NULL DEFAULT 0 CHECK (archived IN (0, 1)),
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL
      )`,
      `CREATE TABLE images (
        id TEXT PRIMARY KEY NOT NULL,
        project_id TEXT REFERENCES projects(id) ON DELETE CASCADE,
        path TEXT NOT NULL UNIQUE,
        source TEXT NOT NULL CHECK (source IN ('camera', 'import', 'export', 'test')),
        width INTEGER NOT NULL DEFAULT 0,
        height INTEGER NOT NULL DEFAULT 0,
        size_bytes INTEGER NOT NULL DEFAULT 0,
        mime_type TEXT,
        captured_at INTEGER,
        metadata_json TEXT NOT NULL DEFAULT '{}',
        created_at INTEGER NOT NULL
      )`,
      'CREATE INDEX idx_images_project ON images(project_id, created_at DESC)',
      `CREATE TABLE processing_history (
        id TEXT PRIMARY KEY NOT NULL,
        image_id TEXT REFERENCES images(id) ON DELETE SET NULL,
        job_id TEXT NOT NULL,
        operations_json TEXT NOT NULL,
        status TEXT NOT NULL CHECK (status IN ('succeeded', 'failed', 'cancelled')),
        error_code TEXT,
        duration_ms INTEGER,
        output_path TEXT,
        created_at INTEGER NOT NULL
      )`,
      'CREATE INDEX idx_history_created ON processing_history(created_at DESC)',
      `CREATE TABLE assets (
        id TEXT PRIMARY KEY NOT NULL,
        kind TEXT NOT NULL CHECK (kind IN ('pack', 'model', 'lut', 'overlay', 'font')),
        name TEXT NOT NULL,
        version TEXT NOT NULL,
        path TEXT NOT NULL,
        size_bytes INTEGER NOT NULL DEFAULT 0,
        checksum TEXT,
        source TEXT NOT NULL DEFAULT 'bundled' CHECK (source IN ('bundled', 'download', 'user')),
        installed_at INTEGER NOT NULL,
        last_used_at INTEGER
      )`,
      'CREATE INDEX idx_assets_kind ON assets(kind)',
    ],
  },
];

const BOOKKEEPING = `CREATE TABLE IF NOT EXISTS schema_migrations (
  version INTEGER PRIMARY KEY NOT NULL,
  name TEXT NOT NULL,
  applied_at INTEGER NOT NULL
)`;

export async function currentSchemaVersion(db: SqlExecutor): Promise<number> {
  await db.execute(BOOKKEEPING);
  const rows = await db.query<{ version: number | null }>('SELECT MAX(version) AS version FROM schema_migrations');
  return rows[0]?.version ?? 0;
}

/**
 * Applies pending migrations, each in its own transaction together with its
 * bookkeeping row, so an interrupted upgrade (app killed, battery died)
 * leaves the database at a consistent version and simply resumes next launch.
 */
export async function runMigrations(
  db: SqlExecutor,
  migrations: readonly Migration[] = MIGRATIONS,
  now: () => number = Date.now,
): Promise<{ from: number; to: number; applied: number[] }> {
  assertOrdered(migrations);
  const from = await currentSchemaVersion(db);
  const latest = migrations.length ? migrations[migrations.length - 1].version : 0;
  if (from > latest) {
    throw new Error(`Database schema v${from} is newer than this app supports (v${latest}). Please update Photo Plus.`);
  }
  const applied: number[] = [];
  for (const migration of migrations) {
    if (migration.version <= from) {
      continue;
    }
    const statements: SqlStatement[] = migration.statements.map(sql => ({ sql }));
    statements.push({
      sql: 'INSERT INTO schema_migrations (version, name, applied_at) VALUES (?, ?, ?)',
      params: [migration.version, migration.name, now()],
    });
    await db.transaction(statements);
    applied.push(migration.version);
    log.info(`Applied migration v${migration.version} (${migration.name})`);
  }
  return { from, to: applied.length ? applied[applied.length - 1] : from, applied };
}

function assertOrdered(migrations: readonly Migration[]) {
  migrations.forEach((m, i) => {
    if (m.version !== i + 1) {
      throw new Error(`Migrations must be contiguous from 1; found v${m.version} at index ${i}`);
    }
  });
}
