import { DeviceEventEmitter, type EmitterSubscription } from 'react-native';

import { errorCode, errorMessage, nativeModules, NativeEvents, requireNative, type EngineSpec } from '../../native';
import { createId } from '../database/ids';
import { createLogger } from '../logging/logger';
import type { EngineInfo, EngineMemoryStats, JobState, ProcessRequest, ProcessResult } from './types';
import { validateRequest } from './validate';

const log = createLogger('processing');

export class ProcessingError extends Error {
  constructor(message: string, readonly code: string) {
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
  addListener(event: string, listener: (payload: { jobId: string; state: JobState }) => void): EmitterSubscription | { remove(): void };
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
      const result = await this.engine.process(jobId, request as unknown as Object);
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
