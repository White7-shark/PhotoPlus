import type { CameraControls, CameraInfo, DeviceCapabilities } from './types';

/**
 * Validates the untyped native payload. Any section that is missing or
 * malformed falls back to conservative defaults and is reported in
 * `missingSections`, so downstream tiering never trusts garbage.
 */

type Obj = Record<string, unknown>;

const isObj = (v: unknown): v is Obj => typeof v === 'object' && v !== null && !Array.isArray(v);
const num = (v: unknown, fallback = 0): number => (typeof v === 'number' && Number.isFinite(v) ? v : fallback);
const numOrNull = (v: unknown): number | null => (typeof v === 'number' && Number.isFinite(v) ? v : null);
const str = (v: unknown, fallback = ''): string => (typeof v === 'string' ? v : fallback);
const bool = (v: unknown): boolean => v === true;
const strList = (v: unknown): string[] => (Array.isArray(v) ? v.filter((x): x is string => typeof x === 'string') : []);
const numList = (v: unknown): number[] =>
  Array.isArray(v) ? v.filter((x): x is number => typeof x === 'number' && Number.isFinite(x)) : [];
const pair = (v: unknown): [number, number] | null => {
  const list = numList(v);
  return list.length === 2 && Array.isArray(v) && v.length === 2 ? [list[0], list[1]] : null;
};

const LENS = ['back', 'front', 'external'] as const;
const LEVELS = ['legacy', 'limited', 'full', 'level3', 'external'] as const;

function parseControls(v: unknown): CameraControls {
  const c = isObj(v) ? v : {};
  return {
    manualSensor: bool(c.manualSensor),
    manualPostProcessing: bool(c.manualPostProcessing),
    raw: bool(c.raw),
    burstCapture: bool(c.burstCapture),
    depthOutput: bool(c.depthOutput),
    logicalMultiCamera: bool(c.logicalMultiCamera),
    manualFocus: bool(c.manualFocus),
    autoFocus: bool(c.autoFocus),
    flash: bool(c.flash),
    opticalStabilization: bool(c.opticalStabilization),
    exposureCompensation: bool(c.exposureCompensation),
  };
}

function parseCamera(v: unknown): CameraInfo | null {
  if (!isObj(v) || typeof v.id !== 'string') {
    return null;
  }
  const ranges = isObj(v.ranges) ? v.ranges : {};
  const jpeg = isObj(v.maxJpegSize) ? v.maxJpegSize : null;
  const lens = str(v.lensFacing);
  const level = str(v.hardwareLevel);
  return {
    id: v.id,
    lensFacing: (LENS as readonly string[]).includes(lens) ? (lens as CameraInfo['lensFacing']) : 'unknown',
    hardwareLevel: (LEVELS as readonly string[]).includes(level) ? (level as CameraInfo['hardwareLevel']) : 'unknown',
    sensorMegapixels: numOrNull(v.sensorMegapixels),
    maxJpegSize: jpeg ? { width: num(jpeg.width), height: num(jpeg.height) } : null,
    controls: parseControls(v.controls),
    ranges: {
      iso: pair(ranges.iso),
      exposureTimeNs: pair(ranges.exposureTimeNs),
      exposureCompensation: pair(ranges.exposureCompensation),
      exposureCompensationStep: numOrNull(ranges.exposureCompensationStep),
      zoomRatio: pair(ranges.zoomRatio),
      maxDigitalZoom: numOrNull(ranges.maxDigitalZoom),
      minFocusDistanceDiopters: numOrNull(ranges.minFocusDistanceDiopters),
    },
    focalLengthsMm: numList(v.focalLengthsMm),
    apertures: numList(v.apertures),
    outputFormats: strList(v.outputFormats),
  };
}

export function parseCapabilities(raw: unknown): DeviceCapabilities {
  const root = isObj(raw) ? raw : {};
  const missing: string[] = [];
  const section = (key: string): Obj => {
    const value = root[key];
    if (isObj(value)) {
      return value;
    }
    missing.push(key);
    return {};
  };

  const os = section('os');
  const memory = section('memory');
  const cpu = section('cpu');
  const storage = section('storage');
  const display = section('display');
  const formats = section('imageFormats');
  const accel = section('acceleration');
  if (!Array.isArray(root.cameras)) {
    missing.push('cameras');
  }

  return {
    schemaVersion: num(root.schemaVersion, 1),
    collectedAt: num(root.collectedAt, Date.now()),
    os: {
      sdkInt: num(os.sdkInt),
      release: str(os.release, 'unknown'),
      manufacturer: str(os.manufacturer, 'unknown'),
      model: str(os.model, 'unknown'),
      supportedAbis: strList(os.supportedAbis),
      is64Bit: bool(os.is64Bit),
    },
    memory: {
      totalBytes: num(memory.totalBytes),
      availableBytes: num(memory.availableBytes),
      lowMemory: bool(memory.lowMemory),
      isLowRamDevice: bool(memory.isLowRamDevice),
    },
    cpu: { cores: num(cpu.cores, 1), socModel: typeof cpu.socModel === 'string' ? cpu.socModel : null },
    storage: { totalBytes: num(storage.totalBytes), availableBytes: num(storage.availableBytes) },
    display: { widthPx: num(display.widthPx), heightPx: num(display.heightPx), density: num(display.density, 1) },
    cameras: (Array.isArray(root.cameras) ? root.cameras : [])
      .map(parseCamera)
      .filter((c): c is CameraInfo => c !== null),
    imageFormats: {
      decode: strList(formats.decode),
      encode: strList(formats.encode),
      hevcEncoder: bool(formats.hevcEncoder),
      ultraHdr: bool(formats.ultraHdr),
    },
    acceleration: {
      glEsVersion: str(accel.glEsVersion, '0.0'),
      glEs31: bool(accel.glEs31),
      vulkan: bool(accel.vulkan),
      vulkanVersion: typeof accel.vulkanVersion === 'string' ? accel.vulkanVersion : null,
      nnapi: bool(accel.nnapi),
      nnapiDeprecated: bool(accel.nnapiDeprecated),
      renderEffectBlur: bool(accel.renderEffectBlur),
    },
    missingSections: missing,
  };
}
