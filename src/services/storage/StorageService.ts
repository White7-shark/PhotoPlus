import { nativeModules, requireNative, type StorageSpec } from '../../native';

export interface AppStoragePaths {
  projects: string;
  captures: string;
  exports: string;
  packs: string;
  models: string;
  processingCache: string;
  testImages: string;
  cache: string;
}

export interface StorageUsage {
  areas: Record<string, number>;
  cacheBytes: number;
  databaseBytes: number;
  appTotalBytes: number;
  deviceAvailableBytes: number;
  deviceTotalBytes: number;
}

export class StorageService {
  private paths: AppStoragePaths | null = null;

  constructor(private readonly native: StorageSpec = requireNative(nativeModules.storage, 'PhotoPlusStorage')) {}

  async getPaths(): Promise<AppStoragePaths> {
    this.paths ??= (await this.native.getPaths()) as unknown as AppStoragePaths;
    return this.paths;
  }

  async usage(): Promise<StorageUsage> {
    return (await this.native.getStorageUsage()) as unknown as StorageUsage;
  }

  clearCache(): Promise<number> {
    return this.native.clearCache();
  }

  deleteFile(path: string): Promise<boolean> {
    return this.native.deleteFile(path);
  }

  exists(path: string): Promise<boolean> {
    return this.native.fileExists(path);
  }
}

let shared: StorageService | null = null;
export function getStorageService(): StorageService {
  shared ??= new StorageService();
  return shared;
}

const UNITS = ['B', 'KB', 'MB', 'GB', 'TB'];

export function formatBytes(bytes: number, fractionDigits = 1): string {
  if (!Number.isFinite(bytes) || bytes <= 0) {
    return '0 B';
  }
  const exponent = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), UNITS.length - 1);
  const value = bytes / Math.pow(1024, exponent);
  return `${exponent === 0 ? value.toFixed(0) : value.toFixed(fractionDigits)} ${UNITS[exponent]}`;
}

export function joinPath(...parts: string[]): string {
  return parts
    .map((p, i) => (i === 0 ? p.replace(/\/+$/, '') : p.replace(/^\/+|\/+$/g, '')))
    .filter(Boolean)
    .join('/');
}
