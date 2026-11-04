import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * Camera control surface (Kotlin: com.photoplus.camera.CameraModule).
 * The live preview is the PhotoPlusCameraPreview native component; this
 * module drives whichever preview session is active. Live state (zoom,
 * focus, sensor metadata, orientation, recovery, capture progress) arrives
 * as DeviceEventEmitter events, see CameraEvents in services/camera.
 */
export interface Spec extends TurboModule {
  isSessionReady(): Promise<boolean>;
  /** What the active camera supports, or null before the first bind. */
  getCapabilities(): Promise<Object | null>;
  /** aspectRatio ('4:3' | '16:9' | '1:1'), resolution ('max' | '12mp' ...), raw (boolean). */
  configure(config: Object): void;
  capturePhoto(options: Object): Promise<Object>;
  cancelCapture(captureId: string): void;
  setZoom(ratio: number): Promise<number>;
  setExposureCompensation(index: number): Promise<number>;
  focusAt(x: number, y: number): Promise<boolean>;
  cancelFocus(): void;
  setFlash(mode: string): void;
  setTorch(enabled: boolean): Promise<boolean>;
  /** color is 0xAARRGGBB; threshold 0..1 (lower highlights more edges). */
  setFocusPeaking(enabled: boolean, color: number, threshold: number): void;
  /** Applies manual controls; resolves with the values actually applied (clamped). */
  setManual(settings: Object): Promise<Object>;
  resetManual(): void;
  addListener(eventName: string): void;
  removeListeners(count: number): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusCamera');
