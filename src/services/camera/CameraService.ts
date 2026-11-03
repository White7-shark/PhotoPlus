import { errorCode, errorMessage, nativeModules, requireNative, type CameraSpec } from '../../native';

export type FlashMode = 'off' | 'on' | 'auto';

export interface CaptureResult {
  path: string;
  width: number;
  height: number;
  sizeBytes: number;
  capturedAt: number;
}

export class CameraError extends Error {
  constructor(message: string, readonly code: string) {
    super(message);
    this.name = 'CameraError';
  }
}

export class CameraService {
  private capturing = false;

  constructor(private readonly native: CameraSpec = requireNative(nativeModules.camera, 'PhotoPlusCamera')) {}

  get isCapturing() {
    return this.capturing;
  }

  isReady(): Promise<boolean> {
    return this.native.isSessionReady();
  }

  /** Serialises captures: a second tap while saving is ignored, not queued. */
  async capture(flash: FlashMode = 'off'): Promise<CaptureResult> {
    if (this.capturing) {
      throw new CameraError('A capture is already in progress', 'BUSY');
    }
    this.capturing = true;
    try {
      return (await this.native.capturePhoto({ flash })) as unknown as CaptureResult;
    } catch (error) {
      throw new CameraError(errorMessage(error), errorCode(error) ?? 'CAPTURE_FAILED');
    } finally {
      this.capturing = false;
    }
  }

  setTorch(enabled: boolean): Promise<boolean> {
    return this.native.setTorch(enabled);
  }
}

let shared: CameraService | null = null;
export function getCameraService(): CameraService {
  shared ??= new CameraService();
  return shared;
}
