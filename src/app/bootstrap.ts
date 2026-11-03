import { detectCapabilities, parseCapabilities, type DeviceCapabilities } from '../services/capabilities';
import { openDatabase, type Repositories } from '../services/database/Database';
import { DEFAULT_SETTINGS, type AppSettings } from '../services/database/repositories/SettingsRepository';
import { createLogger } from '../services/logging/logger';
import type { EngineInfo } from '../services/processing/types';

const log = createLogger('bootstrap');

export interface BootstrapDeps {
  openDatabase: () => Promise<Repositories>;
  detectCapabilities: () => Promise<DeviceCapabilities>;
  engineInfo: () => Promise<EngineInfo>;
  ensurePaths: () => Promise<unknown>;
}

export interface BootstrapResult {
  repositories: Repositories;
  settings: AppSettings;
  capabilities: DeviceCapabilities | null;
  capabilitiesStale: boolean;
  engine: EngineInfo | null;
  warnings: string[];
  durationMs: number;
}

export class BootstrapError extends Error {
  constructor(
    message: string,
    readonly step: string,
    readonly cause?: unknown,
  ) {
    super(message);
    this.name = 'BootstrapError';
  }
}

const CAPABILITY_CACHE_KEY = 'capabilities';

/**
 * Startup sequence. Only the database is critical: without it there is no
 * project data, so failure surfaces a retryable error screen. Everything else
 * degrades — capabilities fall back to the last cached snapshot, a missing
 * engine disables processing features but the camera still works.
 */
export async function bootstrap(deps: BootstrapDeps): Promise<BootstrapResult> {
  const started = Date.now();
  const warnings: string[] = [];

  let repositories: Repositories;
  try {
    repositories = await deps.openDatabase();
  } catch (error) {
    throw new BootstrapError('Photo Plus could not open its local library.', 'database', error);
  }

  let settings: AppSettings = { ...DEFAULT_SETTINGS };
  try {
    settings = await repositories.settings.load();
  } catch (error) {
    warnings.push('Settings could not be loaded; defaults are in use.');
    log.warn('settings load failed', error);
  }

  const [capsResult, engineResult, pathsResult] = await Promise.allSettled([
    deps.detectCapabilities(),
    deps.engineInfo(),
    deps.ensurePaths(),
  ]);

  let capabilities: DeviceCapabilities | null = null;
  let capabilitiesStale = false;
  if (capsResult.status === 'fulfilled') {
    capabilities = capsResult.value;
    repositories.settings.setInternal(CAPABILITY_CACHE_KEY, capabilities).catch(e => log.warn('cache write failed', e));
  } else {
    log.warn('capability probe failed', capsResult.reason);
    const cached = await repositories.settings.getInternal<unknown>(CAPABILITY_CACHE_KEY).catch(() => null);
    if (cached) {
      capabilities = parseCapabilities(cached);
      capabilitiesStale = true;
      warnings.push('Device check was slow; using the last known device profile.');
    } else {
      warnings.push('Device capabilities could not be detected.');
    }
  }

  let engine: EngineInfo | null = null;
  if (engineResult.status === 'fulfilled' && engineResult.value.available) {
    engine = engineResult.value;
  } else {
    warnings.push('The processing engine is unavailable on this device; editing is disabled.');
  }

  if (pathsResult.status === 'rejected') {
    warnings.push('App storage folders could not be prepared.');
  }

  // Housekeeping that must never block startup.
  repositories.history.prune().catch(e => log.warn('history prune failed', e));

  const durationMs = Date.now() - started;
  log.info(`Bootstrap finished in ${durationMs}ms`, { warnings: warnings.length });
  return { repositories, settings, capabilities, capabilitiesStale, engine, warnings, durationMs };
}

export function defaultBootstrapDeps(): BootstrapDeps {
  // Lazy requires keep this module importable in tests without native modules.
  const { getProcessingService } = require('../services/processing/ImageProcessingService');
  const { getStorageService } = require('../services/storage/StorageService');
  return {
    openDatabase: () => openDatabase(),
    detectCapabilities: () => detectCapabilities(),
    engineInfo: () => getProcessingService().info(),
    ensurePaths: () => getStorageService().getPaths(),
  };
}
