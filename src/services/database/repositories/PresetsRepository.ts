import { createId } from '../ids';
import type { SqlExecutor } from '../SqlExecutor';

export type PresetKind = 'product' | 'look' | 'export';

export interface Preset<T = unknown> {
  id: string;
  kind: PresetKind;
  name: string;
  data: T;
  createdAt: number;
  updatedAt: number;
}

interface PresetRow {
  id: string;
  kind: PresetKind;
  name: string;
  data_json: string;
  created_at: number;
  updated_at: number;
}

export const MAX_PRESET_NAME = 60;

export function normalisePresetName(name: string): string {
  const trimmed = name.replace(/\s+/g, ' ').trim();
  if (!trimmed) throw new Error('Give the setup a name');
  return trimmed.slice(0, MAX_PRESET_NAME);
}

/**
 * Saved setups the person reuses: product setups, photo looks and export
 * profiles. Data is stored as JSON and checked by the caller when read back
 * (`valid`), so a damaged row is skipped, never applied.
 */
export class PresetsRepository {
  constructor(
    private readonly db: SqlExecutor,
    private readonly now: () => number = Date.now,
  ) {}

  async save<T>(kind: PresetKind, name: string, data: T): Promise<Preset<T>> {
    const ts = this.now();
    const preset: Preset<T> = {
      id: createId('pre', ts),
      kind,
      name: normalisePresetName(name),
      data,
      createdAt: ts,
      updatedAt: ts,
    };
    await this.db.execute(
      'INSERT INTO presets (id, kind, name, data_json, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?)',
      [preset.id, kind, preset.name, JSON.stringify(data), ts, ts],
    );
    return preset;
  }

  async list<T>(kind: PresetKind, valid: (v: unknown) => v is T): Promise<Preset<T>[]> {
    const rows = await this.db.query<PresetRow>('SELECT * FROM presets WHERE kind = ? ORDER BY updated_at DESC', [
      kind,
    ]);
    const out: Preset<T>[] = [];
    for (const r of rows) {
      let data: unknown;
      try {
        data = JSON.parse(r.data_json);
      } catch {
        continue;
      }
      if (!valid(data)) continue;
      out.push({ id: r.id, kind: r.kind, name: r.name, data, createdAt: r.created_at, updatedAt: r.updated_at });
    }
    return out;
  }

  async rename(id: string, name: string): Promise<void> {
    await this.db.execute('UPDATE presets SET name = ?, updated_at = ? WHERE id = ?', [
      normalisePresetName(name),
      this.now(),
      id,
    ]);
  }

  async update<T>(id: string, data: T): Promise<void> {
    await this.db.execute('UPDATE presets SET data_json = ?, updated_at = ? WHERE id = ?', [
      JSON.stringify(data),
      this.now(),
      id,
    ]);
  }

  async remove(id: string): Promise<void> {
    await this.db.execute('DELETE FROM presets WHERE id = ?', [id]);
  }
}
