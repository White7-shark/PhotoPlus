/**
 * Typed access to Photo Plus native modules.
 *
 * Modules are resolved with TurboModuleRegistry.get (nullable) so the JS
 * layer can render a clear "unavailable" state instead of crashing when a
 * module is missing (tests, previews, a future platform). Callers use
 * `requireNative` when the feature cannot work without the module.
 */
import NativeCamera from './specs/NativePhotoPlusCamera';
import NativeDatabase from './specs/NativePhotoPlusDatabase';
import NativeDevice from './specs/NativePhotoPlusDevice';
import NativeEngine from './specs/NativePhotoPlusEngine';
import NativeStorage from './specs/NativePhotoPlusStorage';

export type { Spec as CameraSpec } from './specs/NativePhotoPlusCamera';
export type { Spec as DatabaseSpec } from './specs/NativePhotoPlusDatabase';
export type { Spec as DeviceSpec } from './specs/NativePhotoPlusDevice';
export type { Spec as EngineSpec } from './specs/NativePhotoPlusEngine';
export type { Spec as StorageSpec } from './specs/NativePhotoPlusStorage';

export class NativeUnavailableError extends Error {
  readonly code = 'NATIVE_UNAVAILABLE';
  constructor(moduleName: string) {
    super(`Native module "${moduleName}" is not available in this build`);
    this.name = 'NativeUnavailableError';
  }
}

export function requireNative<T>(module: T | null | undefined, name: string): T {
  if (module == null) {
    throw new NativeUnavailableError(name);
  }
  return module;
}

export const nativeModules = {
  camera: NativeCamera,
  database: NativeDatabase,
  device: NativeDevice,
  engine: NativeEngine,
  storage: NativeStorage,
};

/** Event names emitted by the Kotlin layer through DeviceEventEmitter. */
export const NativeEvents = {
  memoryPressure: 'PhotoPlusMemoryPressure',
  jobState: 'PhotoPlusJobState',
} as const;

/** Error shape produced by rejected native promises. */
export interface NativeError extends Error {
  code?: string;
}

export function errorCode(error: unknown): string | undefined {
  if (error && typeof error === 'object' && 'code' in error) {
    const code = (error as { code?: unknown }).code;
    return typeof code === 'string' ? code : undefined;
  }
  return undefined;
}

export function errorMessage(error: unknown): string {
  if (error instanceof Error) {
    return error.message;
  }
  return typeof error === 'string' ? error : 'Unknown error';
}
