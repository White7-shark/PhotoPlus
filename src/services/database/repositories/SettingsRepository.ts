import { isVibeLook, type VibeLook } from '../../looks/types';
import type { SqlExecutor } from '../SqlExecutor';

export type ThemePreference = 'dark' | 'light' | 'system';
export type ExportFormat = 'jpeg' | 'png' | 'webp';

export interface AppSettings {
  themePreference: ThemePreference;
  onboardingCompleted: boolean;
  haptics: boolean;
  reduceMotion: 'system' | 'on' | 'off';
  cameraGrid: boolean;
  defaultExportFormat: ExportFormat;
  defaultExportQuality: number;
  /** Longest edge for editing previews/exports; 0 = choose by device tier. */
  processingMaxDimension: number;
  cameraAspectRatio: '4:3' | '16:9' | '1:1';
  /** Capture resolution preset; unavailable presets fall back to the largest. */
  cameraResolution: 'max' | '12mp' | '8mp' | '5mp' | '2mp';
  /** Save selfies mirrored, as the preview shows them. */
  mirrorFrontCamera: boolean;
  /** Vibe Match look in use (premium), or null. */
  vibeLook: VibeLook | null;
}

export const DEFAULT_SETTINGS: AppSettings = {
  themePreference: 'dark',
  onboardingCompleted: false,
  haptics: true,
  reduceMotion: 'system',
  cameraGrid: true,
  defaultExportFormat: 'jpeg',
  defaultExportQuality: 92,
  processingMaxDimension: 0,
  cameraAspectRatio: '4:3',
  cameraResolution: 'max',
  mirrorFrontCamera: true,
  vibeLook: null,
};

type Validators = { [K in keyof AppSettings]: (v: unknown) => v is AppSettings[K] };

const oneOf =
  <T extends string>(...values: T[]) =>
  (v: unknown): v is T =>
    typeof v === 'string' && (values as string[]).includes(v);
const isBool = (v: unknown): v is boolean => typeof v === 'boolean';

const VALIDATORS: Validators = {
  themePreference: oneOf('dark', 'light', 'system'),
  onboardingCompleted: isBool,
  haptics: isBool,
  reduceMotion: oneOf('system', 'on', 'off'),
  cameraGrid: isBool,
  defaultExportFormat: oneOf('jpeg', 'png', 'webp'),
  defaultExportQuality: (v): v is number => typeof v === 'number' && Number.isInteger(v) && v >= 60 && v <= 100,
  processingMaxDimension: (v): v is number => v === 0 || v === 2048 || v === 4096 || v === 8192,
  cameraAspectRatio: oneOf('4:3', '16:9', '1:1'),
  cameraResolution: oneOf('max', '12mp', '8mp', '5mp', '2mp'),
  mirrorFrontCamera: isBool,
  vibeLook: (v): v is VibeLook | null => v === null || isVibeLook(v),
};

export function isValidSetting<K extends keyof AppSettings>(key: K, value: unknown): value is AppSettings[K] {
  return VALIDATORS[key](value);
}

/**
 * Key/value settings with per-key validation. Unknown keys are ignored and
 * corrupt values fall back to defaults, so a bad write can never brick the
 * app at startup.
 */
export class SettingsRepository {
  constructor(
    private readonly db: SqlExecutor,
    private readonly now: () => number = Date.now,
  ) {}

  async load(): Promise<AppSettings> {
    const rows = await this.db.query<{ key: string; value: string }>('SELECT key, value FROM settings');
    const settings: AppSettings = { ...DEFAULT_SETTINGS };
    for (const row of rows) {
      if (!(row.key in VALIDATORS)) {
        continue;
      }
      const key = row.key as keyof AppSettings;
      try {
        const parsed: unknown = JSON.parse(row.value);
        if (isValidSetting(key, parsed)) {
          (settings as unknown as Record<string, unknown>)[key] = parsed;
        }
      } catch {
        // Corrupt JSON: keep default.
      }
    }
    return settings;
  }

  async set<K extends keyof AppSettings>(key: K, value: AppSettings[K]): Promise<void> {
    if (!isValidSetting(key, value)) {
      throw new Error(`Invalid value for setting "${key}"`);
    }
    await this.db.execute(
      `INSERT INTO settings (key, value, updated_at) VALUES (?, ?, ?)
       ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated_at = excluded.updated_at`,
      [key, JSON.stringify(value), this.now()],
    );
  }

  /** Restores user settings to defaults; internal bookkeeping rows are kept. */
  async reset(): Promise<void> {
    await this.db.execute("DELETE FROM settings WHERE key NOT LIKE 'internal.%'");
  }

  /** Arbitrary JSON blobs that are not user settings (e.g. cached capabilities). */
  async getInternal<T>(key: string): Promise<T | null> {
    const rows = await this.db.query<{ value: string }>('SELECT value FROM settings WHERE key = ?', [
      `internal.${key}`,
    ]);
    if (!rows[0]) {
      return null;
    }
    try {
      return JSON.parse(rows[0].value) as T;
    } catch {
      return null;
    }
  }

  async setInternal(key: string, value: unknown): Promise<void> {
    await this.db.execute(
      `INSERT INTO settings (key, value, updated_at) VALUES (?, ?, ?)
       ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated_at = excluded.updated_at`,
      [`internal.${key}`, JSON.stringify(value), this.now()],
    );
  }
}
