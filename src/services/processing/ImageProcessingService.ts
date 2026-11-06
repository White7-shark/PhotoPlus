import { DeviceEventEmitter, type EventSubscription } from 'react-native';

import { errorCode, errorMessage, nativeModules, NativeEvents, requireNative, type EngineSpec } from '../../native';
import { createId } from '../database/ids';
import { createLogger } from '../logging/logger';
import type {
  EngineInfo,
  EngineMemoryStats,
  EnhanceRequest,
  EnhanceResult,
  JobState,
  LookResult,
  ProcessRequest,
  ProcessResult,
  QualityReport,
  SpotlightRequest,
  SpotlightResult,
  TransformRequest,
  TransformResult,
} from './types';
import type { EditDocument } from '../edit/document';
import type { FaceGeometry } from '../edit/placement';
import type { WatermarkSpec } from '../edit/watermark';
import { isLensReport, type LensReport } from './lens';

export interface RenderEditRequest {
  inputPath: string;
  outputPath: string;
  doc: EditDocument;
  /** Show the watermark as an export would draw it. */
  watermark?: WatermarkSpec | null;
  preview?: boolean;
  maxDimension?: number;
  /** Props shown live on top while being moved (not drawn into the preview). */
  skipProps?: number[];
  skipFrame?: boolean;
  /** Saving on the free plan: the copy carries the mark; this keeps it clean. */
  cleanOutputPath?: string;
}

export interface RenderEditResult {
  outputPath: string;
  cleanPath: string | null;
  width: number;
  height: number;
  sizeBytes: number;
  /** Where the photo sits in the result (x, y, w, h): all of it unless framed. */
  photoRect: [number, number, number, number];
  portrait: { people: number; spots: number; skin: number; subject: number } | null;
}

export interface ExportRequest {
  inputPath: string;
  doc?: EditDocument | null;
  watermark: WatermarkSpec;
  format?: 'jpeg' | 'png' | 'webp';
  quality?: number;
  destination: 'share' | 'gallery';
}

export interface ExportResult {
  outputPath: string;
  width: number;
  height: number;
  sizeBytes: number;
  galleryUri: string | null;
  watermark: { applied: boolean; enforced: boolean; plan: 'free' | 'premium' };
}
import { validateRequest } from './validate';

const log = createLogger('processing');

export class ProcessingError extends Error {
  constructor(
    message: string,
    readonly code: string,
  ) {
    super(message);
    this.name = 'ProcessingError';
  }
  get cancelled() {
    return this.code === 'CANCELLED';
  }
}

export interface ProcessOptions {
  jobId?: string;
  signal?: AbortSignal;
  onState?: (state: JobState) => void;
}

interface EventSource {
  addListener(
    event: string,
    listener: (payload: { jobId: string; state: JobState }) => void,
  ): EventSubscription | { remove(): void };
}

/**
 * JS façade over the native engine. Every job has an id so it can be
 * cancelled from anywhere (navigating away, app backgrounded, user tap).
 */
export class ImageProcessingService {
  private readonly active = new Set<string>();

  constructor(
    private readonly engine: EngineSpec = requireNative(nativeModules.engine, 'PhotoPlusEngine'),
    private readonly events: EventSource = DeviceEventEmitter,
  ) {}

  get activeJobIds(): string[] {
    return [...this.active];
  }

  async info(): Promise<EngineInfo> {
    return (await this.engine.getEngineInfo()) as unknown as EngineInfo;
  }

  async memoryStats(): Promise<EngineMemoryStats> {
    return (await this.engine.getMemoryStats()) as unknown as EngineMemoryStats;
  }

  async profile(): Promise<Record<string, { calls: number; totalMs: number; avgMs: number; maxMs: number }>> {
    try {
      return JSON.parse(await this.engine.getProfileReport());
    } catch {
      return {};
    }
  }

  resetProfiler() {
    this.engine.resetProfiler();
  }

  /** Starts a fresh peak-memory window (e.g. before a measured run). */
  resetMemoryPeak() {
    this.engine.resetMemoryPeak();
  }

  generateTestImage(width: number, height: number): Promise<string> {
    return this.engine.generateTestImage(width, height);
  }

  async process(request: ProcessRequest, options: ProcessOptions = {}): Promise<ProcessResult> {
    const invalid = validateRequest(request);
    if (invalid) {
      throw new ProcessingError(invalid, 'INVALID_ARGUMENT');
    }
    if (options.signal?.aborted) {
      throw new ProcessingError('Processing was cancelled', 'CANCELLED');
    }
    const jobId = options.jobId ?? createId('job');
    const subscription = options.onState
      ? this.events.addListener(NativeEvents.jobState, payload => {
          if (payload.jobId === jobId) {
            options.onState?.(payload.state);
          }
        })
      : null;
    const onAbort = () => this.cancel(jobId);
    options.signal?.addEventListener('abort', onAbort);
    this.active.add(jobId);
    try {
      const result = await this.engine.process(jobId, request);
      return result as unknown as ProcessResult;
    } catch (error) {
      const code = errorCode(error) ?? 'INTERNAL';
      if (code !== 'CANCELLED') {
        log.warn(`Job ${jobId} failed (${code})`, errorMessage(error));
      }
      throw new ProcessingError(errorMessage(error), code);
    } finally {
      this.active.delete(jobId);
      options.signal?.removeEventListener('abort', onAbort);
      subscription?.remove();
    }
  }

  /** Restores an existing photo (denoise, deblur, tone, optional 2x). Cancellable like process(). */
  enhance(request: EnhanceRequest, options: Omit<ProcessOptions, 'onState'> = {}): Promise<EnhanceResult> {
    return this.runJob('enh', request, options, jobId => this.engine.enhance(jobId, request)) as Promise<EnhanceResult>;
  }

  /** Spotlight on a saved photo. Cancellable like process(). */
  spotlight(request: SpotlightRequest, options: Omit<ProcessOptions, 'onState'> = {}): Promise<SpotlightResult> {
    return this.runJob('spot', request, options, jobId =>
      this.engine.portrait(jobId, request),
    ) as Promise<SpotlightResult>;
  }

  /** Transform on a saved photo (its effects are re-applied). Cancellable like process(). */
  transform(request: TransformRequest, options: Omit<ProcessOptions, 'onState'> = {}): Promise<TransformResult> {
    return this.runJob('tf', request, options, jobId =>
      this.engine.transform(jobId, request),
    ) as Promise<TransformResult>;
  }

  /** Editor: renders a photo with its edits (a preview, or a copy for the library). */
  renderEdit(request: RenderEditRequest, options: Omit<ProcessOptions, 'onState'> = {}): Promise<RenderEditResult> {
    return this.runJob('edit', request, options, jobId =>
      this.engine.renderEdit(jobId, request),
    ) as Promise<RenderEditResult>;
  }

  /**
   * Prepares a photo to leave the app (share sheet or gallery). The native
   * side applies the watermark for the plan it holds, whatever is asked.
   */
  async exportImage(request: ExportRequest, options: Omit<ProcessOptions, 'onState'> = {}): Promise<ExportResult> {
    const jobId = options.jobId ?? createId('export');
    try {
      return (await this.engine.exportImage(jobId, request)) as unknown as ExportResult;
    } catch (error) {
      throw new ProcessingError(errorMessage(error), errorCode(error) ?? 'INTERNAL');
    }
  }

  /** Faces in the photo as edited (for fitting props). */
  async detectFaces(inputPath: string, doc: EditDocument | null): Promise<FaceGeometry[]> {
    try {
      const r = (await this.engine.detectFaces({ inputPath, doc })) as unknown as { faces?: FaceGeometry[] };
      return Array.isArray(r.faces) ? r.faces : [];
    } catch {
      return [];
    }
  }

  /** Every frame design around a photo, small (paths by frame id); {} if unavailable. */
  async frameThumbnails(inputPath: string, size = 240): Promise<Record<string, string>> {
    try {
      const r = (await this.engine.frameThumbnails({ inputPath, size })) as unknown as {
        thumbnails?: Record<string, string>;
      };
      return r.thumbnails ?? {};
    } catch {
      return {};
    }
  }

  /** Premium: cuts the tapped object out of a photo and saves it as a prop. */
  async createCustomProp(
    inputPath: string,
    points: readonly { x: number; y: number }[],
  ): Promise<{ id: string; path: string; width: number; height: number }> {
    try {
      return (await this.engine.createCustomProp(createId('prop'), {
        inputPath,
        points: points.flatMap(p => [p.x, p.y]),
      })) as unknown as { id: string; path: string; width: number; height: number };
    } catch (error) {
      throw new ProcessingError(errorMessage(error), errorCode(error) ?? 'INTERNAL');
    }
  }

  async deleteCustomProp(id: string): Promise<boolean> {
    try {
      return await this.engine.deleteCustomProp(id);
    } catch {
      return false;
    }
  }

  /** The plan held natively (decides watermarks on everything that leaves the app). */
  async plan(): Promise<'free' | 'premium'> {
    try {
      return (await this.engine.getPlan()) === 'premium' ? 'premium' : 'free';
    } catch {
      return 'free';
    }
  }

  /** What the camera's lens left in a photo. */
  async analyzeLens(path: string): Promise<LensReport> {
    let r: unknown;
    try {
      r = await this.engine.analyzeLens(path);
    } catch (error) {
      throw new ProcessingError(errorMessage(error), errorCode(error) ?? 'INTERNAL');
    }
    if (!isLensReport(r)) {
      throw new ProcessingError('Unexpected lens report', 'INTERNAL');
    }
    return r;
  }

  /** Vibe Match on a saved photo. */
  applyLook(
    request: { inputPath: string; outputPath: string; look: number[]; strength?: number; watermark?: WatermarkSpec },
    options: Omit<ProcessOptions, 'onState'> = {},
  ): Promise<LookResult> {
    return this.runJob('look', request, options, jobId => this.engine.applyLook(jobId, request)) as Promise<LookResult>;
  }

  /** Measures a reference photo's look. */
  async analyzeLook(path: string): Promise<number[]> {
    try {
      const r = (await this.engine.analyzeLook(path)) as unknown as { look: number[] };
      return r.look;
    } catch (error) {
      throw new ProcessingError(errorMessage(error), errorCode(error) ?? 'INTERNAL');
    }
  }

  /** Shared job plumbing for edits of saved photos: ids, cancel, errors. */
  private async runJob(
    prefix: string,
    io: { inputPath: string; outputPath: string },
    options: Omit<ProcessOptions, 'onState'>,
    start: (jobId: string) => Promise<Object>,
  ): Promise<unknown> {
    // Edits always go to a new file: the original is never overwritten.
    if (!io.inputPath || !io.outputPath || io.inputPath === io.outputPath) {
      throw new ProcessingError('Edits need distinct input and output paths', 'INVALID_ARGUMENT');
    }
    if (options.signal?.aborted) {
      throw new ProcessingError('Processing was cancelled', 'CANCELLED');
    }
    const jobId = options.jobId ?? createId(prefix);
    const onAbort = () => this.cancel(jobId);
    options.signal?.addEventListener('abort', onAbort);
    this.active.add(jobId);
    try {
      return await start(jobId);
    } catch (error) {
      throw new ProcessingError(errorMessage(error), errorCode(error) ?? 'INTERNAL');
    } finally {
      this.active.delete(jobId);
      options.signal?.removeEventListener('abort', onAbort);
    }
  }

  /** EXIF + display size of a stored photo. */
  async metadata(path: string): Promise<Record<string, unknown>> {
    return (await this.engine.readImageMetadata(path)) as unknown as Record<string, unknown>;
  }

  async analyzeQuality(path: string): Promise<QualityReport> {
    try {
      return (await this.engine.analyzeQuality(path)) as unknown as QualityReport;
    } catch (error) {
      throw new ProcessingError(errorMessage(error), errorCode(error) ?? 'INTERNAL');
    }
  }

  cancel(jobId: string) {
    this.engine.cancel(jobId);
  }

  cancelAll() {
    this.engine.cancelAll();
  }
}

let shared: ImageProcessingService | null = null;
export function getProcessingService(): ImageProcessingService {
  shared ??= new ImageProcessingService();
  return shared;
}
