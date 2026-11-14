import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * The photoshoot studio (Kotlin: com.photoplus.studio.StudioModule): a person
 * cut out of their photo and placed into another scene, fully on the device.
 * A session keeps the photo and its person masks while the person corrects
 * the cut-out and tries backgrounds.
 */
export interface Spec extends TurboModule {
  /** Finds the person in a photo: a session and a first cut-out (previews). */
  startSession(jobId: string, request: Object): Promise<Object>;
  /** The cut-out again with corrections (strokes and edge settings). */
  refineCutout(jobId: string, request: Object): Promise<Object>;
  /** The person in a scene: a preview, or the full-quality photo (Premium). */
  renderScene(jobId: string, request: Object): Promise<Object>;
  /** Adds a saved studio photo to the device gallery (Pictures/Photo Plus). */
  saveToGallery(path: string): Promise<string>;
  endSession(sessionId: string): void;
  cancel(jobId: string): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusStudio');
