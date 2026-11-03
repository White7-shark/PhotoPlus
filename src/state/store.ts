import { create } from 'zustand';

import { bootstrap, BootstrapError, defaultBootstrapDeps, type BootstrapDeps } from '../app/bootstrap';
import { classifyTier, type DeviceCapabilities, type DeviceTier } from '../services/capabilities';
import type { Repositories } from '../services/database/Database';
import type { Project } from '../services/database/repositories/ProjectsRepository';
import { DEFAULT_SETTINGS, type AppSettings } from '../services/database/repositories/SettingsRepository';
import type { MemoryPressure } from '../services/lifecycle/LifecycleCoordinator';
import { createLogger } from '../services/logging/logger';
import type { EngineInfo } from '../services/processing/types';
import { errorMessage } from '../native';

const log = createLogger('store');

export type BootStatus = 'idle' | 'loading' | 'ready' | 'error';
export type AsyncStatus = 'idle' | 'loading' | 'ready' | 'error';

export interface AppStore {
  // ---- app
  status: BootStatus;
  bootError: string | null;
  warnings: string[];
  memoryPressure: MemoryPressure;
  isForeground: boolean;
  repos: Repositories | null;

  // ---- device
  capabilities: DeviceCapabilities | null;
  capabilitiesStale: boolean;
  tier: DeviceTier;
  engine: EngineInfo | null;

  // ---- settings
  settings: AppSettings;

  // ---- projects
  projects: Project[];
  projectsStatus: AsyncStatus;
  projectsError: string | null;

  // ---- actions
  start: (deps?: BootstrapDeps) => Promise<void>;
  updateSetting: <K extends keyof AppSettings>(key: K, value: AppSettings[K]) => Promise<void>;
  loadProjects: () => Promise<void>;
  createProject: (name: string) => Promise<Project | null>;
  deleteProject: (id: string) => Promise<string[]>;
  setMemoryPressure: (level: MemoryPressure) => void;
  setForeground: (active: boolean) => void;
  dismissWarning: (index: number) => void;
}

export const useAppStore = create<AppStore>((set, get) => ({
  status: 'idle',
  bootError: null,
  warnings: [],
  memoryPressure: 'normal',
  isForeground: true,
  repos: null,

  capabilities: null,
  capabilitiesStale: false,
  tier: 'entry',
  engine: null,

  settings: DEFAULT_SETTINGS,

  projects: [],
  projectsStatus: 'idle',
  projectsError: null,

  async start(deps = defaultBootstrapDeps()) {
    if (get().status === 'loading') {
      return;
    }
    set({ status: 'loading', bootError: null });
    try {
      const result = await bootstrap(deps);
      set({
        status: 'ready',
        repos: result.repositories,
        settings: result.settings,
        capabilities: result.capabilities,
        capabilitiesStale: result.capabilitiesStale,
        tier: result.capabilities ? classifyTier(result.capabilities) : 'entry',
        engine: result.engine,
        warnings: result.warnings,
      });
      void get().loadProjects();
    } catch (error) {
      log.error('Bootstrap failed', error);
      set({
        status: 'error',
        bootError: error instanceof BootstrapError ? error.message : `Startup failed: ${errorMessage(error)}`,
      });
    }
  },

  async updateSetting(key, value) {
    const previous = get().settings;
    // Optimistic: the UI responds instantly, rolled back if the write fails.
    set({ settings: { ...previous, [key]: value } });
    const repos = get().repos;
    if (!repos) {
      return;
    }
    try {
      await repos.settings.set(key, value);
    } catch (error) {
      log.error(`Failed to save setting ${String(key)}`, error);
      set({ settings: previous });
      throw error;
    }
  },

  async loadProjects() {
    const repos = get().repos;
    if (!repos) {
      return;
    }
    set({ projectsStatus: 'loading', projectsError: null });
    try {
      set({ projects: await repos.projects.list(), projectsStatus: 'ready' });
    } catch (error) {
      set({ projectsStatus: 'error', projectsError: errorMessage(error) });
    }
  },

  async createProject(name) {
    const repos = get().repos;
    if (!repos) {
      return null;
    }
    const project = await repos.projects.create(name);
    set({ projects: [project, ...get().projects] });
    return project;
  },

  async deleteProject(id) {
    const repos = get().repos;
    if (!repos) {
      return [];
    }
    const paths = await repos.projects.delete(id);
    set({ projects: get().projects.filter(p => p.id !== id) });
    return paths;
  },

  setMemoryPressure(level) {
    set({ memoryPressure: level });
  },

  setForeground(active) {
    set({ isForeground: active });
  },

  dismissWarning(index) {
    set({ warnings: get().warnings.filter((_, i) => i !== index) });
  },
}));
