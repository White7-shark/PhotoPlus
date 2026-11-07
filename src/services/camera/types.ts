import type { WatermarkSpec } from '../edit/watermark';
/**
 * Shapes exchanged with the Kotlin camera layer (com.photoplus.camera).
 * Native maps are validated by `parseCameraCapabilities` before use, so a
 * malformed or older native build degrades to "feature hidden" rather than
 * crashing the camera screen.
 */

export type LensFacing = 'back' | 'front';
export type FlashMode = 'off' | 'on' | 'auto';
export type AspectRatio = '4:3' | '16:9' | '1:1';
export type ResolutionId = 'max' | '12mp' | '8mp' | '5mp' | '2mp';
export type CaptureModeId = 'standard' | 'raw' | 'enhance' | 'night' | 'superres' | 'spotlight';
export type WhiteBalanceId = 'auto' | 'incandescent' | 'fluorescent' | 'daylight' | 'cloudy' | 'shade' | 'kelvin';
export type DeviceClass = 'entry' | 'standard' | 'performance';

export interface ResolutionOption {
  id: ResolutionId;
  width: number;
  height: number;
  megapixels: number;
}

export interface CapturePlan {
  frames: number;
  workingDimension: number;
  upscale: number;
}

export interface ManualCapabilities {
  exposure: boolean;
  iso: [number, number] | null;
  exposureTimeNs: [number, number] | null;
  isoStops: number[];
  shutterStops: number[];
  focus: boolean;
  minFocusDiopters: number | null;
  whiteBalanceModes: WhiteBalanceId[];
  kelvin: boolean;
  aeLock: boolean;
  awbLock: boolean;
}

export interface CameraCapabilities {
  lensFacing: LensFacing;
  hardwareLevel: string;
  zoom: { min: number; max: number };
  exposureCompensation: { minIndex: number; maxIndex: number; step: number } | null;
  flash: boolean;
  torch: boolean;
  tapToFocus: boolean;
  manual: ManualCapabilities;
  raw: boolean;
  aspectRatios: AspectRatio[];
  resolutions: Partial<Record<AspectRatio, ResolutionOption[]>>;
  captureModes: CaptureModeId[];
  focusPeaking: boolean;
  captureSize: { width: number; height: number } | null;
  tier: DeviceClass;
  capturePlans: Partial<Record<CaptureModeId, CapturePlan>>;
}

export interface ManualSettings {
  iso?: number | null;
  exposureTimeNs?: number | null;
  focusDiopters?: number | null;
  whiteBalance?: WhiteBalanceId;
  kelvin?: number | null;
  aeLock?: boolean;
  awbLock?: boolean;
}

export interface SessionConfig {
  aspectRatio?: AspectRatio;
  resolution?: ResolutionId;
  raw?: boolean;
  /** Glide: the session records video (preview + video stream) instead of photos. */
  video?: boolean;
  /** Beauty: the preview shows the beauty recipe live (GPU); photos get it either way. */
  beauty?: boolean;
}

/** Glide recording options (the clip ends at maxMs, at most 7 s). */
export interface VideoOptions {
  audio: boolean;
  maxMs: number;
}

/** A finished Glide recording (the untouched clip in app storage). */
export interface VideoRecording {
  path: string;
  durationMs: number;
}

/** Spotlight settings, 0..1. */
export interface SpotlightSettings {
  blur: number;
  retouch: number;
  brighten: number;
  pop: number;
}

export interface CaptureOptions {
  mode: CaptureModeId;
  flash: FlashMode;
  mirrorFront?: boolean;
  denoise?: number;
  sharpen?: number;
  detail?: number;
  vibrance?: number;
  autoTone?: boolean;
  /** Spotlight: what should stand out, as taps on the preview (x0, y0, x1, y1, … in 0..1). */
  points?: number[];
  blur?: number;
  retouch?: number;
  brighten?: number;
  pop?: number;
  /** Camera beauty outside Spotlight (Glow and its face touches). */
  beauty?: { retouch: number; brighten: number; eyes: number; underEye: number; shine: number; faceLight: number };
  /** Beauty mode (and Portrait mode's controls in Spotlight): the resolved recipe. Supersedes `beauty`. */
  beautyRecipe?: import('../beauty/recipe').BeautyRecipe;
  /** The beauty preset's id, kept with the photo. */
  beautyPreset?: string;
  /** Vibe Match look numbers and how strongly to apply them. */
  look?: number[];
  lookStrength?: number;
  /** The watermark free-plan photos carry (WatermarkSpec). */
  watermark?: WatermarkSpec;
  /** Props worn in the preview, composited into the photo. */
  props?: CameraPropRequest[];
}

/**
 * A prop as placed on the preview (preview fractions, rotation clockwise),
 * with how it sits on a face so the photo can fit it to the face again.
 */
export interface CameraPropRequest {
  id: string;
  cx: number;
  cy: number;
  width: number;
  rotation: number;
  flip: boolean;
  anchor: 'head' | 'eyes' | 'neck' | 'free';
  ax: number;
  ay: number;
  fit: number;
  drop: number;
  aspect: number;
  /** Following a face: the person's shift (face widths, along the face), size and turn on top of the fit. */
  tracked: boolean;
  du?: number;
  dv?: number;
  scale?: number;
  turn?: number;
}

export interface SpotlightReport {
  people: number;
  spots: number;
  skin: number;
  subject: number;
}

export interface EnhanceReport {
  framesInput?: number;
  framesUsed?: number;
  referenceIndex?: number;
  noiseBefore: number;
  noiseAfter: number;
  blurBefore: number;
  deblurSigma: number;
  upscale: number;
}

export interface CaptureResult {
  captureId: string;
  path: string;
  width: number;
  height: number;
  sizeBytes: number;
  capturedAt: number;
  mode: CaptureModeId;
  rawPath?: string | null;
  /** The photo without the Photo Plus mark (free plan). */
  cleanPath?: string | null;
  originalPath?: string;
  originalWidth?: number;
  originalHeight?: number;
  report?: EnhanceReport | null;
  portrait?: SpotlightReport | null;
  /** The Spotlight settings used, with taps in the upright photo (re-applied by Transform). */
  spotlight?: { points: number[]; blur: number; retouch: number; brighten: number; pop: number } | null;
  /** Camera beauty used outside Spotlight (re-applied by Transform). */
  beauty?: {
    retouch: number;
    brighten: number;
    eyes: number;
    underEye: number;
    shine: number;
    faceLight: number;
  } | null;
  look?: boolean;
  timings?: { captureMs?: number; processMs?: number; totalMs: number };
}

// ---- events (DeviceEventEmitter)

export const CameraEvents = {
  state: 'PhotoPlusCameraState',
  zoom: 'PhotoPlusCameraZoom',
  torch: 'PhotoPlusCameraTorch',
  orientation: 'PhotoPlusCameraOrientation',
  focus: 'PhotoPlusCameraFocus',
  metadata: 'PhotoPlusCameraMetadata',
  peaking: 'PhotoPlusCameraPeaking',
  faces: 'PhotoPlusCameraFaces',
  shutter: 'PhotoPlusCaptureShutter',
  progress: 'PhotoPlusCaptureProgress',
  /** Glide: a recording started. */
  recording: 'PhotoPlusRecording',
  /** Beauty: whether the live preview runs on this phone (after each camera start). */
  beautyLive: 'PhotoPlusBeautyLive',
} as const;

export type CaptureStage = 'capturing' | 'processing' | 'styling' | 'saving';

export type CameraStateName = 'pendingOpen' | 'opening' | 'open' | 'closing' | 'closed' | 'recovering';

export interface CameraEventPayloads {
  [CameraEvents.state]: { state: CameraStateName; error?: string | null; critical?: boolean; attempt?: number };
  [CameraEvents.zoom]: { ratio: number; linear: number; min: number; max: number };
  [CameraEvents.torch]: { on: boolean };
  [CameraEvents.orientation]: { degrees: 0 | 90 | 180 | 270 };
  [CameraEvents.focus]: { x: number; y: number; success: boolean };
  [CameraEvents.metadata]: {
    iso: number | null;
    exposureTimeNs: number | null;
    focusDiopters: number | null;
    aperture: number | null;
    focusLocked: boolean;
  };
  [CameraEvents.peaking]: { sharpness: number };
  /** Faces in the preview (preview fractions), while props are on. */
  [CameraEvents.faces]: { faces: import('../edit/placement').FaceGeometry[] };
  [CameraEvents.shutter]: { captureId: string };
  [CameraEvents.progress]: { captureId: string; stage: CaptureStage; progress: number };
  [CameraEvents.recording]: { recording: boolean; maxMs: number };
  [CameraEvents.beautyLive]: { requested: boolean; live: boolean; reason: string | null };
}

export type CameraEventName = keyof CameraEventPayloads;
