import { joinPath } from '../storage/StorageService';
import type { HistoryRepository } from '../database/repositories/HistoryRepository';
import type { ImageProcessingService } from './ImageProcessingService';
import { ProcessingError } from './ImageProcessingService';
import type { EditOperation } from './types';

export interface SelfTestReport {
  iterations: number;
  avgProcessMs: number;
  avgTotalMs: number;
  outputPath: string;
  memory: { baselineBytes: number; peakBytes: number; afterBytes: number; leakedBytes: number };
  memoryReturnedToBaseline: boolean;
  operations: { name: string; calls: number; avgMs: number }[];
}

export const SELF_TEST_PIPELINE: EditOperation[] = [
  { type: 'tone', exposure: 0.4, brightness: 0.05, contrast: 0.15 },
  { type: 'rotate', quarterTurns: 1 },
  { type: 'crop', x: 40, y: 40, width: 1000, height: 1400 },
  { type: 'resize', width: 800, height: 1120 },
  { type: 'sharpen', amount: 0.6 },
  { type: 'saturation', amount: 0.2 },
];

interface Options {
  service: ImageProcessingService;
  outputDir: string;
  iterations?: number;
  signal?: AbortSignal;
  onProgress?: (fraction: number) => void;
  history?: HistoryRepository;
}

export async function runSelfTest({ service, outputDir, iterations = 10, signal, onProgress, history }: Options): Promise<SelfTestReport> {
  service.resetProfiler();
  const input = await service.generateTestImage(1600, 1200);
  // Measure this run only, not whatever an earlier export peaked at.
  service.resetMemoryPeak();
  const baseline = (await service.memoryStats()).currentBytes;
  let processSum = 0;
  let totalSum = 0;
  let outputPath = '';
  for (let i = 0; i < iterations; i++) {
    if (signal?.aborted) {
      throw new ProcessingError('Processing was cancelled', 'CANCELLED');
    }
    const jobId = `selftest-${Date.now()}-${i}`;
    try {
      const result = await service.process(
        { inputPath: input, outputPath: joinPath(outputDir, 'selftest.jpg'), operations: SELF_TEST_PIPELINE, format: 'jpeg', quality: 90 },
        { jobId, signal },
      );
      processSum += result.timings.processMs;
      totalSum += result.timings.totalMs;
      outputPath = result.outputPath;
      if (i === iterations - 1) {
        await history?.record({ imageId: null, jobId, operations: SELF_TEST_PIPELINE, status: 'succeeded', errorCode: null, durationMs: result.timings.totalMs, outputPath });
      }
    } catch (error) {
      if (error instanceof ProcessingError) {
        await history?.record({ imageId: null, jobId, operations: SELF_TEST_PIPELINE, status: error.cancelled ? 'cancelled' : 'failed', errorCode: error.code, durationMs: null, outputPath: null }).catch(() => undefined);
      }
      throw error;
    }
    onProgress?.((i + 1) / iterations);
  }
  const stats = await service.memoryStats();
  const profile = await service.profile();
  return {
    iterations,
    avgProcessMs: processSum / iterations,
    avgTotalMs: totalSum / iterations,
    outputPath,
    memory: {
      baselineBytes: baseline,
      peakBytes: stats.peakBytes,
      afterBytes: stats.currentBytes,
      leakedBytes: Math.max(0, stats.currentBytes - baseline),
    },
    memoryReturnedToBaseline: stats.currentBytes <= baseline,
    operations: Object.entries(profile)
      .filter(([name]) => name.startsWith('op.') || name === 'pipeline.total')
      .map(([name, e]) => ({ name: name.replace('op.', '').replace('pipeline.total', 'full pipeline'), calls: e.calls, avgMs: e.avgMs }))
      .sort((a, b) => b.avgMs - a.avgMs),
  };
}
