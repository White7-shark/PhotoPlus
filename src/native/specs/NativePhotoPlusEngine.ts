import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * Bridge to the C/C++ processing engine (Kotlin: com.photoplus.engine.EngineModule).
 * Jobs run on a bounded native worker pool and can be cancelled by id.
 */
export interface Spec extends TurboModule {
  getEngineInfo(): Promise<Object>;
  process(jobId: string, request: Object): Promise<Object>;
  /** Restores a saved photo (denoise, deblur, tone; upscale 2 = super-resolution). */
  enhance(jobId: string, request: Object): Promise<Object>;
  analyzeQuality(path: string): Promise<Object>;
  /** Spotlight on a saved photo (subject stands out, natural skin retouching). */
  portrait(jobId: string, request: Object): Promise<Object>;
  /** Applies a look ("Vibe Match") to a saved photo. */
  applyLook(jobId: string, request: Object): Promise<Object>;
  /** Measures a reference photo's look. */
  analyzeLook(path: string): Promise<Object>;
  cancel(jobId: string): void;
  cancelAll(): void;
  getMemoryStats(): Promise<Object>;
  getProfileReport(): Promise<string>;
  resetProfiler(): void;
  resetMemoryPeak(): void;
  generateTestImage(width: number, height: number): Promise<string>;
  readImageMetadata(path: string): Promise<Object>;
  addListener(eventName: string): void;
  removeListeners(count: number): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusEngine');
