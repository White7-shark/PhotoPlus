import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/** App file-system areas and usage (Kotlin: com.photoplus.storage.StorageModule). */
export interface Spec extends TurboModule {
  getPaths(): Promise<Object>;
  getStorageUsage(): Promise<Object>;
  clearCache(): Promise<number>;
  deleteFile(path: string): Promise<boolean>;
  fileExists(path: string): Promise<boolean>;
  /**
   * Opens the system photo picker (no storage permission needed) and copies
   * the chosen photo into app storage. `area` is 'imports', 'looks', 'poses' or 'backgrounds'.
   * Resolves with the new path, or null if the user cancelled.
   */
  pickImage(area: string): Promise<string | null>;
  /** Opens the system share sheet for photos in app storage. */
  shareImages(paths: string[], title: string): Promise<boolean>;
  /** Saves a Glide clip from the exports area to the gallery (Movies/Photo Plus); resolves with its content URI. */
  saveVideoToGallery(path: string): Promise<string>;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusStorage');
