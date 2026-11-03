import type { CameraInfo, DeviceCapabilities } from './types';

/**
 * Turns raw capabilities into product decisions: which performance tier the
 * device is in, which camera controls the UI may expose, and which on-device
 * models it can run. Thresholds follow the product spec (3 GB minimum,
 * 4 GB recommended, 6 GB+ for demanding features) and are deliberately
 * centralised here so device-lab results can retune them in one place.
 */

const GB = 1024 * 1024 * 1024;
export const MIN_SDK = 29;

export type DeviceTier = 'unsupported' | 'entry' | 'standard' | 'performance';

/**
 * Phones marketed as "N GB" report less than N GiB as total memory because
 * the kernel and carve-outs reserve some. Compare against 90% of nominal.
 */
const nominal = (gb: number) => gb * GB * 0.9;

export function classifyTier(caps: DeviceCapabilities): DeviceTier {
  const ram = caps.memory.totalBytes;
  if (caps.os.sdkInt > 0 && caps.os.sdkInt < MIN_SDK) {
    return 'unsupported';
  }
  if (ram <= 0) {
    return 'entry'; // unknown RAM: be conservative, never block the user
  }
  if (caps.memory.isLowRamDevice || ram < nominal(3)) {
    return 'unsupported';
  }
  if (ram < nominal(4)) {
    return 'entry';
  }
  if (ram < nominal(6)) {
    return 'standard';
  }
  return 'performance';
}

export const tierLabel: Record<DeviceTier, string> = {
  unsupported: 'Below minimum',
  entry: 'Entry',
  standard: 'Recommended',
  performance: 'Performance',
};

export function primaryCamera(caps: DeviceCapabilities, facing: 'back' | 'front' = 'back'): CameraInfo | null {
  const matches = caps.cameras.filter(c => c.lensFacing === facing);
  if (matches.length === 0) {
    return null;
  }
  // Prefer the most capable sensor of that facing.
  const rank = { level3: 4, full: 3, limited: 2, external: 1, legacy: 0, unknown: 0 } as const;
  return [...matches].sort(
    (a, b) => rank[b.hardwareLevel] - rank[a.hardwareLevel] || (b.sensorMegapixels ?? 0) - (a.sensorMegapixels ?? 0),
  )[0];
}

export interface CameraFeatureSupport {
  manualExposure: boolean;
  manualIso: boolean;
  manualFocus: boolean;
  manualWhiteBalance: boolean;
  rawCapture: boolean;
  exposureCompensation: boolean;
  flash: boolean;
  frontCamera: boolean;
  zoom: boolean;
}

export function cameraFeatureSupport(caps: DeviceCapabilities): CameraFeatureSupport {
  const back = primaryCamera(caps, 'back');
  const c = back?.controls;
  // Manual sensor control on LEGACY devices is unreliable even when advertised.
  const manualCapable = !!c?.manualSensor && back?.hardwareLevel !== 'legacy';
  return {
    manualExposure: manualCapable && !!back?.ranges.exposureTimeNs,
    manualIso: manualCapable && !!back?.ranges.iso,
    manualFocus: !!c?.manualFocus && manualCapable,
    manualWhiteBalance: !!c?.manualPostProcessing,
    rawCapture: !!c?.raw && !!back?.outputFormats.includes('raw'),
    exposureCompensation: !!c?.exposureCompensation,
    flash: !!c?.flash,
    frontCamera: primaryCamera(caps, 'front') !== null,
    zoom: (back?.ranges.zoomRatio?.[1] ?? back?.ranges.maxDigitalZoom ?? 1) > 1,
  };
}

/** On-device model descriptors planned for later phases. */
export interface ModelRequirement {
  id: string;
  name: string;
  purpose: string;
  sizeMb: number;
  minTier: Exclude<DeviceTier, 'unsupported'>;
  needsGpu: boolean;
}

export const MODEL_CATALOG: readonly ModelRequirement[] = [
  {
    id: 'segmentation-lite',
    name: 'Subject segmentation (lite)',
    purpose: 'Portrait focus, basic background effects',
    sizeMb: 4,
    minTier: 'entry',
    needsGpu: false,
  },
  {
    id: 'pose-lite',
    name: 'Pose detection (lite)',
    purpose: 'Live pose guidance',
    sizeMb: 6,
    minTier: 'entry',
    needsGpu: false,
  },
  {
    id: 'face-landmarks',
    name: 'Face landmarks',
    purpose: 'Retouching and lighting analysis',
    sizeMb: 3,
    minTier: 'entry',
    needsGpu: false,
  },
  {
    id: 'segmentation-hq',
    name: 'Subject segmentation (HQ)',
    purpose: 'Background removal and replacement',
    sizeMb: 28,
    minTier: 'standard',
    needsGpu: true,
  },
  {
    id: 'pose-full',
    name: 'Pose matching (full)',
    purpose: 'Reference pose matching',
    sizeMb: 24,
    minTier: 'standard',
    needsGpu: false,
  },
  {
    id: 'relight',
    name: 'Relighting',
    purpose: 'Lighting and shadow reproduction',
    sizeMb: 60,
    minTier: 'performance',
    needsGpu: true,
  },
  {
    id: 'garment-transfer',
    name: 'Garment transfer',
    purpose: 'Clothing transformation for compatible packs',
    sizeMb: 140,
    minTier: 'performance',
    needsGpu: true,
  },
];

export type InferenceDelegate = 'gpu' | 'nnapi' | 'cpu';

export interface ModelCompatibility {
  model: ModelRequirement;
  compatible: boolean;
  delegate: InferenceDelegate | null;
  reason: string;
}

const tierRank: Record<DeviceTier, number> = { unsupported: 0, entry: 1, standard: 2, performance: 3 };

export function preferredDelegate(caps: DeviceCapabilities): InferenceDelegate {
  if (caps.acceleration.glEs31 || caps.acceleration.vulkan) {
    return 'gpu';
  }
  if (caps.acceleration.nnapi && !caps.acceleration.nnapiDeprecated) {
    return 'nnapi';
  }
  return 'cpu';
}

export function modelCompatibility(
  caps: DeviceCapabilities,
  catalog: readonly ModelRequirement[] = MODEL_CATALOG,
): ModelCompatibility[] {
  const tier = classifyTier(caps);
  const delegate = preferredDelegate(caps);
  const freeStorage = caps.storage.availableBytes;
  return catalog.map(model => {
    if (!caps.os.is64Bit && model.sizeMb > 50) {
      return { model, compatible: false, delegate: null, reason: 'Requires a 64-bit device' };
    }
    if (tierRank[tier] < tierRank[model.minTier]) {
      return { model, compatible: false, delegate: null, reason: `Requires ${tierLabel[model.minTier]} tier or above` };
    }
    if (model.needsGpu && delegate !== 'gpu') {
      return {
        model,
        compatible: false,
        delegate: null,
        reason: 'Requires GPU acceleration (OpenGL ES 3.1 or Vulkan)',
      };
    }
    if (freeStorage > 0 && freeStorage < model.sizeMb * 1024 * 1024 * 3) {
      return { model, compatible: false, delegate: null, reason: 'Not enough free storage to install' };
    }
    return { model, compatible: true, delegate, reason: 'Supported' };
  });
}

export interface StorageHealth {
  level: 'ok' | 'low' | 'critical';
  availableBytes: number;
}

export function storageHealth(availableBytes: number): StorageHealth {
  if (availableBytes > 0 && availableBytes < 500 * 1024 * 1024) {
    return { level: 'critical', availableBytes };
  }
  if (availableBytes > 0 && availableBytes < 2 * GB) {
    return { level: 'low', availableBytes };
  }
  return { level: 'ok', availableBytes };
}
