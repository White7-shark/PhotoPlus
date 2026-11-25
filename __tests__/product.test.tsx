import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { BatchScreen } from '../src/screens/product/BatchScreen';
import { ProductStudioScreen } from '../src/screens/product/ProductStudioScreen';
import { ProjectScreen } from '../src/screens/product/ProjectScreen';
import { openDatabase, type Repositories } from '../src/services/database/Database';
import { MIGRATIONS } from '../src/services/database/migrations';
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { DEFAULT_WATERMARK } from '../src/services/edit/watermark';
import {
  FREE_BATCH,
  MAX_BATCH,
  lookDocument,
  runBatch,
  shapeCrop,
  type BatchDeps,
} from '../src/services/product/batch';
import { ProductError } from '../src/services/product/ProductService';
import {
  CATALOG_PRESETS,
  DEFAULT_SETUP,
  EXPORT_PROFILES,
  LOOK_PRESETS,
  PACKS,
  TEMPLATES,
  isLookValues,
  isProductSetup,
  packSuits,
  setupPremium,
  setupRequest,
} from '../src/services/product/setup';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';
import { SqlJsExecutor } from './helpers/sqljsExecutor';

// ---- The device, mocked.
const cut = (found = true, plain = true) => ({
  cutoutPath: '/cache/product/cut_1.png',
  width: 1000,
  height: 1250,
  report: {
    found,
    coverage: 0.3,
    box: [0.2, 0.1, 0.8, 0.9] as [number, number, number, number],
    backdrop: '#F2F3F7',
    plain,
    objects: 1,
  },
});
const mockProduct = {
  openProduct: jest.fn(async (_id: string, req: { inputPath: string }) => ({
    ...cut(!req.inputPath.includes('empty')),
    sessionId: `s-${req.inputPath}`,
    photoPath: '/cache/product/photo.jpg',
    model: true,
  })),
  refineProduct: jest.fn(async () => cut()),
  renderProduct: jest.fn(async (_id: string, req: { quality: string; profile: { format: string } }) => ({
    outputPath:
      req.quality === 'full'
        ? `/files/studio/Product_${Math.random().toString(36).slice(2)}.jpg`
        : '/cache/product-previews/p.jpg',
    cleanPath: null,
    width: 1080,
    height: 1350,
    sizeBytes: 2000,
    format: req.profile.format === 'png' ? 'png' : 'jpeg',
  })),
  closeProduct: jest.fn(),
  cancel: jest.fn(),
};
const mockStudio = { saveToGallery: jest.fn(async () => 'content://1') };
const mockStorage = { pickImage: jest.fn(async () => '/files/imports/mug.jpg'), exists: jest.fn(async () => false) };
const mockEngine = {
  metadata: jest.fn(async () => ({ width: 3000, height: 4000, sizeBytes: 100, mimeType: 'image/jpeg' })),
};
jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get product() {
        return mockProduct;
      },
      get studio() {
        return mockStudio;
      },
      get storage() {
        return mockStorage;
      },
    },
  };
});
jest.mock('../src/services/processing/ImageProcessingService', () => ({
  getProcessingService: () => ({
    metadata: (...a: unknown[]) => mockEngine.metadata(...(a as [])),
    renderEdit: jest.fn(async (r: { outputPath: string }) => ({
      outputPath: r.outputPath,
      cleanPath: null,
      width: 3000,
      height: 3000,
      sizeBytes: 1,
      photoRect: [0, 0, 1, 1],
      portrait: null,
    })),
  }),
}));

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const tick = () => new Promise<void>(r => setTimeout(() => r(), 0));
const settle = async () => {
  for (let i = 0; i < 12; i++) await tick();
};
const step = async (fn: () => unknown) =>
  act(async () => {
    await fn();
    await settle();
  });
interface RenderReq {
  quality: string;
  setup: { backdrop: { kind: string; wall: string }; layout: { ground: boolean; flip: boolean } };
  profile: { aspectW: number; aspectH: number; resolution: number };
}
const renderReq = (i: number) => mockProduct.renderProduct.mock.calls.at(i)![1] as unknown as RenderReq;
const navigation = () => ({
  navigate: jest.fn(),
  goBack: jest.fn(),
  setParams: jest.fn(),
  addListener: jest.fn(() => () => undefined),
});

async function freshRepos(): Promise<Repositories> {
  const db = await SqlJsExecutor.create();
  return openDatabase(async () => db);
}

const photo = (repos: Repositories, projectId: string | null, path: string, extra: object = {}) =>
  repos.images.add({
    projectId,
    path,
    source: 'import',
    width: 3000,
    height: 4000,
    sizeBytes: 1,
    mimeType: 'image/jpeg',
    capturedAt: 1,
    ...extra,
  });

beforeEach(() => {
  jest.clearAllMocks();
});

describe('the catalogue', () => {
  it('free setups stay free, Premium ones say why', () => {
    expect(setupPremium(DEFAULT_SETUP)).toBeNull();
    for (const p of CATALOG_PRESETS) expect(!!setupPremium(p.setup) || p.profile === 'png').toBe(p.premium);
    expect(setupPremium({ ...DEFAULT_SETUP, shadow: { ...DEFAULT_SETUP.shadow, reflection: 0.3 } })).toMatch(/Premium/);
    // Every preset is a valid setup with a known profile; templates keep the product on the frame.
    for (const p of CATALOG_PRESETS) {
      expect(isProductSetup(p.setup)).toBe(true);
      expect(EXPORT_PROFILES.some(x => x.id === p.profile)).toBe(true);
    }
    for (const t of TEMPLATES) expect(t.layout.fill).toBeGreaterThan(0.1);
    for (const l of LOOK_PRESETS) expect(isLookValues(l.look)).toBe(true);
  });

  it('requests are clamped as the device clamps them', () => {
    const r = setupRequest({
      ...DEFAULT_SETUP,
      backdrop: { ...DEFAULT_SETUP.backdrop, wall: 'red', horizon: 3, kind: 'scene', plate: 'cafe-wall' },
      shadow: { ...DEFAULT_SETUP.shadow, castAngle: 400, contact: NaN },
      layout: { ...DEFAULT_SETUP.layout, fill: 0 },
    });
    expect(r.backdrop.wall).toBe(DEFAULT_SETUP.backdrop.wall);
    expect(r.backdrop.horizon).toBe(0.95);
    expect(r.backdrop.plate).toBe('cafe-wall');
    expect(r.shadow.castAngle).toBe(70);
    expect(r.shadow.contact).toBe(0);
    expect(r.layout.fill).toBe(0.1);
    expect(isProductSetup({ ...DEFAULT_SETUP, backdrop: { ...DEFAULT_SETUP.backdrop, kind: 'marble' } })).toBe(false);
  });

  it('packs suit the projects they are made for', () => {
    const product = PACKS.find(p => p.kind === 'product')!;
    const looks = PACKS.find(p => p.kind === 'look')!;
    expect(packSuits(product, 'product')).toBe(true);
    expect(packSuits(product, 'mixed')).toBe(true);
    expect(packSuits(product, 'portrait')).toBe(false);
    expect(packSuits(looks, 'portrait')).toBe(true);
    // Every pack's items exist.
    for (const p of PACKS)
      for (const id of p.items) expect([...CATALOG_PRESETS, ...LOOK_PRESETS].some(x => x.id === id)).toBe(true);
  });

  it('a shape crops each photo around its middle', () => {
    expect(shapeCrop(3000, 4000, 1, 1)).toEqual({ x: 0, y: 0.125, w: 1, h: 0.75 });
    const wide = shapeCrop(4000, 3000, 4, 5)!;
    expect(wide.w).toBeCloseTo(0.6);
    expect(wide.x).toBeCloseTo(0.2);
    expect(shapeCrop(1000, 1000, 1, 1)).toBeNull();
  });
});

describe('the library keeps setups, project kinds and defaults', () => {
  it('migrates to the workflow schema', async () => {
    const repos = await freshRepos();
    const cols = await repos.executor.query<{ name: string }>('PRAGMA table_info(projects)');
    expect(cols.map(c => c.name)).toEqual(expect.arrayContaining(['kind', 'defaults_json']));
    expect(MIGRATIONS.at(-1)?.name).toBe('professional_workflows');
  });

  it('saves, lists, renames and removes setups, skipping damaged ones', async () => {
    const repos = await freshRepos();
    const a = await repos.presets.save('product', '  Shop   white ', DEFAULT_SETUP);
    expect(a.name).toBe('Shop white');
    await repos.presets.save('look', 'Bright', LOOK_PRESETS[0].look);
    await repos.executor.execute("INSERT INTO presets VALUES ('bad', 'product', 'Bad', '{oops', 1, 1)");
    await repos.executor.execute("INSERT INTO presets VALUES ('bad2', 'product', 'Bad', '{\"backdrop\":1}', 1, 1)");
    const list = await repos.presets.list('product', isProductSetup);
    expect(list.map(p => p.name)).toEqual(['Shop white']);
    expect(list[0].data).toEqual(DEFAULT_SETUP);
    await repos.presets.rename(a.id, 'Shop grey');
    expect((await repos.presets.list('product', isProductSetup))[0].name).toBe('Shop grey');
    await repos.presets.remove(a.id);
    expect(await repos.presets.list('product', isProductSetup)).toEqual([]);
    await expect(repos.presets.save('product', '   ', DEFAULT_SETUP)).rejects.toThrow();
  });

  it('projects have a kind and defaults; photos move between projects', async () => {
    const repos = await freshRepos();
    const shop = await repos.projects.create('Shop', 'product');
    const plain = await repos.projects.create('Family');
    expect(shop.kind).toBe('product');
    expect(plain.kind).toBe('mixed');
    expect(plain.defaults).toEqual({});
    await repos.projects.setDefaults(shop.id, {
      packId: 'catalogue-essentials',
      setupId: 'marketplace-white',
      profileId: 'square',
    });
    const d = await repos.projects.setDefaults(shop.id, { profileId: null, sizeId: '1080p' });
    expect(d).toEqual({ packId: 'catalogue-essentials', setupId: 'marketplace-white', sizeId: '1080p' });
    await repos.executor.execute("UPDATE projects SET defaults_json = 'nonsense' WHERE id = ?", [plain.id]);
    expect((await repos.projects.get(plain.id))?.defaults).toEqual({});
    await repos.projects.setKind(plain.id, 'portrait');
    expect((await repos.projects.get(plain.id))?.kind).toBe('portrait');

    const mug = await photo(repos, plain.id, '/files/imports/mug.jpg');
    await repos.images.moveToProject([mug.id], shop.id);
    expect((await repos.images.listByProject(shop.id)).map(r => r.id)).toEqual([mug.id]);
    const shot = await photo(repos, shop.id, '/files/studio/P.jpg', {
      source: 'export',
      metadata: { sourceImageId: mug.id },
    });
    expect((await repos.images.listDerived(mug.id)).map(r => r.id)).toEqual([shot.id]);
  });
});

describe('batch editing', () => {
  const deps = (repos: Repositories, product: Partial<BatchDeps['product']> = {}): BatchDeps => ({
    repos,
    product: {
      open: jest.fn(async (p: string) => ({
        ...cut(!p.includes('empty')),
        sessionId: `s-${p}`,
        photoPath: p,
        model: true,
      })),
      render: jest.fn(async () => ({
        outputPath: `/files/studio/P_${Math.random()}.jpg`,
        cleanPath: null,
        width: 1080,
        height: 1080,
        sizeBytes: 1,
        format: 'jpeg' as const,
      })),
      close: jest.fn(),
      ...product,
    },
    renderEdit: jest.fn(async r => ({
      outputPath: r.outputPath,
      cleanPath: null,
      width: 3000,
      height: 3000,
      sizeBytes: 1,
      photoRect: [0, 0, 1, 1] as [number, number, number, number],
      portrait: null,
    })),
    exists: jest.fn(async () => false),
  });

  it('makes a product shot of each photo beside its untouched original, and goes on past a failure', async () => {
    const repos = await freshRepos();
    const shop = await repos.projects.create('Shop', 'product');
    const a = await photo(repos, shop.id, '/files/imports/a.jpg');
    const b = await photo(repos, shop.id, '/files/imports/empty.jpg');
    const c = await photo(repos, shop.id, '/files/imports/c.jpg');
    const d = deps(repos);
    const seen: string[] = [];
    const out = await runBatch(
      [a, b, c],
      {
        kind: 'product',
        name: 'Marketplace white',
        setup: CATALOG_PRESETS[0].setup,
        profile: EXPORT_PROFILES[0],
        resolution: 1080,
      },
      d,
      { projectId: null, onItem: (i, it) => seen.push(`${i}:${it.status}`) },
    );
    expect(out.map(i => i.status)).toEqual(['done', 'failed', 'done']);
    expect(out[1].error).toMatch(/No product/);
    expect(seen).toEqual(['0:working', '0:done', '1:working', '1:failed', '2:working', '2:done']);
    // Every session is closed, even the failed one.
    expect(d.product.close).toHaveBeenCalledTimes(3);
    const all = await repos.images.listByProject(shop.id);
    expect(all).toHaveLength(5);
    // The originals are as they were; each result knows its original.
    expect((await repos.images.get(a.id))?.path).toBe('/files/imports/a.jpg');
    expect(out[0].result?.metadata.sourceImageId).toBe(a.id);
    expect(out[0].result?.metadata.batch).toBe('Marketplace white');
  });

  it('stops at a Premium refusal and when asked to', async () => {
    // The free plan batches a few photos; Premium many.
    expect([FREE_BATCH, MAX_BATCH]).toEqual([3, 50]);
    const repos = await freshRepos();
    const imgs = await Promise.all([1, 2, 3].map(i => photo(repos, null, `/files/imports/${i}.jpg`)));
    const refused = deps(repos, {
      render: jest.fn(async () => {
        throw new ProductError('Premium', 'PREMIUM_REQUIRED');
      }),
    });
    await expect(
      runBatch(
        imgs,
        { kind: 'product', name: 'x', setup: DEFAULT_SETUP, profile: EXPORT_PROFILES[0], resolution: 2160 },
        refused,
        { projectId: null },
      ),
    ).rejects.toThrow('Premium');
    expect(refused.product.open).toHaveBeenCalledTimes(1);
    const signal = { cancelled: false };
    const d = deps(repos);
    const out = await runBatch(
      imgs,
      { kind: 'product', name: 'x', setup: DEFAULT_SETUP, profile: EXPORT_PROFILES[0], resolution: 480 },
      d,
      {
        projectId: null,
        signal,
        onItem: (i, it) => {
          if (i === 0 && it.status === 'done') signal.cancelled = true;
        },
      },
    );
    expect(out.map(i => i.status)).toEqual(['done', 'waiting', 'waiting']);
  });

  it('a look edits each photo from its untouched photo, cropped to the shape', async () => {
    const repos = await freshRepos();
    const original = await photo(repos, null, '/files/imports/a.jpg');
    const edited = await photo(repos, null, '/files/imports/a_edit.jpg', {
      source: 'export',
      metadata: { sourceImageId: original.id, editBase: '/files/imports/a.jpg' },
    });
    const d = deps(repos);
    (d.exists as jest.Mock).mockImplementationOnce(async () => true);
    const job = {
      kind: 'look' as const,
      name: 'Bright & clean',
      look: LOOK_PRESETS[0].look,
      shape: { aspectW: 1, aspectH: 1 },
      watermark: DEFAULT_WATERMARK,
    };
    const out = await runBatch([original, edited], job, d, { projectId: null });
    expect(out.map(i => i.status)).toEqual(['done', 'done']);
    const calls = (d.renderEdit as jest.Mock).mock.calls.map(c => c[0]);
    // An existing name is not overwritten.
    expect(calls[0].outputPath).toBe('/files/imports/a_look_1.jpg');
    expect(calls[1].inputPath).toBe('/files/imports/a.jpg');
    expect(calls[0].doc.exposure).toBe(LOOK_PRESETS[0].look.exposure);
    expect(calls[0].doc.crop).toEqual(lookDocument(job, original).crop);
    expect(out[0].result?.metadata.edit).toEqual(calls[0].doc);
  });
});

describe('Product Studio', () => {
  it('opens a photo, shows before and after, changes the setup and saves into the project', async () => {
    const repos = await freshRepos();
    const shop = await repos.projects.create('Shop', 'product');
    await repos.projects.setDefaults(shop.id, { packId: 'catalogue-essentials', setupId: 'marketplace-white' });
    useAppStore.setState({
      repos,
      settings: { ...DEFAULT_SETTINGS },
      loadProjects: jest.fn(async () => undefined),
    } as never);
    const nav = navigation();
    await step(() =>
      wrap(
        <ProductStudioScreen
          navigation={nav as never}
          route={{ key: 'p', name: 'ProductStudio', params: { projectId: shop.id } } as never}
        />,
      ),
    );
    await step(() => fireEvent.press(screen.getByTestId('product-pick')));
    expect(mockProduct.openProduct).toHaveBeenCalledWith(expect.any(String), {
      inputPath: '/files/imports/mug.jpg',
      points: [],
    });
    // The project's pack and setup: marketplace white, only the pack's setups listed.
    expect(screen.getByTestId('product-preset-marketplace-white').props.accessibilityState.selected).toBe(true);
    expect(screen.queryByTestId('product-preset-tabletop')).toBeNull();
    const first = renderReq(-1);
    expect(first.quality).toBe('preview');
    expect(first.setup.backdrop.kind).toBe('solid');
    // Before shows the original; after the shot.
    await step(() => fireEvent.press(screen.getByTestId('product-view-before')));
    expect(screen.getByTestId('product-preview').props.source.uri).toBe('file:///cache/product/photo.jpg');
    await step(() => fireEvent.press(screen.getByTestId('product-view-after')));
    // Backdrop, shadow and layout are the person's to change.
    await step(() => fireEvent.press(screen.getByTestId('product-tab-backdrop')));
    await step(() => fireEvent.press(screen.getByTestId('product-kind-sweep')));
    await step(() => fireEvent.press(screen.getByTestId('product-wall-1F2C44')));
    await step(() => fireEvent.press(screen.getByTestId('product-tab-layout')));
    await step(() => fireEvent.press(screen.getByTestId('product-template-standing')));
    await step(() => fireEvent.press(screen.getByTestId('product-flip')));
    const req = renderReq(-1);
    expect(req.setup.backdrop).toEqual(expect.objectContaining({ kind: 'sweep', wall: '#1F2C44' }));
    expect(req.setup.layout).toEqual(expect.objectContaining({ ground: true, flip: true }));
    // Cut-out corrections go to the device.
    await step(() => fireEvent.press(screen.getByTestId('product-tab-product')));
    expect(screen.getByText(/1 object found/)).toBeTruthy();
    // A setup saved for reuse.
    await step(() => fireEvent.press(screen.getByTestId('product-tab-setups')));
    await step(() => fireEvent.press(screen.getByTestId('product-setup-new')));
    await step(() => fireEvent.changeText(screen.getByTestId('product-setup-name'), 'Navy sweep'));
    await step(() => fireEvent.press(screen.getByTestId('product-setup-save')));
    const saved = await repos.presets.list('product', isProductSetup);
    expect(saved.map(s => s.name)).toEqual(['Navy sweep']);
    expect(saved[0].data.backdrop.wall).toBe('#1F2C44');
    // Saved into the project, at the chosen profile and size.
    await step(() => fireEvent.press(screen.getByTestId('product-tab-export')));
    await step(() => fireEvent.press(screen.getByTestId('product-profile-square')));
    await step(() => fireEvent.press(screen.getByTestId('product-save')));
    const full = renderReq(-1);
    expect(full.quality).toBe('full');
    // Enforcement is off until billing ships, so the size starts at 1080p.
    expect(full.profile).toEqual(expect.objectContaining({ aspectW: 1, resolution: 1080 }));
    expect(mockStudio.saveToGallery).toHaveBeenCalled();
    const inShop = await repos.images.listByProject(shop.id);
    expect(inShop).toHaveLength(1);
    expect(inShop[0].metadata.captureMode).toBe('product');
    expect(nav.navigate).toHaveBeenCalledWith('PhotoPreview', { imageId: inShop[0].id });
  });

  it('says so when no product is found', async () => {
    const repos = await freshRepos();
    useAppStore.setState({
      repos,
      settings: { ...DEFAULT_SETTINGS },
      loadProjects: jest.fn(async () => undefined),
    } as never);
    mockStorage.pickImage.mockImplementationOnce(async () => '/files/imports/empty.jpg');
    await step(() =>
      wrap(
        <ProductStudioScreen navigation={navigation() as never} route={{ key: 'p', name: 'ProductStudio' } as never} />,
      ),
    );
    await step(() => fireEvent.press(screen.getByTestId('product-pick')));
    expect(screen.getByText(/No product was found/)).toBeTruthy();
    expect(screen.getByTestId('product-save').props.accessibilityState.disabled).toBe(true);
  });
});

describe('Batch and Project screens', () => {
  it('batch edits the chosen photos of a project with its defaults', async () => {
    const repos = await freshRepos();
    const shop = await repos.projects.create('Shop', 'product');
    await repos.projects.setDefaults(shop.id, { setupId: 'studio-grey', profileId: 'post' });
    const imgs = [];
    for (const n of ['a', 'b']) imgs.push(await photo(repos, shop.id, `/files/imports/${n}.jpg`));
    useAppStore.setState({
      repos,
      settings: { ...DEFAULT_SETTINGS },
      loadProjects: jest.fn(async () => undefined),
    } as never);
    await step(() =>
      wrap(
        <BatchScreen
          navigation={navigation() as never}
          route={{ key: 'b', name: 'Batch', params: { projectId: shop.id } } as never}
        />,
      ),
    );
    expect(screen.getByTestId('batch-setup-studio-grey').props.accessibilityState.selected).toBe(true);
    for (const r of imgs) await step(() => fireEvent.press(screen.getByTestId(`batch-photo-${r.id}`)));
    // Enforcement is off until billing ships: every plan sees no limit yet.
    expect(screen.getByText(/Photos · 2/)).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('batch-run')));
    expect(mockProduct.openProduct).toHaveBeenCalledTimes(2);
    const req = renderReq(0);
    expect(req.profile).toEqual(expect.objectContaining({ aspectW: 4, aspectH: 5 }));
    expect(screen.getByText('Done: 2 of 2')).toBeTruthy();
    expect(await repos.images.listByProject(shop.id)).toHaveLength(4);
  });

  it('a project offers the packs that suit it, filters originals and moves photos', async () => {
    const repos = await freshRepos();
    const family = await repos.projects.create('Family', 'portrait');
    const other = await repos.projects.create('Shop', 'product');
    const a = await photo(repos, family.id, '/files/imports/a.jpg');
    await photo(repos, family.id, '/files/imports/a_edit.jpg', { source: 'export', metadata: { sourceImageId: a.id } });
    useAppStore.setState({
      repos,
      projects: await repos.projects.list(),
      settings: { ...DEFAULT_SETTINGS },
      loadProjects: jest.fn(async () => undefined),
    } as never);
    const nav = navigation();
    await step(() =>
      wrap(
        <ProjectScreen
          navigation={nav as never}
          route={{ key: 'pr', name: 'Project', params: { projectId: family.id } } as never}
        />,
      ),
    );
    expect(screen.getByText(/1 original · 1 edited/)).toBeTruthy();
    // A product pack does not suit a portrait project; a look pack does.
    expect(screen.getByTestId('project-pack-catalogue-essentials').props.accessibilityState.disabled).toBe(true);
    await step(() => fireEvent.press(screen.getByTestId('project-pack-everyday-looks')));
    expect((await repos.projects.get(family.id))?.defaults).toEqual({
      packId: 'everyday-looks',
      lookId: 'bright-clean',
    });
    expect(screen.getByText('Bright & clean')).toBeTruthy();
    // Originals only.
    await step(() => fireEvent.press(screen.getByTestId('project-filter-originals')));
    expect(screen.getAllByLabelText('Original photo')).toHaveLength(1);
    expect(screen.queryByLabelText('Edited photo')).toBeNull();
    // Select and move.
    await step(() => fireEvent.press(screen.getByTestId('project-select')));
    await step(() => fireEvent.press(screen.getByTestId(`project-photo-${a.id}`)));
    await step(() => fireEvent.press(screen.getByTestId('project-action-move')));
    await step(() => fireEvent.press(screen.getByTestId(`project-move-${other.id}`)));
    expect((await repos.images.get(a.id))?.projectId).toBe(other.id);
    // Switching to products takes off nothing that still suits.
    await step(() => fireEvent.press(screen.getByTestId('project-kind-product')));
    expect((await repos.projects.get(family.id))?.defaults.packId).toBe('everyday-looks');
  });
});
