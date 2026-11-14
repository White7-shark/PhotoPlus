import { nativeModules, requireNative, type StorageSpec } from '../../native';

export interface AppStoragePaths {
  projects: string;
  captures: string;
  exports: string;
  imports: string;
  looks: string;
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

  /** System photo picker; the chosen photo is copied into app storage. Null if cancelled. */
  pickImage(area: 'imports' | 'looks' | 'poses' | 'backgrounds' = 'imports'): Promise<string | null> {
    return this.native.pickImage(area);
  }

  /** Opens the share sheet so photos can be posted to other apps. */
  share(paths: string[], title = 'Share photo'): Promise<boolean> {
    return this.native.shareImages(paths, title);
  }

  /** Saves a Glide clip (from the exports area) to the gallery; resolves with its address there. */
  saveVideoToGallery(path: string): Promise<string> {
    return this.native.saveVideoToGallery(path);
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
