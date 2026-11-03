/** Normalised device capability snapshot (mirrors CapabilityProbe.kt, schema v1). */

export type LensFacing = 'back' | 'front' | 'external' | 'unknown';
export type CameraHardwareLevel = 'legacy' | 'limited' | 'full' | 'level3' | 'external' | 'unknown';

export interface CameraControls {
  manualSensor: boolean;
  manualPostProcessing: boolean;
  raw: boolean;
  burstCapture: boolean;
  depthOutput: boolean;
  logicalMultiCamera: boolean;
  manualFocus: boolean;
  autoFocus: boolean;
  flash: boolean;
  opticalStabilization: boolean;
  exposureCompensation: boolean;
}

export interface CameraInfo {
  id: string;
  lensFacing: LensFacing;
  hardwareLevel: CameraHardwareLevel;
  sensorMegapixels: number | null;
  maxJpegSize: { width: number; height: number } | null;
  controls: CameraControls;
  ranges: {
    iso: [number, number] | null;
    exposureTimeNs: [number, number] | null;
    exposureCompensation: [number, number] | null;
    exposureCompensationStep: number | null;
    zoomRatio: [number, number] | null;
    maxDigitalZoom: number | null;
    minFocusDistanceDiopters: number | null;
  };
  focalLengthsMm: number[];
  apertures: number[];
  outputFormats: string[];
}

export interface DeviceCapabilities {
  schemaVersion: number;
  collectedAt: number;
  os: {
    sdkInt: number;
    release: string;
    manufacturer: string;
    model: string;
    supportedAbis: string[];
    is64Bit: boolean;
  };
  memory: {
    totalBytes: number;
    availableBytes: number;
    lowMemory: boolean;
    isLowRamDevice: boolean;
  };
  cpu: { cores: number; socModel: string | null };
  storage: { totalBytes: number; availableBytes: number };
  display: { widthPx: number; heightPx: number; density: number };
  cameras: CameraInfo[];
  imageFormats: { decode: string[]; encode: string[]; hevcEncoder: boolean; ultraHdr: boolean };
  acceleration: {
    glEsVersion: string;
    glEs31: boolean;
    vulkan: boolean;
    vulkanVersion: string | null;
    nnapi: boolean;
    nnapiDeprecated: boolean;
    renderEffectBlur: boolean;
  };
  /** Sections whose native probe failed; their values are defaults. */
  missingSections: string[];
}
