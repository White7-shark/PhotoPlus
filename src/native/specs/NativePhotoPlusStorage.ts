import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/** App file-system areas and usage (Kotlin: com.photoplus.storage.StorageModule). */
export interface Spec extends TurboModule {
  getPaths(): Promise<Object>;
  getStorageUsage(): Promise<Object>;
  clearCache(): Promise<number>;
  deleteFile(path: string): Promise<boolean>;
  fileExists(path: string): Promise<boolean>;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusStorage');
