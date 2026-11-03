import { nativeModules, requireNative } from '../../native';
import { createLogger } from '../logging/logger';
import { parseCapabilities } from './parse';
import type { DeviceCapabilities } from './types';

const log = createLogger('capabilities');

export interface CapabilitySource {
  getCapabilities(): Promise<Object>;
}

/** Probes capabilities with a timeout so a slow camera HAL cannot stall startup. */
export async function detectCapabilities(
  source: CapabilitySource = requireNative(nativeModules.device, 'PhotoPlusDevice'),
  timeoutMs = 4000,
): Promise<DeviceCapabilities> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  const timeout = new Promise<never>((_, reject) => {
    timer = setTimeout(() => reject(new Error(`Capability probe timed out after ${timeoutMs}ms`)), timeoutMs);
  });
  try {
    const raw = await Promise.race([source.getCapabilities(), timeout]);
    const caps = parseCapabilities(raw);
    if (caps.missingSections.length > 0) {
      log.warn('Some capability sections are unavailable', caps.missingSections);
    }
    return caps;
  } finally {
    if (timer) {
      clearTimeout(timer);
    }
  }
}
