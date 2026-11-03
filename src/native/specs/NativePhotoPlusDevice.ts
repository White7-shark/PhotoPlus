import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * Device capability probe (Kotlin: com.photoplus.device.DeviceModule).
 * Objects are returned untyped across the bridge and validated in
 * src/services/capabilities, which owns the TypeScript shape.
 */
export interface Spec extends TurboModule {
  getCapabilities(): Promise<Object>;
  getMemorySnapshot(): Promise<Object>;
  // Required by NativeEventEmitter for the memory-pressure event stream.
  addListener(eventName: string): void;
  removeListeners(count: number): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusDevice');
