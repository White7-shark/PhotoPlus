import { bootstrap, type BootstrapDeps } from '../src/app/bootstrap';
import { openDatabase } from '../src/services/database/Database';
import { checkFeature, FREE_STATE, packAccess } from '../src/services/entitlements/EntitlementService';
import { FEATURES } from '../src/services/entitlements/features';
import { startLifecycleCoordinator } from '../src/services/lifecycle/LifecycleCoordinator';
import { clearLogs, createLogger, getRecentLogs } from '../src/services/logging/logger';
import { formatBytes, joinPath } from '../src/services/storage/StorageService';
import { useAppStore } from '../src/state/store';
import { rawCapabilities } from './helpers/fixtures';
import { parseCapabilities } from '../src/services/capabilities';
import { SqlJsExecutor } from './helpers/sqljsExecutor';

const engineInfo = { available: true, version: '0.1.0', loadError: null, workers: 2, memoryBudgetBytes: 1, maxDecodeDimension: 4096, operations: [], outputFormats: [] };

function deps(overrides: Partial<BootstrapDeps> = {}): BootstrapDeps {
  return {
    openDatabase: async () => openDatabase(() => SqlJsExecutor.create()),
    detectCapabilities: async () => parseCapabilities(rawCapabilities()),
    engineInfo: async () => engineInfo,
    ensurePaths: async () => ({}),
    ...overrides,
  };
}

describe('bootstrap', () => {
  it('succeeds with everything available and caches capabilities', async () => {
    const result = await bootstrap(deps());
    expect(result.warnings).toEqual([]);
    expect(result.capabilities?.os.model).toBe('Camon 30');
    expect(result.engine?.version).toBe('0.1.0');
    await new Promise<void>(r => setTimeout(() => r(), 0));
    expect(await result.repositories.settings.getInternal('capabilities')).toBeTruthy();
  });

  it('fails clearly when the database cannot open', async () => {
    await expect(bootstrap(deps({ openDatabase: async () => Promise.reject(new Error('disk')) }))).rejects.toMatchObject({ step: 'database' });
  });

  it('degrades: falls back to cached capabilities and flags a missing engine', async () => {
    const db = await SqlJsExecutor.create();
    const first = await bootstrap(deps({ openDatabase: () => openDatabase(async () => db) }));
    await new Promise<void>(r => setTimeout(() => r(), 0));
    expect(first.capabilitiesStale).toBe(false);
    const second = await bootstrap(
      deps({
        openDatabase: () => openDatabase(async () => db),
        detectCapabilities: async () => Promise.reject(new Error('HAL timeout')),
        engineInfo: async () => ({ ...engineInfo, available: false }),
        ensurePaths: async () => Promise.reject(new Error('no space')),
      }),
    );
    expect(second.capabilitiesStale).toBe(true);
    expect(second.capabilities?.os.model).toBe('Camon 30');
    expect(second.engine).toBeNull();
    expect(second.warnings).toHaveLength(3);
  });
});

describe('app store', () => {
  beforeEach(() => useAppStore.setState({ status: 'idle', repos: null, warnings: [], projects: [] }));

  it('boots into ready state with tier and projects loaded', async () => {
    await useAppStore.getState().start(deps());
    const s = useAppStore.getState();
    expect(s.status).toBe('ready');
    expect(s.tier).toBe('performance');
    await s.createProject('Wedding');
    expect(useAppStore.getState().projects.map(p => p.name)).toEqual(['Wedding']);
  });

  it('enters a retryable error state', async () => {
    await useAppStore.getState().start(deps({ openDatabase: async () => Promise.reject(new Error('x')) }));
    expect(useAppStore.getState().status).toBe('error');
    expect(useAppStore.getState().bootError).toMatch(/local library/);
    await useAppStore.getState().start(deps());
    expect(useAppStore.getState().status).toBe('ready');
  });

  it('updates settings optimistically and rolls back on failure', async () => {
    await useAppStore.getState().start(deps());
    await useAppStore.getState().updateSetting('themePreference', 'light');
    expect(useAppStore.getState().settings.themePreference).toBe('light');
    const repos = useAppStore.getState().repos!;
    jest.spyOn(repos.settings, 'set').mockRejectedValueOnce(new Error('disk full'));
    await expect(useAppStore.getState().updateSetting('themePreference', 'dark')).rejects.toThrow('disk full');
    expect(useAppStore.getState().settings.themePreference).toBe('light');
  });

  it('deletes projects and returns file paths for cleanup', async () => {
    await useAppStore.getState().start(deps());
    const p = await useAppStore.getState().createProject('Temp');
    await useAppStore.getState().repos!.images.add({ projectId: p!.id, path: '/f.jpg', source: 'camera', width: 1, height: 1, sizeBytes: 1, mimeType: null, capturedAt: null });
    expect(await useAppStore.getState().deleteProject(p!.id)).toEqual(['/f.jpg']);
    expect(useAppStore.getState().projects).toEqual([]);
  });
});

describe('entitlements (infrastructure, enforcement off in Phase 1)', () => {
  const premium = { plan: 'premium' as const, ownedPackIds: ['neon-noir'], includedPackIds: ['golden-coast'] };

  it('catalogue matches the spec split', () => {
    const premiumIds = Object.values(FEATURES).filter(f => f.plan === 'premium').map(f => f.id);
    expect(premiumIds).toEqual(expect.arrayContaining(['marketplace.packAccess', 'assistant.pose', 'replication.background', 'editing.customWatermark', 'replication.clothing']));
    expect(FEATURES.marketplaceBrowse.plan).toBe('free');
    expect(FEATURES.watermarkStyles.plan).toBe('free');
  });

  it('allows everything while enforcement is off but reports what would be blocked', () => {
    expect(checkFeature('poseGuidance', FREE_STATE)).toEqual({ allowed: true, requiredPlan: 'premium', wouldBeBlocked: true });
    expect(checkFeature('cropRotate', FREE_STATE).wouldBeBlocked).toBe(false);
    expect(packAccess('golden-coast', FREE_STATE)).toBe('apply');
  });

  it('applies the spec pack rules when enforcement is on', () => {
    expect(checkFeature('poseGuidance', FREE_STATE, true).allowed).toBe(false);
    expect(packAccess('golden-coast', FREE_STATE, true)).toBe('locked-subscription-required');
    expect(packAccess('golden-coast', premium, true)).toBe('apply');
    expect(packAccess('neon-noir', premium, true)).toBe('apply');
    expect(packAccess('kente-royale', premium, true)).toBe('locked-purchase-required');
    // Owned pack, but Premium lapsed: still owned, cannot be applied.
    expect(packAccess('neon-noir', { ...premium, plan: 'free' }, true)).toBe('locked-subscription-required');
  });
});

describe('lifecycle coordinator', () => {
  it('signals background/foreground transitions and memory pressure', () => {
    let appListener: (s: string) => void = () => undefined;
    let memListener: (p: { level: 'normal' | 'moderate' | 'critical' }) => void = () => undefined;
    const removed: string[] = [];
    const handlers = { onForeground: jest.fn(), onBackground: jest.fn(), onMemoryPressure: jest.fn() };
    const stop = startLifecycleCoordinator(
      handlers,
      {
        currentState: 'active',
        addEventListener: (_t, l) => {
          appListener = l as never;
          return { remove: () => removed.push('app') };
        },
      },
      {
        addListener: (_e, l) => {
          memListener = l;
          return { remove: () => removed.push('mem') };
        },
      },
    );
    appListener('inactive');
    appListener('background');
    appListener('active');
    memListener({ level: 'critical' });
    expect(handlers.onBackground).toHaveBeenCalledTimes(1);
    expect(handlers.onForeground).toHaveBeenCalledTimes(1);
    expect(handlers.onMemoryPressure).toHaveBeenCalledWith('critical');
    stop();
    expect(removed).toEqual(['app', 'mem']);
  });
});

describe('utilities', () => {
  it('formats bytes', () => {
    expect(formatBytes(0)).toBe('0 B');
    expect(formatBytes(512)).toBe('512 B');
    expect(formatBytes(1536)).toBe('1.5 KB');
    expect(formatBytes(3 * 1024 ** 3)).toBe('3.0 GB');
    expect(formatBytes(NaN)).toBe('0 B');
  });

  it('joins paths', () => {
    expect(joinPath('/data/cache/', '/processing/', 'a.jpg')).toBe('/data/cache/processing/a.jpg');
  });

  it('logger keeps a bounded ring buffer', () => {
    clearLogs();
    const log = createLogger('test');
    for (let i = 0; i < 250; i++) log.info(`m${i}`);
    log.error('bad');
    expect(getRecentLogs()).toHaveLength(200);
    expect(getRecentLogs('error')).toHaveLength(1);
  });
});
