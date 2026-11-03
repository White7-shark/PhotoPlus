import {
  cameraFeatureSupport,
  classifyTier,
  detectCapabilities,
  modelCompatibility,
  parseCapabilities,
  preferredDelegate,
  primaryCamera,
  storageHealth,
} from '../src/services/capabilities';
import { rawCapabilities } from './helpers/fixtures';

describe('parseCapabilities', () => {
  it('normalises a complete native payload', () => {
    const caps = parseCapabilities(rawCapabilities());
    expect(caps.missingSections).toEqual([]);
    expect(caps.os.sdkInt).toBe(34);
    expect(caps.cameras).toHaveLength(2);
    expect(caps.cameras[0].ranges.iso).toEqual([50, 6400]);
    expect(caps.cameras[1].ranges.iso).toBeNull();
  });

  it('survives garbage and reports missing sections', () => {
    const caps = parseCapabilities({ os: 'nope', cameras: [{ id: 3 }, null, { id: 'x', lensFacing: 'sideways', ranges: { iso: [1] } }] });
    expect(caps.missingSections).toEqual(expect.arrayContaining(['os', 'memory', 'storage', 'acceleration']));
    expect(caps.cameras).toHaveLength(1);
    expect(caps.cameras[0].lensFacing).toBe('unknown');
    expect(caps.cameras[0].ranges.iso).toBeNull();
    expect(parseCapabilities(undefined).cameras).toEqual([]);
  });
});

describe('device tiers', () => {
  const tierFor = (o: Parameters<typeof rawCapabilities>[0]) => classifyTier(parseCapabilities(rawCapabilities(o)));

  it('follows the 3 / 4 / 6 GB product targets using marketed sizes', () => {
    expect(tierFor({ ramGb: 2 })).toBe('unsupported');
    expect(tierFor({ ramGb: 3 })).toBe('entry');
    expect(tierFor({ ramGb: 4 })).toBe('standard');
    expect(tierFor({ ramGb: 6 })).toBe('performance');
    expect(tierFor({ ramGb: 12 })).toBe('performance');
  });

  it('treats low-RAM devices and pre-Android-10 as unsupported', () => {
    expect(tierFor({ ramGb: 8, lowRam: true })).toBe('unsupported');
    expect(tierFor({ sdkInt: 28 })).toBe('unsupported');
  });

  it('never blocks when RAM is unknown', () => {
    const caps = parseCapabilities({ ...rawCapabilities(), memory: { totalBytes: 0 } });
    expect(classifyTier(caps)).toBe('entry');
  });
});

describe('camera feature support', () => {
  it('exposes manual controls only when the main sensor supports them', () => {
    const s = cameraFeatureSupport(parseCapabilities(rawCapabilities()));
    expect(s).toMatchObject({ manualExposure: true, manualIso: true, rawCapture: true, frontCamera: true, zoom: true, flash: true });
  });

  it('ignores manual-sensor claims on LEGACY hardware', () => {
    const raw = rawCapabilities();
    raw.cameras[0].hardwareLevel = 'legacy';
    expect(cameraFeatureSupport(parseCapabilities(raw)).manualExposure).toBe(false);
  });

  it('prefers the most capable back camera', () => {
    const raw = rawCapabilities();
    raw.cameras.push({ ...raw.cameras[0], id: '2', hardwareLevel: 'limited', sensorMegapixels: 8 });
    expect(primaryCamera(parseCapabilities(raw), 'back')?.id).toBe('0');
  });

  it('handles devices without cameras', () => {
    const s = cameraFeatureSupport(parseCapabilities({ ...rawCapabilities(), cameras: [] }));
    expect(s.manualExposure).toBe(false);
    expect(s.frontCamera).toBe(false);
  });
});

describe('model compatibility', () => {
  it('enables everything on a performance device with GPU', () => {
    const results = modelCompatibility(parseCapabilities(rawCapabilities({ ramGb: 8 })));
    expect(results.every(r => r.compatible)).toBe(true);
    expect(results[0].delegate).toBe('gpu');
  });

  it('limits heavy models on entry devices and explains why', () => {
    const results = modelCompatibility(parseCapabilities(rawCapabilities({ ramGb: 3 })));
    const garment = results.find(r => r.model.id === 'garment-transfer');
    expect(garment?.compatible).toBe(false);
    expect(garment?.reason).toMatch(/Performance tier/);
    expect(results.find(r => r.model.id === 'pose-lite')?.compatible).toBe(true);
  });

  it('requires GPU for GPU-only models', () => {
    const caps = parseCapabilities(rawCapabilities({ ramGb: 8, glEs31: false }));
    expect(preferredDelegate(caps)).toBe('nnapi');
    expect(modelCompatibility(caps).find(r => r.model.id === 'relight')?.reason).toMatch(/GPU/);
  });

  it('accounts for free storage', () => {
    const caps = parseCapabilities(rawCapabilities({ ramGb: 8, freeGb: 0.2 }));
    expect(modelCompatibility(caps).find(r => r.model.id === 'garment-transfer')?.reason).toMatch(/storage/);
  });

  it('grades storage health', () => {
    expect(storageHealth(100 * 1024 * 1024).level).toBe('critical');
    expect(storageHealth(1024 * 1024 * 1024).level).toBe('low');
    expect(storageHealth(10 * 1024 * 1024 * 1024).level).toBe('ok');
  });
});

describe('detectCapabilities', () => {
  it('parses the native result', async () => {
    const caps = await detectCapabilities({ getCapabilities: async () => rawCapabilities() });
    expect(caps.os.model).toBe('Camon 30');
  });

  it('times out instead of hanging startup', async () => {
    await expect(detectCapabilities({ getCapabilities: () => new Promise(() => undefined) }, 20)).rejects.toThrow(/timed out/);
  });
});
