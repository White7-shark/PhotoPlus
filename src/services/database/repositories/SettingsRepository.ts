import {
  DEFAULT_WATERMARK,
  isBrandingPresets,
  isWatermarkSpec,
  type BrandingPreset,
  type WatermarkSpec,
} from '../../edit/watermark';
import { isCustomProps, type CustomProp } from '../../edit/catalog';
import { isVibeLook, type VibeLook } from '../../looks/types';
import type { SqlExecutor } from '../SqlExecutor';
import {
  DEFAULT_CAMERA_BEAUTY,
  DEFAULT_PORTRAIT,
  isCameraBeauty,
  isPortraitControls,
  type CameraBeauty,
} from '../../beauty/camera';
import type { PortraitControls } from '../../beauty/recipe';
import type { PoseDef } from '../../pose/library';
import {
  DEFAULT_POSE_ASSISTANT,
  isGridStyle,
  isMyPoses,
  isPoseAssistantSettings,
  type GridStyle,
  type PoseAssistantSettings,
} from '../../pose/settings';

export type ThemePreference = 'dark' | 'light' | 'system';
export type ExportFormat = 'jpeg' | 'png' | 'webp';

export interface AppSettings {
  themePreference: ThemePreference;
  onboardingCompleted: boolean;
  haptics: boolean;
  reduceMotion: 'system' | 'on' | 'off';
  cameraGrid: boolean;
  /** Which composition grid the camera shows. */
  cameraGridStyle: GridStyle;
  /** The pose assistant: level, face guide, chosen pose, guided and automatic capture. */
  poseAssistant: PoseAssistantSettings;
  /** Poses taken from the user's own photos (Premium). */
  myPoses: PoseDef[];
  defaultExportFormat: ExportFormat;
  defaultExportQuality: number;
  /** Longest edge for editing previews/exports; 0 = choose by device tier. */
  processingMaxDimension: number;
  cameraAspectRatio: '4:3' | '16:9' | '1:1';
  /** Capture resolution preset; unavailable presets fall back to the largest. */
  cameraResolution: 'max' | '12mp' | '8mp' | '5mp' | '2mp';
  /** Save selfies mirrored, as the preview shows them. */
  mirrorFrontCamera: boolean;
  /** Auto Light: dark rooms, night and harsh light handled automatically. */
  autoLight: boolean;
  /** Camera beauty (Glow) for photos outside Spotlight. Superseded by cameraBeauty; kept for saved settings. */
  cameraGlow: 'off' | 'natural' | 'smooth' | 'flawless';
  /** Beauty mode: on/off, the preset and its sliders. */
  cameraBeauty: CameraBeauty;
  /** Portrait mode's own controls (depth blur, subject and background light, tone, edges). */
  portraitControls: PortraitControls;
  /** Vibe Match look in use (premium), or null. */
  vibeLook: VibeLook | null;
  /** The watermark design (free plan: always the Photo Plus mark). */
  watermark: WatermarkSpec;
  /** Saved branding looks (Premium). */
  brandingPresets: BrandingPreset[];
  /** Props people made from their own photos (Premium). */
  customProps: CustomProp[];
}

export const DEFAULT_SETTINGS: AppSettings = {
  themePreference: 'dark',
  onboardingCompleted: false,
  haptics: true,
  reduceMotion: 'system',
  cameraGrid: true,
  cameraGridStyle: 'thirds',
  poseAssistant: DEFAULT_POSE_ASSISTANT,
  myPoses: [],
  defaultExportFormat: 'jpeg',
  defaultExportQuality: 92,
  processingMaxDimension: 0,
  cameraAspectRatio: '4:3',
  cameraResolution: 'max',
  mirrorFrontCamera: true,
  autoLight: true,
  cameraGlow: 'off',
  cameraBeauty: DEFAULT_CAMERA_BEAUTY,
  portraitControls: DEFAULT_PORTRAIT,
  vibeLook: null,
  watermark: DEFAULT_WATERMARK,
  brandingPresets: [],
  customProps: [],
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
  cameraGridStyle: isGridStyle,
  poseAssistant: isPoseAssistantSettings,
  myPoses: isMyPoses,
  defaultExportFormat: oneOf('jpeg', 'png', 'webp'),
  defaultExportQuality: (v): v is number => typeof v === 'number' && Number.isInteger(v) && v >= 60 && v <= 100,
  processingMaxDimension: (v): v is number => v === 0 || v === 2048 || v === 4096 || v === 8192,
  cameraAspectRatio: oneOf('4:3', '16:9', '1:1'),
  cameraResolution: oneOf('max', '12mp', '8mp', '5mp', '2mp'),
  mirrorFrontCamera: isBool,
  autoLight: isBool,
  cameraGlow: oneOf('off', 'natural', 'smooth', 'flawless'),
  cameraBeauty: isCameraBeauty,
  portraitControls: isPortraitControls,
  vibeLook: (v): v is VibeLook | null => v === null || isVibeLook(v),
  watermark: isWatermarkSpec,
  brandingPresets: isBrandingPresets,
  customProps: isCustomProps,
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
