import { DeviceEventEmitter, type EventSubscription } from 'react-native';

import { errorCode, errorMessage, nativeModules, requireNative, type CameraSpec } from '../../native';
import { parseCameraCapabilities } from './parse';
import {
  type CameraCapabilities,
  type CameraEventName,
  type CameraEventPayloads,
  type CaptureOptions,
  type CaptureResult,
  type FlashMode,
  type ManualSettings,
  type SessionConfig,
} from './types';

export * from './types';

export class CameraError extends Error {
  constructor(
    message: string,
    readonly code: string,
  ) {
    super(message);
    this.name = 'CameraError';
  }
}

type EventSource = Pick<typeof DeviceEventEmitter, 'addListener'>;

let captureCounter = 0;
const nextCaptureId = () => `cap-${Date.now().toString(36)}-${(captureCounter++).toString(36)}`;

/**
 * Typed façade over the native camera module. Native rejections become
 * CameraError with a stable code; capability maps are validated.
 */
export class CameraService {
  private readonly inFlight = new Set<string>();

  constructor(
    private readonly native: CameraSpec = requireNative(nativeModules.camera, 'PhotoPlusCamera'),
    private readonly events: EventSource = DeviceEventEmitter,
  ) {}

  /** Captures currently saving or processing. */
  get pendingCaptures(): number {
    return this.inFlight.size;
  }

  isReady(): Promise<boolean> {
    return this.native.isSessionReady();
  }

  async getCapabilities(): Promise<CameraCapabilities | null> {
    return parseCameraCapabilities(await this.native.getCapabilities());
  }

  configure(config: SessionConfig): void {
    this.native.configure(config);
  }

  /**
   * Takes a photo. Resolves when it is saved, which for processed modes
   * includes the engine run; listen to `progress` events in the meantime.
   */
  async capture(options: CaptureOptions, captureId: string = nextCaptureId()): Promise<CaptureResult> {
    if (this.inFlight.has(captureId)) {
      throw new CameraError('This capture is already in progress', 'BUSY');
    }
    this.inFlight.add(captureId);
    try {
      const result = (await this.native.capturePhoto({ ...options, captureId })) as unknown as CaptureResult;
      return { ...result, captureId };
    } catch (error) {
      throw new CameraError(errorMessage(error), errorCode(error) ?? 'CAPTURE_FAILED');
    } finally {
      this.inFlight.delete(captureId);
    }
  }

  cancelCapture(captureId: string): void {
    this.native.cancelCapture(captureId);
  }

  newCaptureId(): string {
    return nextCaptureId();
  }

  setZoom(ratio: number): Promise<number> {
    return this.native.setZoom(ratio);
  }

  setExposureCompensation(index: number): Promise<number> {
    return this.native.setExposureCompensation(index);
  }

  focusAt(x: number, y: number): Promise<boolean> {
    return this.native.focusAt(x, y);
  }

  cancelFocus(): void {
    this.native.cancelFocus();
  }

  setFlash(mode: FlashMode): void {
    this.native.setFlash(mode);
  }

  setTorch(enabled: boolean): Promise<boolean> {
    return this.native.setTorch(enabled);
  }

  /**
   * color is 0xAARRGGBB. The default threshold marks ~10–15% of a sharp,
   * textured frame (measured on the Kodak set at analysis size); 0.16 lit up
   * over half of it, which hides where focus actually is.
   */
  setFocusPeaking(enabled: boolean, color = 0xffff3b6b, threshold = 0.4): void {
    this.native.setFocusPeaking(enabled, color, threshold);
  }

  async setManual(settings: ManualSettings): Promise<ManualSettings> {
    return this.native.setManual(settings);
  }

  resetManual(): void {
    this.native.resetManual();
  }

  on<E extends CameraEventName>(event: E, listener: (payload: CameraEventPayloads[E]) => void): EventSubscription {
    return this.events.addListener(event, listener as (payload: unknown) => void);
  }
}

let shared: CameraService | null = null;
export function getCameraService(): CameraService {
  shared ??= new CameraService();
  return shared;
}
