import { createRepositories, openDatabase, type Repositories } from '../src/services/database/Database';
import { createId } from '../src/services/database/ids';
import { currentSchemaVersion, MIGRATIONS, runMigrations, type Migration } from '../src/services/database/migrations';
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { SqlJsExecutor } from './helpers/sqljsExecutor';

async function freshRepos(): Promise<{ db: SqlJsExecutor; repos: Repositories }> {
  const db = await SqlJsExecutor.create();
  const repos = await openDatabase(async () => db);
  return { db, repos };
}

describe('migrations', () => {
  it('migrates an empty database to the latest version', async () => {
    const db = await SqlJsExecutor.create();
    const result = await runMigrations(db);
    expect(result).toEqual({ from: 0, to: MIGRATIONS.length, applied: MIGRATIONS.map(m => m.version) });
    const tables = (
      await db.query<{ name: string }>("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name")
    ).map(t => t.name);
    expect(tables).toEqual(
      expect.arrayContaining(['settings', 'projects', 'images', 'processing_history', 'assets', 'schema_migrations']),
    );
  });

  it('is idempotent', async () => {
    const db = await SqlJsExecutor.create();
    await runMigrations(db);
    const again = await runMigrations(db);
    expect(again.applied).toEqual([]);
    expect(await currentSchemaVersion(db)).toBe(MIGRATIONS.length);
  });

  it('rolls back a failing migration atomically and resumes later', async () => {
    const db = await SqlJsExecutor.create();
    const broken: Migration[] = [
      ...MIGRATIONS,
      {
        version: MIGRATIONS.length + 1,
        name: 'broken',
        statements: ['CREATE TABLE extra (id TEXT)', 'THIS IS NOT SQL'],
      },
    ];
    await expect(runMigrations(db, broken)).rejects.toThrow();
    expect(await currentSchemaVersion(db)).toBe(MIGRATIONS.length);
    const extra = await db.query("SELECT name FROM sqlite_master WHERE name = 'extra'");
    expect(extra).toHaveLength(0);
  });

  it('refuses to run against a newer schema', async () => {
    const db = await SqlJsExecutor.create();
    await runMigrations(db);
    await db.execute('INSERT INTO schema_migrations (version, name, applied_at) VALUES (99, ?, 0)', ['future']);
    await expect(runMigrations(db)).rejects.toThrow(/newer than this app supports/);
  });

  it('rejects non-contiguous migration lists', async () => {
    const db = await SqlJsExecutor.create();
    await expect(runMigrations(db, [{ version: 2, name: 'x', statements: [] }])).rejects.toThrow(/contiguous/);
  });
});

describe('SettingsRepository', () => {
  it('returns defaults, persists valid values and ignores corrupt rows', async () => {
    const { db, repos } = await freshRepos();
    expect(await repos.settings.load()).toEqual(DEFAULT_SETTINGS);
    await repos.settings.set('themePreference', 'light');
    await repos.settings.set('defaultExportQuality', 80);
    await db.execute("INSERT INTO settings VALUES ('haptics', 'not json', 0)");
    await db.execute("INSERT INTO settings VALUES ('cameraGrid', '\"yes\"', 0)");
    await db.execute("INSERT INTO settings VALUES ('unknownKey', 'true', 0)");
    const loaded = await repos.settings.load();
    expect(loaded.themePreference).toBe('light');
    expect(loaded.defaultExportQuality).toBe(80);
    expect(loaded.haptics).toBe(DEFAULT_SETTINGS.haptics);
    expect(loaded.cameraGrid).toBe(DEFAULT_SETTINGS.cameraGrid);
  });

  it('rejects invalid writes', async () => {
    const { repos } = await freshRepos();
    // @ts-expect-error invalid on purpose
    await expect(repos.settings.set('themePreference', 'neon')).rejects.toThrow(/Invalid/);
    await expect(repos.settings.set('defaultExportQuality', 30)).rejects.toThrow(/Invalid/);
  });

  it('reset restores defaults but keeps internal bookkeeping', async () => {
    const { repos } = await freshRepos();
    const inbox = await repos.projects.ensureInbox();
    await repos.settings.set('themePreference', 'light');
    await repos.settings.reset();
    expect(await repos.settings.load()).toEqual(DEFAULT_SETTINGS);
    expect((await repos.projects.ensureInbox()).id).toBe(inbox.id);
  });

  it('stores internal blobs separately from user settings', async () => {
    const { repos } = await freshRepos();
    await repos.settings.setInternal('capabilities', { a: 1 });
    expect(await repos.settings.getInternal('capabilities')).toEqual({ a: 1 });
    expect(await repos.settings.getInternal('missing')).toBeNull();
    expect(await repos.settings.load()).toEqual(DEFAULT_SETTINGS);
  });
});

describe('ProjectsRepository & ImagesRepository', () => {
  it('creates, lists, renames, archives and deletes projects with cascading images', async () => {
    const { repos } = await freshRepos();
    let t = 1000;
    const projects = createRepositories(repos.executor).projects;
    (projects as unknown as { now: () => number }).now = () => ++t;
    const a = await projects.create('  Studio   portraits ');
    expect(a.name).toBe('Studio portraits');
    const b = await projects.create('Beach');
    expect((await projects.list()).map(p => p.name)).toEqual(['Beach', 'Studio portraits']);

    await repos.images.add({
      projectId: a.id,
      path: '/x/1.jpg',
      source: 'camera',
      width: 4000,
      height: 3000,
      sizeBytes: 2_000_000,
      mimeType: 'image/jpeg',
      capturedAt: 1,
      metadata: { iso: 100 },
    });
    await repos.images.add({
      projectId: a.id,
      path: '/x/2.jpg',
      source: 'camera',
      width: 4000,
      height: 3000,
      sizeBytes: 1_000_000,
      mimeType: 'image/jpeg',
      capturedAt: 2,
    });
    const reloaded = await projects.get(a.id);
    expect(reloaded?.imageCount).toBe(2);
    expect(reloaded?.coverPath).toBeTruthy();
    expect(await repos.images.totalBytes()).toBe(3_000_000);
    expect((await repos.images.listByProject(a.id))[0].metadata).toEqual(expect.any(Object));

    await projects.rename(b.id, 'Beach day');
    await projects.setArchived(b.id, true);
    expect((await projects.list()).map(p => p.name)).toEqual(['Studio portraits']);
    expect(await projects.list({ includeArchived: true })).toHaveLength(2);

    const removed = await projects.delete(a.id);
    expect(removed.sort()).toEqual(['/x/1.jpg', '/x/2.jpg']);
    expect(await repos.images.listRecent()).toHaveLength(0);
  });

  it('validates names and unique image paths', async () => {
    const { repos } = await freshRepos();
    await expect(repos.projects.create('   ')).rejects.toThrow(/empty/);
    const p = await repos.projects.create('A');
    const img = {
      projectId: p.id,
      path: '/dup.jpg',
      source: 'import' as const,
      width: 1,
      height: 1,
      sizeBytes: 1,
      mimeType: null,
      capturedAt: null,
    };
    await repos.images.add(img);
    await expect(repos.images.add(img)).rejects.toThrow();
  });

  it('ensureInbox recovers from a corrupt stored id', async () => {
    const { db, repos } = await freshRepos();
    await db.execute("INSERT INTO settings VALUES ('internal.inboxProjectId', '{broken', 0)");
    const inbox = await repos.projects.ensureInbox();
    expect(inbox.name).toBe('Captures');
    expect((await repos.projects.ensureInbox()).id).toBe(inbox.id);
  });

  it('ensureInbox is stable across calls and recreates a deleted inbox', async () => {
    const { repos } = await freshRepos();
    const first = await repos.projects.ensureInbox();
    const second = await repos.projects.ensureInbox();
    expect(second.id).toBe(first.id);
    await repos.projects.delete(first.id);
    const third = await repos.projects.ensureInbox();
    expect(third.id).not.toBe(first.id);
  });

  it('removes single images and returns their path', async () => {
    const { repos } = await freshRepos();
    const img = await repos.images.add({
      projectId: null,
      path: '/a.jpg',
      source: 'test',
      width: 1,
      height: 1,
      sizeBytes: 1,
      mimeType: null,
      capturedAt: null,
    });
    expect(await repos.images.remove(img.id)).toBe('/a.jpg');
    expect(await repos.images.remove(img.id)).toBeNull();
  });
});

describe('HistoryRepository', () => {
  it('records and prunes to the retention limit', async () => {
    const { repos } = await freshRepos();
    for (let i = 0; i < 12; i++) {
      await repos.history.record({
        imageId: null,
        jobId: `j${i}`,
        operations: [{ type: 'grayscale' }],
        status: i % 3 === 0 ? 'failed' : 'succeeded',
        errorCode: null,
        durationMs: i * 10.4,
        outputPath: null,
      });
    }
    expect(await repos.history.prune(5)).toBe(7);
    const recent = await repos.history.listRecent();
    expect(recent).toHaveLength(5);
    expect(recent[0].jobId).toBe('j11');
    expect(recent[0].operations).toEqual([{ type: 'grayscale' }]);
  });

  it('keeps history when the image is deleted (ON DELETE SET NULL)', async () => {
    const { repos } = await freshRepos();
    const img = await repos.images.add({
      projectId: null,
      path: '/h.jpg',
      source: 'test',
      width: 1,
      height: 1,
      sizeBytes: 1,
      mimeType: null,
      capturedAt: null,
    });
    await repos.history.record({
      imageId: img.id,
      jobId: 'j',
      operations: [],
      status: 'succeeded',
      errorCode: null,
      durationMs: 5,
      outputPath: '/o.jpg',
    });
    await repos.images.remove(img.id);
    expect((await repos.history.listRecent())[0].imageId).toBeNull();
  });
});

describe('AssetRegistry', () => {
  it('registers (upserting), lists, tracks usage, sizes and eviction order', async () => {
    const { repos } = await freshRepos();
    let t = 0;
    (repos.assets as unknown as { now: () => number }).now = () => ++t;
    await repos.assets.register({
      id: 'pack.golden',
      kind: 'pack',
      name: 'Golden Coast',
      version: '1.0.0',
      path: '/p/golden',
      sizeBytes: 50_000_000,
      checksum: 'abc',
      source: 'download',
    });
    await repos.assets.register({
      id: 'model.pose',
      kind: 'model',
      name: 'Pose',
      version: '1',
      path: '/m/pose',
      sizeBytes: 6_000_000,
      checksum: null,
      source: 'download',
    });
    await repos.assets.register({
      id: 'lut.base',
      kind: 'lut',
      name: 'Base',
      version: '1',
      path: '/l/base',
      sizeBytes: 1000,
      checksum: null,
      source: 'bundled',
    });
    await repos.assets.register({
      id: 'pack.golden',
      kind: 'pack',
      name: 'Golden Coast',
      version: '1.1.0',
      path: '/p/golden',
      sizeBytes: 52_000_000,
      checksum: 'def',
      source: 'download',
    });
    expect(await repos.assets.list()).toHaveLength(3);
    expect((await repos.assets.get('pack.golden'))?.version).toBe('1.1.0');
    expect(await repos.assets.totalBytes('pack')).toBe(52_000_000);
    await repos.assets.markUsed('pack.golden');
    const evict = await repos.assets.evictionCandidates();
    expect(evict.map(a => a.id)).toEqual(['model.pose', 'pack.golden']); // bundled never evicted
    expect((await repos.assets.remove('model.pose'))?.name).toBe('Pose');
    expect(await repos.assets.remove('model.pose')).toBeNull();
  });

  it('enforces asset kinds at the schema level', async () => {
    const { repos } = await freshRepos();
    await expect(
      repos.assets.register({
        id: 'x',
        // @ts-expect-error invalid on purpose
        kind: 'virus',
        name: 'x',
        version: '1',
        path: '/x',
        sizeBytes: 1,
        checksum: null,
        source: 'user',
      }),
    ).rejects.toThrow();
  });
});

describe('ids', () => {
  it('are prefixed, unique and time ordered', () => {
    const ids = new Set(Array.from({ length: 2000 }, () => createId('img')));
    expect(ids.size).toBe(2000);
    expect(createId('prj', 1000) < createId('prj', 2000)).toBe(true);
    expect(createId('img')).toMatch(/^img_[0-9a-z]{21}$/);
  });
});
