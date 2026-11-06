import type { EngineSpec } from '../src/native';
import { ImageProcessingService, ProcessingError } from '../src/services/processing/ImageProcessingService';
import { runSelfTest, SELF_TEST_PIPELINE } from '../src/services/processing/selfTest';
import { validateRequest } from '../src/services/processing/validate';

type Listener = (p: { jobId: string; state: 'queued' | 'running' | 'succeeded' | 'failed' | 'cancelled' }) => void;

function fakeEngine(opts: { delayMs?: number; failWith?: string } = {}) {
  const listeners = new Set<Listener>();
  const pending = new Map<string, (e: Error) => void>();
  const timers = new Map<string, ReturnType<typeof setTimeout>>();
  let current = 0;
  let peak = 0;
  const engine = {
    getEngineInfo: jest.fn(async () => ({ available: true, version: '0.1.0' })),
    process: jest.fn(
      (jobId: string, _request?: object) =>
        new Promise((resolve, reject) => {
          listeners.forEach(l => l({ jobId, state: 'running' }));
          current += 1000;
          peak = Math.max(peak, current);
          if (opts.failWith) {
            current -= 1000;
            reject(Object.assign(new Error('boom'), { code: opts.failWith }));
            return;
          }
          pending.set(jobId, reject);
          const timer = setTimeout(() => {
            if (!pending.has(jobId)) return;
            pending.delete(jobId);
            current -= 1000;
            listeners.forEach(l => l({ jobId, state: 'succeeded' }));
            resolve({
              outputPath: '/cache/out.jpg',
              width: 10,
              height: 10,
              sourceWidth: 10,
              sourceHeight: 10,
              format: 'jpeg',
              sizeBytes: 100,
              timings: { decodeMs: 1, processMs: 4, totalMs: 6 },
            });
          }, opts.delayMs ?? 0);
          timers.set(jobId, timer);
        }),
    ),
    cancel: jest.fn((jobId: string) => {
      const reject = pending.get(jobId);
      if (reject) {
        clearTimeout(timers.get(jobId));
        pending.delete(jobId);
        current -= 1000;
        reject(Object.assign(new Error('Processing was cancelled'), { code: 'CANCELLED' }));
      }
    }),
    cancelAll: jest.fn(),
    getMemoryStats: jest.fn(async () => ({
      currentBytes: current,
      peakBytes: peak,
      budgetBytes: 1e9,
      liveAllocations: 0,
      totalAllocations: 0,
      failedAllocations: 0,
      activeJobs: pending.size,
    })),
    getProfileReport: jest.fn(async () =>
      JSON.stringify({
        'op.tone': { calls: 3, totalMs: 6, avgMs: 2, maxMs: 3 },
        'pipeline.total': { calls: 3, totalMs: 30, avgMs: 10, maxMs: 12 },
        other: { calls: 1, totalMs: 1, avgMs: 1, maxMs: 1 },
      }),
    ),
    resetProfiler: jest.fn(),
    resetMemoryPeak: jest.fn(() => {
      peak = current;
    }),
    generateTestImage: jest.fn(async () => '/cache/test.jpg'),
    readImageMetadata: jest.fn(),
    addListener: jest.fn(),
    removeListeners: jest.fn(),
  };
  const events = {
    addListener: (_: string, l: Listener) => {
      listeners.add(l);
      return { remove: () => listeners.delete(l) };
    },
  };
  return { engine: engine as unknown as EngineSpec & typeof engine, events, listenerCount: () => listeners.size };
}

const request = { inputPath: '/cache/in.jpg', outputPath: '/cache/out.jpg', operations: SELF_TEST_PIPELINE };

describe('validateRequest', () => {
  it('accepts the self-test pipeline', () => {
    expect(validateRequest(request)).toBeNull();
  });
  it.each([
    [{ ...request, inputPath: '' }, /required/],
    [{ ...request, quality: 0 }, /quality/],
    [{ ...request, operations: [{ type: 'rotate' as const, quarterTurns: 1.5 }] }, /integer/],
    [{ ...request, operations: [{ type: 'crop' as const, x: 0, y: 0, width: 0, height: 5 }] }, /crop/],
    [{ ...request, operations: [{ type: 'resize' as const, width: 20000, height: 5 }] }, /resize/],
    [{ ...request, operations: [{ type: 'tone' as const, exposure: Infinity }] }, /finite/],
    [{ ...request, operations: Array(65).fill({ type: 'grayscale' }) }, /Too many/],
  ])('rejects invalid request %#', (req, message) => {
    expect(validateRequest(req)).toMatch(message);
  });
});

describe('ImageProcessingService', () => {
  it('runs a job, reports state changes and cleans up listeners', async () => {
    const { engine, events, listenerCount } = fakeEngine();
    const svc = new ImageProcessingService(engine, events);
    const states: string[] = [];
    const result = await svc.process(request, { jobId: 'j1', onState: s => states.push(s) });
    expect(result.outputPath).toBe('/cache/out.jpg');
    expect(states).toEqual(['running', 'succeeded']);
    expect(listenerCount()).toBe(0);
    expect(svc.activeJobIds).toEqual([]);
  });

  it('rejects invalid requests before reaching native code', async () => {
    const { engine, events } = fakeEngine();
    const svc = new ImageProcessingService(engine, events);
    await expect(svc.process({ ...request, quality: 500 })).rejects.toMatchObject({ code: 'INVALID_ARGUMENT' });
    expect(engine.process).not.toHaveBeenCalled();
  });

  it('cancels a running job through an AbortSignal', async () => {
    const { engine, events } = fakeEngine({ delayMs: 1000 });
    const svc = new ImageProcessingService(engine, events);
    const controller = new AbortController();
    const promise = svc.process(request, { jobId: 'j2', signal: controller.signal });
    controller.abort();
    const error: ProcessingError = await promise.catch(e => e);
    expect(error).toBeInstanceOf(ProcessingError);
    expect(error.cancelled).toBe(true);
    expect(engine.cancel).toHaveBeenCalledWith('j2');
    expect(svc.activeJobIds).toEqual([]);
  });

  it('does not start when the signal is already aborted', async () => {
    const { engine, events } = fakeEngine();
    const controller = new AbortController();
    controller.abort();
    await expect(
      new ImageProcessingService(engine, events).process(request, { signal: controller.signal }),
    ).rejects.toMatchObject({ code: 'CANCELLED' });
    expect(engine.process).not.toHaveBeenCalled();
  });

  it('maps native error codes', async () => {
    const { engine, events } = fakeEngine({ failWith: 'BUDGET_EXCEEDED' });
    await expect(new ImageProcessingService(engine, events).process(request)).rejects.toMatchObject({
      code: 'BUDGET_EXCEEDED',
    });
  });

  it('tolerates a malformed profile report', async () => {
    const { engine, events } = fakeEngine();
    engine.getProfileReport.mockResolvedValueOnce('not json');
    expect(await new ImageProcessingService(engine, events).profile()).toEqual({});
  });
});

describe('runSelfTest', () => {
  it('runs N iterations, reports timings, memory baseline and records history', async () => {
    const { engine, events } = fakeEngine();
    const svc = new ImageProcessingService(engine, events);
    const record = jest.fn(async () => ({}));
    const progress: number[] = [];
    const report = await runSelfTest({
      service: svc,
      outputDir: '/cache/processing/',
      iterations: 4,
      onProgress: p => progress.push(p),
      history: { record } as never,
    });
    expect(engine.process).toHaveBeenCalledTimes(4);
    expect(engine.process.mock.calls[0][1]).toMatchObject({ outputPath: '/cache/processing/selftest.jpg' });
    expect(report.avgProcessMs).toBe(4);
    expect(report.memoryReturnedToBaseline).toBe(true);
    expect(progress).toEqual([0.25, 0.5, 0.75, 1]);
    expect(report.operations.map(o => o.name)).toEqual(['full pipeline', 'tone']);
    expect(record).toHaveBeenCalledTimes(1);
    expect(engine.resetMemoryPeak).toHaveBeenCalledTimes(1);
  });

  it('stops on cancellation and records it', async () => {
    const { engine, events } = fakeEngine({ delayMs: 50 });
    const svc = new ImageProcessingService(engine, events);
    const controller = new AbortController();
    const record = jest.fn(async () => ({}));
    const run = runSelfTest({
      service: svc,
      outputDir: '/c',
      iterations: 5,
      signal: controller.signal,
      history: { record } as never,
    });
    setTimeout(() => controller.abort(), 5);
    await expect(run).rejects.toMatchObject({ code: 'CANCELLED' });
    expect(record).toHaveBeenCalledWith(expect.objectContaining({ status: 'cancelled' }));
  });
});

describe('job progress and perspective (engine phase 4)', () => {
  type Payload = { jobId: string; state?: string; progress?: number };
  function namedEvents() {
    const byName = new Map<string, Set<(p: Payload) => void>>();
    return {
      addListener: (name: string, l: (p: Payload) => void) => {
        if (!byName.has(name)) byName.set(name, new Set());
        byName.get(name)!.add(l);
        return { remove: () => byName.get(name)!.delete(l) };
      },
      emit: (name: string, p: Payload) => byName.get(name)?.forEach(l => l(p)),
      count: () => [...byName.values()].reduce((n, s) => n + s.size, 0),
    };
  }

  it('reports progress for its own job only, clamped, and stops listening afterwards', async () => {
    const { engine } = fakeEngine();
    const events = namedEvents();
    const renderEdit = jest.fn(async (jobId: string) => {
      events.emit('PhotoPlusJobProgress', { jobId, progress: 0.4 });
      events.emit('PhotoPlusJobProgress', { jobId: 'someone-else', progress: 0.9 });
      events.emit('PhotoPlusJobProgress', { jobId, progress: Number.NaN });
      events.emit('PhotoPlusJobProgress', { jobId, progress: 1.7 });
      return { outputPath: '/cache/o.jpg' };
    });
    const svc = new ImageProcessingService(Object.assign(engine, { renderEdit }), events as never);
    const seen: number[] = [];
    await svc.renderEdit({ inputPath: '/cache/a.jpg', outputPath: '/cache/b.jpg', doc: null } as never, {
      jobId: 'e1',
      onProgress: p => seen.push(p),
    });
    expect(seen).toEqual([0.4, 1]);
    expect(events.count()).toBe(0);
  });

  it('process() reports progress alongside its states', async () => {
    const { engine } = fakeEngine();
    const events = namedEvents();
    engine.process.mockImplementationOnce(async (jobId: string) => {
      events.emit('PhotoPlusJobState', { jobId, state: 'running' });
      events.emit('PhotoPlusJobProgress', { jobId, progress: 0.5 });
      return { outputPath: '/cache/out.jpg' };
    });
    const svc = new ImageProcessingService(engine, events as never);
    const log: string[] = [];
    await svc.process(request, { jobId: 'p1', onState: s => log.push(s), onProgress: p => log.push(`p${p}`) });
    expect(log).toEqual(['running', 'p0.5']);
    expect(events.count()).toBe(0);
  });

  it('detectPerspective returns a safe value whatever the native side does', async () => {
    const { engine, events } = fakeEngine();
    const detect = jest.fn();
    const svc = new ImageProcessingService(Object.assign(engine, { detectPerspective: detect }), events);
    detect.mockResolvedValueOnce({ vertical: 0.3 });
    expect(await svc.detectPerspective('/cache/a.jpg', null)).toBe(0.3);
    detect.mockResolvedValueOnce({ vertical: 4 });
    expect(await svc.detectPerspective('/cache/a.jpg', null)).toBe(1);
    detect.mockResolvedValueOnce({ vertical: 'x' });
    expect(await svc.detectPerspective('/cache/a.jpg', null)).toBe(0);
    detect.mockRejectedValueOnce(new Error('no'));
    expect(await svc.detectPerspective('/cache/a.jpg', null)).toBe(0);
  });
});
