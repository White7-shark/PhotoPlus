import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * Bridge to the C/C++ processing engine (Kotlin: com.photoplus.engine.EngineModule).
 * Jobs run on a bounded native worker pool and can be cancelled by id.
 */
export interface Spec extends TurboModule {
  getEngineInfo(): Promise<Object>;
  process(jobId: string, request: Object): Promise<Object>;
  cancel(jobId: string): void;
  cancelAll(): void;
  getMemoryStats(): Promise<Object>;
  getProfileReport(): Promise<string>;
  resetProfiler(): void;
  generateTestImage(width: number, height: number): Promise<string>;
  readImageMetadata(path: string): Promise<Object>;
  addListener(eventName: string): void;
  removeListeners(count: number): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusEngine');
