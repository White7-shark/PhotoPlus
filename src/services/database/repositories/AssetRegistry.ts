import type { SqlExecutor } from '../SqlExecutor';

export type AssetKind = 'pack' | 'model' | 'lut' | 'overlay' | 'font';
export type AssetSource = 'bundled' | 'download' | 'user';

export interface AssetRecord {
  id: string;
  kind: AssetKind;
  name: string;
  version: string;
  path: string;
  sizeBytes: number;
  checksum: string | null;
  source: AssetSource;
  installedAt: number;
  lastUsedAt: number | null;
}

interface AssetRow {
  id: string;
  kind: AssetKind;
  name: string;
  version: string;
  path: string;
  size_bytes: number;
  checksum: string | null;
  source: AssetSource;
  installed_at: number;
  last_used_at: number | null;
}

const toAsset = (r: AssetRow): AssetRecord => ({
  id: r.id,
  kind: r.kind,
  name: r.name,
  version: r.version,
  path: r.path,
  sizeBytes: r.size_bytes,
  checksum: r.checksum,
  source: r.source,
  installedAt: r.installed_at,
  lastUsedAt: r.last_used_at,
});

/**
 * Registry of locally installed assets (photoshoot packs, models, LUTs…).
 * Records *what is on disk*, not entitlement — whether an asset may be used
 * is decided by the entitlement layer (see services/entitlements).
 */
export class AssetRegistry {
  constructor(private readonly db: SqlExecutor, private readonly now: () => number = Date.now) {}

  async register(asset: Omit<AssetRecord, 'installedAt' | 'lastUsedAt'>): Promise<AssetRecord> {
    const installedAt = this.now();
    await this.db.execute(
      `INSERT INTO assets (id, kind, name, version, path, size_bytes, checksum, source, installed_at, last_used_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, NULL)
       ON CONFLICT(id) DO UPDATE SET kind = excluded.kind, name = excluded.name, version = excluded.version,
         path = excluded.path, size_bytes = excluded.size_bytes, checksum = excluded.checksum,
         source = excluded.source, installed_at = excluded.installed_at`,
      [asset.id, asset.kind, asset.name, asset.version, asset.path, Math.round(asset.sizeBytes), asset.checksum, asset.source, installedAt],
    );
    return { ...asset, installedAt, lastUsedAt: null };
  }

  async get(id: string): Promise<AssetRecord | null> {
    const rows = await this.db.query<AssetRow>('SELECT * FROM assets WHERE id = ?', [id]);
    return rows[0] ? toAsset(rows[0]) : null;
  }

  async list(kind?: AssetKind): Promise<AssetRecord[]> {
    const rows = kind
      ? await this.db.query<AssetRow>('SELECT * FROM assets WHERE kind = ? ORDER BY name', [kind])
      : await this.db.query<AssetRow>('SELECT * FROM assets ORDER BY kind, name');
    return rows.map(toAsset);
  }

  async markUsed(id: string): Promise<void> {
    await this.db.execute('UPDATE assets SET last_used_at = ? WHERE id = ?', [this.now(), id]);
  }

  async remove(id: string): Promise<AssetRecord | null> {
    const existing = await this.get(id);
    if (existing) {
      await this.db.execute('DELETE FROM assets WHERE id = ?', [id]);
    }
    return existing;
  }

  async totalBytes(kind?: AssetKind): Promise<number> {
    const rows = kind
      ? await this.db.query<{ total: number | null }>('SELECT SUM(size_bytes) AS total FROM assets WHERE kind = ?', [kind])
      : await this.db.query<{ total: number | null }>('SELECT SUM(size_bytes) AS total FROM assets');
    return rows[0]?.total ?? 0;
  }

  /** Least-recently-used assets first — candidates for eviction when storage is low. */
  async evictionCandidates(limit = 10): Promise<AssetRecord[]> {
    const rows = await this.db.query<AssetRow>(
      "SELECT * FROM assets WHERE source != 'bundled' ORDER BY COALESCE(last_used_at, installed_at) ASC LIMIT ?",
      [limit],
    );
    return rows.map(toAsset);
  }
}
