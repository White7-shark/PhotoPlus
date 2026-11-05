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
} from './types';
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

  /** Vibe Match on a saved photo. */
  applyLook(
    request: { inputPath: string; outputPath: string; look: number[]; strength?: number },
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
