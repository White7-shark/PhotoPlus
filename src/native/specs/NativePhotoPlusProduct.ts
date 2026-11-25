import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * Product photography (Kotlin: com.photoplus.studio.ProductModule): an
 * object cut out of its photo and made a catalogue shot, fully on the
 * device. A session keeps the photo and its object mask while the cut-out is
 * corrected and setups are tried. The photo itself is never changed.
 */
export interface Spec extends TurboModule {
  /** Finds the product in a photo: a session and a first cut-out (previews). */
  openProduct(jobId: string, request: Object): Promise<Object>;
  /** The cut-out again with corrections (strokes and edge settings). */
  refineProduct(jobId: string, request: Object): Promise<Object>;
  /** The catalogue shot: a preview, or the saved photo at an export profile. */
  renderProduct(jobId: string, request: Object): Promise<Object>;
  closeProduct(sessionId: string): void;
  cancel(jobId: string): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusProduct');
