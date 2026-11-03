import { createLogger } from '../logging/logger';
import { runMigrations } from './migrations';
import { NativeSqlExecutor } from './NativeSqlExecutor';
import { AssetRegistry } from './repositories/AssetRegistry';
import { HistoryRepository } from './repositories/HistoryRepository';
import { ImagesRepository } from './repositories/ImagesRepository';
import { ProjectsRepository } from './repositories/ProjectsRepository';
import { SettingsRepository } from './repositories/SettingsRepository';
import type { SqlExecutor } from './SqlExecutor';

const log = createLogger('database');

export const DATABASE_NAME = 'photoplus';

export interface Repositories {
  executor: SqlExecutor;
  settings: SettingsRepository;
  projects: ProjectsRepository;
  images: ImagesRepository;
  history: HistoryRepository;
  assets: AssetRegistry;
}

export function createRepositories(executor: SqlExecutor): Repositories {
  return {
    executor,
    settings: new SettingsRepository(executor),
    projects: new ProjectsRepository(executor),
    images: new ImagesRepository(executor),
    history: new HistoryRepository(executor),
    assets: new AssetRegistry(executor),
  };
}

/** Opens the app database, migrates it, and returns repositories. */
export async function openDatabase(
  open: () => Promise<SqlExecutor> = () => NativeSqlExecutor.open(DATABASE_NAME),
): Promise<Repositories> {
  const executor = await open();
  const result = await runMigrations(executor);
  if (result.applied.length) {
    log.info(`Database migrated v${result.from} → v${result.to}`);
  }
  return createRepositories(executor);
}
