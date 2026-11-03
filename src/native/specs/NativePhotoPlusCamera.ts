import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * Camera control surface (Kotlin: com.photoplus.camera.CameraModule).
 * The live preview is the PhotoPlusCameraPreview native component; this
 * module drives capture against whichever preview session is active.
 */
export interface Spec extends TurboModule {
  isSessionReady(): Promise<boolean>;
  capturePhoto(options: Object): Promise<Object>;
  setTorch(enabled: boolean): Promise<boolean>;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusCamera');
