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
  /** Glide (sessions configured with video: true): records up to 10 s (30 s with Premium, read natively); resolves { path, durationMs } when it ends. */
  startVideo(options: Object): Promise<Object>;
  /** Ends a Glide recording early (startVideo then resolves). */
  stopVideo(): void;
  /** Beauty: the resolved recipe shown live on the preview (null = none); never rebinds. */
  setLiveBeauty(recipe: Object | null): void;
  /** Beauty: while true the preview shows the camera untouched ("Original"). */
  setBeautyOriginal(original: boolean): void;
  setZoom(ratio: number): Promise<number>;
  setExposureCompensation(index: number): Promise<number>;
  focusAt(x: number, y: number): Promise<boolean>;
  cancelFocus(): void;
  setFlash(mode: string): void;
  setTorch(enabled: boolean): Promise<boolean>;
  /** color is 0xAARRGGBB; threshold 0..1 (lower highlights more edges). */
  setFocusPeaking(enabled: boolean, color: number, threshold: number): void;
  /** Props in the camera: live face positions arrive as PhotoPlusCameraFaces events. */
  setFaceTracking(enabled: boolean): void;
  /**
   * Pose assistant: null turns it off. Otherwise { enabled?, reference?: { landmarks (33 × [x, y, z,
   * visibility, presence]), aspect }, mirrorReference, framing, strictness }; the live pose (and its
   * match with the reference) arrives as PhotoPlusPose events, PhotoPlusPoseState says whether it runs.
   */
  setPoseGuide(config: Object | null): void;
  /** The level: PhotoPlusLevel events ({ roll, pitch, orientation } in degrees) while enabled. */
  setLevel(enabled: boolean): void;
  /** The pose in a photo (content:// or path): { valid, aspect, landmarks (165 numbers), shot, unit }. */
  analyzePosePhoto(uri: string): Promise<Object>;
  /** Applies manual controls; resolves with the values actually applied (clamped). */
  setManual(settings: Object): Promise<Object>;
  resetManual(): void;
  addListener(eventName: string): void;
  removeListeners(count: number): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusCamera');
